"""
Integration tests for the Coup server + C rule engine.

Connects real TCP clients to a live server, exercises the SNCP protocol,
and verifies game state transitions end-to-end.

The server now sends INPUT_RELAY messages instead of individual event messages.
Each test client runs a local copy of the C engine (initialized from the seed
in GAME_START) and feeds INPUT_RELAY inputs to it to derive game state.

Run:  cd tools/coup_server && python3 -m pytest test_server_integration.py -v
"""

import os
import socket
import struct
import tempfile
import threading
import time
import unittest

from server import (
    CoupServer,
    SHARED_SECRET,
    AUTH_MAGIC,
    AUTH_OK,
    MSG_CONNECT,
    MSG_SET_USERNAME,
    MSG_USERNAME_REQUIRED,
    MSG_WELCOME,
    MSG_USERNAME_TAKEN,
    COUP_MSG_READY,
    COUP_MSG_ACTION,
    COUP_MSG_RESPONSE,
    COUP_MSG_BLOCK_CLAIM,
    COUP_MSG_LOSE_INFLUENCE,
    COUP_MSG_EXCHANGE_CHOICE,
    COUP_MSG_START_GAME_REQ,
    COUP_MSG_ADD_BOT,
    COUP_MSG_REMOVE_BOT,
    COUP_MSG_SET_BOT_DIFFICULTY,
    COUP_MSG_RESYNC_REQ,
    COUP_MSG_LOBBY_STATE,
    COUP_MSG_GAME_START,
    COUP_MSG_INPUT_RELAY,
    COUP_MSG_RESYNC,
    COUP_MSG_RESYNC_FULL,
    COUP_MSG_ACTION_REJECTED,
    COUP_MSG_LOG,
    RELAY_START_GAME,
    RELAY_ACTION,
    RELAY_RESPONSE,
    RELAY_BLOCK_CLAIM,
    RELAY_LOSE_INFLUENCE,
    RELAY_EXCHANGE_CHOICE,
    RELAY_TIMEOUT,
    RESP_PASS,
    RESP_CHALLENGE,
    RESP_BLOCK,
    ACT_INCOME,
    ACT_FOREIGN_AID,
    ACT_COUP,
    ACT_TAX,
    ACT_STEAL,
    ACT_ASSASSINATE,
    ACT_EXCHANGE,
    CHAR_DUKE,
    CHAR_ASSASSIN,
    CHAR_CAPTAIN,
    CHAR_AMBASSADOR,
    CHAR_CONTESSA,
    CHAR_FACEDOWN,
)

from coup_engine import (
    CoupEngine,
    PHASE_WAITING_FOR_ACTION,
    PHASE_CHALLENGE_WINDOW,
    PHASE_BLOCK_WINDOW,
    PHASE_BLOCK_CHALLENGE_WINDOW,
    PHASE_WAITING_FOR_INFLUENCE_LOSS,
    PHASE_WAITING_FOR_EXCHANGE,
)


# ---------------------------------------------------------------------------
# Test client — maintains state from server messages via local engine
# ---------------------------------------------------------------------------

