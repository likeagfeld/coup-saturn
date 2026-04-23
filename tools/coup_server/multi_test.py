#!/usr/bin/env python3
"""Connect 5 human players, ready up, and start a game."""

import select
import socket
import struct
import sys
import time

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
MSG_ACTION_REJECTED = 0xB5
MSG_RESYNC        = 0xB3
MSG_RESYNC_FULL   = 0xB4

RELAY_NAMES = ["StartGame", "Action", "Response", "BlockClaim", "LoseInfluence", "Exchange", "Timeout"]
ACTION_NAMES = ["Income", "ForeignAid", "Coup", "Tax", "Assassinate", "Steal", "Exchange"]
RESP_NAMES = ["Pass", "Challenge", "Block"]
CHAR_NAMES = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa", "Facedown"]

NAMES = ["Alice", "Bob", "Carol", "Dave", "Eve"]

HOST = "18.118.161.220"
PORT = 38271


def encode(payload):
    return struct.pack("!H", len(payload)) + payload


class Player:
    def __init__(self, name):
        self.name = name
        self.sock = None
        self.buf = b""
        self.user_id = None
        self.engine_pid = None
        self.in_game = False
        self.welcomed = False

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect((HOST, PORT))
        # Auth
        self.sock.sendall(AUTH_MAGIC + bytes([len(SHARED_SECRET)]) + SHARED_SECRET)
        resp = self.sock.recv(1)
        if not resp or resp[0] != 0x01:
            raise ConnectionError(f"{self.name}: auth rejected")
        self.sock.setblocking(False)
        # CONNECT
        self.send(bytes([MSG_CONNECT]))
        print(f"  {self.name} connected")

    def send(self, payload):
        self.sock.sendall(encode(payload))

    def recv_frames(self):
        try:
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError(f"{self.name}: server closed")
            self.buf += data
        except (BlockingIOError, socket.timeout):
            pass
        frames = []
        while len(self.buf) >= 2:
            plen = struct.unpack("!H", self.buf[:2])[0]
            if len(self.buf) < 2 + plen:
                break
            frames.append(self.buf[2:2 + plen])
            self.buf = self.buf[2 + plen:]
        return frames

    def handle(self, payload):
        if not payload:
            return
        mt = payload[0]

        if mt == MSG_USERNAME_REQ:
            nb = self.name.encode("utf-8")[:16]
            self.send(bytes([MSG_SET_USERNAME, len(nb)]) + nb)

        elif mt in (MSG_WELCOME, MSG_WELCOME_BACK):
            self.user_id = payload[1]
            self.welcomed = True
            tag = "back" if mt == MSG_WELCOME_BACK else "new"
            print(f"  {self.name}: welcomed ({tag}), user_id={self.user_id}")

        elif mt == MSG_USERNAME_TAKEN:
            self.name += "2"
            nb = self.name.encode("utf-8")[:16]
            self.send(bytes([MSG_SET_USERNAME, len(nb)]) + nb)

        elif mt == MSG_LOBBY_STATE:
            count = payload[1]
            off = 2
            players = []
            for _ in range(count):
                pid = payload[off]
                nlen = payload[off + 1]
                pname = payload[off + 2:off + 2 + nlen].decode()
                ready = payload[off + 2 + nlen]
                is_bot = payload[off + 3 + nlen]
                _difficulty = payload[off + 4 + nlen]
                off += 5 + nlen
                players.append(f"{pname}({'R' if ready else '-'})")
            print(f"  LOBBY: {' | '.join(players)}")

        elif mt == MSG_GAME_START:
            seed = struct.unpack("!I", payload[1:5])[0]
            self.engine_pid = payload[5]
            self.in_game = True
            print(f"  {self.name}: GAME START seed={seed} pid={self.engine_pid}")

        elif mt == MSG_INPUT_RELAY:
            seq = struct.unpack("!H", payload[1:3])[0]
            rtype = payload[3]
            pid = payload[4]
            data = payload[5:]
            rname = RELAY_NAMES[rtype] if rtype < len(RELAY_NAMES) else f"?{rtype}"

            extra = ""
            if rtype == 1 and len(data) >= 2:  # Action
                aname = ACTION_NAMES[data[0]] if data[0] < len(ACTION_NAMES) else f"?{data[0]}"
                extra = f" {aname} target={data[1]}"
            elif rtype == 2 and data:  # Response
                extra = f" {RESP_NAMES[data[0]]}" if data[0] < len(RESP_NAMES) else f" ?{data[0]}"
            elif rtype == 3 and data:  # BlockClaim
                extra = f" as {CHAR_NAMES[data[0]]}" if data[0] < len(CHAR_NAMES) else ""
            elif rtype == 4 and data:  # LoseInfluence
                extra = f" card={data[0]}"

            # Only print from first player's perspective to avoid spam
            if self.name == NAMES[0]:
                print(f"  [{seq}] P{pid} {rname}{extra}")

        elif mt == MSG_LOG:
            tlen = payload[1]
            text = payload[2:2 + tlen].decode("utf-8", errors="replace")
            if self.name == NAMES[0]:
                print(f"  [LOG] {text}")

        elif mt == MSG_ACTION_REJECTED:
            seq = struct.unpack("!H", payload[1:3])[0]
            print(f"  {self.name}: ACTION REJECTED seq={seq}")


def pump_all(players, duration=1.0):
    """Process messages for all players for `duration` seconds."""
    end = time.time() + duration
    while time.time() < end:
        socks = [p.sock for p in players if p.sock]
        if not socks:
            break
        readable, _, _ = select.select(socks, [], [], 0.1)
        for p in players:
            if p.sock in readable:
                for frame in p.recv_frames():
                    p.handle(frame)


def main():
    players = [Player(n) for n in NAMES]

    print(f"Connecting 5 players to {HOST}:{PORT}...")
    for p in players:
        p.connect()
        time.sleep(0.3)

    # Wait for welcome messages
    print("\nWaiting for auth...")
    pump_all(players, 3)

    # Ready up
    print("\nAll players readying up...")
    for p in players:
        p.send(bytes([MSG_READY]))
        time.sleep(0.2)

    pump_all(players, 2)

    # Start game
    print("\nRequesting game start...")
    players[0].send(bytes([MSG_START_GAME]))

    # Monitor game
    print("\nMonitoring game (showing Alice's view, Ctrl+C to stop)...\n")
    try:
        while True:
            pump_all(players, 0.5)
            # Send heartbeats periodically
    except KeyboardInterrupt:
        print("\n\nStopping...")

    for p in players:
        try:
            p.sock.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
