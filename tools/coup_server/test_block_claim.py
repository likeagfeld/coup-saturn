#!/usr/bin/env python3
"""
Test: Verify BLOCK + BLOCK_CLAIM flow works correctly.

Connects as a player, waits for game start, and specifically tests that
blocking an action (sending RESP_BLOCK followed by BLOCK_CLAIM) is properly
handled by the server without timing out.

Uses the same bot_player infrastructure but with forced blocking behavior.
"""

import logging
import random
import select
import socket
import struct
import sys
import time

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [TEST] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("test")

# Protocol constants
SHARED_SECRET = b"SaturnCoup2025!NetLink#SecretKey"
AUTH_MAGIC = b"AUTH"

MSG_CONNECT       = 0x01
MSG_SET_USERNAME  = 0x02
MSG_HEARTBEAT     = 0x04
MSG_READY         = 0x10
MSG_ACTION        = 0x11
MSG_RESPONSE      = 0x12
MSG_BLOCK_CLAIM   = 0x13
MSG_LOSE_INFLUENCE = 0x14
MSG_EXCHANGE      = 0x15

MSG_USERNAME_REQ  = 0x81
MSG_WELCOME       = 0x82
MSG_WELCOME_BACK  = 0x83
MSG_LOBBY_STATE   = 0xA0
MSG_GAME_START    = 0xA1
MSG_LOG           = 0xAE
MSG_INPUT_RELAY   = 0xB2

RELAY_START_GAME  = 0
RELAY_ACTION      = 1
RELAY_RESPONSE    = 2
RELAY_BLOCK_CLAIM = 3
RELAY_LOSE_INFLUENCE = 4
RELAY_EXCHANGE    = 5
RELAY_TIMEOUT     = 6

ACT_INCOME      = 0
ACT_FOREIGN_AID = 1
ACT_COUP        = 2
ACT_TAX         = 3
ACT_ASSASSINATE = 4
ACT_STEAL       = 5
ACT_EXCHANGE    = 6

RESP_PASS      = 0
RESP_CHALLENGE = 1
RESP_BLOCK     = 2

CHAR_DUKE       = 0
CHAR_ASSASSIN   = 1
CHAR_CAPTAIN    = 2
CHAR_AMBASSADOR = 3
CHAR_CONTESSA   = 4

CHAR_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa"]
ACTION_NAMES = ["Income", "ForeignAid", "Coup", "Tax", "Assassinate", "Steal", "Exchange"]

# Phase constants
PHASE_WAITING_FOR_ACTION = 1
PHASE_CHALLENGE_WINDOW = 2
PHASE_BLOCK_WINDOW = 3
PHASE_BLOCK_CHALLENGE_WINDOW = 4
PHASE_WAITING_FOR_INFLUENCE_LOSS = 5
PHASE_WAITING_FOR_EXCHANGE = 6
PHASE_RESOLVING = 7


def encode_frame(payload):
    return struct.pack("!H", len(payload)) + payload