class TestClient:
    """TCP client that speaks SNCP and tracks game state via local engine."""

    def __init__(self):
        self.sock = None
        self.user_id = None
        self.uuid = None
        self.username = None
        self._recv_buf = b""
        # Engine state
        self.engine = None
        self.engine_pid = None
        self.seed = None
        # State derived from engine
        self.coins = {}          # {engine_pid: coins}
        self.my_turn = False
        self.cards = (None, None)
        self.game_started = False
        self.game_over = False
        self.winner_id = None
        self.eliminated = set()
        self.lobby_players = 0
        self._lobby_is_bot = []
        self._lobby_difficulty = []
        self.logs = []
        self.inbox = []
        self._pending = []
        # Tracking
        self.turn_player_id = None
        self.input_relays = []
        self.relay_seqs = []        # sequence numbers from INPUT_RELAYs
        self.player_count = 0
        self.action_rejected = []   # list of (seq, phase) from ACTION_REJECTED
        self.resync_msgs = []       # raw RESYNC payloads received
        self.resync_full_msgs = []  # raw RESYNC_FULL payloads received

    # -- connection lifecycle --

    def connect(self, host, port, timeout=5.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect((host, port))
        self.sock.sendall(AUTH_MAGIC + bytes([len(SHARED_SECRET)]) + SHARED_SECRET)
        resp = self._recv_exact(1)
        if resp[0] != AUTH_OK:
            raise RuntimeError(f"Auth failed, got 0x{resp[0]:02x}")

    def register(self, username):
        self.username = username
        self.send_msg(bytes([MSG_CONNECT]))
        msg = self.wait_for(MSG_USERNAME_REQUIRED)
        if msg is None:
            raise RuntimeError("Did not receive USERNAME_REQUIRED")
        name_bytes = username.encode("utf-8")
        self.send_msg(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)
        msg = self.wait_for(MSG_WELCOME)
        if msg is None:
            raise RuntimeError("Did not receive WELCOME")
        self.user_id = msg[1]
        self.uuid = msg[2:38].decode("ascii").rstrip("\x00")

    # -- framed send/recv --

    def send_msg(self, payload):
        frame = struct.pack("!H", len(payload)) + payload
        self.sock.sendall(frame)

    def _read_frames(self, timeout=1.0):
        """Read available frames from socket, feed into inbox + state."""
        msgs = []
        deadline = time.monotonic() + timeout
        self.sock.setblocking(False)
        try:
            while time.monotonic() < deadline:
                while len(self._recv_buf) >= 2:
                    plen = struct.unpack("!H", self._recv_buf[:2])[0]
                    if len(self._recv_buf) < 2 + plen:
                        break
                    payload = self._recv_buf[2:2 + plen]
                    self._recv_buf = self._recv_buf[2 + plen:]
                    self._process(payload)
                    msgs.append(payload)
                try:
                    data = self.sock.recv(4096)
                    if not data:
                        break
                    self._recv_buf += data
                except BlockingIOError:
                    if msgs:
                        break
                    time.sleep(0.02)
        finally:
            self.sock.setblocking(True)
        while len(self._recv_buf) >= 2:
            plen = struct.unpack("!H", self._recv_buf[:2])[0]
            if len(self._recv_buf) < 2 + plen:
                break
            payload = self._recv_buf[2:2 + plen]
            self._recv_buf = self._recv_buf[2 + plen:]
            self._process(payload)
            msgs.append(payload)
        return msgs

    def _process(self, payload):
        """Update local state from a received message."""
        self.inbox.append(payload)
        if not payload:
            return
        t = payload[0]

        if t == COUP_MSG_GAME_START:
            # Simplified format: [0xA1][seed:4 BE][my_engine_pid:1]
            if len(payload) < 6:
                return
            self.seed = struct.unpack("!I", payload[1:5])[0]
            self.engine_pid = payload[5]
            self.game_started = True
            # Use lobby state (is_bot flags) to initialize local engine
            is_bot_flags = self._lobby_is_bot
            if not is_bot_flags:
                # Fallback: if no lobby state received yet, assume all human
                is_bot_flags = [False] * (self.lobby_players or 2)
            self.player_count = len(is_bot_flags)
            self.engine = CoupEngine(isolated=True)
            self.engine.init(self.seed)
            for i in range(self.player_count):
                if is_bot_flags[i]:
                    self.engine.submit_add_bot()
                else:
                    self.engine.submit_add_player()
                    self.engine.submit_set_ready(i, 1)
            self.coins = {i: 2 for i in range(self.player_count)}

        elif t == COUP_MSG_ACTION_REJECTED:
            # [0xB5][seq_hi:1][seq_lo:1][phase:1]
            if len(payload) >= 4:
                seq = (payload[1] << 8) | payload[2]
                phase = payload[3]
                self.action_rejected.append((seq, phase))

        elif t == COUP_MSG_RESYNC:
            self.resync_msgs.append(payload)

        elif t == COUP_MSG_RESYNC_FULL:
            self.resync_full_msgs.append(payload)

        elif t == COUP_MSG_INPUT_RELAY:
            # [0xB2][seq_hi:1][seq_lo:1][input_type:1][player_id:1][data...]
            if len(payload) < 5:
                return
            self.input_relays.append(payload)
            seq = (payload[1] << 8) | payload[2]
            self.relay_seqs.append(seq)
            input_type = payload[3]
            pid = payload[4]
            data = payload[5:]

            if self.engine is None:
                return

            # Submit to local engine
            if input_type == RELAY_START_GAME:
                self.engine.submit_start()
            elif input_type == RELAY_ACTION and len(data) >= 2:
                self.engine.submit_action(pid, data[0], data[1])
            elif input_type == RELAY_RESPONSE and len(data) >= 1:
                self.engine.submit_response(pid, data[0])
            elif input_type == RELAY_BLOCK_CLAIM and len(data) >= 1:
                self.engine.submit_block_claim(pid, data[0])
            elif input_type == RELAY_LOSE_INFLUENCE and len(data) >= 1:
                self.engine.submit_lose_influence(pid, data[0])
            elif input_type == RELAY_EXCHANGE_CHOICE and len(data) >= 2:
                self.engine.submit_exchange(pid, data[0], data[1])
            elif input_type == RELAY_TIMEOUT:
                self.engine.submit_timeout()

            # Sync state from engine
            self._sync_from_engine()

        elif t == COUP_MSG_LOBBY_STATE and len(payload) >= 2:
            count = payload[1]
            self.lobby_players = count
            # Parse per-player entries to extract is_bot + difficulty
            off = 2
            is_bot_list = []
            difficulty_list = []
            for i in range(count):
                if off >= len(payload):
                    break
                off += 1  # skip id
                if off >= len(payload):
                    break
                name_len = payload[off]; off += 1 + name_len  # skip LP name
                if off + 3 > len(payload):
                    break
                _ready = payload[off]; off += 1
                bot_flag = payload[off]; off += 1
                diff = payload[off]; off += 1
                is_bot_list.append(bool(bot_flag))
                difficulty_list.append(diff)
            self._lobby_is_bot = is_bot_list
            self._lobby_difficulty = difficulty_list

        elif t == COUP_MSG_LOG and len(payload) >= 2:
            tlen = payload[1]
            self.logs.append(payload[2:2 + tlen].decode("utf-8", errors="replace"))

    def _sync_from_engine(self):
        """Derive game state from local engine after processing an INPUT_RELAY."""
        if not self.engine:
            return

        # Update coins
        for pid in range(self.player_count):
            self.coins[pid] = self.engine.player_coins(pid)

        # Update cards (own hand)
        if self.engine_pid is not None:
            c0 = self.engine.player_card(self.engine_pid, 0)
            c1 = self.engine.player_card(self.engine_pid, 1)
            self.cards = (c0, c1)

        # Update turn info
        phase = self.engine.phase()
        if phase == PHASE_WAITING_FOR_ACTION:
            cur = self.engine.current_player()
            self.turn_player_id = cur
            self.my_turn = (cur == self.engine_pid)
        else:
            self.my_turn = False

        # Check elimination
        for pid in range(self.player_count):
            if not self.engine.player_alive(pid):
                self.eliminated.add(pid)

        # Check game over
        if not self.engine.game_active():
            self.game_over = True
            # Find winner (last alive player)
            for pid in range(self.player_count):
                if self.engine.player_alive(pid):
                    self.winner_id = pid
                    break

    def wait_for(self, msg_type, timeout=5.0):
        """Poll until a message with the given type is received."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            msgs = self._read_frames(timeout=min(remaining, 0.3))
            for m in msgs:
                if m and m[0] == msg_type:
                    return m
        return None

    def wait_for_phase(self, phase, timeout=5.0):
        """Poll until the local engine reaches the given phase."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.engine and self.engine.phase() == phase:
                return True
            remaining = deadline - time.monotonic()
            self._read_frames(timeout=min(remaining, 0.3))
        return self.engine and self.engine.phase() == phase

    def wait_for_turn(self, timeout=5.0):
        """Poll until it's this client's turn (WAITING_FOR_ACTION and current_player == me)."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.my_turn:
                return True
            remaining = deadline - time.monotonic()
            self._read_frames(timeout=min(remaining, 0.3))
        return self.my_turn

    def wait_for_next_turn(self, timeout=5.0):
        """Poll until a new INPUT_RELAY arrives and engine is in WAITING_FOR_ACTION."""
        initial_relays = len(self.input_relays)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            self._read_frames(timeout=min(remaining, 0.3))
            if (len(self.input_relays) > initial_relays and
                    self.engine and self.engine.phase() == PHASE_WAITING_FOR_ACTION):
                cur = self.engine.current_player()
                self.turn_player_id = cur
                self.my_turn = (cur == self.engine_pid)
                return True
        return False

    def poll(self, timeout=1.0):
        """Read all available messages, updating state."""
        self._read_frames(timeout=timeout)

    # -- convenience senders --

    def send_ready(self):
        self.send_msg(bytes([COUP_MSG_READY]))

    def send_add_bot(self, difficulty=1):
        self.send_msg(bytes([COUP_MSG_ADD_BOT, difficulty]))

    def send_remove_bot(self):
        self.send_msg(bytes([COUP_MSG_REMOVE_BOT]))

    def send_start_game(self):
        self.send_msg(bytes([COUP_MSG_START_GAME_REQ]))

    def send_action(self, action, target_pid=0xFF):
        self.my_turn = False
        self.send_msg(bytes([COUP_MSG_ACTION, action, target_pid]))

    def send_response(self, resp):
        self.send_msg(bytes([COUP_MSG_RESPONSE, resp]))

    def send_block_claim(self, char):
        self.send_msg(bytes([COUP_MSG_BLOCK_CLAIM, char]))

    def send_lose_influence(self, card_idx):
        self.send_msg(bytes([COUP_MSG_LOSE_INFLUENCE, card_idx]))

    def send_exchange(self, keep0, keep1):
        self.send_msg(bytes([COUP_MSG_EXCHANGE_CHOICE, keep0, keep1]))

    def send_resync_req(self, last_seen_seq):
        self.send_msg(bytes([COUP_MSG_RESYNC_REQ,
                             (last_seen_seq >> 8) & 0xFF,
                             last_seen_seq & 0xFF]))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Connection closed")
            buf += chunk
        return buf


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_msg(msgs, msg_type):
    for m in msgs:
        if m and m[0] == msg_type:
            return m
    return None


# ---------------------------------------------------------------------------
# Base test case
# ---------------------------------------------------------------------------

class ServerTestCase(unittest.TestCase):

    def setUp(self):
        self.server = CoupServer(host="127.0.0.1", port=0)
        self._uuid_fd, self._uuid_path = tempfile.mkstemp(suffix=".txt", prefix="coup_test_uuid_")
        os.close(self._uuid_fd)
        self.server._uuid_file = self._uuid_path
        self._clients = []
        self._server_thread = threading.Thread(target=self.server.run, daemon=True)
        self._server_thread.start()
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if self.server._running and self.server.port != 0:
                break
            time.sleep(0.05)
        self.assertTrue(self.server._running, "Server failed to start")

    def tearDown(self):
        for c in self._clients:
            c.close()
        self.server._running = False
        self.server.shutdown()
        self._server_thread.join(timeout=3.0)
        try:
            os.unlink(self._uuid_path)
        except OSError:
            pass

    def make_client(self, username):
        c = TestClient()
        c.connect("127.0.0.1", self.server.port)
        c.register(username)
        self._clients.append(c)
        return c

    def start_2p_game(self):
        """Connect 2 clients, ready up, start game, wait for GAME_START + INPUT_RELAY on both."""
        host = self.make_client("Alice")
        guest = self.make_client("Bob")
        host.poll(timeout=0.5)
        guest.poll(timeout=0.5)
        host.send_ready()
        guest.send_ready()
        host.poll(timeout=0.5)
        guest.poll(timeout=0.5)
        host.send_start_game()
        self.assertIsNotNone(host.wait_for(COUP_MSG_GAME_START), "Host missing GAME_START")
        self.assertIsNotNone(guest.wait_for(COUP_MSG_GAME_START), "Guest missing GAME_START")
        # Wait for INPUT_RELAY(START_GAME) and subsequent INPUT_RELAY(s) to arrive
        host.poll(timeout=0.5)
        guest.poll(timeout=0.5)
        return host, guest

    def start_2p_seeded(self, seed, first_player="Alice"):
        """Start 2-player game with known seed and turn order."""
        self.server._test_seed = seed
        self.server._test_turn_order = [first_player, "Bob" if first_player == "Alice" else "Alice"]
        return self.start_2p_game()

    def who_has_turn(self, c1, c2):
        """Return (active, passive) based on who currently has the turn."""
        if c1.my_turn:
            return c1, c2
        if c2.my_turn:
            return c2, c1
        c1.poll(timeout=0.5)
        c2.poll(timeout=0.5)
        if c1.my_turn:
            return c1, c2
        if c2.my_turn:
            return c2, c1
        # Fallback: check engine
        if c1.engine and c1.engine.phase() == PHASE_WAITING_FOR_ACTION:
            cur = c1.engine.current_player()
            if cur == c1.engine_pid:
                return c1, c2
            if cur == c2.engine_pid:
                return c2, c1
        self.fail("Neither client has the turn")

    def do_income_turns(self, c1, c2, n):
        """Play n income turns (alternating between c1 and c2)."""
        for _ in range(n):
            active, passive = self.who_has_turn(c1, c2)
            other = c2 if active is c1 else c1
            active.send_action(ACT_INCOME)
            # Wait for next turn to arrive
            self.assertTrue(other.wait_for_next_turn(timeout=3.0) or
                            active.wait_for_next_turn(timeout=1.0),
                            "Next turn didn't arrive after income")


# ---------------------------------------------------------------------------
# Auth & lobby tests
# ---------------------------------------------------------------------------

class TestAuthAndLobby(ServerTestCase):

    def test_auth_and_lobby(self):
        """2 clients connect+register, both see LOBBY_STATE with 2 players."""
        c1 = self.make_client("Alice")
        c2 = self.make_client("Bob")
        c1.poll(timeout=1.0)
        c2.poll(timeout=1.0)
        self.assertTrue(
            c1.lobby_players == 2 or c2.lobby_players == 2,
            f"Expected 2 players in lobby, got c1={c1.lobby_players} c2={c2.lobby_players}")

    def test_username_taken(self):
        """Second client trying the same username gets USERNAME_TAKEN."""
        self.make_client("Alice")
        c2 = TestClient()
        c2.connect("127.0.0.1", self.server.port)
        self._clients.append(c2)
        c2.send_msg(bytes([MSG_CONNECT]))
        c2.wait_for(MSG_USERNAME_REQUIRED)
        name_bytes = b"Alice"
        c2.send_msg(bytes([MSG_SET_USERNAME, len(name_bytes)]) + name_bytes)
        taken = c2.wait_for(MSG_USERNAME_TAKEN, timeout=3.0)
        self.assertIsNotNone(taken, "Expected USERNAME_TAKEN")

    def test_ready_toggle(self):
        """READY toggles ready state in LOBBY_STATE."""
        c1 = self.make_client("Alice")
        c1.poll(timeout=0.5)
        c1.send_ready()
        lobby = c1.wait_for(COUP_MSG_LOBBY_STATE, timeout=2.0)
        self.assertIsNotNone(lobby)
        offset = 2
        name_len = lobby[offset + 1]
        ready_byte = lobby[offset + 2 + name_len]
        self.assertEqual(ready_byte, 1, "Expected ready=True after READY toggle")


# ---------------------------------------------------------------------------
# Game start
# ---------------------------------------------------------------------------

class TestGameStart(ServerTestCase):

    def test_host_start_game(self):
        """Host sends START_GAME_REQ, both get GAME_START with engine and valid cards."""
        host, guest = self.start_2p_game()
        self.assertTrue(host.game_started)
        self.assertTrue(guest.game_started)
        # Both should have received cards from their local engine
        for c in (host, guest):
            self.assertIn(c.cards[0], range(5), f"Invalid card0: {c.cards[0]}")
            self.assertIn(c.cards[1], range(5), f"Invalid card1: {c.cards[1]}")


# ---------------------------------------------------------------------------
# Single-action tests
# ---------------------------------------------------------------------------

class TestGameActions(ServerTestCase):

    def test_income_action(self):
        """Active player sends Income -> +1 coin, turn passes."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        active.send_action(ACT_INCOME)
        self.assertTrue(passive.wait_for_next_turn(timeout=3.0))
        active.poll(timeout=0.5)
        self.assertEqual(active.coins.get(active.engine_pid), 3,
                         f"Expected 3 coins after income, got {active.coins}")

    def test_foreign_aid_no_block(self):
        """Foreign Aid with pass -> +2 coins."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        active.send_action(ACT_FOREIGN_AID)
        # Wait for block window
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_next_turn(timeout=5.0))
        active.poll(timeout=0.5)
        self.assertEqual(active.coins.get(active.engine_pid), 4,
                         f"Expected 4 coins after foreign aid, got {active.coins}")

    def test_foreign_aid_blocked(self):
        """Foreign Aid blocked by Duke -> action cancelled."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        active.send_action(ACT_FOREIGN_AID)
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))

        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_DUKE)

        # Active gets block-challenge window -> pass
        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_next_turn(timeout=5.0))
        active.poll(timeout=0.5)
        self.assertEqual(active.coins.get(active.engine_pid), 2,
                         f"Expected 2 coins (blocked), got {active.coins}")
        # Verify the action was cancelled by confirming coins unchanged
        # (logs are now generated client-side from local engine events)
        self.assertEqual(active.coins.get(active.engine_pid), 2,
                         "Coins should remain unchanged after block")

    def test_tax_no_challenge(self):
        """Tax with pass -> +3 coins."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        active.send_action(ACT_TAX)
        # Tax claims Duke -> challenge window
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_next_turn(timeout=5.0))
        active.poll(timeout=0.5)
        self.assertEqual(active.coins.get(active.engine_pid), 5,
                         f"Expected 5 coins after tax, got {active.coins}")


# ---------------------------------------------------------------------------
# Coup + influence loss
# ---------------------------------------------------------------------------

class TestCoupAction(ServerTestCase):

    def test_coup_loses_influence(self):
        """Build coins via income, then coup -> target loses influence."""
        host, guest = self.start_2p_game()

        for _ in range(20):
            active, passive = self.who_has_turn(host, guest)
            my_coins = active.coins.get(active.engine_pid, 2)

            if my_coins >= 7:
                active.send_action(ACT_COUP, passive.engine_pid)
                self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0),
                                "Target didn't reach INFLUENCE_LOSS phase")
                passive.send_lose_influence(0)
                # Wait for next turn to ensure all relays arrive
                self.assertTrue(
                    active.wait_for_next_turn(timeout=3.0) or
                    passive.wait_for_next_turn(timeout=1.0))
                # Influence loss verified by phase transition above.
                # Logs are now generated client-side from local engine events.
                return

            active.send_action(ACT_INCOME)
            host.wait_for_next_turn(timeout=3.0)
            guest.poll(timeout=0.3)

        self.fail("Could not build enough coins for coup")


# ---------------------------------------------------------------------------
# Full game to completion
# ---------------------------------------------------------------------------

class TestFullGame(ServerTestCase):

    def test_full_game_to_completion(self):
        """Income->coup loop until game over, then lobby returns."""
        host, guest = self.start_2p_game()

        for _ in range(40):
            if host.game_over or guest.game_over:
                break

            active, passive = self.who_has_turn(host, guest)
            my_coins = active.coins.get(active.engine_pid, 2)

            if my_coins >= 7:
                active.send_action(ACT_COUP, passive.engine_pid)
                if passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0):
                    passive.send_lose_influence(0)
                active.poll(timeout=1.0)
                passive.poll(timeout=1.0)
                if host.game_over or guest.game_over:
                    break
                host.wait_for_next_turn(timeout=3.0)
                guest.poll(timeout=0.3)
                continue

            active.send_action(ACT_INCOME)
            host.wait_for_next_turn(timeout=3.0)
            guest.poll(timeout=0.3)

        self.assertTrue(host.game_over or guest.game_over, "Game did not reach GAME_OVER")
        winner = host if host.game_over else guest
        self.assertIn(winner.winner_id, (host.engine_pid, guest.engine_pid))

        host.poll(timeout=1.0)
        guest.poll(timeout=1.0)
        self.assertTrue(
            host.lobby_players > 0 or guest.lobby_players > 0,
            "No LOBBY_STATE after game over")


# ---------------------------------------------------------------------------
# Disconnect
# ---------------------------------------------------------------------------

class TestDisconnect(ServerTestCase):

    def test_disconnect_during_game(self):
        """Player disconnects mid-game -> other player wins or server stays up."""
        host, guest = self.start_2p_game()

        if host.my_turn:
            host.send_action(ACT_INCOME)
            time.sleep(0.3)

        guest.close()
        self._clients.remove(guest)

        time.sleep(1.5)
        host.poll(timeout=3.0)

        if host.game_over:
            self.assertEqual(host.winner_id, host.engine_pid)
        else:
            self.assertTrue(self.server._running, "Server crashed after disconnect")


# ===========================================================================
# BROADCAST TESTS — INPUT_RELAY protocol
# ===========================================================================


# ---------------------------------------------------------------------------
# Both clients see same engine state via INPUT_RELAY
# ---------------------------------------------------------------------------

class TestBroadcastUniversality(ServerTestCase):

    def test_both_clients_agree_on_turn(self):
        """After game start, both clients' engines agree on whose turn it is."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        self.assertIsNotNone(active.turn_player_id)
        self.assertIsNotNone(passive.turn_player_id)
        self.assertEqual(active.turn_player_id, passive.turn_player_id)
        self.assertTrue(active.my_turn)
        self.assertFalse(passive.my_turn)

    def test_challenge_window_reachable(self):
        """Tax (claims Duke) -> both engines enter CHALLENGE_WINDOW phase."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_TAX)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        active.poll(timeout=0.5)
        self.assertEqual(active.engine.phase(), PHASE_CHALLENGE_WINDOW)

        passive.send_response(RESP_PASS)
        passive.wait_for_next_turn(timeout=5.0)

    def test_block_window_reachable(self):
        """Foreign Aid -> both engines enter BLOCK_WINDOW phase."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_FOREIGN_AID)
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        active.poll(timeout=0.5)
        self.assertEqual(active.engine.phase(), PHASE_BLOCK_WINDOW)

        passive.send_response(RESP_PASS)
        passive.wait_for_next_turn(timeout=5.0)

    def test_lose_influence_phase_reachable(self):
        """Coup -> both engines reach WAITING_FOR_INFLUENCE_LOSS."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")

        for _ in range(20):
            active, passive = self.who_has_turn(alice, bob)
            my_coins = active.coins.get(active.engine_pid, 2)
            if my_coins >= 7:
                active.send_action(ACT_COUP, passive.engine_pid)
                self.assertTrue(active.wait_for_phase(
                    PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0))
                self.assertTrue(passive.wait_for_phase(
                    PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0))
                passive.send_lose_influence(0)
                active.poll(timeout=1.0)
                return
            active.send_action(ACT_INCOME)
            alice.wait_for_next_turn(timeout=3.0)
            bob.poll(timeout=0.3)

        self.fail("Could not build enough coins for coup")

    def test_block_challenge_window_reachable(self):
        """Foreign Aid -> block -> both reach BLOCK_CHALLENGE_WINDOW."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_FOREIGN_AID)
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_DUKE)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))

        active.send_response(RESP_PASS)
        passive.wait_for_next_turn(timeout=5.0)


