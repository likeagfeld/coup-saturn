#!/usr/bin/env python3
"""
Coup Bot Stress Test - Run N automated games and report bugs.

Starts the server in-process, connects bot clients, and plays N full
games. Tracks anomalies: engine desyncs, stuck phases, rejected inputs,
rule violations, and unexpected game-over states.

Usage:
    python bot_stress_test.py [--games 10] [--players 4] [--port 4899]
"""

import argparse
import logging
import random
import select
import socket
import struct
import subprocess
import sys
import threading
import time

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)-10s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("stress")

# --- Protocol constants ---
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
MSG_START_GAME    = 0x16

MSG_USERNAME_REQ  = 0x81
MSG_WELCOME       = 0x82
MSG_WELCOME_BACK  = 0x83
MSG_USERNAME_TAKEN = 0x84
MSG_LOBBY_STATE   = 0xA0
MSG_GAME_START    = 0xA1
MSG_LOG           = 0xAE
MSG_INPUT_RELAY   = 0xB2

RELAY_START_GAME     = 0
RELAY_ACTION         = 1
RELAY_RESPONSE       = 2
RELAY_BLOCK_CLAIM    = 3
RELAY_LOSE_INFLUENCE = 4
RELAY_EXCHANGE       = 5
RELAY_TIMEOUT        = 6

ACT_INCOME      = 0
ACT_FOREIGN_AID = 1
ACT_COUP        = 2
ACT_TAX         = 3
ACT_ASSASSINATE = 4
ACT_STEAL       = 5
ACT_EXCHANGE    = 6

ACTION_NAMES = ["Income", "Foreign Aid", "Coup", "Tax", "Assassinate", "Steal", "Exchange"]

RESP_PASS      = 0
RESP_CHALLENGE = 1
RESP_BLOCK     = 2

CHAR_DUKE       = 0
CHAR_ASSASSIN   = 1
CHAR_CAPTAIN    = 2
CHAR_AMBASSADOR = 3
CHAR_CONTESSA   = 4
CHAR_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa", "Facedown"]

# Engine phases
PHASE_LOBBY = 0
PHASE_WAITING_FOR_ACTION = 1
PHASE_CHALLENGE_WINDOW = 2
PHASE_BLOCK_WINDOW = 3
PHASE_BLOCK_CHALLENGE_WINDOW = 4
PHASE_WAITING_FOR_INFLUENCE_LOSS = 5
PHASE_WAITING_FOR_EXCHANGE = 6
PHASE_RESOLVING = 7


def encode_frame(payload: bytes) -> bytes:
    return struct.pack("!H", len(payload)) + payload


