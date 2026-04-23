#!/usr/bin/env python3
"""
Interactive human client for Coup server testing.

Connects to a Coup server, authenticates, and provides a text-based
interface for playing the game as a human player.

Usage:
    python human_client.py --host 18.118.161.220 --port 38271 --name Alice
    python human_client.py --host 18.118.161.220 --port 38271 --name Bob
"""

import argparse
import logging
import select
import socket
import struct
import sys
import threading
import time

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [CLIENT] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("human")

# --- Protocol constants ---
SHARED_SECRET = b"SaturnCoup2025!NetLink#SecretKey"
AUTH_MAGIC = b"AUTH"

# Client -> Server
MSG_CONNECT        = 0x01
MSG_SET_USERNAME   = 0x02
MSG_HEARTBEAT      = 0x04
MSG_DISCONNECT     = 0x05
MSG_READY          = 0x10
MSG_ACTION         = 0x11
MSG_RESPONSE       = 0x12
MSG_BLOCK_CLAIM    = 0x13
MSG_LOSE_INFLUENCE = 0x14
MSG_EXCHANGE       = 0x15
MSG_START_GAME     = 0x16
MSG_ADD_BOT        = 0x17
MSG_REMOVE_BOT     = 0x18
MSG_RESYNC_REQ     = 0x1A

# Server -> Client
MSG_USERNAME_REQ   = 0x81
MSG_WELCOME        = 0x82
MSG_WELCOME_BACK   = 0x83
MSG_USERNAME_TAKEN = 0x84
MSG_LOBBY_STATE    = 0xA0
MSG_GAME_START     = 0xA1
MSG_LOG            = 0xAE
MSG_INPUT_RELAY    = 0xB2
MSG_RESYNC         = 0xB3
MSG_RESYNC_FULL    = 0xB4
MSG_ACTION_REJECTED = 0xB5

# Relay types
RELAY_START_GAME     = 0
RELAY_ACTION         = 1
RELAY_RESPONSE       = 2
RELAY_BLOCK_CLAIM    = 3
RELAY_LOSE_INFLUENCE = 4
RELAY_EXCHANGE       = 5
RELAY_TIMEOUT        = 6

RELAY_NAMES = ["StartGame", "Action", "Response", "BlockClaim", "LoseInfluence", "Exchange", "Timeout"]

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

RESP_NAMES = ["Pass", "Challenge", "Block"]

# Characters
CHAR_DUKE       = 0
CHAR_ASSASSIN   = 1
CHAR_CAPTAIN    = 2
CHAR_AMBASSADOR = 3
CHAR_CONTESSA   = 4

CHAR_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa", "Facedown"]


def encode_frame(payload: bytes) -> bytes:
    return struct.pack("!H", len(payload)) + payload