# ---------------------------------------------------------------------------
# State consistency: all players agree on game state
# ---------------------------------------------------------------------------

class TestStateConsistency(ServerTestCase):

    def test_all_players_agree_on_turn(self):
        """After each action, both clients' engines agree on turn."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        self.assertEqual(active.turn_player_id, passive.turn_player_id)

        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        self.assertEqual(alice.turn_player_id, bob.turn_player_id)

    def test_all_players_agree_on_coins(self):
        """After Income, both players see the same coin count."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        self.assertEqual(alice.coins.get(active.engine_pid),
                         bob.coins.get(active.engine_pid),
                         f"Coin disagreement: alice={alice.coins} bob={bob.coins}")

    def test_all_players_agree_on_elimination(self):
        """After Coup kills a player, both see elimination."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        for _ in range(20):
            if alice.game_over or bob.game_over:
                break
            active, passive = self.who_has_turn(alice, bob)
            my_coins = active.coins.get(active.engine_pid, 2)
            if my_coins >= 7:
                active.send_action(ACT_COUP, passive.engine_pid)
                if passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0):
                    passive.send_lose_influence(0)
                active.poll(timeout=1.0)
                passive.poll(timeout=1.0)
                self.assertEqual(alice.eliminated, bob.eliminated,
                                 "Elimination disagreement")
                return
            active.send_action(ACT_INCOME)
            alice.wait_for_next_turn(timeout=3.0)
            bob.poll(timeout=0.3)

        self.fail("Could not reach coup for elimination test")

    def test_lose_influence_targets_correct_player(self):
        """After coup, engine's influence_loser matches target."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")

        for _ in range(20):
            active, passive = self.who_has_turn(alice, bob)
            my_coins = active.coins.get(active.engine_pid, 2)
            if my_coins >= 7:
                active.send_action(ACT_COUP, passive.engine_pid)
                self.assertTrue(active.wait_for_phase(
                    PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0))
                self.assertEqual(active.engine.influence_loser(),
                                 passive.engine_pid)
                passive.send_lose_influence(0)
                active.poll(timeout=1.0)
                return
            active.send_action(ACT_INCOME)
            alice.wait_for_next_turn(timeout=3.0)
            bob.poll(timeout=0.3)

        self.fail("Could not build enough coins for coup")