class BotClient:
    """A bot that connects to the server and plays automatically."""

    def __init__(self, name, host, port, is_host=False, findings=None):
        self.name = name
        self.host = host
        self.port = port
        self.is_host = is_host
        self.findings = findings if findings is not None else []
        self.log = logging.getLogger(name[:10])

        self.sock = None
        self.buf = b""
        self.my_user_id = None
        self.my_uuid = None
        self.my_engine_pid = None
        self.player_count = 0
        self.seed = 0
        self.engine = None
        self.in_game = False
        self.game_over = False
        self.game_number = 0
        self.turn_count = 0
        self.lobby_ready_count = 0
        self.expected_players = 0
        self.winner_pid = -1
        self.actions_taken = 0
        self.rejected_inputs = 0

    def connect(self, expected_players):
        self.expected_players = expected_players
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))

        # Auth
        self.sock.sendall(AUTH_MAGIC + bytes([len(SHARED_SECRET)]) + SHARED_SECRET)
        resp = self.sock.recv(1)
        if not resp or resp[0] != 0x01:
            raise ConnectionError(f"{self.name}: Auth rejected")

        # CONNECT
        self._send(bytes([MSG_CONNECT]))
        self.sock.setblocking(False)

    def _send(self, payload: bytes):
        try:
            self.sock.sendall(encode_frame(payload))
        except OSError as e:
            self.log.error("Send failed: %s", e)

    def process(self):
        """Read and handle all available messages. Returns True if still running."""
        try:
            data = self.sock.recv(4096)
            if not data:
                return False
            self.buf += data
        except BlockingIOError:
            pass
        except socket.timeout:
            pass
        except OSError:
            return False

        while len(self.buf) >= 2:
            plen = struct.unpack("!H", self.buf[:2])[0]
            if len(self.buf) < 2 + plen:
                break
            payload = self.buf[2:2 + plen]
            self.buf = self.buf[2 + plen:]
            self._handle(payload)

        return True

    def _handle(self, payload):
        if not payload:
            return
        msg = payload[0]

        if msg == MSG_USERNAME_REQ:
            name_bytes = self.name.encode("utf-8")[:16]
            self._send(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)

        elif msg in (MSG_WELCOME, MSG_WELCOME_BACK):
            self.my_user_id = payload[1]
            self.my_uuid = payload[2:38].decode("ascii").rstrip("\x00")
            self._send(bytes([MSG_READY]))

        elif msg == MSG_USERNAME_TAKEN:
            self.name = self.name[:13] + str(random.randint(10, 99))
            name_bytes = self.name.encode("utf-8")[:16]
            self._send(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)

        elif msg == MSG_LOBBY_STATE:
            count = payload[1]
            ready = 0
            offset = 2
            for _ in range(count):
                nlen = payload[offset + 1]
                r = payload[offset + 2 + nlen]
                if r:
                    ready += 1
                offset += 2 + nlen + 1
            self.lobby_ready_count = ready

            # Host auto-starts when all players are ready
            if self.is_host and not self.in_game and ready >= self.expected_players:
                time.sleep(0.1)
                self._send(bytes([MSG_START_GAME]))

        elif msg == MSG_GAME_START:
            self.my_engine_pid = payload[1]
            self.seed = struct.unpack("!I", payload[2:6])[0]
            self.player_count = payload[6]
            self.in_game = True
            self.game_over = False
            self.turn_count = 0
            self.actions_taken = 0
            self.winner_pid = -1
            self.game_number += 1
            self._init_engine()

        elif msg == MSG_INPUT_RELAY:
            self._handle_relay(payload)

        elif msg == MSG_LOG:
            text_len = payload[1]
            text = payload[2:2 + text_len].decode("utf-8", errors="replace")
            # Check for interesting log messages
            if "wins" in text.lower():
                self.log.info("Game %d: %s", self.game_number, text)

    def _init_engine(self):
        try:
            sys.path.insert(0, "/Users/r11/Projects/cui_sandbox/tools/coup_server")
            from coup_engine import CoupEngine
            self.engine = CoupEngine(isolated=True)
            self.engine.init(self.player_count, self.seed)
        except Exception as e:
            self.findings.append(f"Game {self.game_number}: {self.name} failed to init engine: {e}")
            self.engine = None

    def _handle_relay(self, payload):
        relay_type = payload[1]
        player_id = payload[2]
        data = payload[3:]

        if not self.engine:
            return

        # Feed to engine
        result = None
        try:
            if relay_type == RELAY_START_GAME:
                result = self.engine.submit_start()
            elif relay_type == RELAY_ACTION:
                result = self.engine.submit_action(player_id, data[0], data[1])
                if player_id == self.my_engine_pid:
                    self.actions_taken += 1
            elif relay_type == RELAY_RESPONSE:
                result = self.engine.submit_response(player_id, data[0])
            elif relay_type == RELAY_BLOCK_CLAIM:
                result = self.engine.submit_block_claim(player_id, data[0])
            elif relay_type == RELAY_LOSE_INFLUENCE:
                result = self.engine.submit_lose_influence(player_id, data[0])
            elif relay_type == RELAY_EXCHANGE:
                result = self.engine.submit_exchange(player_id, data[0], data[1])
            elif relay_type == RELAY_TIMEOUT:
                result = self.engine.submit_timeout()
        except Exception as e:
            self.findings.append(
                f"Game {self.game_number}: {self.name} engine exception on relay_type={relay_type}: {e}")
            return

        if result is not None and result < 0:
            self.rejected_inputs += 1
            self.findings.append(
                f"Game {self.game_number}: {self.name} engine rejected relay_type={relay_type} "
                f"pid={player_id} phase={self.engine.phase()} (result={result})")
            return

        # Validate state
        self._validate_state()

        # Check game over
        if not self.engine.game_active():
            self.game_over = True
            self.in_game = False
            # Find winner
            for pid in range(self.player_count):
                if self.engine.player_alive(pid):
                    self.winner_pid = pid
                    break

            # Validate: exactly one player alive
            alive = sum(1 for p in range(self.player_count) if self.engine.player_alive(p))
            if alive != 1:
                self.findings.append(
                    f"Game {self.game_number}: BUG - game over with {alive} alive players (expected 1)")

            # Auto-ready for next game after a short delay
            if self.is_host:
                time.sleep(0.2)
            self._send(bytes([MSG_READY]))
            return

        # Decide action
        self._decide()

    def _validate_state(self):
        """Run validation checks on engine state."""
        if not self.engine or not self.engine.game_active():
            return

        phase = self.engine.phase()

        # Check: no player has negative-ish coins (overflow)
        for pid in range(self.player_count):
            coins = self.engine.player_coins(pid)
            if coins > 50:
                self.findings.append(
                    f"Game {self.game_number}: BUG - P{pid} has {coins} coins (likely overflow)")

        # Check: eliminated players have both cards revealed
        for pid in range(self.player_count):
            if not self.engine.player_alive(pid):
                r0 = self.engine.player_revealed(pid, 0)
                r1 = self.engine.player_revealed(pid, 1)
                if not (r0 and r1):
                    self.findings.append(
                        f"Game {self.game_number}: BUG - P{pid} eliminated but cards not both revealed "
                        f"(r0={r0}, r1={r1})")

        # Check: current player should be alive during action phase
        if phase == PHASE_WAITING_FOR_ACTION:
            cp = self.engine.current_player()
            if cp < self.player_count and not self.engine.player_alive(cp):
                self.findings.append(
                    f"Game {self.game_number}: BUG - current player P{cp} is dead in action phase")

        # Track turn count
        if phase == PHASE_WAITING_FOR_ACTION:
            self.turn_count += 1
            if self.turn_count > 200:
                self.findings.append(
                    f"Game {self.game_number}: SUSPICIOUS - {self.turn_count} turns, possible infinite loop")

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
            self._send(bytes([MSG_BLOCK_CLAIM, block_char]))
        elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS and self.engine.influence_loser() == me:
            self._choose_influence_loss()
        elif phase == PHASE_WAITING_FOR_EXCHANGE and self.engine.exchange_player() == me:
            self._choose_exchange()

    def _choose_action(self):
        coins = self.engine.player_coins(self.my_engine_pid)
        valid = self.engine.valid_actions()

        # Must coup at 10+
        if coins >= 10:
            target = self._pick_target()
            self._send(bytes([MSG_ACTION, ACT_COUP, target]))
            return

        # Validate: must coup flag
        if coins >= 10 and not (valid & (1 << ACT_COUP)):
            self.findings.append(
                f"Game {self.game_number}: BUG - {coins} coins but coup not valid (mask={valid:#04x})")

        # Random strategy for variety
        choice = random.random()

        if coins >= 7 and choice < 0.5:
            target = self._pick_target()
            self._send(bytes([MSG_ACTION, ACT_COUP, target]))
        elif choice < 0.25 and (valid & (1 << ACT_TAX)):
            self._send(bytes([MSG_ACTION, ACT_TAX, 0xFF]))
        elif choice < 0.40 and (valid & (1 << ACT_STEAL)):
            target = self._pick_target()
            self._send(bytes([MSG_ACTION, ACT_STEAL, target]))
        elif choice < 0.50 and (valid & (1 << ACT_ASSASSINATE)) and coins >= 3:
            target = self._pick_target()
            self._send(bytes([MSG_ACTION, ACT_ASSASSINATE, target]))
        elif choice < 0.60 and (valid & (1 << ACT_EXCHANGE)):
            self._send(bytes([MSG_ACTION, ACT_EXCHANGE, 0xFF]))
        elif choice < 0.70 and (valid & (1 << ACT_FOREIGN_AID)):
            self._send(bytes([MSG_ACTION, ACT_FOREIGN_AID, 0xFF]))
        else:
            self._send(bytes([MSG_ACTION, ACT_INCOME, 0xFF]))

    def _choose_response(self, phase):
        r = random.random()
        if r < 0.2:
            self._send(bytes([MSG_RESPONSE, RESP_CHALLENGE]))
        elif phase == PHASE_BLOCK_WINDOW and r < 0.35:
            self._send(bytes([MSG_RESPONSE, RESP_BLOCK]))
        else:
            self._send(bytes([MSG_RESPONSE, RESP_PASS]))

    def _choose_influence_loss(self):
        for slot in range(2):
            if not self.engine.player_revealed(self.my_engine_pid, slot):
                self._send(bytes([MSG_LOSE_INFLUENCE, slot]))
                return
        self._send(bytes([MSG_LOSE_INFLUENCE, 0]))

    def _choose_exchange(self):
        count = self.engine.exchange_count()
        if count >= 2:
            self._send(bytes([MSG_EXCHANGE, 0, 1]))
        else:
            self._send(bytes([MSG_EXCHANGE, 0, 1]))

    def _pick_target(self):
        targets = [p for p in range(self.player_count)
                   if p != self.my_engine_pid and self.engine.player_alive(p)]
        return random.choice(targets) if targets else 0

    def _pick_block_char(self):
        """Pick block character. Check our cards, fallback to Contessa."""
        me = self.my_engine_pid
        for slot in range(2):
            if not self.engine.player_revealed(me, slot):
                c = self.engine.player_card(me, slot)
                if c in (CHAR_DUKE, CHAR_CAPTAIN, CHAR_AMBASSADOR, CHAR_CONTESSA):
                    return c
        return CHAR_CONTESSA

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass


def run_stress_test(num_games, num_players, port):
    findings = []

    # Start server
    log.info("Starting server on port %d...", port)
    server_proc = subprocess.Popen(
        [sys.executable, "tools/coup_server/server.py", "--port", str(port)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True,
    )
    time.sleep(1.0)

    if server_proc.poll() is not None:
        log.error("Server failed to start!")
        out = server_proc.stdout.read()
        log.error(out)
        return findings

    log.info("Server started (pid=%d)", server_proc.pid)

    # Create bots
    bot_names = ["Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel"]
    bots = []
    for i in range(num_players):
        bot = BotClient(
            name=bot_names[i % len(bot_names)],
            host="localhost",
            port=port,
            is_host=(i == 0),
            findings=findings,
        )
        bots.append(bot)

    # Connect all bots
    for bot in bots:
        try:
            bot.connect(num_players)
            log.info("%s connected", bot.name)
        except Exception as e:
            log.error("Failed to connect %s: %s", bot.name, e)
            findings.append(f"Connection failure: {bot.name}: {e}")

    games_completed = 0
    start_time = time.time()
    max_time = 300  # 5 min safety limit
    last_game = 0
    stall_start = None

    log.info("Running %d games with %d players...", num_games, num_players)
    log.info("=" * 60)

    while games_completed < num_games:
        elapsed = time.time() - start_time
        if elapsed > max_time:
            findings.append(f"TIMEOUT: Only completed {games_completed}/{num_games} games in {max_time}s")
            break

        # Gather sockets for select
        socks = [b.sock for b in bots if b.sock]
        if not socks:
            findings.append("All bots disconnected!")
            break

        try:
            readable, _, _ = select.select(socks, [], [], 0.5)
        except (ValueError, OSError):
            break

        for bot in bots:
            if bot.sock in readable:
                if not bot.process():
                    findings.append(f"Game {bot.game_number}: {bot.name} disconnected unexpectedly")

        # Check game completion
        completed = min(b.game_number for b in bots if b.game_over or b.game_number > 0) if any(b.game_number > 0 for b in bots) else 0
        # Use host bot as source of truth for game count
        host_bot = bots[0]
        if host_bot.game_over and host_bot.game_number > games_completed:
            games_completed = host_bot.game_number
            winner = host_bot.winner_pid
            turns = host_bot.turn_count
            log.info("Game %d/%d complete — winner: P%d, turns: %d",
                     games_completed, num_games, winner, turns)

            if turns < 2:
                findings.append(f"Game {games_completed}: SUSPICIOUS - only {turns} turns")
            if turns > 150:
                findings.append(f"Game {games_completed}: SUSPICIOUS - {turns} turns (very long game)")

            host_bot.game_over = False  # Reset so we detect next game

            # Stall detection reset
            stall_start = None
            last_game = games_completed

            # Re-ready for next game (host auto-starts via lobby handler)
        else:
            # Stall detection
            if stall_start is None:
                stall_start = time.time()
            elif time.time() - stall_start > 30:
                phase = host_bot.engine.phase() if host_bot.engine else "?"
                findings.append(
                    f"Game {last_game + 1}: STALL - no progress for 30s (phase={phase})")
                stall_start = time.time()  # Reset to avoid spam

    elapsed = time.time() - start_time

    # Collect stats
    log.info("=" * 60)
    log.info("Stress test complete: %d/%d games in %.1fs", games_completed, num_games, elapsed)

    total_rejected = sum(b.rejected_inputs for b in bots)
    if total_rejected > 0:
        log.info("Total rejected engine inputs: %d", total_rejected)

    # Clean up
    for bot in bots:
        bot.close()
    server_proc.terminate()
    server_proc.wait(timeout=5)

    return findings


def main():
    parser = argparse.ArgumentParser(description="Coup Bot Stress Test")
    parser.add_argument("--games", type=int, default=10, help="Number of games to play")
    parser.add_argument("--players", type=int, default=4, help="Number of bot players")
    parser.add_argument("--port", type=int, default=4899, help="Server port (avoid conflict)")
    args = parser.parse_args()

    findings = run_stress_test(args.games, args.players, args.port)

    print()
    print("=" * 60)
    print(f"FINDINGS ({len(findings)} issues)")
    print("=" * 60)
    if findings:
        for i, f in enumerate(findings, 1):
            print(f"  {i}. {f}")
    else:
        print("  No bugs or anomalies detected!")
    print("=" * 60)


if __name__ == "__main__":
    main()
