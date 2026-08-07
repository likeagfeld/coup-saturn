#!/usr/bin/env python3
"""qa_midgame_spectator.py - a client that dials in mid-match must be enrolled
as a SPECTATOR, whichever of the two authentication paths it arrives on.

REPORTED
    Dialling in from a Saturn while a web browser on the same LAN was playing
    dropped the Saturn into the LOBBY instead of a spectator view.

WHAT THIS GATE MEASURES
    The client cannot decide for itself that a game is in progress - it has no
    other source for that fact. It learns it from GAME_START carrying the
    spectator sentinel engine pid 0xFF, and the Saturn client handles that
    correctly today (tests/coup/test_coup_identity.c:
    a_mid_game_joiner_is_a_spectator_not_a_lobby_member passes). What was
    missing is the server ever SENDING it on this path.

    server.py has two authentication paths and only ONE of them enrolls a
    spectator:

      _handle_set_username()  - a NEW user, no UUID. When game_active it sets
                                is_spectating, sends GAME_START(0xFF), then
                                RESYNC_FULL and the relay replay.

      _handle_connect()       - a RETURNING user presenting a UUID. When
                                game_active it sent the roster and nothing
                                else. No GAME_START, so the client never
                                called coup_start_game(), so is_spectator
                                stayed false and it sat in the lobby.

    A Saturn that has played before stores its UUID in backup RAM and so takes
    the SECOND path every time after its first ever connection - which is why
    this reproduces on a real console and not on a fresh test client.

    The gate drives both paths against the same server and requires them to
    agree. It runs the real server object; nothing is stubbed except the
    sockets, and no game is started, so the C rules library is not needed.

Exit code 0 = GREEN, 1 = RED.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "tools", "coup_server"))

from server import (                                    # noqa: E402
    CoupServer,
    MSG_CONNECT,
    MSG_SET_USERNAME,
    COUP_MSG_GAME_START,
    ClientInfo,
)

UUID_LEN = 36
SPECTATOR_PID = 0xFF


class FakeSocket:
    """Captures frames instead of writing them to a network."""

    def __init__(self, name):
        self.name = name
        self.frames = []

    def sendall(self, frame):
        # Strip the [LEN_HI][LEN_LO] framing so tests read payloads.
        body = frame[2:]
        if body:
            self.frames.append(body)

    def messages_of_type(self, msg_type):
        return [f for f in self.frames if f and f[0] == msg_type]

    def __hash__(self):
        return id(self)


def make_server():
    srv = CoupServer(host="127.0.0.1", port=0)
    fd, path = tempfile.mkstemp(suffix=".txt", prefix="qa_spectator_uuid_")
    os.close(fd)
    srv._uuid_file = path
    return srv, path


def add_client(srv, name):
    sock = FakeSocket(name)
    info = ClientInfo(sock, (name, 0))
    srv.clients[sock] = info
    return sock, info


def register(srv, sock, username):
    """Take a client through the NEW-user path and return its UUID."""
    payload = bytes([MSG_SET_USERNAME, len(username)]) + username.encode()
    srv._handle_set_username(sock, srv.clients[sock], payload)
    return srv.clients[sock].uuid


def put_a_game_in_progress(srv, playing):
    """Mark a match under way without needing the C engine."""
    srv.game_active = True
    srv.engine_seed = 31337
    srv.turn_order = list(playing)
    srv.relay_log = [(0, 0, b"")]
    srv.in_process_bots = {}
    for info in playing:
        info.in_game = True


def game_start_pid(sock):
    """Engine pid from the GAME_START the client received, or None.

    Payload: [0xA1][seed:4 BE][my_engine_pid:1][count:1][uid...]
    """
    msgs = sock.messages_of_type(COUP_MSG_GAME_START)
    if not msgs:
        return None
    body = msgs[-1]
    if len(body) < 6:
        return None
    return body[5]


FAILURES = []


def check(condition, description):
    status = "PASS" if condition else "FAIL"
    print("  [%s] %s" % (status, description))
    if not condition:
        FAILURES.append(description)


def main():
    srv, uuid_path = make_server()
    try:
        print("Mid-match join must produce a spectator on BOTH auth paths")

        # Two players are mid-match.
        web_sock, web_info = add_client(srv, "WEB")
        register(srv, web_sock, "FARKUS")
        other_sock, other_info = add_client(srv, "OTHER")
        register(srv, other_sock, "GRETA")

        # A Saturn that has played here before: it registered once, went away,
        # and kept its UUID in backup RAM.
        sat_sock, sat_info = add_client(srv, "SATURN-FIRST-VISIT")
        saturn_uuid = register(srv, sat_sock, "A")
        check(len(saturn_uuid) == UUID_LEN,
              "the returning client has a UUID to present (%d chars)"
              % len(saturn_uuid))
        del srv.clients[sat_sock]

        put_a_game_in_progress(srv, [web_info, other_info])

        # Everything below is about what happens DURING the match. The roster
        # broadcasts from the three registrations above were legitimate - the
        # lobby was open then - so forget them, or the mid-match assertions
        # would be reading pre-match traffic.
        for s in (web_sock, other_sock):
            s.frames.clear()

        # --- Path 1: a brand new client, no UUID. The known-good path, here
        # to prove the gate is measuring a real difference and not an
        # impossible expectation. ---
        new_sock, _ = add_client(srv, "NEW")
        register(srv, new_sock, "NEWCOMER")
        new_pid = game_start_pid(new_sock)
        check(new_pid == SPECTATOR_PID,
              "a NEW client mid-match receives GAME_START with pid 0xFF "
              "(got %r)" % new_pid)
        check(srv.clients[new_sock].is_spectating,
              "a NEW client mid-match is flagged is_spectating")

        # --- Path 2: the returning Saturn presenting its UUID. ---
        ret_sock, ret_info = add_client(srv, "SATURN-RETURNING")
        payload = bytes([MSG_CONNECT]) + saturn_uuid.encode("ascii")
        srv._handle_connect(ret_sock, ret_info, payload)

        check(ret_info.authenticated,
              "the returning client authenticated from its UUID")

        ret_pid = game_start_pid(ret_sock)
        check(ret_pid == SPECTATOR_PID,
              "a RETURNING client mid-match receives GAME_START with pid 0xFF "
              "(got %r)" % ret_pid)
        check(ret_info.is_spectating,
              "a RETURNING client mid-match is flagged is_spectating")

        # The roster still has to arrive - that is what names the seats it is
        # watching, and it is the fix ac3e356 put in. It must not be lost.
        from server import COUP_MSG_LOBBY_STATE
        check(len(ret_sock.messages_of_type(COUP_MSG_LOBBY_STATE)) >= 1,
              "a RETURNING client mid-match still receives the roster")

        # And nobody already playing may be disturbed - broadcasting the roster
        # mid-game is what froze matches (ae78adf).
        check(len(web_sock.messages_of_type(COUP_MSG_LOBBY_STATE)) == 0,
              "a player already in the match receives no roster broadcast")
    finally:
        try:
            os.unlink(uuid_path)
        except OSError:
            pass

    print()
    if FAILURES:
        print("RED - %d check(s) failed:" % len(FAILURES))
        for f in FAILURES:
            print("  - %s" % f)
        return 1
    print("GREEN - a mid-match joiner becomes a spectator on both auth paths")
    return 0


if __name__ == "__main__":
    sys.exit(main())