# ---------------------------------------------------------------------------
# Info hiding: local engine has full visibility, but rendering hides cards
# ---------------------------------------------------------------------------

class TestInfoHiding(ServerTestCase):

    def test_owner_sees_real_cards(self):
        """After game start, each client sees their own real cards from local engine."""
        alice, bob = self.start_2p_seeded(seed=5, first_player="Alice")
        # Both should have valid cards
        for c in (alice, bob):
            self.assertIn(c.cards[0], range(5), f"Card0 should be real: {c.cards[0]}")
            self.assertIn(c.cards[1], range(5), f"Card1 should be real: {c.cards[1]}")

    def test_challenge_reveals_card_in_engine(self):
        """Tax challenge where defender has Duke -> challenger loses influence.
        Both engines agree on revealed state."""
        # Seed 5: P0=[Duke,Ambassador]
        alice, bob = self.start_2p_seeded(seed=5, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_TAX)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_CHALLENGE)

        # Bob must lose influence (challenge failed — Alice has Duke)
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        passive.send_lose_influence(0)

        # Wait for both engines to process the lose_influence relay
        self.assertTrue(active.wait_for_next_turn(timeout=3.0))
        passive.wait_for_next_turn(timeout=1.0)

        # Bob's card 0 should be revealed in both engines
        self.assertTrue(alice.engine.player_revealed(passive.engine_pid, 0))
        self.assertTrue(bob.engine.player_revealed(passive.engine_pid, 0))

    def test_exchange_cards_visible_to_owner(self):
        """After Exchange, owner engine has 4 real exchange cards."""
        # Seed 1: P0=[Ambassador,Assassin]
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_EXCHANGE)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_EXCHANGE, timeout=5.0))
        # Owner engine should have exchange cards
        count = active.engine.exchange_count()
        self.assertGreater(count, 0, "Exchange count should be > 0")
        for i in range(count):
            card = active.engine.exchange_card(i)
            self.assertIn(card, range(5), f"Exchange card {i} should be real: {card}")

        active.send_exchange(0, 1)
        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

    def test_player_state_hidden_in_engine(self):
        """Non-owner can see opponent's unrevealed cards in their engine
        (full visibility), but the rendering layer applies info hiding."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        # Both engines have full state — info hiding is a rendering concern
        other_pid = bob.engine_pid
        c0 = alice.engine.player_card(other_pid, 0)
        c1 = alice.engine.player_card(other_pid, 1)
        # Cards should be valid (engine has full visibility)
        self.assertIn(c0, range(5))
        self.assertIn(c1, range(5))


# ---------------------------------------------------------------------------
# Steal challenge/block matrix
# ---------------------------------------------------------------------------

class TestStealChallengeBlockMatrix(ServerTestCase):

    def test_steal_unchallenged_unblocked(self):
        """Steal -> challenge pass -> block pass -> coins transferred."""
        # Seed 4: P0=[Captain,Contessa]
        alice, bob = self.start_2p_seeded(seed=4, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        passive.wait_for_next_turn(timeout=5.0)
        active.poll(timeout=0.5)

        self.assertEqual(active.coins.get(active.engine_pid), 4)
        self.assertEqual(active.coins.get(passive.engine_pid), 0)
        self.assertEqual(passive.coins.get(active.engine_pid), 4)
        self.assertEqual(passive.coins.get(passive.engine_pid), 0)

    def test_steal_challenged_defender_has_card(self):
        """Steal (Captain) -> challenge -> defender reveals Captain ->
        challenger loses influence."""
        # Seed 4: P0=[Captain,Contessa] P1=[Captain,Captain]
        alice, bob = self.start_2p_seeded(seed=4, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_CHALLENGE)

        # Bob loses influence (challenge failed)
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        passive.send_lose_influence(0)

        # Steal continues -> block window
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=5.0))
        passive.send_response(RESP_PASS)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

    def test_steal_challenged_defender_bluffing(self):
        """Steal (Captain) -> challenge -> no Captain -> defender loses influence."""
        # Seed 1: P0=[Ambassador,Assassin] - no Captain
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_CHALLENGE)

        # Alice loses influence (caught bluffing)
        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        active.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        # Coins unchanged (action cancelled)
        self.assertEqual(active.coins.get(active.engine_pid, 2), 2)
        self.assertEqual(active.coins.get(passive.engine_pid, 2), 2)

    def test_steal_blocked_by_captain_unchallenged(self):
        """Steal -> pass challenge -> block with Captain -> pass -> cancelled."""
        # Seed 4: P0=[Captain,Contessa] P1=[Captain,Captain]
        alice, bob = self.start_2p_seeded(seed=4, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_CAPTAIN)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_PASS)

        passive.wait_for_next_turn(timeout=5.0)
        active.poll(timeout=0.5)

        self.assertEqual(active.coins.get(active.engine_pid, 2), 2)
        self.assertEqual(active.coins.get(passive.engine_pid, 2), 2)

    def test_steal_blocked_by_ambassador_unchallenged(self):
        """Steal -> pass challenge -> block with Ambassador -> pass -> cancelled."""
        # Seed 7: P0=[Assassin,Captain] P1=[Ambassador,Ambassador]
        alice, bob = self.start_2p_seeded(seed=7, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_AMBASSADOR)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_PASS)

        passive.wait_for_next_turn(timeout=5.0)
        active.poll(timeout=0.5)

        self.assertEqual(active.coins.get(active.engine_pid, 2), 2)
        self.assertEqual(active.coins.get(passive.engine_pid, 2), 2)

    def test_steal_blocked_by_ambassador_challenged_has_card(self):
        """Steal -> block Ambassador -> challenge block -> blocker has Ambassador ->
        challenger loses influence -> steal cancelled.
        Verifies Ambassador is a valid steal-blocker (not just Captain)."""
        # Seed 7: P0=[Assassin,Captain] P1=[Ambassador,Ambassador]
        alice, bob = self.start_2p_seeded(seed=7, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_AMBASSADOR)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_CHALLENGE)

        # Alice challenged the block, but Bob HAS Ambassador -> Alice loses influence
        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        active.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        # Steal cancelled — coins unchanged
        self.assertEqual(active.coins.get(active.engine_pid, 2), 2)
        self.assertEqual(active.coins.get(passive.engine_pid, 2), 2)
        # Alice's card 0 should be revealed (she lost influence for failed challenge)
        self.assertTrue(active.engine.player_revealed(active.engine_pid, 0))

    def test_steal_block_challenged_blocker_has_card(self):
        """Steal -> block Captain -> challenge block -> blocker reveals ->
        challenger loses influence -> steal cancelled."""
        # Seed 4: P0=[Captain,Contessa] P1=[Captain,Captain]
        alice, bob = self.start_2p_seeded(seed=4, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_CAPTAIN)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_CHALLENGE)

        # Alice loses influence for failed block-challenge
        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        active.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        self.assertEqual(active.coins.get(active.engine_pid, 2), 2)
        self.assertEqual(active.coins.get(passive.engine_pid, 2), 2)

    def test_steal_block_challenged_blocker_bluffing(self):
        """Steal -> block Ambassador (bluff) -> challenge block ->
        blocker loses influence -> steal proceeds."""
        # Seed 4: P0=[Captain,Contessa] P1=[Captain,Captain]
        alice, bob = self.start_2p_seeded(seed=4, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_STEAL, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_AMBASSADOR)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_CHALLENGE)

        # Blocker (Bob) loses influence
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        passive.send_lose_influence(0)

        alice.wait_for_next_turn(timeout=5.0)
        bob.poll(timeout=0.5)

        self.assertEqual(active.coins.get(active.engine_pid), 4,
                         "Steal should have succeeded after block-bluff caught")


# ---------------------------------------------------------------------------
# Assassinate challenge/block matrix
# ---------------------------------------------------------------------------

class TestAssassinateChallengeBlockMatrix(ServerTestCase):

    def test_assassinate_unchallenged_unblocked(self):
        """Assassinate -> pass challenge -> pass block -> target loses influence."""
        # Seed 1: P0=[Ambassador,Assassin]
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        self.do_income_turns(alice, bob, 2)

        active, passive = self.who_has_turn(alice, bob)
        initial_coins = active.coins.get(active.engine_pid, 3)
        active.send_action(ACT_ASSASSINATE, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        # Target loses influence
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        passive.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        self.assertEqual(active.coins.get(active.engine_pid), initial_coins - 3)

    def test_assassinate_challenged_has_card(self):
        """Assassinate -> challenge -> defender has Assassin -> challenger loses influence."""
        # Seed 1: P0=[Ambassador,Assassin]
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        self.do_income_turns(alice, bob, 2)

        active, passive = self.who_has_turn(alice, bob)
        active.send_action(ACT_ASSASSINATE, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_CHALLENGE)

        # Challenge fails - Alice has Assassin, Bob loses influence
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        passive.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        # Block prompt might appear if target still alive
        if passive.engine and passive.engine.phase() == PHASE_BLOCK_WINDOW:
            passive.send_response(RESP_PASS)
            if passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0):
                passive.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

    def test_assassinate_challenged_bluffing(self):
        """Assassinate -> challenge -> no Assassin -> actor loses influence."""
        # Seed 5: P0=[Duke,Ambassador] - no Assassin
        alice, bob = self.start_2p_seeded(seed=5, first_player="Alice")
        self.do_income_turns(alice, bob, 2)

        active, passive = self.who_has_turn(alice, bob)
        active.send_action(ACT_ASSASSINATE, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_CHALLENGE)

        # Alice caught bluffing - loses influence
        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        active.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        self.assertNotIn(passive.engine_pid, active.eliminated)

    def test_assassinate_blocked_by_contessa(self):
        """Assassinate -> Contessa block -> pass block-challenge -> cancelled."""
        # Seed 2: P0=[Assassin,Ambassador] P1=[Contessa,Contessa]
        alice, bob = self.start_2p_seeded(seed=2, first_player="Alice")
        self.do_income_turns(alice, bob, 2)

        active, passive = self.who_has_turn(alice, bob)
        active.send_action(ACT_ASSASSINATE, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_CONTESSA)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_PASS)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        self.assertNotIn(passive.engine_pid, active.eliminated)

    def test_assassinate_block_challenged_blocker_bluffing(self):
        """Block Contessa -> challenged -> no Contessa -> blocker loses influence."""
        # Seed 1: P0=[Ambassador,Assassin] P1=[Captain,Ambassador]
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        self.do_income_turns(alice, bob, 2)

        active, passive = self.who_has_turn(alice, bob)
        active.send_action(ACT_ASSASSINATE, passive.engine_pid)

        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_CONTESSA)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_CHALLENGE)

        # Bob loses influence for bluffing the block
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=5.0))
        passive.send_lose_influence(0)

        # Assassinate proceeds - Bob may also lose another influence
        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

        if passive.engine and passive.engine.phase() == PHASE_WAITING_FOR_INFLUENCE_LOSS:
            passive.send_lose_influence(0)

        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)


# ---------------------------------------------------------------------------
# Exchange flow
# ---------------------------------------------------------------------------

class TestExchangeFlow(ServerTestCase):

    def test_exchange_reaches_exchange_phase(self):
        """Exchange -> pass challenge -> engine reaches WAITING_FOR_EXCHANGE."""
        # Seed 1: P0=[Ambassador,Assassin]
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_EXCHANGE)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_EXCHANGE, timeout=5.0))
        self.assertGreater(active.engine.exchange_count(), 0)

        active.send_exchange(0, 1)
        active.poll(timeout=1.0)
        passive.poll(timeout=1.0)

    def test_exchange_completes_with_card_update(self):
        """After exchange choice, owner's cards update in engine."""
        # Seed 1: P0=[Ambassador,Assassin]
        alice, bob = self.start_2p_seeded(seed=1, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_EXCHANGE)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))
        passive.send_response(RESP_PASS)

        self.assertTrue(active.wait_for_phase(PHASE_WAITING_FOR_EXCHANGE, timeout=5.0))
        active.send_exchange(0, 1)

        active.wait_for_next_turn(timeout=5.0)
        passive.poll(timeout=0.5)

        # Cards should be valid after exchange
        self.assertIn(active.cards[0], range(5))
        self.assertIn(active.cards[1], range(5))


