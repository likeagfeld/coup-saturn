"""
Test various player arrangements: all-human, all-bot, and mixed.

Exercises the C rule engine directly (no server) to verify that every
combination of humans and bots can complete a game successfully.
Also tests server-mediated games for arrangements involving bots.

Run:  cd tools/coup_server && python3 test_game_arrangements.py
"""

import time
import unittest

from coup_engine import (
    CoupEngine,
    PHASE_WAITING_FOR_ACTION,
    PHASE_CHALLENGE_WINDOW,
    PHASE_BLOCK_WINDOW,
    PHASE_BLOCK_CHALLENGE_WINDOW,
    PHASE_WAITING_FOR_INFLUENCE_LOSS,
    PHASE_WAITING_FOR_EXCHANGE,
    PHASE_RESOLVING,
)

from server import (
    COUP_MSG_GAME_START,
    RESP_PASS,
    ACT_INCOME as SRV_ACT_INCOME,
)

from test_server_integration import ServerTestCase

BOT_DIFFICULTY_MEDIUM = 1
BOT_DIFFICULTY_HARD = 2

# Re-export ACT_INCOME for human logic (from coup_engine, not server)
ACT_INCOME_ENGINE = 0  # matches coup_engine.ACT_INCOME
ACT_COUP_ENGINE = 2    # matches coup_engine.ACT_COUP


# ======================================================================
# Engine-only game runner (no server needed)
# ======================================================================

def run_engine_game(n_humans, n_bots, seed=42, difficulty=BOT_DIFFICULTY_MEDIUM,
                    max_turns=500):
    """Run a complete game using only the C engine.

    Human players always do Income + pass on challenges/blocks.
    Bot players use the C bot_decide AI.

    Returns dict with game results.
    """
    engine = CoupEngine()
    engine.init(seed)
    n_players = n_humans + n_bots

    # Add humans first, then bots (mirrors server behavior)
    for i in range(n_humans):
        engine.submit_add_player()
        engine.submit_set_ready(i, 1)
    for i in range(n_bots):
        engine.submit_add_bot()

    engine.submit_start()

    rng = {i: seed + i * 7919 for i in range(n_players)}
    human_pids = set(range(n_humans))
    turns = 0

    for turn in range(max_turns):
        if not engine.game_active():
            break

        phase = engine.phase()

        if phase == PHASE_WAITING_FOR_ACTION:
            cur = engine.current_player()
            if cur in human_pids:
                valid = engine.valid_actions()
                if valid == (1 << ACT_COUP_ENGINE):
                    # Forced coup — target first alive non-self player
                    target = 0xFF
                    for t in range(n_players):
                        if t != cur and engine.player_alive(t):
                            target = t
                            break
                    engine.submit_action(cur, ACT_COUP_ENGINE, target)
                else:
                    engine.submit_action(cur, ACT_INCOME_ENGINE, 0xFF)
                turns += 1
            else:
                new_rng, decision = engine.bot_decide(cur, difficulty, rng[cur])
                rng[cur] = new_rng
                if decision:
                    engine.submit_action(
                        decision["player_id"],
                        decision["action"],
                        decision["target"])
                    turns += 1

        elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                       PHASE_BLOCK_CHALLENGE_WINDOW):
            for pid in range(n_players):
                if engine.pending_response(pid):
                    if pid in human_pids:
                        engine.submit_response(pid, RESP_PASS)
                    else:
                        new_rng, decision = engine.bot_decide(
                            pid, difficulty, rng[pid])
                        rng[pid] = new_rng
                        if decision:
                            engine.submit_response(
                                decision["player_id"],
                                decision["response"])
                            if (decision.get("block_claim") and
                                    engine.phase() == PHASE_RESOLVING):
                                engine.submit_block_claim(
                                    decision["player_id"],
                                    decision["block_claim"]["character"])
            if engine.phase() in (PHASE_CHALLENGE_WINDOW,
                                  PHASE_BLOCK_WINDOW,
                                  PHASE_BLOCK_CHALLENGE_WINDOW):
                engine.submit_timeout()

        elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
            pid = engine.influence_loser()
            if pid != 0xFF:
                if pid in human_pids:
                    engine.submit_lose_influence(pid, 0)
                else:
                    new_rng, decision = engine.bot_decide(
                        pid, difficulty, rng[pid])
                    rng[pid] = new_rng
                    if decision:
                        engine.submit_lose_influence(
                            decision["player_id"],
                            decision["card_idx"])

        elif phase == PHASE_WAITING_FOR_EXCHANGE:
            pid = engine.exchange_player()
            if pid != 0xFF:
                if pid in human_pids:
                    engine.submit_exchange(pid, 0, 1)
                else:
                    new_rng, decision = engine.bot_decide(
                        pid, difficulty, rng[pid])
                    rng[pid] = new_rng
                    if decision:
                        engine.submit_exchange(
                            decision["player_id"],
                            decision["keep0"],
                            decision["keep1"])

        elif phase == PHASE_RESOLVING:
            blocker = engine.blocker_id()
            if blocker != 0xFF and blocker < n_players and blocker not in human_pids:
                new_rng, decision = engine.bot_decide(
                    blocker, difficulty, rng[blocker])
                rng[blocker] = new_rng
            else:
                engine.submit_timeout()

    # Find winner
    winner = None
    for pid in range(n_players):
        if engine.player_alive(pid):
            winner = pid
            break

    return {
        "completed": not engine.game_active(),
        "turns": turns,
        "winner": winner,
        "winner_is_human": winner in human_pids if winner is not None else None,
        "winner_is_bot": winner not in human_pids if winner is not None else None,
    }