class HumanClient:
    def __init__(self, host, port, name):
        self.host = host
        self.port = port
        self.name = name
        self.sock = None
        self.buf = b""
        self.my_user_id = None
        self.my_engine_pid = None
        self.in_game = False
        self.player_names = {}  # id -> name
        self.running = True
        self.relay_seq = 0

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect((self.host, self.port))
        log.info("Connected to %s:%d", self.host, self.port)

        # Shared secret auth
        self.sock.sendall(AUTH_MAGIC + bytes([len(SHARED_SECRET)]) + SHARED_SECRET)
        resp = self.sock.recv(1)
        if not resp or resp[0] != 0x01:
            raise ConnectionError("Auth rejected by server")
        log.info("Authenticated")

        self.sock.setblocking(False)
        # SNCP CONNECT
        self._send(bytes([MSG_CONNECT]))

    def _send(self, payload: bytes):
        self.sock.sendall(encode_frame(payload))

    def _recv_frames(self):
        try:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("Server closed connection")
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

    def _handle_message(self, payload):
        if not payload:
            return
        msg_type = payload[0]

        if msg_type == MSG_USERNAME_REQ:
            log.info("Server requests username, sending '%s'", self.name)
            name_bytes = self.name.encode("utf-8")[:16]
            self._send(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)

        elif msg_type in (MSG_WELCOME, MSG_WELCOME_BACK):
            self.my_user_id = payload[1]
            uuid_str = payload[2:38].decode("ascii").rstrip("\x00")
            name_len = payload[38]
            name = payload[39:39 + name_len].decode("utf-8")
            tag = "Welcome back" if msg_type == MSG_WELCOME_BACK else "Welcome"
            print(f"\n*** {tag}, {name}! (user_id={self.my_user_id}, uuid={uuid_str[:8]}...)")
            print("Type 'help' for commands.\n")

        elif msg_type == MSG_USERNAME_TAKEN:
            print("*** Username taken! Enter a different name:")
            new_name = input("> ").strip()
            if new_name:
                self.name = new_name
            name_bytes = self.name.encode("utf-8")[:16]
            self._send(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)

        elif msg_type == MSG_LOBBY_STATE:
            count = payload[1]
            offset = 2
            self.player_names = {}
            print("\n=== LOBBY ===")
            for i in range(count):
                pid = payload[offset]
                nlen = payload[offset + 1]
                pname = payload[offset + 2:offset + 2 + nlen].decode("utf-8")
                ready = payload[offset + 2 + nlen]
                is_bot = payload[offset + 3 + nlen]
                _difficulty = payload[offset + 4 + nlen]
                offset += 5 + nlen
                self.player_names[pid] = pname
                status = "READY" if ready else "not ready"
                bot_tag = " [BOT]" if is_bot else ""
                print(f"  [{pid}] {pname}{bot_tag} - {status}")
            print("=============\n")

        elif msg_type == MSG_GAME_START:
            seed = struct.unpack("!I", payload[1:5])[0]
            self.my_engine_pid = payload[5]
            self.in_game = True
            self.relay_seq = 0
            print(f"\n*** GAME STARTED! seed={seed}, you are player {self.my_engine_pid}")
            print("*** Waiting for game actions...\n")

        elif msg_type == MSG_INPUT_RELAY:
            seq = struct.unpack("!H", payload[1:3])[0]
            relay_type = payload[3]
            player_id = payload[4]
            data = payload[5:]
            self.relay_seq = seq
            pname = self.player_names.get(player_id, f"P{player_id}")

            if relay_type == RELAY_ACTION:
                action = data[0]
                target = data[1]
                tname = self.player_names.get(target, f"P{target}")
                act_name = ACTION_NAMES[action] if action < len(ACTION_NAMES) else f"?{action}"
                if action in (ACT_COUP, ACT_ASSASSINATE, ACT_STEAL):
                    print(f"  [{seq}] {pname} -> {act_name} on {tname}")
                else:
                    print(f"  [{seq}] {pname} -> {act_name}")
            elif relay_type == RELAY_RESPONSE:
                resp = data[0]
                resp_name = RESP_NAMES[resp] if resp < len(RESP_NAMES) else f"?{resp}"
                print(f"  [{seq}] {pname} -> {resp_name}")
            elif relay_type == RELAY_BLOCK_CLAIM:
                char = data[0]
                char_name = CHAR_NAMES[char] if char < len(CHAR_NAMES) else f"?{char}"
                print(f"  [{seq}] {pname} -> Block as {char_name}")
            elif relay_type == RELAY_LOSE_INFLUENCE:
                card_idx = data[0]
                print(f"  [{seq}] {pname} -> Loses influence (card {card_idx})")
            elif relay_type == RELAY_EXCHANGE:
                print(f"  [{seq}] {pname} -> Exchange choice: keep {data[0]},{data[1]}")
            elif relay_type == RELAY_TIMEOUT:
                print(f"  [{seq}] {pname} -> TIMEOUT")
            elif relay_type == RELAY_START_GAME:
                print(f"  [{seq}] Game started")
            else:
                print(f"  [{seq}] {pname} -> relay type {relay_type}: {data.hex()}")

        elif msg_type == MSG_LOG:
            text_len = payload[1]
            text = payload[2:2 + text_len].decode("utf-8", errors="replace")
            print(f"  [LOG] {text}")

        elif msg_type == MSG_ACTION_REJECTED:
            seq = struct.unpack("!H", payload[1:3])[0]
            phase = payload[3] if len(payload) > 3 else -1
            print(f"  *** ACTION REJECTED (server seq={seq}, phase={phase})")

        elif msg_type == MSG_RESYNC:
            count = payload[1]
            print(f"  *** RESYNC: {count} entries")

        elif msg_type == MSG_RESYNC_FULL:
            seed = struct.unpack("!I", payload[1:5])[0]
            pid = payload[5]
            total = struct.unpack("!H", payload[6:8])[0]
            print(f"  *** RESYNC_FULL: seed={seed} pid={pid} total={total}")

        else:
            print(f"  [MSG 0x{msg_type:02X}] {payload[1:].hex()}")

    def _print_help(self):
        print("""
=== COMMANDS ===
  ready          - Toggle ready state
  start          - Request game start
  addbot [0-2]   - Add bot (0=easy, 1=medium, 2=hard)
  rmbot          - Remove last bot

  --- In-game actions ---
  income         - Take income (+1 coin)
  aid            - Foreign aid (+2 coins)
  tax            - Tax (+3 coins, claim Duke)
  steal <pid>    - Steal from player
  assassinate <pid> - Assassinate player (costs 3)
  coup <pid>     - Coup player (costs 7)
  exchange       - Exchange cards (claim Ambassador)

  --- Responses ---
  pass           - Pass on challenge/block
  challenge      - Challenge the action
  block          - Block the action

  --- Block claims ---
  blockduke      - Block as Duke
  blockassassin  - Block as Assassin  (N/A usually)
  blockcaptain   - Block as Captain
  blockambassador - Block as Ambassador
  blockcontessa  - Block as Contessa

  --- Other ---
  lose <0|1>     - Lose influence (card index 0 or 1)
  keepcards <a> <b> - Exchange: keep cards at index a,b
  resync         - Request resync from server
  quit           - Disconnect
================
""")

    def _process_command(self, cmd):
        parts = cmd.strip().split()
        if not parts:
            return
        c = parts[0].lower()

        try:
            if c == "help":
                self._print_help()
            elif c == "ready":
                self._send(bytes([MSG_READY]))
                print("  -> Toggled ready")
            elif c == "start":
                self._send(bytes([MSG_START_GAME]))
                print("  -> Requested game start")
            elif c == "addbot":
                diff = int(parts[1]) if len(parts) > 1 else 1
                self._send(bytes([MSG_ADD_BOT, diff]))
                print(f"  -> Added bot (difficulty {diff})")
            elif c == "rmbot":
                self._send(bytes([MSG_REMOVE_BOT]))
                print("  -> Removed bot")

            # Actions
            elif c == "income":
                self._send(bytes([MSG_ACTION, ACT_INCOME, 0]))
                print("  -> Income")
            elif c == "aid":
                self._send(bytes([MSG_ACTION, ACT_FOREIGN_AID, 0]))
                print("  -> Foreign Aid")
            elif c == "tax":
                self._send(bytes([MSG_ACTION, ACT_TAX, 0]))
                print("  -> Tax")
            elif c == "steal":
                target = int(parts[1])
                self._send(bytes([MSG_ACTION, ACT_STEAL, target]))
                print(f"  -> Steal from player {target}")
            elif c in ("assassinate", "ass"):
                target = int(parts[1])
                self._send(bytes([MSG_ACTION, ACT_ASSASSINATE, target]))
                print(f"  -> Assassinate player {target}")
            elif c == "coup":
                target = int(parts[1])
                self._send(bytes([MSG_ACTION, ACT_COUP, target]))
                print(f"  -> Coup player {target}")
            elif c == "exchange":
                self._send(bytes([MSG_ACTION, ACT_EXCHANGE, 0]))
                print("  -> Exchange")

            # Responses
            elif c == "pass":
                self._send(bytes([MSG_RESPONSE, RESP_PASS]))
                print("  -> Pass")
            elif c == "challenge":
                self._send(bytes([MSG_RESPONSE, RESP_CHALLENGE]))
                print("  -> Challenge!")
            elif c == "block":
                self._send(bytes([MSG_RESPONSE, RESP_BLOCK]))
                print("  -> Block")

            # Block claims
            elif c == "blockduke":
                self._send(bytes([MSG_BLOCK_CLAIM, CHAR_DUKE]))
                print("  -> Block as Duke")
            elif c == "blockcaptain":
                self._send(bytes([MSG_BLOCK_CLAIM, CHAR_CAPTAIN]))
                print("  -> Block as Captain")
            elif c == "blockambassador":
                self._send(bytes([MSG_BLOCK_CLAIM, CHAR_AMBASSADOR]))
                print("  -> Block as Ambassador")
            elif c == "blockcontessa":
                self._send(bytes([MSG_BLOCK_CLAIM, CHAR_CONTESSA]))
                print("  -> Block as Contessa")

            # Lose influence
            elif c == "lose":
                idx = int(parts[1])
                self._send(bytes([MSG_LOSE_INFLUENCE, idx]))
                print(f"  -> Lose influence card {idx}")

            # Exchange keep
            elif c == "keepcards":
                a, b = int(parts[1]), int(parts[2])
                self._send(bytes([MSG_EXCHANGE, a, b]))
                print(f"  -> Keep cards {a}, {b}")

            elif c == "resync":
                seq = self.relay_seq
                self._send(bytes([MSG_RESYNC_REQ]) + struct.pack("!H", seq))
                print(f"  -> Resync from seq {seq}")

            elif c == "quit":
                self._send(bytes([MSG_DISCONNECT]))
                self.running = False

            else:
                print(f"  Unknown command: {c}. Type 'help' for commands.")

        except (IndexError, ValueError) as e:
            print(f"  Bad command args: {e}. Type 'help' for commands.")

    def run(self):
        self.connect()

        # Heartbeat thread
        def heartbeat():
            while self.running:
                time.sleep(15)
                try:
                    self._send(bytes([MSG_HEARTBEAT]))
                except Exception:
                    break

        hb = threading.Thread(target=heartbeat, daemon=True)
        hb.start()

        print("Connected! Waiting for server messages...\n")

        while self.running:
            # Check for server messages
            readable, _, _ = select.select([self.sock, sys.stdin], [], [], 0.1)

            if self.sock in readable:
                for payload in self._recv_frames():
                    self._handle_message(payload)

            if sys.stdin in readable:
                try:
                    line = sys.stdin.readline()
                    if not line:
                        self.running = False
                        break
                    self._process_command(line)
                except EOFError:
                    self.running = False
                    break

        print("Disconnected.")
        self.sock.close()


def main():
    parser = argparse.ArgumentParser(description="Coup human test client")
    parser.add_argument("--host", default="18.118.161.220")
    parser.add_argument("--port", type=int, default=38271)
    parser.add_argument("--name", default="Player1")
    args = parser.parse_args()

    client = HumanClient(args.host, args.port, args.name)
    try:
        client.run()
    except KeyboardInterrupt:
        print("\nDisconnecting...")
    except ConnectionError as e:
        print(f"Connection error: {e}")


if __name__ == "__main__":
    main()