# ---------------------------------------------------------------------------
# Adversarial edge cases
# ---------------------------------------------------------------------------

class TestAdversarialEdgeCases(ServerTestCase):

    def test_non_actor_cannot_receive_interactive_turn(self):
        """Only the active player has my_turn=True."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        self.assertTrue(active.my_turn)
        self.assertFalse(passive.my_turn)

        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        self.assertTrue(passive.my_turn)
        self.assertFalse(active.my_turn)

    def test_both_engines_deterministic(self):
        """Both clients' local engines produce identical state after same inputs."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        # All coins must match
        for pid in range(alice.player_count):
            self.assertEqual(alice.engine.player_coins(pid),
                             bob.engine.player_coins(pid),
                             f"Coin mismatch for pid={pid}")

        # Phase must match
        self.assertEqual(alice.engine.phase(), bob.engine.phase())

    def test_state_matches_after_full_complex_turn(self):
        """After block + block-challenge pass, both engines agree."""
        alice, bob = self.start_2p_seeded(seed=42, first_player="Alice")
        active, passive = self.who_has_turn(alice, bob)

        active.send_action(ACT_FOREIGN_AID)
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))
        passive.send_response(RESP_BLOCK)
        time.sleep(0.1)
        passive.send_block_claim(CHAR_DUKE)

        self.assertTrue(active.wait_for_phase(PHASE_BLOCK_CHALLENGE_WINDOW, timeout=3.0))
        active.send_response(RESP_PASS)

        passive.wait_for_next_turn(timeout=5.0)
        active.poll(timeout=0.5)

        self.assertEqual(alice.turn_player_id, bob.turn_player_id)
        for pid in range(alice.player_count):
            self.assertEqual(alice.coins[pid], bob.coins[pid],
                             f"Coin mismatch for pid={pid}")


class TestBotIntegration(ServerTestCase):
    """Tests for server-side bot games (1 human + N bots)."""

    def setUp(self):
        super().setUp()
        # Zero out bot think delays for fast tests
        import server as _srv
        self._orig_min = _srv.BOT_THINK_DELAY_MIN
        self._orig_max = _srv.BOT_THINK_DELAY_MAX
        _srv.BOT_THINK_DELAY_MIN = 0.0
        _srv.BOT_THINK_DELAY_MAX = 0.0

    def tearDown(self):
        import server as _srv
        _srv.BOT_THINK_DELAY_MIN = self._orig_min
        _srv.BOT_THINK_DELAY_MAX = self._orig_max
        super().tearDown()

    def test_add_bot_lobby_state(self):
        """Adding a bot updates LOBBY_STATE with the bot player."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)
        initial_count = host.lobby_players

        host.send_add_bot()
        host.poll(timeout=0.5)
        self.assertEqual(host.lobby_players, initial_count + 1)

    def test_remove_bot_lobby_state(self):
        """Removing a bot decrements LOBBY_STATE count."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        host.send_add_bot()
        host.poll(timeout=0.5)
        after_add = host.lobby_players

        host.send_remove_bot()
        host.poll(timeout=0.5)
        self.assertEqual(host.lobby_players, after_add - 1)

    def test_one_human_with_bots_game_start(self):
        """1 human + 3 bots: GAME_START includes bot flags, engines match."""
        self.server._test_seed = 42
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        # Add 3 bots
        for _ in range(3):
            host.send_add_bot()
            host.poll(timeout=0.3)

        host.send_ready()
        host.poll(timeout=0.5)

        host.send_start_game()
        gs = host.wait_for(COUP_MSG_GAME_START, timeout=3.0)
        self.assertIsNotNone(gs, "Missing GAME_START")
        self.assertTrue(host.game_started)
        self.assertEqual(host.player_count, 4)  # 1 human + 3 bots

        # Wait for INPUT_RELAY(START_GAME) to arrive
        host.poll(timeout=1.0)

        # Verify engine initialized correctly — human is pid 0, bots are 1-3
        self.assertFalse(host.engine.player_is_bot(0))
        for pid in range(1, 4):
            self.assertTrue(host.engine.player_is_bot(pid),
                            f"pid={pid} should be bot")

        # Verify engine state matches server
        self.assertEqual(host.engine.phase(), self.server.engine.phase())
        for pid in range(4):
            self.assertEqual(host.engine.player_coins(pid),
                             self.server.engine.player_coins(pid),
                             f"Coin mismatch pid={pid}")

    def test_add_bot_lobby_state_has_is_bot_flag(self):
        """LOBBY_STATE marks bots with is_bot=True and humans with is_bot=False."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        # After connecting, only human — is_bot should be [False]
        self.assertEqual(host._lobby_is_bot, [False])

        host.send_add_bot()
        host.poll(timeout=0.5)
        # Now [human, bot]
        self.assertEqual(host._lobby_is_bot, [False, True])

    def test_add_bot_with_difficulty(self):
        """Adding a bot with explicit difficulty is visible in LOBBY_STATE."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        host.send_add_bot(difficulty=2)  # Hard
        host.poll(timeout=0.5)

        self.assertEqual(host.lobby_players, 2)
        # Client should see [human=0, bot=2] in parsed difficulty
        self.assertEqual(host._lobby_difficulty, [0, 2])

    def test_set_bot_difficulty(self):
        """SET_BOT_DIFFICULTY updates difficulty visible in LOBBY_STATE."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        # Add a bot (default difficulty=1)
        host.send_add_bot(difficulty=1)
        host.poll(timeout=0.5)
        self.assertEqual(host._lobby_difficulty, [0, 1])  # human=0, bot=1

        # Change difficulty to Hard (2)
        host.send_msg(bytes([COUP_MSG_SET_BOT_DIFFICULTY, 0, 2]))
        host.poll(timeout=0.5)
        self.assertEqual(host._lobby_difficulty, [0, 2])  # bot updated to 2

    def test_set_bot_difficulty_invalid_index_ignored(self):
        """SET_BOT_DIFFICULTY with out-of-range index leaves LOBBY_STATE unchanged."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        host.send_add_bot(difficulty=1)
        host.poll(timeout=0.5)
        before = list(host._lobby_difficulty)

        # Send for index 5 (only 1 bot exists at index 0)
        host.send_msg(bytes([COUP_MSG_SET_BOT_DIFFICULTY, 5, 2]))
        host.poll(timeout=0.5)

        # Client-visible difficulty should be unchanged
        self.assertEqual(host._lobby_difficulty, before)

    def test_set_bot_difficulty_multiple_bots(self):
        """SET_BOT_DIFFICULTY targets specific bot by index in LOBBY_STATE."""
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        host.send_add_bot(difficulty=0)  # Easy
        host.poll(timeout=0.3)
        host.send_add_bot(difficulty=0)  # Easy
        host.poll(timeout=0.3)
        self.assertEqual(host._lobby_difficulty, [0, 0, 0])  # human + 2 easy bots

        # Change second bot to Hard
        host.send_msg(bytes([COUP_MSG_SET_BOT_DIFFICULTY, 1, 2]))
        host.poll(timeout=0.5)

        self.assertEqual(host._lobby_difficulty, [0, 0, 2])  # only 2nd bot changed

    def test_one_human_with_bot_takes_turns(self):
        """1 human + 1 bot: both take turns, engines stay in sync."""
        self.server._test_seed = 42
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        host.send_add_bot()
        host.poll(timeout=0.3)
        host.send_ready()
        host.poll(timeout=0.5)
        host.send_start_game()
        host.wait_for(COUP_MSG_GAME_START, timeout=3.0)
        host.poll(timeout=1.0)

        self.assertTrue(host.game_started)
        self.assertEqual(host.player_count, 2)

        # Verify engines are in sync after game start
        self.assertEqual(host.engine.phase(), self.server.engine.phase(),
                         "Phase mismatch after game start")

        # Play a few turns to verify bot + human interaction works
        turns_played = 0
        for _ in range(30):
            host.poll(timeout=1.5)
            if host.game_over:
                break

            if not host.engine:
                continue

            phase = host.engine.phase()
            my_pid = host.engine_pid

            if phase == PHASE_WAITING_FOR_ACTION:
                if host.engine.current_player() == my_pid:
                    host.send_action(ACT_INCOME)
                    turns_played += 1
            elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                           PHASE_BLOCK_CHALLENGE_WINDOW):
                if host.engine.pending_response(my_pid):
                    host.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if host.engine.influence_loser() == my_pid:
                    host.send_lose_influence(0)

        # Human should have taken at least 1 turn
        self.assertGreater(turns_played, 0,
                           "Human never got a turn — bot integration broken")

        # Engines should still be in sync (if game hasn't ended)
        host.poll(timeout=1.0)
        if self.server.engine is not None:
            for pid in range(host.player_count):
                self.assertEqual(host.engine.player_coins(pid),
                                 self.server.engine.player_coins(pid),
                                 f"Coin mismatch for pid={pid} after {turns_played} turns")

    def test_lobby_bots_persist_after_game(self):
        """After a game ends, lobby should still show the bots for rematch."""
        self.server._test_seed = 42
        host = self.make_client("Alice")
        host.poll(timeout=0.5)

        # Add a bot and start game
        host.send_add_bot()
        host.poll(timeout=0.3)
        self.assertEqual(host.lobby_players, 2)
        self.assertEqual(host._lobby_is_bot, [False, True])

        host.send_ready()
        host.poll(timeout=0.5)
        host.send_start_game()
        host.wait_for(COUP_MSG_GAME_START, timeout=3.0)
        host.poll(timeout=1.0)
        self.assertTrue(host.game_started)

        # Play until game over (human does income, bot acts on its own)
        for _ in range(200):
            host.poll(timeout=1.5)
            if host.game_over:
                break
            if not host.engine:
                continue
            phase = host.engine.phase()
            my_pid = host.engine_pid
            if phase == PHASE_WAITING_FOR_ACTION:
                if host.engine.current_player() == my_pid:
                    host.send_action(ACT_INCOME)
            elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                           PHASE_BLOCK_CHALLENGE_WINDOW):
                if host.engine.pending_response(my_pid):
                    host.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if host.engine.influence_loser() == my_pid:
                    host.send_lose_influence(0)
            elif phase == PHASE_WAITING_FOR_EXCHANGE:
                if host.engine.exchange_player() == my_pid:
                    host.send_exchange(0, 1)

        self.assertTrue(host.game_over, "Game didn't end in time")

        # After game over, poll for LOBBY_STATE — bot should still be there
        host.poll(timeout=2.0)
        self.assertEqual(host.lobby_players, 2,
                         f"Expected 2 players in lobby after game, got {host.lobby_players}")
        self.assertEqual(host._lobby_is_bot, [False, True],
                         "Bot should persist in lobby after game ends")