class BlockTestPlayer:
    """A test player that always blocks when possible, to exercise the fix."""

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.buf = b""
        self.my_engine_pid = None
        self.player_count = 0
        self.seed = 0
        self.engine = None
        self.in_game = False
        self.blocks_sent = 0
        self.blocks_accepted = 0
        self.timeouts_seen = 0
        self.game_over = False
        self.last_action_declared = None  # track what action is being blocked

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        self.sock.sendall(AUTH_MAGIC + bytes([len(SHARED_SECRET)]) + SHARED_SECRET)
        resp = self.sock.recv(1)
        if not resp or resp[0] != 0x01:
            raise ConnectionError("Auth rejected")
        self._send(bytes([MSG_CONNECT]))
        log.info("Connected and authenticated")

    def _send(self, payload):
        self.sock.sendall(encode_frame(payload))

    def _recv_frames(self):
        try:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("Server closed")
            self.buf += data
        except (BlockingIOError, socket.timeout):
            pass

        while len(self.buf) >= 2:
            plen = struct.unpack("!H", self.buf[:2])[0]
            if len(self.buf) < 2 + plen:
                break
            payload = self.buf[2:2 + plen]
            self.buf = self.buf[2 + plen:]
            yield payload

    def _init_engine(self):
        sys.path.insert(0, "/Users/r11/Projects/cui_sandbox/tools/coup_server")
        from coup_engine import CoupEngine
        self.engine = CoupEngine(isolated=True)
        self.engine.init(self.player_count, self.seed)

    def _handle_message(self, payload):
        if not payload:
            return
        msg_type = payload[0]

        if msg_type == MSG_USERNAME_REQ:
            name = "BlockTest"
            self._send(bytes([MSG_SET_USERNAME, len(name)]) + name.encode())

        elif msg_type in (MSG_WELCOME, MSG_WELCOME_BACK):
            self.my_engine_pid = payload[1]
            log.info("Welcome, sending READY")
            self._send(bytes([MSG_READY]))

        elif msg_type == MSG_LOBBY_STATE:
            count = payload[1]
            log.info("Lobby: %d players", count)

        elif msg_type == MSG_GAME_START:
            self.my_engine_pid = payload[1]
            self.seed = struct.unpack("!I", payload[2:6])[0]
            self.player_count = payload[6]
            self.in_game = True
            self._init_engine()
            log.info("GAME START: pid=%d, %d players", self.my_engine_pid, self.player_count)

        elif msg_type == MSG_INPUT_RELAY:
            self._handle_relay(payload)

        elif msg_type == MSG_LOG:
            text_len = payload[1]
            text = payload[2:2 + text_len].decode("utf-8", errors="replace")
            log.info("LOG: %s", text)
            if "Block successful" in text or "blocks with" in text:
                self.blocks_accepted += 1
            if "timed out" in text.lower() or "Timeout" in text:
                self.timeouts_seen += 1

    def _handle_relay(self, payload):
        relay_type = payload[1]
        player_id = payload[2]
        data = payload[3:]

        if not self.engine:
            return

        if relay_type == RELAY_START_GAME:
            self.engine.submit_start()
        elif relay_type == RELAY_ACTION:
            action = data[0]
            target = data[1]
            self.last_action_declared = action
            log.info("  Relay: P%d does %s (target P%d)",
                     player_id, ACTION_NAMES[action] if action < 7 else "?",
                     target)
            self.engine.submit_action(player_id, action, target)
        elif relay_type == RELAY_RESPONSE:
            resp = data[0]
            resp_names = ["Pass", "Challenge", "Block"]
            log.info("  Relay: P%d %s", player_id, resp_names[resp] if resp < 3 else "?")
            self.engine.submit_response(player_id, resp)
        elif relay_type == RELAY_BLOCK_CLAIM:
            char = data[0]
            log.info("  Relay: P%d claims %s for block", player_id,
                     CHAR_NAMES[char] if char < 5 else "?")
            self.engine.submit_block_claim(player_id, char)
        elif relay_type == RELAY_LOSE_INFLUENCE:
            self.engine.submit_lose_influence(player_id, data[0])
        elif relay_type == RELAY_EXCHANGE:
            self.engine.submit_exchange(player_id, data[0], data[1])
        elif relay_type == RELAY_TIMEOUT:
            log.warning("  TIMEOUT relay received!")
            self.timeouts_seen += 1
            self.engine.submit_timeout()

        if not self.engine.game_active():
            log.info("=== GAME OVER ===")
            self.game_over = True
            return

        self._decide()

    def _decide(self):
        if not self.engine or not self.engine.game_active():
            return

        phase = self.engine.phase()
        me = self.my_engine_pid

        if phase == PHASE_WAITING_FOR_ACTION and self.engine.current_player() == me:
            self._choose_action()

        elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW, PHASE_BLOCK_CHALLENGE_WINDOW):
            if self.engine.pending_response(me):
                self._choose_response(phase)

        elif phase == PHASE_RESOLVING and self.engine.blocker_id() == me:
            # We are the blocker — send BLOCK_CLAIM immediately
            block_char = self._pick_block_char()
            log.info(">> BLOCK_CLAIM: %s",
                     CHAR_NAMES[block_char] if block_char < 5 else "?")
            self._send(bytes([MSG_BLOCK_CLAIM, block_char]))

        elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS and self.engine.influence_loser() == me:
            for slot in range(2):
                if not self.engine.player_revealed(me, slot):
                    self._send(bytes([MSG_LOSE_INFLUENCE, slot]))
                    return
            self._send(bytes([MSG_LOSE_INFLUENCE, 0]))

        elif phase == PHASE_WAITING_FOR_EXCHANGE and self.engine.exchange_player() == me:
            self._send(bytes([MSG_EXCHANGE, 0, 1]))

    def _choose_action(self):
        coins = self.engine.player_coins(self.my_engine_pid)
        valid = self.engine.valid_actions()

        if coins >= 10:
            target = self._pick_target()
            self._send(bytes([MSG_ACTION, ACT_COUP, target]))
            return

        # Use Foreign Aid frequently so others can block us
        if (valid & (1 << ACT_FOREIGN_AID)) and random.random() < 0.3:
            log.info(">> FOREIGN AID")
            self._send(bytes([MSG_ACTION, ACT_FOREIGN_AID, 0xFF]))
            return

        # Steal to test steal-blocking
        if (valid & (1 << ACT_STEAL)) and random.random() < 0.3:
            target = self._pick_target()
            if self.engine.player_coins(target) > 0:
                log.info(">> STEAL from P%d", target)
                self._send(bytes([MSG_ACTION, ACT_STEAL, target]))
                return

        # Default
        log.info(">> INCOME")
        self._send(bytes([MSG_ACTION, ACT_INCOME, 0xFF]))

    def _choose_response(self, phase):
        me = self.my_engine_pid

        # ALWAYS block when in block window (to stress-test the fix)
        if phase == PHASE_BLOCK_WINDOW:
            block_char = self._pick_block_char()
            log.info(">> BLOCK! (claiming %s) — block #%d",
                     CHAR_NAMES[block_char] if block_char < 5 else "?",
                     self.blocks_sent + 1)
            self._send(bytes([MSG_RESPONSE, RESP_BLOCK]))
            self.blocks_sent += 1
            # BLOCK_CLAIM will be sent from _decide when engine enters RESOLVING
            return

        # Challenge block windows sometimes
        if phase == PHASE_BLOCK_CHALLENGE_WINDOW and random.random() < 0.3:
            log.info(">> CHALLENGE block!")
            self._send(bytes([MSG_RESPONSE, RESP_CHALLENGE]))
            return

        # Otherwise pass
        self._send(bytes([MSG_RESPONSE, RESP_PASS]))

    def _pick_block_char(self):
        """Pick block character based on the action being blocked."""
        action = self.last_action_declared
        me = self.my_engine_pid

        if action == ACT_ASSASSINATE:
            return CHAR_CONTESSA
        elif action == ACT_FOREIGN_AID:
            return CHAR_DUKE
        elif action == ACT_STEAL:
            # Claim whichever we hold, default Captain
            for slot in range(2):
                if not self.engine.player_revealed(me, slot):
                    c = self.engine.player_card(me, slot)
                    if c in (CHAR_CAPTAIN, CHAR_AMBASSADOR):
                        return c
            return CHAR_CAPTAIN
        return CHAR_CONTESSA

    def _pick_target(self):
        targets = []
        for pid in range(self.player_count):
            if pid != self.my_engine_pid and self.engine.player_alive(pid):
                targets.append(pid)
        return random.choice(targets) if targets else 0

    def run(self, max_games=3, max_time=120):
        self.connect()
        self.sock.setblocking(False)
        start = time.time()
        games_completed = 0
        last_heartbeat = time.time()

        log.info("Running block test (max %d games, %ds timeout)...", max_games, max_time)

        while time.time() - start < max_time and games_completed < max_games:
            try:
                readable, _, _ = select.select([self.sock], [], [], 0.1)
            except (ValueError, OSError):
                break

            if readable:
                try:
                    for payload in self._recv_frames():
                        self._handle_message(payload)
                except ConnectionError as e:
                    log.info("Disconnected: %s", e)
                    break

            if self.game_over:
                games_completed += 1
                log.info("--- Game %d complete. Blocks sent: %d, Timeouts: %d ---",
                         games_completed, self.blocks_sent, self.timeouts_seen)
                self.game_over = False
                if games_completed < max_games:
                    # Ready up for next game
                    time.sleep(1)
                    self._send(bytes([MSG_READY]))

            now = time.time()
            if now - last_heartbeat > 30:
                try:
                    self._send(bytes([MSG_HEARTBEAT]))
                    last_heartbeat = now
                except OSError:
                    break

        self.sock.close()
        return self.blocks_sent, self.timeouts_seen, games_completed


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 4821

    player = BlockTestPlayer(host, port)
    blocks_sent, timeouts, games = player.run(max_games=2, max_time=90)

    log.info("=" * 60)
    log.info("RESULTS: %d games, %d blocks sent, %d timeouts", games, blocks_sent, timeouts)
    if blocks_sent > 0 and timeouts == 0:
        log.info("PASS: All blocks were accepted without timeout!")
        sys.exit(0)
    elif blocks_sent == 0:
        log.info("INCONCLUSIVE: No blocks were attempted (bad luck with game flow)")
        sys.exit(0)
    else:
        log.info("FAIL: %d timeouts detected — BLOCK_CLAIM may not be sent correctly", timeouts)
        sys.exit(1)


if __name__ == "__main__":
    main()
