#!/usr/bin/env python3
"""
Coup Bot Player - Simulates a human player connecting to the Coup server.

Connects via TCP, authenticates, joins the lobby, readies up, and plays
the game using a simple AI strategy (income/coup when forced, random
otherwise). Uses the C rule engine to track game state locally, just
like a real Saturn client would.

Usage:
    python bot_player.py [--host HOST] [--port PORT] [--name NAME]
"""

import argparse
import logging
import random
import select
import socket
import struct
import sys
import time

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [BOT] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("bot")

# --- Protocol constants ---
SHARED_SECRET = b"SaturnCoup2025!NetLink#SecretKey"
AUTH_MAGIC = b"AUTH"

# Client -> Server
MSG_CONNECT       = 0x01
MSG_SET_USERNAME  = 0x02
MSG_HEARTBEAT     = 0x04
MSG_DISCONNECT    = 0x05
MSG_READY         = 0x10
MSG_ACTION        = 0x11
MSG_RESPONSE      = 0x12
MSG_BLOCK_CLAIM   = 0x13
MSG_LOSE_INFLUENCE = 0x14
MSG_EXCHANGE      = 0x15
MSG_START_GAME    = 0x16

# Server -> Client
MSG_USERNAME_REQ  = 0x81
MSG_WELCOME       = 0x82
MSG_WELCOME_BACK  = 0x83
MSG_USERNAME_TAKEN = 0x84
MSG_LOBBY_STATE   = 0xA0
MSG_GAME_START    = 0xA1
MSG_LOG           = 0xAE
MSG_INPUT_RELAY   = 0xB2

# Relay types
RELAY_START_GAME     = 0
RELAY_ACTION         = 1
RELAY_RESPONSE       = 2
RELAY_BLOCK_CLAIM    = 3
RELAY_LOSE_INFLUENCE = 4
RELAY_EXCHANGE       = 5
RELAY_TIMEOUT        = 6

# Actions
ACT_INCOME      = 0
ACT_FOREIGN_AID = 1
ACT_COUP        = 2
ACT_TAX         = 3
ACT_ASSASSINATE = 4
ACT_STEAL       = 5
ACT_EXCHANGE    = 6

ACTION_NAMES = ["Income", "Foreign Aid", "Coup", "Tax", "Assassinate", "Steal", "Exchange"]

# Responses
RESP_PASS      = 0
RESP_CHALLENGE = 1
RESP_BLOCK     = 2

# Characters
CHAR_DUKE       = 0
CHAR_ASSASSIN   = 1
CHAR_CAPTAIN    = 2
CHAR_AMBASSADOR = 3
CHAR_CONTESSA   = 4

CHAR_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa", "Facedown"]


def encode_frame(payload: bytes) -> bytes:
    return struct.pack("!H", len(payload)) + payload