# ---------------------------------------------------------------------------
# Timeout enforcement tests
# ---------------------------------------------------------------------------

class TestTimeoutEnforcement(ServerTestCase):

    def test_challenge_timeout_auto_passes(self):
        """When challenge window times out, server sends RELAY_TIMEOUT and game proceeds."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Declare Tax (enters challenge window)
        active.send_action(ACT_TAX)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0),
                        "Passive didn't enter challenge window")

        # Wait for server to also be in CHALLENGE_WINDOW
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            if self.server.engine and self.server.engine.phase() == PHASE_CHALLENGE_WINDOW:
                break
            time.sleep(0.05)
        self.assertEqual(self.server.engine.phase(), PHASE_CHALLENGE_WINDOW,
                         "Server not in challenge window")

        # Set deadline to past — let the server's own loop trigger the timeout
        self.server.deadline = time.time() - 1

        # Wait for action to resolve (Tax gives 3 coins: 2 initial + 3 = 5)
        resolve_deadline = time.monotonic() + 5.0
        while time.monotonic() < resolve_deadline:
            active.poll(timeout=0.3)
            passive.poll(timeout=0.3)
            if active.coins.get(active.engine_pid, 0) == 5:
                break

        self.assertEqual(active.coins[active.engine_pid], 5,
                         f"Active coins should be 5 after Tax, got {active.coins[active.engine_pid]}")

    def test_turn_timeout_forces_income(self):
        """When turn times out, server forces Income for the current player."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        initial_coins = active.coins[active.engine_pid]

        # Set deadline to past — let the server's own loop trigger the timeout
        self.server.deadline = time.time() - 1

        # Wait for the forced Income to resolve
        expected = initial_coins + 1
        resolve_deadline = time.monotonic() + 5.0
        while time.monotonic() < resolve_deadline:
            active.poll(timeout=0.3)
            passive.poll(timeout=0.3)
            if active.coins.get(active.engine_pid, 0) == expected:
                break

        self.assertEqual(active.coins[active.engine_pid], expected,
                         "Active should have gained 1 coin from forced Income")


# ---------------------------------------------------------------------------
# Disconnect mid-game tests
# ---------------------------------------------------------------------------

class TestDisconnectMidGame(ServerTestCase):

    def test_disconnect_during_own_turn_forces_income(self):
        """If the active player disconnects during their turn, Income is forced."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        initial_coins = active.coins[active.engine_pid]
        active_pid = active.engine_pid

        # Disconnect the active player
        active.close()
        self._clients.remove(active)

        # Give server time to detect disconnect and force Income
        time.sleep(0.5)
        self.server.check_timeouts()
        passive.poll(timeout=2.0)

        # Passive should see the forced Income for the active player
        self.assertEqual(passive.coins[active_pid], initial_coins + 1,
                         f"Disconnected player should have gained 1 coin from forced Income")

    def test_disconnect_during_response_window(self):
        """If the responding player disconnects during challenge window, timeout fires."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Declare Tax (enters challenge window where passive must respond)
        active.send_action(ACT_TAX)
        self.assertTrue(passive.wait_for_phase(PHASE_CHALLENGE_WINDOW, timeout=3.0))

        # Disconnect the passive player (who needs to respond)
        passive.close()
        self._clients.remove(passive)

        # Server should detect disconnect and force timeout
        time.sleep(0.5)
        self.server.check_timeouts()
        active.poll(timeout=2.0)

        # Tax should have resolved after timeout
        self.assertEqual(active.coins[active.engine_pid], 5,
                         "Tax should have resolved after passive disconnected")


# ---------------------------------------------------------------------------
# Bot targeted-action tests (Coup, Assassinate, Steal with bot targets)
# ---------------------------------------------------------------------------