# ======================================================================
# Test cases: Engine-only arrangements (fast, no server)
# ======================================================================

class TestEngineArrangements(unittest.TestCase):
    """Test all player arrangements using the C engine directly."""

    # -- All human --

    def test_2_humans_0_bots(self):
        """2 humans, 0 bots: classic 2-player."""
        result = run_engine_game(2, 0, seed=42)
        self.assertTrue(result["completed"], "2H+0B game did not complete")
        self.assertTrue(result["winner_is_human"])
        print(f"  2H+0B: completed in {result['turns']} turns, winner=P{result['winner']}")

    def test_3_humans_0_bots(self):
        """3 humans, 0 bots."""
        result = run_engine_game(3, 0, seed=42)
        self.assertTrue(result["completed"], "3H+0B game did not complete")
        self.assertTrue(result["winner_is_human"])
        print(f"  3H+0B: completed in {result['turns']} turns, winner=P{result['winner']}")

    def test_4_humans_0_bots(self):
        """4 humans, 0 bots."""
        result = run_engine_game(4, 0, seed=42)
        self.assertTrue(result["completed"], "4H+0B game did not complete")
        print(f"  4H+0B: completed in {result['turns']} turns, winner=P{result['winner']}")

    def test_6_humans_0_bots(self):
        """6 humans (max), 0 bots."""
        result = run_engine_game(6, 0, seed=42)
        self.assertTrue(result["completed"], "6H+0B game did not complete")
        print(f"  6H+0B: completed in {result['turns']} turns, winner=P{result['winner']}")

    # -- All bot --

    def test_0_humans_2_bots(self):
        """0 humans, 2 bots: minimum all-bot game."""
        result = run_engine_game(0, 2, seed=42)
        self.assertTrue(result["completed"], "0H+2B game did not complete")
        self.assertTrue(result["winner_is_bot"])
        print(f"  0H+2B: completed in {result['turns']} turns, winner=P{result['winner']}")

    def test_0_humans_3_bots(self):
        """0 humans, 3 bots."""
        result = run_engine_game(0, 3, seed=42)
        self.assertTrue(result["completed"], "0H+3B game did not complete")
        print(f"  0H+3B: completed in {result['turns']} turns, winner=P{result['winner']}")

    def test_0_humans_4_bots(self):
        """0 humans, 4 bots."""
        result = run_engine_game(0, 4, seed=42)
        self.assertTrue(result["completed"], "0H+4B game did not complete")
        print(f"  0H+4B: completed in {result['turns']} turns, winner=P{result['winner']}")

    def test_0_humans_6_bots(self):
        """0 humans, 6 bots (max)."""
        result = run_engine_game(0, 6, seed=42)
        self.assertTrue(result["completed"], "0H+6B game did not complete")
        print(f"  0H+6B: completed in {result['turns']} turns, winner=P{result['winner']}")

    # -- Mixed: 1 human + bots --

    def test_1_human_1_bot(self):
        """1 human + 1 bot: the Saturn bug scenario."""
        result = run_engine_game(1, 1, seed=42)
        self.assertTrue(result["completed"], "1H+1B game did not complete")
        print(f"  1H+1B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    def test_1_human_2_bots(self):
        """1 human + 2 bots."""
        result = run_engine_game(1, 2, seed=42)
        self.assertTrue(result["completed"], "1H+2B game did not complete")
        print(f"  1H+2B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    def test_1_human_3_bots(self):
        """1 human + 3 bots."""
        result = run_engine_game(1, 3, seed=42)
        self.assertTrue(result["completed"], "1H+3B game did not complete")
        print(f"  1H+3B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    def test_1_human_5_bots(self):
        """1 human + 5 bots (max players)."""
        result = run_engine_game(1, 5, seed=42)
        self.assertTrue(result["completed"], "1H+5B game did not complete")
        print(f"  1H+5B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    # -- Mixed: 2+ humans + bots --

    def test_2_humans_1_bot(self):
        """2 humans + 1 bot."""
        result = run_engine_game(2, 1, seed=42)
        self.assertTrue(result["completed"], "2H+1B game did not complete")
        print(f"  2H+1B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    def test_2_humans_2_bots(self):
        """2 humans + 2 bots."""
        result = run_engine_game(2, 2, seed=42)
        self.assertTrue(result["completed"], "2H+2B game did not complete")
        print(f"  2H+2B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    def test_3_humans_2_bots(self):
        """3 humans + 2 bots."""
        result = run_engine_game(3, 2, seed=42)
        self.assertTrue(result["completed"], "3H+2B game did not complete")
        print(f"  3H+2B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    def test_3_humans_3_bots(self):
        """3 humans + 3 bots (max players)."""
        result = run_engine_game(3, 3, seed=42)
        self.assertTrue(result["completed"], "3H+3B game did not complete")
        print(f"  3H+3B: completed in {result['turns']} turns, "
              f"winner=P{result['winner']} ({'human' if result['winner_is_human'] else 'bot'})")

    # -- Multiple seeds for robustness --

    def test_1h_1b_multiple_seeds(self):
        """1 human + 1 bot across 10 different seeds."""
        for seed in [1, 7, 42, 100, 999, 1337, 9999, 31337, 55555, 65535]:
            result = run_engine_game(1, 1, seed=seed)
            self.assertTrue(result["completed"],
                            f"1H+1B seed={seed} did not complete")
        print("  1H+1B x 10 seeds: all completed")

    def test_0h_4b_multiple_seeds(self):
        """0 humans + 4 bots across 10 different seeds."""
        for seed in [1, 7, 42, 100, 999, 1337, 9999, 31337, 55555, 65535]:
            result = run_engine_game(0, 4, seed=seed)
            self.assertTrue(result["completed"],
                            f"0H+4B seed={seed} did not complete")
        print("  0H+4B x 10 seeds: all completed")

    # -- Difficulty levels --

    def test_1h_3b_easy(self):
        """1 human + 3 easy bots."""
        result = run_engine_game(1, 3, seed=42, difficulty=0)
        self.assertTrue(result["completed"])
        print(f"  1H+3B(easy): completed in {result['turns']} turns")

    def test_1h_3b_hard(self):
        """1 human + 3 hard bots."""
        result = run_engine_game(1, 3, seed=42, difficulty=BOT_DIFFICULTY_HARD)
        self.assertTrue(result["completed"])
        print(f"  1H+3B(hard): completed in {result['turns']} turns")


# ======================================================================
# Server-mediated tests (slower, validates full protocol)
# ======================================================================

class TestServerArrangements(ServerTestCase):
    """Test arrangements through the actual server with TCP clients."""

    def setUp(self):
        super().setUp()
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

    def _play_turns(self, client, max_iters=30):
        """Play turns as a human client (income + pass), return turns played."""
        turns = 0
        for _ in range(max_iters):
            client.poll(timeout=1.5)
            if client.game_over:
                break
            if not client.engine:
                continue
            phase = client.engine.phase()
            pid = client.engine_pid
            if phase == PHASE_WAITING_FOR_ACTION:
                if client.engine.current_player() == pid:
                    client.send_action(SRV_ACT_INCOME)
                    turns += 1
            elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                           PHASE_BLOCK_CHALLENGE_WINDOW):
                if client.engine.pending_response(pid):
                    client.send_response(RESP_PASS)
            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                if client.engine.influence_loser() == pid:
                    client.send_lose_influence(0)
            elif phase == PHASE_WAITING_FOR_EXCHANGE:
                if client.engine.exchange_player() == pid:
                    client.send_exchange(0, 1)
        return turns

    def test_server_2_humans(self):
        """Server: 2 humans, no bots."""
        self.server._test_seed = 42
        alice = self.make_client("Alice")
        bob = self.make_client("Bob")
        alice.poll(timeout=0.5)
        bob.poll(timeout=0.5)
        alice.send_ready()
        bob.send_ready()
        alice.poll(timeout=0.5)
        bob.poll(timeout=0.5)
        alice.send_start_game()
        self.assertIsNotNone(alice.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        self.assertIsNotNone(bob.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        alice.poll(timeout=0.5)
        bob.poll(timeout=0.5)

        self.assertTrue(alice.game_started)
        self.assertTrue(bob.game_started)
        self.assertEqual(alice.player_count, 2)

        # Verify engine states match
        self.assertEqual(alice.engine.phase(), bob.engine.phase())
        self.assertEqual(alice.engine.phase(), self.server.engine.phase())
        print("  Server 2H: game started, engines in sync")

    def test_server_1_human_1_bot(self):
        """Server: 1 human + 1 bot — the key Saturn fix scenario."""
        self.server._test_seed = 42
        alice = self.make_client("Alice")
        alice.poll(timeout=0.5)
        alice.send_add_bot()
        alice.poll(timeout=0.3)
        alice.send_ready()
        alice.poll(timeout=0.5)
        alice.send_start_game()
        self.assertIsNotNone(alice.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        alice.poll(timeout=1.0)

        self.assertTrue(alice.game_started)
        self.assertEqual(alice.player_count, 2)
        self.assertFalse(alice.engine.player_is_bot(0))
        self.assertTrue(alice.engine.player_is_bot(1))
        self.assertEqual(alice.engine.phase(), self.server.engine.phase())

        turns = self._play_turns(alice, max_iters=30)
        self.assertGreater(turns, 0)
        print(f"  Server 1H+1B: {turns} human turns played, engines in sync")

    def test_server_1_human_3_bots(self):
        """Server: 1 human + 3 bots."""
        self.server._test_seed = 42
        alice = self.make_client("Alice")
        alice.poll(timeout=0.5)
        for _ in range(3):
            alice.send_add_bot()
            alice.poll(timeout=0.3)
        alice.send_ready()
        alice.poll(timeout=0.5)
        alice.send_start_game()
        self.assertIsNotNone(alice.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        alice.poll(timeout=1.0)

        self.assertTrue(alice.game_started)
        self.assertEqual(alice.player_count, 4)
        self.assertFalse(alice.engine.player_is_bot(0))
        for pid in range(1, 4):
            self.assertTrue(alice.engine.player_is_bot(pid))

        turns = self._play_turns(alice, max_iters=30)
        self.assertGreater(turns, 0)

        alice.poll(timeout=1.0)
        for pid in range(4):
            self.assertEqual(
                alice.engine.player_coins(pid),
                self.server.engine.player_coins(pid),
                f"Coin mismatch pid={pid}")
        print(f"  Server 1H+3B: {turns} human turns, engines in sync")

    def test_server_2_humans_2_bots(self):
        """Server: 2 humans + 2 bots."""
        self.server._test_seed = 42
        alice = self.make_client("Alice")
        bob = self.make_client("Bob")
        alice.poll(timeout=0.5)
        bob.poll(timeout=0.5)
        for _ in range(2):
            alice.send_add_bot()
            alice.poll(timeout=0.3)
        alice.send_ready()
        bob.send_ready()
        alice.poll(timeout=0.5)
        bob.poll(timeout=0.5)
        alice.send_start_game()
        self.assertIsNotNone(alice.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        self.assertIsNotNone(bob.wait_for(COUP_MSG_GAME_START, timeout=3.0))
        alice.poll(timeout=1.0)
        bob.poll(timeout=1.0)

        self.assertTrue(alice.game_started)
        self.assertTrue(bob.game_started)
        self.assertEqual(alice.player_count, 4)

        for c in [alice, bob]:
            self.assertFalse(c.engine.player_is_bot(0))
            self.assertFalse(c.engine.player_is_bot(1))
            self.assertTrue(c.engine.player_is_bot(2))
            self.assertTrue(c.engine.player_is_bot(3))

        self.assertEqual(alice.engine.phase(), bob.engine.phase())
        self.assertEqual(alice.engine.phase(), self.server.engine.phase())
        print("  Server 2H+2B: 4-player game started, all engines in sync")


if __name__ == "__main__":
    unittest.main(verbosity=2)