class CoupBot:
    def __init__(self, host, port, name, auto_start=0):
        self.host = host
        self.port = port
        self.name = name
        self.auto_start = auto_start  # start game when N players ready (0=disabled)
        self.sock = None
        self.buf = b""

        # Game state
        self.my_user_id = None
        self.my_uuid = None
        self.my_engine_pid = None
        self.player_count = 0
        self.seed = 0
        self.engine = None
        self.in_game = False
        self._pending_action = None   # (send_time, payload)
        self._think_delay = 2.0       # seconds before bot responds
        self._responded_this_window = False  # track if we already responded

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        log.info("Connected to %s:%d", self.host, self.port)

        # Shared secret auth
        self.sock.sendall(AUTH_MAGIC + bytes([len(SHARED_SECRET)]) + SHARED_SECRET)
        resp = self.sock.recv(1)
        if not resp or resp[0] != 0x01:
            raise ConnectionError("Auth rejected by server")
        log.info("Authenticated")

        # SNCP CONNECT
        self._send(bytes([MSG_CONNECT]))

    def _send(self, payload: bytes):
        self.sock.sendall(encode_frame(payload))

    def _recv_frames(self):
        """Read available data and yield complete frame payloads."""
        try:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("Server closed connection")
            self.buf += data
        except BlockingIOError:
            pass
        except socket.timeout:
            pass

        while len(self.buf) >= 2:
            plen = struct.unpack("!H", self.buf[:2])[0]
            if len(self.buf) < 2 + plen:
                break
            payload = self.buf[2:2 + plen]
            self.buf = self.buf[2 + plen:]
            yield payload

    def _init_engine(self):
        """Initialize local C rule engine using lobby flow."""
        try:
            sys.path.insert(0, "/Users/r11/Projects/cui_sandbox/tools/coup_server")
            from coup_engine import CoupEngine
            self.engine = CoupEngine(isolated=True)
            self.engine.init(self.seed)
            for i in range(self.player_count):
                self.engine.submit_add_player()
                self.engine.submit_set_ready(i, 1)
            log.info("C engine initialized: %d players, seed=%d", self.player_count, self.seed)
        except Exception as e:
            log.error("Failed to load C engine: %s", e)
            self.engine = None

    def _handle_message(self, payload: bytes):
        if not payload:
            return
        msg_type = payload[0]

        if msg_type == MSG_USERNAME_REQ:
            log.info("Server requests username, sending '%s'", self.name)
            name_bytes = self.name.encode("utf-8")[:16]
            self._send(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)

        elif msg_type in (MSG_WELCOME, MSG_WELCOME_BACK):
            self.my_user_id = payload[1]
            self.my_uuid = payload[2:38].decode("ascii").rstrip("\x00")
            name_len = payload[38]
            name = payload[39:39 + name_len].decode("utf-8")
            tag = "Welcome back" if msg_type == MSG_WELCOME_BACK else "Welcome"
            log.info("%s! user_id=%d uuid=%s name=%s", tag, self.my_user_id, self.my_uuid[:8], name)

            # Auto-ready
            log.info("Sending READY")
            self._send(bytes([MSG_READY]))

        elif msg_type == MSG_USERNAME_TAKEN:
            log.warning("Username taken, trying with suffix")
            self.name = self.name[:13] + str(random.randint(10, 99))
            name_bytes = self.name.encode("utf-8")[:16]
            self._send(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)

        elif msg_type == MSG_LOBBY_STATE:
            count = payload[1]
            names = []
            ready_count = 0
            offset = 2
            for _ in range(count):
                uid = payload[offset]
                nlen = payload[offset + 1]
                pname = payload[offset + 2:offset + 2 + nlen].decode("utf-8")
                ready = payload[offset + 2 + nlen]
                _is_bot = payload[offset + 3 + nlen]
                _difficulty = payload[offset + 4 + nlen]
                if ready:
                    ready_count += 1
                names.append(f"{pname}({'R' if ready else '-'})")
                offset += 2 + nlen + 3
            self.player_count = count
            log.info("Lobby: %s", " | ".join(names))

            # Auto-start when enough players are ready
            if self.auto_start > 0 and ready_count >= self.auto_start and not self.in_game:
                log.info("Auto-starting game (%d ready)", ready_count)
                self._send(bytes([MSG_START_GAME]))

        elif msg_type == MSG_GAME_START:
            self.seed = struct.unpack("!I", payload[1:5])[0]
            self.my_engine_pid = payload[5]
            self.in_game = True
            log.info("GAME START! I am player %d, seed=%d, %d players",
                     self.my_engine_pid, self.seed, self.player_count)
            self._init_engine()

        elif msg_type == MSG_INPUT_RELAY:
            self._handle_relay(payload)

        elif msg_type == MSG_LOG:
            text_len = payload[1]
            text = payload[2:2 + text_len].decode("utf-8", errors="replace")
            log.info("LOG: %s", text)

        else:
            log.debug("Unhandled msg 0x%02X (%d bytes)", msg_type, len(payload))

    def _handle_relay(self, payload: bytes):
        """Process INPUT_RELAY and feed to local engine, then decide actions."""
        # Format: [0xB2][seq_hi][seq_lo][input_type][player_id][data...]
        seq = struct.unpack("!H", payload[1:3])[0]
        relay_type = payload[3]
        player_id = payload[4]
        data = payload[5:]

        if not self.engine:
            return

        # Cancel any pending action — phase may have changed
        self._pending_action = None

        # Track phase changes to reset response dedup flag
        old_phase = self.engine.phase()

        # Feed relay to local engine
        if relay_type == RELAY_START_GAME:
            self.engine.submit_start()
            log.info("Engine started")

        elif relay_type == RELAY_ACTION:
            action = data[0]
            target = data[1]
            self._last_action = action
            actor_name = f"P{player_id}"
            target_str = f" -> P{target}" if target != 0xFF else ""
            log.info("Action: %s does %s%s", actor_name, ACTION_NAMES[action], target_str)
            self.engine.submit_action(player_id, action, target)

        elif relay_type == RELAY_RESPONSE:
            resp = data[0]
            resp_name = ["Pass", "Challenge", "Block"][resp]
            log.info("Response: P%d %s", player_id, resp_name)
            self.engine.submit_response(player_id, resp)

        elif relay_type == RELAY_BLOCK_CLAIM:
            char = data[0]
            log.info("Block claim: P%d claims %s", player_id, CHAR_NAMES[char])
            self.engine.submit_block_claim(player_id, char)

        elif relay_type == RELAY_LOSE_INFLUENCE:
            card_idx = data[0]
            log.info("Lose influence: P%d loses card %d", player_id, card_idx)
            self.engine.submit_lose_influence(player_id, card_idx)

        elif relay_type == RELAY_EXCHANGE:
            keep0, keep1 = data[0], data[1]
            log.info("Exchange: P%d keeps cards %d,%d", player_id, keep0, keep1)
            self.engine.submit_exchange(player_id, keep0, keep1)

        elif relay_type == RELAY_TIMEOUT:
            log.info("Timeout")
            self.engine.submit_timeout()

        # Reset response dedup flag when phase changes
        new_phase = self.engine.phase() if self.engine else -1
        if new_phase != old_phase:
            self._responded_this_window = False

        # Check game over
        if not self.engine or not self.engine.game_active():
            log.info("=== GAME OVER ===")
            self.in_game = False
            self.engine = None
            self._pending_action = None
            self._responded_this_window = False
            # Re-ready for next game
            log.info("Re-readying for next game")
            self._send(bytes([MSG_READY]))
            return

        # Now decide what to do
        self._decide()

    def _schedule(self, payload):
        """Schedule an action to be sent after the think delay."""
        self._pending_action = (time.time() + self._think_delay, payload)

    def _flush_pending(self):
        """Send the pending action if the delay has elapsed."""
        if self._pending_action and time.time() >= self._pending_action[0]:
            self._send(self._pending_action[1])
            self._pending_action = None

    def _decide(self):
        """Check engine state and schedule an action if it's our turn."""
        if not self.engine or not self.engine.game_active():
            return
        # Don't queue a new action if one is already pending
        if self._pending_action:
            return

        phase = self.engine.phase()
        me = self.my_engine_pid

        # WAITING_FOR_ACTION and it's my turn
        if phase == 1 and self.engine.current_player() == me:
            self._choose_action()

        # Challenge/block windows - need to respond
        elif phase in (2, 3, 4):
            if self.engine.pending_response(me) and not self._responded_this_window:
                self._responded_this_window = True
                self._choose_response(phase)

        # RESOLVING: if we are the blocker, send BLOCK_CLAIM
        elif phase == 7 and self.engine.blocker_id() == me and not self._responded_this_window:
            self._responded_this_window = True
            block_char = self._pick_block_char()
            char_name = CHAR_NAMES[block_char] if block_char < len(CHAR_NAMES) else "?"
            log.info(">> BLOCK_CLAIM: %s", char_name)
            self._send(bytes([MSG_BLOCK_CLAIM, block_char]))

        # Influence loss - pick a card to lose
        elif phase == 5 and self.engine.influence_loser() == me:
            self._choose_influence_loss()

        # Exchange - pick cards to keep
        elif phase == 6 and self.engine.exchange_player() == me:
            self._choose_exchange()

    def _choose_action(self):
        """Pick an action. Simple strategy: coup if 7+ coins, else income/tax."""
        coins = self.engine.player_coins(self.my_engine_pid)
        valid = self.engine.valid_actions()  # bitmask

        # Must coup at 10+
        if coins >= 10:
            target = self._pick_target()
            log.info(">> COUP P%d (forced, %d coins)", target, coins)
            self._schedule(bytes([MSG_ACTION, ACT_COUP, target]))
            return

        # Coup if we can afford it
        if coins >= 7 and (valid & (1 << ACT_COUP)):
            target = self._pick_target()
            log.info(">> COUP P%d (%d coins)", target, coins)
            self._schedule(bytes([MSG_ACTION, ACT_COUP, target]))
            return

        # Try tax (claim Duke) sometimes
        if (valid & (1 << ACT_TAX)) and random.random() < 0.4:
            log.info(">> TAX (claiming Duke)")
            self._schedule(bytes([MSG_ACTION, ACT_TAX, 0xFF]))
            return

        # Try steal sometimes
        if (valid & (1 << ACT_STEAL)) and random.random() < 0.3:
            target = self._pick_target()
            if self.engine.player_coins(target) > 0:
                log.info(">> STEAL from P%d", target)
                self._schedule(bytes([MSG_ACTION, ACT_STEAL, target]))
                return

        # Default: income
        log.info(">> INCOME")
        self._schedule(bytes([MSG_ACTION, ACT_INCOME, 0xFF]))

    def _choose_response(self, phase):
        """Respond to challenge/block windows. Usually pass, occasionally challenge."""
        # Small chance to challenge
        if random.random() < 0.15:
            log.info(">> CHALLENGE!")
            self._send(bytes([MSG_RESPONSE, RESP_CHALLENGE]))
            return
        # Small chance to block (in block window)
        elif phase == 3 and random.random() < 0.2:
            block_char = self._pick_block_char()
            char_name = CHAR_NAMES[block_char] if block_char < len(CHAR_NAMES) else "?"
            log.info(">> BLOCK (claiming %s)", char_name)
            # Send BLOCK response; BLOCK_CLAIM will be sent from _decide
            # when engine enters RESOLVING and we are the blocker
            self._send(bytes([MSG_RESPONSE, RESP_BLOCK]))
        else:
            log.info(">> PASS")
            self._send(bytes([MSG_RESPONSE, RESP_PASS]))

    def _choose_influence_loss(self):
        """Lose an influence card. Pick the first unrevealed card."""
        for slot in range(2):
            if not self.engine.player_revealed(self.my_engine_pid, slot):
                log.info(">> LOSE card %d", slot)
                self._send(bytes([MSG_LOSE_INFLUENCE, slot]))
                return
        # Fallback
        self._send(bytes([MSG_LOSE_INFLUENCE, 0]))

    def _choose_exchange(self):
        """Pick cards to keep in exchange. Keep first two."""
        log.info(">> EXCHANGE: keep 0, 1")
        self._send(bytes([MSG_EXCHANGE, 0, 1]))

    def _pick_target(self):
        """Pick a random alive opponent."""
        targets = []
        for pid in range(self.player_count):
            if pid != self.my_engine_pid and self.engine.player_alive(pid):
                targets.append(pid)
        return random.choice(targets) if targets else 0

    def _pick_block_char(self):
        """Pick a valid character to claim when blocking the current action."""
        me = self.my_engine_pid

        # Determine which characters can block by scanning last block_opened event
        valid_block_chars = set()
        for evt_type, idx in self.engine.get_events():
            if evt_type == 5:  # EVT_BLOCK_OPENED
                mask = self.engine.evt_block_opened_blockable_by(idx)
                for c in range(5):
                    if mask & (1 << c):
                        valid_block_chars.add(c)

        # If we couldn't find valid chars from events, infer from last known action
        if not valid_block_chars:
            # Fallback: use the last action we saw
            if hasattr(self, '_last_action'):
                action = self._last_action
                if action == ACT_FOREIGN_AID:
                    valid_block_chars = {CHAR_DUKE}
                elif action == ACT_STEAL:
                    valid_block_chars = {CHAR_CAPTAIN, CHAR_AMBASSADOR}
                elif action == ACT_ASSASSINATE:
                    valid_block_chars = {CHAR_CONTESSA}

        if not valid_block_chars:
            return CHAR_CONTESSA  # last resort

        # Prefer a character we actually hold
        my_cards = []
        for slot in range(2):
            if not self.engine.player_revealed(me, slot):
                my_cards.append(self.engine.player_card(me, slot))

        for card in my_cards:
            if card in valid_block_chars:
                return card

        # Bluff: pick any valid blocking character
        return next(iter(valid_block_chars))

    def run(self):
        """Main loop."""
        self.connect()
        self.sock.setblocking(False)
        last_heartbeat = time.time()

        log.info("Bot running, waiting for game...")

        while True:
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

            # Flush delayed actions
            self._flush_pending()

            # Heartbeat every 30s
            now = time.time()
            if now - last_heartbeat > 30:
                try:
                    self._send(bytes([MSG_HEARTBEAT]))
                    last_heartbeat = now
                except OSError:
                    break

        if self.sock:
            self.sock.close()
        log.info("Bot exited")


def main():
    parser = argparse.ArgumentParser(description="Coup Bot Player")
    parser.add_argument("--host", default="localhost", help="Server host")
    parser.add_argument("--port", type=int, default=4821, help="Server port")
    parser.add_argument("--name", default="CoupBot", help="Bot username")
    parser.add_argument("--auto-start", type=int, default=0,
                        help="Auto-start game when N players are ready (0=disabled)")
    args = parser.parse_args()

    bot = CoupBot(args.host, args.port, args.name, auto_start=args.auto_start)
    try:
        bot.run()
    except KeyboardInterrupt:
        log.info("Interrupted")
    except Exception as e:
        log.error("Fatal: %s", e)
        raise


if __name__ == "__main__":
    main()