class TestBotTargetedActions(ServerTestCase):
    """Tests for human using targeted actions against bots.

    Reproduces the reported issue: Coup and Assassinate hang when targeting
    a bot.  Each test verifies the action relay is correct, the engine
    accepts it, and the game advances to the next turn.
    """

    def setUp(self):
        super().setUp()
        # Zero out bot think delays for fast tests
        import server as _srv
        self._orig_min = _srv.BOT_THINK_DELAY_MIN
        self._orig_max = _srv.BOT_THINK_DELAY_MAX
        _srv.BOT_THINK_DELAY_MIN = 0.0
        _srv.BOT_THINK_DELAY_MAX = 0.0

    def tearDown(self):
        import server as _srv
        _srv.BOT_THINK_DELAY_MIN = self._orig_min
        _srv.BOT_THINK_DELAY_MAX = self._orig_max
        super().tearDown()

    def _start_human_plus_bots(self, num_bots=2, seed=42):
        """Start a game with 1 human (pid=0) + N bots. Human goes first."""
        self.server._test_seed = seed
        self.server._test_turn_order = ["Alice"]
        host = self.make_client("Alice")
        host.poll(timeout=0.5)
        for _ in range(num_bots):
            host.send_add_bot()
            host.poll(timeout=0.3)
        host.send_ready()
        host.poll(timeout=0.5)
        host.send_start_game()
        self.assertIsNotNone(host.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        host.poll(timeout=1.0)
        self.assertTrue(host.game_started)
        self.assertEqual(host.player_count, 1 + num_bots)
        return host

    def _do_income_until_coins(self, host, target_coins, max_rounds=40):
        """Play income turns until the human reaches target_coins."""
        for _ in range(max_rounds):
            host.poll(timeout=2.0)
            if host.game_over:
                self.fail("Game ended before human reached target coins")

            phase = host.engine.phase()
            my_pid = host.engine_pid

            if phase == PHASE_WAITING_FOR_ACTION:
                if host.engine.current_player() == my_pid:
                    if host.coins.get(my_pid, 0) >= target_coins:
                        return  # ready
                    host.send_action(ACT_INCOME)
                # else bot's turn, wait for tick_bots
            elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                           PHASE_BLOCK_CHALLENGE_WINDOW):
                if host.engine.pending_response(my_pid):
                    host.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if host.engine.influence_loser() == my_pid:
                    host.send_lose_influence(0)

        self.fail(f"Could not reach {target_coins} coins in {max_rounds} rounds")

    def _find_alive_bot(self, host):
        """Return the pid of the first alive bot."""
        for pid in range(host.player_count):
            if pid != host.engine_pid and host.engine.player_alive(pid):
                return pid
        self.fail("No alive bot found")

    def _assert_engines_in_sync(self, host, label=""):
        """Assert client and server engines agree on phase and coins."""
        if self.server.engine is None:
            return  # game ended
        client_phase = host.engine.phase()
        server_phase = self.server.engine.phase()
        self.assertEqual(client_phase, server_phase,
                         f"Phase mismatch {label}: client={client_phase} server={server_phase}")
        for pid in range(host.player_count):
            self.assertEqual(host.engine.player_coins(pid),
                             self.server.engine.player_coins(pid),
                             f"Coin mismatch pid={pid} {label}")

    def test_human_coup_bot(self):
        """Human does Coup on a bot: action accepted, bot loses influence,
        game advances to next turn. Engines stay in sync.
        Tries multiple seeds to find one where human survives to 7 coins."""
        for seed in [100, 200, 300, 400, 500]:
            host = self._start_human_plus_bots(num_bots=1, seed=seed)
            my_pid = host.engine_pid

            # Try to build coins; if human dies, try next seed
            survived = True
            for _ in range(40):
                host.poll(timeout=1.5)
                if not host.engine or not host.engine.game_active():
                    survived = False
                    break
                phase = host.engine.phase()
                if phase == PHASE_WAITING_FOR_ACTION and host.engine.current_player() == my_pid:
                    if host.coins.get(my_pid, 0) >= 7:
                        break
                    host.send_action(ACT_INCOME)
                elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                               PHASE_BLOCK_CHALLENGE_WINDOW):
                    if host.engine.pending_response(my_pid):
                        host.send_response(RESP_PASS)
                elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                    if host.engine.influence_loser() == my_pid:
                        host.send_lose_influence(0)

            if not survived or host.coins.get(my_pid, 0) < 7:
                host.close()
                self._clients.remove(host)
                continue

            # Human has 7+ coins and it's their turn — do Coup
            target_bot = self._find_alive_bot(host)
            coins_before = host.coins[my_pid]
            relays_before = len(host.input_relays)

            host.send_action(ACT_COUP, target_bot)

            resolved = False
            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                host.poll(timeout=1.0)
                if host.game_over:
                    resolved = True
                    break
                phase = host.engine.phase()
                # Handle reactive phases (bots may act back immediately)
                if phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                             PHASE_BLOCK_CHALLENGE_WINDOW):
                    if host.engine.pending_response(my_pid):
                        host.send_response(RESP_PASS)
                elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                    if host.engine.influence_loser() == my_pid:
                        host.send_lose_influence(0)
                elif (phase == PHASE_WAITING_FOR_ACTION and
                        len(host.input_relays) > relays_before):
                    resolved = True
                    break

            self.assertTrue(resolved,
                            f"Coup hung at seed={seed} phase={host.engine.phase()}")
            self.assertEqual(host.coins[my_pid], coins_before - 7)

            action_relays = [r for r in host.input_relays[relays_before:]
                             if len(r) >= 7 and r[3] == RELAY_ACTION and r[4] == my_pid]
            self.assertTrue(len(action_relays) >= 1, "No ACTION relay for Coup")
            self.assertEqual(action_relays[0][5], ACT_COUP)
            self.assertEqual(action_relays[0][6], target_bot)
            self._assert_engines_in_sync(host, "after Coup")
            return  # success

        self.fail("Human died before reaching 7 coins across all seeds")

    def test_human_assassinate_bot(self):
        """Human does Assassinate on a bot: action accepted, challenge/block
        windows resolve, bot loses influence. Engines stay in sync."""
        host = self._start_human_plus_bots(num_bots=2, seed=42)

        # Build up to 3 coins
        self._do_income_until_coins(host, 3)

        my_pid = host.engine_pid
        coins_before = host.coins[my_pid]
        self.assertGreaterEqual(coins_before, 3)

        target_bot = self._find_alive_bot(host)

        # Wait for human's turn
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            host.poll(timeout=1.0)
            if (host.engine.phase() == PHASE_WAITING_FOR_ACTION and
                    host.engine.current_player() == my_pid):
                break
        self.assertEqual(host.engine.current_player(), my_pid,
                         "Not human's turn when trying to Assassinate")

        relays_before = len(host.input_relays)

        # Send Assassinate
        host.send_action(ACT_ASSASSINATE, target_bot)

        # Assassinate opens challenge window, then possibly block window.
        # Bots should handle all of this automatically.
        # Wait for the action to fully resolve.
        resolved = False
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            host.poll(timeout=1.0)
            if host.game_over:
                resolved = True
                break
            phase = host.engine.phase()
            # If human needs to respond to something (e.g. block challenge), pass
            if phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                         PHASE_BLOCK_CHALLENGE_WINDOW):
                if host.engine.pending_response(my_pid):
                    host.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if host.engine.influence_loser() == my_pid:
                    host.send_lose_influence(0)
            elif (phase == PHASE_WAITING_FOR_ACTION and
                  len(host.input_relays) > relays_before):
                resolved = True
                break

        self.assertTrue(resolved,
                        f"Assassinate action hung. Phase={host.engine.phase()}, "
                        f"relays={len(host.input_relays) - relays_before}")

        # Verify coins: Assassinate costs 3
        self.assertEqual(host.coins[my_pid], coins_before - 3,
                         "Assassinate should deduct 3 coins")

        # Verify the ACTION relay
        action_relays = [r for r in host.input_relays[relays_before:]
                         if len(r) >= 6 and r[3] == RELAY_ACTION]
        self.assertTrue(len(action_relays) >= 1,
                        "No ACTION relay received for Assassinate")
        relay = action_relays[0]
        self.assertEqual(relay[4], my_pid, "ACTION relay has wrong actor")
        self.assertEqual(relay[5], ACT_ASSASSINATE, "ACTION relay has wrong action")
        self.assertEqual(relay[6], target_bot, "ACTION relay has wrong target")

        self._assert_engines_in_sync(host, "after Assassinate")

    def test_human_steal_bot(self):
        """Human does Steal on a bot: action accepted, challenge/block
        windows resolve, coins transfer. Engines stay in sync."""
        host = self._start_human_plus_bots(num_bots=2, seed=42)

        my_pid = host.engine_pid

        # Wait for human's first turn (steal is always available)
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            host.poll(timeout=1.0)
            if (host.engine.phase() == PHASE_WAITING_FOR_ACTION and
                    host.engine.current_player() == my_pid):
                break

        target_bot = self._find_alive_bot(host)
        coins_before_me = host.coins[my_pid]
        coins_before_target = host.coins[target_bot]

        relays_before = len(host.input_relays)

        host.send_action(ACT_STEAL, target_bot)

        resolved = False
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            host.poll(timeout=1.0)
            if host.game_over:
                resolved = True
                break
            phase = host.engine.phase()
            if phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                         PHASE_BLOCK_CHALLENGE_WINDOW):
                if host.engine.pending_response(my_pid):
                    host.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if host.engine.influence_loser() == my_pid:
                    host.send_lose_influence(0)
            elif (phase == PHASE_WAITING_FOR_ACTION and
                  len(host.input_relays) > relays_before):
                resolved = True
                break

        self.assertTrue(resolved,
                        f"Steal action hung. Phase={host.engine.phase()}, "
                        f"relays={len(host.input_relays) - relays_before}")

        # Verify the ACTION relay
        action_relays = [r for r in host.input_relays[relays_before:]
                         if len(r) >= 6 and r[3] == RELAY_ACTION]
        self.assertTrue(len(action_relays) >= 1,
                        "No ACTION relay received for Steal")
        relay = action_relays[0]
        self.assertEqual(relay[4], my_pid, "ACTION relay has wrong actor")
        self.assertEqual(relay[5], ACT_STEAL, "ACTION relay has wrong action")
        self.assertEqual(relay[6], target_bot, "ACTION relay has wrong target")

        self._assert_engines_in_sync(host, "after Steal")

    def test_full_bot_game_completes(self):
        """1 human + 2 bots: play a full game (income + responses) until
        game over, verifying engines stay in sync throughout."""
        host = self._start_human_plus_bots(num_bots=2, seed=100)
        my_pid = host.engine_pid
        turns_played = 0

        for _ in range(80):
            host.poll(timeout=1.5)
            if host.game_over:
                break
            if not host.engine or not host.engine.game_active():
                break

            phase = host.engine.phase()
            if phase == PHASE_WAITING_FOR_ACTION:
                if host.engine.current_player() == my_pid:
                    host.send_action(ACT_INCOME)
                    turns_played += 1
            elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                           PHASE_BLOCK_CHALLENGE_WINDOW):
                if host.engine.pending_response(my_pid):
                    host.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if host.engine.influence_loser() == my_pid:
                    host.send_lose_influence(0)

        self.assertGreater(turns_played, 0, "Human never got a turn")
        self._assert_engines_in_sync(host, "after full game")

    def test_relay_action_matches_what_human_sent(self):
        """Verify that when human sends an action, the INPUT_RELAY broadcast
        contains exactly the same action, target, and actor."""
        host = self._start_human_plus_bots(num_bots=1, seed=42)
        my_pid = host.engine_pid

        # Wait for human's turn
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            host.poll(timeout=1.0)
            if (host.engine.phase() == PHASE_WAITING_FOR_ACTION and
                    host.engine.current_player() == my_pid):
                break

        target_bot = self._find_alive_bot(host)
        relays_before = len(host.input_relays)

        # Send Steal (always valid, has a target)
        host.send_action(ACT_STEAL, target_bot)

        # Collect relays
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            host.poll(timeout=0.5)
            new_relays = host.input_relays[relays_before:]
            action_relays = [r for r in new_relays
                             if len(r) >= 7 and r[3] == RELAY_ACTION]
            if action_relays:
                break

        self.assertTrue(len(action_relays) >= 1,
                        "No ACTION relay received")
        r = action_relays[0]
        # [0xB2][seq_hi][seq_lo][RELAY_ACTION][pid][action][target]
        self.assertEqual(r[3], RELAY_ACTION)
        self.assertEqual(r[4], my_pid, "Relay actor should match human pid")
        self.assertEqual(r[5], ACT_STEAL, "Relay action should be Steal")
        self.assertEqual(r[6], target_bot, "Relay target should match sent target")

    def test_all_human_game_coup_works(self):
        """2-human game: Coup action works (baseline sanity check)."""
        host, guest = self.start_2p_seeded(seed=42, first_player="Alice")

        # Build coins to 7 via alternating income, re-checking turn each time
        for _ in range(10):
            active, passive = self.who_has_turn(host, guest)
            if active.coins.get(active.engine_pid, 2) >= 7:
                break
            active.send_action(ACT_INCOME)
            self.assertTrue(passive.wait_for_turn(timeout=3.0),
                            "Passive didn't get turn")
            passive.send_action(ACT_INCOME)
            self.assertTrue(active.wait_for_turn(timeout=3.0),
                            "Active didn't get turn back")

        active, passive = self.who_has_turn(host, guest)
        self.assertGreaterEqual(active.coins.get(active.engine_pid, 2), 7,
                                "Active player needs 7+ coins for Coup")

        target_pid = passive.engine_pid
        relays_before = len(active.input_relays)

        active.send_action(ACT_COUP, target_pid)

        # Passive must lose influence
        self.assertTrue(passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0),
                        "Target didn't enter influence loss phase")
        passive.send_lose_influence(0)

        # Wait for next turn
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            active.poll(timeout=0.5)
            passive.poll(timeout=0.5)
            if active.engine.phase() == PHASE_WAITING_FOR_ACTION:
                break

        self.assertEqual(active.engine.phase(), PHASE_WAITING_FOR_ACTION)

        # Verify relay
        action_relays = [r for r in active.input_relays[relays_before:]
                         if len(r) >= 7 and r[3] == RELAY_ACTION]
        self.assertTrue(len(action_relays) >= 1)
        r = action_relays[0]
        self.assertEqual(r[5], ACT_COUP)
        self.assertEqual(r[6], target_pid)

    def test_all_human_game_assassinate_works(self):
        """2-human game: Assassinate action works (baseline sanity check)."""
        host, guest = self.start_2p_seeded(seed=42, first_player="Alice")

        # Build both players to 3+ coins via alternating income
        for _ in range(2):
            active, passive = self.who_has_turn(host, guest)
            active.send_action(ACT_INCOME)
            self.assertTrue(active.wait_for_next_turn(timeout=3.0) or
                            passive.wait_for_next_turn(timeout=1.0))

        # Now Alice should have 3 coins (2 initial + 1 income)
        active, passive = self.who_has_turn(host, guest)
        self.assertGreaterEqual(active.coins.get(active.engine_pid, 0), 3,
                                "Active player needs 3+ coins")

        target_pid = passive.engine_pid
        relays_before = len(active.input_relays)

        active.send_action(ACT_ASSASSINATE, target_pid)

        # Wait for the action to fully resolve (challenge/block/influence loss)
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            active.poll(timeout=0.3)
            passive.poll(timeout=0.3)
            phase_a = active.engine.phase()
            phase_p = passive.engine.phase()
            # Respond to any prompts for passive
            if phase_p == PHASE_CHALLENGE_WINDOW and passive.engine.pending_response(target_pid):
                passive.send_response(RESP_PASS)
            elif phase_p == PHASE_BLOCK_WINDOW and passive.engine.pending_response(target_pid):
                passive.send_response(RESP_PASS)
            elif phase_p == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if passive.engine.influence_loser() == target_pid:
                    passive.send_lose_influence(0)
            if phase_a == PHASE_WAITING_FOR_ACTION and len(active.input_relays) > relays_before:
                break

        # Verify the first ACTION relay matches what was sent
        action_relays = [r for r in active.input_relays[relays_before:]
                         if len(r) >= 7 and r[3] == RELAY_ACTION and r[4] == active.engine_pid]
        self.assertTrue(len(action_relays) >= 1, "No ACTION relay for assassinate")
        r = action_relays[0]
        self.assertEqual(r[5], ACT_ASSASSINATE)
        self.assertEqual(r[6], target_pid)


# ---------------------------------------------------------------------------
# Resync / sequence number tests
# ---------------------------------------------------------------------------

class TestResync(ServerTestCase):

    def test_input_relay_has_sequence_number(self):
        """INPUT_RELAY messages contain incrementing sequence numbers."""
        host, guest = self.start_2p_game()
        # After game start, both should have received at least one relay
        # (START_GAME relay). Verify seq starts at 0 and increments.
        self.assertTrue(len(host.relay_seqs) >= 1,
                        "Expected at least 1 relay with seq")
        self.assertEqual(host.relay_seqs[0], 0,
                         "First relay seq should be 0")

        # Do a couple income turns and check seq increments
        active, passive = self.who_has_turn(host, guest)
        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        # Both clients should have same seq progression
        self.assertEqual(len(host.relay_seqs), len(guest.relay_seqs),
                         "Both clients should receive same number of relays")
        for i in range(len(host.relay_seqs)):
            self.assertEqual(host.relay_seqs[i], i,
                             f"Host seq[{i}] should be {i}, got {host.relay_seqs[i]}")
            self.assertEqual(guest.relay_seqs[i], i,
                             f"Guest seq[{i}] should be {i}, got {guest.relay_seqs[i]}")

    def test_wrong_player_action_gets_rejected(self):
        """Sending action from wrong player triggers ACTION_REJECTED."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Passive player sends action — should be rejected
        passive.send_action(ACT_INCOME)
        time.sleep(0.3)
        passive.poll(timeout=1.0)

        self.assertTrue(len(passive.action_rejected) >= 1,
                        "Expected ACTION_REJECTED for wrong-player action")
        seq, phase = passive.action_rejected[0]
        # seq should match current relay seq
        expected_seq = len(passive.relay_seqs) - 1
        self.assertEqual(seq, expected_seq,
                         f"ACTION_REJECTED seq should be {expected_seq}, got {seq}")

    def test_wrong_phase_action_gets_rejected(self):
        """Sending action during wrong phase triggers ACTION_REJECTED."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Active sends Foreign Aid → goes into block window
        active.send_action(ACT_FOREIGN_AID)
        self.assertTrue(passive.wait_for_phase(PHASE_BLOCK_WINDOW, timeout=3.0))

        # Active tries to send another action during block window — should be rejected
        active.send_action(ACT_INCOME)
        time.sleep(0.3)
        active.poll(timeout=1.0)

        self.assertTrue(len(active.action_rejected) >= 1,
                        "Expected ACTION_REJECTED for wrong-phase action")

    def test_resync_req_returns_missed_relays(self):
        """RESYNC_REQ with old seq returns batch of missed relays."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Do an income turn to get more relays
        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        # Both should now have seq 0 (start) and seq 1 (income action)
        self.assertTrue(len(host.relay_seqs) >= 2)

        # Request resync from seq 0 (pretend we only got the first one)
        host.send_resync_req(0)
        time.sleep(0.5)
        host.poll(timeout=1.0)

        # Should receive a RESYNC message with the missed relays
        self.assertTrue(len(host.resync_msgs) >= 1,
                        "Expected RESYNC response to RESYNC_REQ")

    def test_action_rejected_contains_current_seq(self):
        """ACTION_REJECTED message contains current server seq and phase."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Passive sends action — rejected
        passive.send_action(ACT_INCOME)
        time.sleep(0.3)
        passive.poll(timeout=1.0)

        self.assertTrue(len(passive.action_rejected) >= 1)
        seq, phase = passive.action_rejected[0]
        # Phase should be WAITING_FOR_ACTION (since it's the active player's turn)
        from coup_engine import PHASE_WAITING_FOR_ACTION as PH_ACTION
        self.assertEqual(phase, PH_ACTION,
                         f"ACTION_REJECTED phase should be WAITING_FOR_ACTION, got {phase}")


    def test_targeted_action_uses_engine_pid(self):
        """Targeted actions (steal, coup) use engine_pid as target in INPUT_RELAY."""
        host, guest = self.start_2p_game()

        # Build coins for coup
        for _ in range(20):
            active, passive = self.who_has_turn(host, guest)
            my_coins = active.coins.get(active.engine_pid, 2)
            if my_coins >= 7:
                # Send coup targeting the other player's engine_pid
                active.send_action(ACT_COUP, passive.engine_pid)
                passive.wait_for_phase(PHASE_WAITING_FOR_INFLUENCE_LOSS, timeout=3.0)
                active.poll(timeout=0.5)

                # Find the coup INPUT_RELAY
                for relay in active.input_relays:
                    if len(relay) >= 6 and relay[3] == RELAY_ACTION:
                        action_type = relay[5]
                        target_pid = relay[6] if len(relay) > 6 else None
                        if action_type == ACT_COUP:
                            # Target should be 0-based engine_pid, not 1-based user_id
                            self.assertEqual(
                                target_pid, passive.engine_pid,
                                f"Coup target should be engine_pid {passive.engine_pid}, got {target_pid}")
                            passive.send_lose_influence(0)
                            active.poll(timeout=1.0)
                            return
                self.fail("No coup INPUT_RELAY found")
            active.send_action(ACT_INCOME)
            host.wait_for_next_turn(timeout=3.0)
            guest.poll(timeout=0.3)
        self.fail("Could not build enough coins for coup")

    def test_relay_input_rejection_sends_action_rejected(self):
        """Engine rejection in _relay_input sends ACTION_REJECTED to client."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Active player tries Coup with only 2 coins — passes server pre-validation
        # (correct player, correct phase) but engine rejects (insufficient coins).
        initial_rejected = len(active.action_rejected)
        active.send_action(ACT_COUP, passive.engine_pid)
        time.sleep(0.3)
        active.poll(timeout=1.0)

        self.assertTrue(len(active.action_rejected) > initial_rejected,
                        "Expected ACTION_REJECTED when engine rejects coup with insufficient coins")

    def test_resync_req_up_to_date_gets_response(self):
        """RESYNC_REQ when already up to date gets an empty RESYNC response."""
        host, guest = self.start_2p_game()
        active, passive = self.who_has_turn(host, guest)

        # Do an income turn to advance seq
        active.send_action(ACT_INCOME)
        passive.wait_for_next_turn(timeout=3.0)
        active.poll(timeout=0.5)

        # Now send RESYNC_REQ with the latest seq (already up to date)
        latest_seq = host.relay_seqs[-1] if host.relay_seqs else 0
        host.resync_msgs.clear()
        host.send_resync_req(latest_seq)
        time.sleep(0.5)
        host.poll(timeout=1.0)

        # Should receive a RESYNC response (even though we're up to date)
        self.assertTrue(len(host.resync_msgs) >= 1,
                        "Expected RESYNC response even when up to date")
        # The RESYNC should have count=0 (empty batch)
        resync = host.resync_msgs[-1]
        if len(resync) >= 2:
            count = resync[1]
            self.assertEqual(count, 0,
                             f"Expected empty RESYNC (count=0), got count={count}")


if __name__ == "__main__":
    unittest.main()
