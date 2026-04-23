"""
Integration tests for the shared C bot AI library.

Tests the bot_decide bridge through the CoupEngine Python wrapper.
Verifies that bots can play a complete automated game.

Run:  cd tools/coup_server && python3 -m pytest test_bot_integration.py -v
"""

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
    PHASE_LOBBY,
    RESP_BLOCK,
)

BOT_DIFFICULTY_EASY = 0
BOT_DIFFICULTY_MEDIUM = 1
BOT_DIFFICULTY_HARD = 2


class TestBotBridge(unittest.TestCase):
    """Test bot_decide via CoupEngine.bot_decide()."""

    def setUp(self):
        self.engine = CoupEngine()

    def _start_game(self, player_count=4, seed=42):
        """Start a game with the given number of players."""
        self.engine.init(seed)
        for i in range(player_count):
            self.engine.submit_add_player()
            self.engine.submit_set_ready(i, 1)
        self.engine.submit_start()

    def test_bot_decide_returns_valid_action(self):
        """Bot should return a valid action when it's their turn."""
        self._start_game(3, seed=42)
        self.assertEqual(self.engine.phase(), PHASE_WAITING_FOR_ACTION)

        cur = self.engine.current_player()
        rng, decision = self.engine.bot_decide(cur, BOT_DIFFICULTY_MEDIUM, 12345)

        self.assertIsNotNone(decision)
        self.assertEqual(decision["type"], 1)  # INPUT_ACTION
        self.assertEqual(decision["player_id"], cur)
        self.assertIn("action", decision)
        self.assertIn("target", decision)

    def test_bot_decide_returns_none_when_not_turn(self):
        """Bot should return None when it's not their turn."""
        self._start_game(3, seed=42)
        cur = self.engine.current_player()
        other = (cur + 1) % 3

        rng, decision = self.engine.bot_decide(other, BOT_DIFFICULTY_EASY, 99999)
        self.assertIsNone(decision)

    def test_bot_decide_deterministic(self):
        """Same state + same RNG → same decision."""
        self._start_game(3, seed=42)
        cur = self.engine.current_player()

        rng1, d1 = self.engine.bot_decide(cur, BOT_DIFFICULTY_MEDIUM, 12345)
        rng2, d2 = self.engine.bot_decide(cur, BOT_DIFFICULTY_MEDIUM, 12345)

        self.assertEqual(rng1, rng2)
        self.assertEqual(d1, d2)

    def test_bot_action_accepted_by_engine(self):
        """Bot's action should be accepted by the engine."""
        self._start_game(3, seed=42)
        cur = self.engine.current_player()

        rng, decision = self.engine.bot_decide(cur, BOT_DIFFICULTY_MEDIUM, 42)
        self.assertIsNotNone(decision)

        # Submit action to engine
        result = self.engine.submit_action(
            decision["player_id"],
            decision["action"],
            decision["target"])
        self.assertGreater(result, 0)


class TestBotFullGame(unittest.TestCase):
    """Test a fully automated game driven by bots."""

    def test_all_bots_complete_game(self):
        """Run a full game with all bot players — should terminate."""
        engine = CoupEngine()
        engine.init(42)
        n_players = 4

        for i in range(n_players):
            engine.submit_add_player()
            engine.submit_set_ready(i, 1)
        engine.submit_start()

        # Per-bot RNG state
        rng = {i: 12345 + i * 7919 for i in range(n_players)}
        max_turns = 500

        for turn in range(max_turns):
            if not engine.game_active():
                break

            phase = engine.phase()

            if phase == PHASE_WAITING_FOR_ACTION:
                cur = engine.current_player()
                new_rng, decision = engine.bot_decide(
                    cur, BOT_DIFFICULTY_MEDIUM, rng[cur])
                rng[cur] = new_rng
                if decision:
                    engine.submit_action(
                        decision["player_id"],
                        decision["action"],
                        decision["target"])

            elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                           PHASE_BLOCK_CHALLENGE_WINDOW):
                # Have each pending player respond
                for pid in range(n_players):
                    if engine.pending_response(pid):
                        new_rng, decision = engine.bot_decide(
                            pid, BOT_DIFFICULTY_MEDIUM, rng[pid])
                        rng[pid] = new_rng
                        if decision:
                            engine.submit_response(
                                decision["player_id"],
                                decision["response"])
                            # Handle block claim if needed
                            if (decision.get("block_claim") and
                                    engine.phase() == PHASE_RESOLVING):
                                engine.submit_block_claim(
                                    decision["player_id"],
                                    decision["block_claim"]["character"])

                # If still pending after all bots responded, timeout
                if engine.phase() in (PHASE_CHALLENGE_WINDOW,
                                      PHASE_BLOCK_WINDOW,
                                      PHASE_BLOCK_CHALLENGE_WINDOW):
                    engine.submit_timeout()

            elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                pid = engine.influence_loser()
                if pid != 0xFF:
                    new_rng, decision = engine.bot_decide(
                        pid, BOT_DIFFICULTY_MEDIUM, rng[pid])
                    rng[pid] = new_rng
                    if decision:
                        engine.submit_lose_influence(
                            decision["player_id"],
                            decision["card_idx"])

            elif phase == PHASE_WAITING_FOR_EXCHANGE:
                pid = engine.exchange_player()
                if pid != 0xFF:
                    new_rng, decision = engine.bot_decide(
                        pid, BOT_DIFFICULTY_MEDIUM, rng[pid])
                    rng[pid] = new_rng
                    if decision:
                        engine.submit_exchange(
                            decision["player_id"],
                            decision["keep0"],
                            decision["keep1"])

            elif phase == PHASE_RESOLVING:
                # May need block claim or timeout
                blocker = engine.blocker_id()
                if blocker != 0xFF and blocker < n_players:
                    new_rng, decision = engine.bot_decide(
                        blocker, BOT_DIFFICULTY_MEDIUM, rng[blocker])
                    rng[blocker] = new_rng
                else:
                    engine.submit_timeout()

        self.assertFalse(engine.game_active(),
                         f"Game did not complete in {max_turns} turns")

    def test_multiple_games_different_seeds(self):
        """Run multiple games with different seeds — all should complete."""
        for seed in [1, 42, 9999, 31337, 65535]:
            engine = CoupEngine()
            engine.init(seed)
            n_players = 3

            for i in range(n_players):
                engine.submit_add_player()
                engine.submit_set_ready(i, 1)
            engine.submit_start()

            rng = {i: seed + i * 7919 for i in range(n_players)}

            for turn in range(300):
                if not engine.game_active():
                    break

                phase = engine.phase()

                if phase == PHASE_WAITING_FOR_ACTION:
                    cur = engine.current_player()
                    new_rng, decision = engine.bot_decide(
                        cur, BOT_DIFFICULTY_HARD, rng[cur])
                    rng[cur] = new_rng
                    if decision:
                        engine.submit_action(
                            decision["player_id"],
                            decision["action"],
                            decision["target"])

                elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                               PHASE_BLOCK_CHALLENGE_WINDOW):
                    for pid in range(n_players):
                        if engine.pending_response(pid):
                            new_rng, decision = engine.bot_decide(
                                pid, BOT_DIFFICULTY_HARD, rng[pid])
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
                        new_rng, decision = engine.bot_decide(
                            pid, BOT_DIFFICULTY_HARD, rng[pid])
                        rng[pid] = new_rng
                        if decision:
                            engine.submit_lose_influence(
                                decision["player_id"],
                                decision["card_idx"])

                elif phase == PHASE_WAITING_FOR_EXCHANGE:
                    pid = engine.exchange_player()
                    if pid != 0xFF:
                        new_rng, decision = engine.bot_decide(
                            pid, BOT_DIFFICULTY_HARD, rng[pid])
                        rng[pid] = new_rng
                        if decision:
                            engine.submit_exchange(
                                decision["player_id"],
                                decision["keep0"],
                                decision["keep1"])

                elif phase == PHASE_RESOLVING:
                    engine.submit_timeout()

            self.assertFalse(
                engine.game_active(),
                f"Game with seed={seed} did not complete")


class TestBotBlockFlow(unittest.TestCase):
    """Test bot block → block_claim two-step flow."""

    def test_bot_block_produces_block_claim(self):
        """When bot decides to block, decision includes block_claim with character."""
        # Try many seeds to find one where a bot decides to block
        found_block = False
        for seed in range(1, 200):
            engine = CoupEngine()
            engine.init(seed)
            n = 3
            for i in range(n):
                engine.submit_add_player()
                engine.submit_set_ready(i, 1)
            engine.submit_start()

            rng = {i: 42 + i for i in range(n)}

            # Play a few turns to get an action that can be blocked
            for turn in range(50):
                if not engine.game_active():
                    break
                phase = engine.phase()

                if phase == PHASE_WAITING_FOR_ACTION:
                    cur = engine.current_player()
                    new_rng, d = engine.bot_decide(cur, BOT_DIFFICULTY_HARD, rng[cur])
                    rng[cur] = new_rng
                    if d:
                        engine.submit_action(d["player_id"], d["action"], d["target"])

                elif phase in (PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                               PHASE_BLOCK_CHALLENGE_WINDOW):
                    for pid in range(n):
                        if not engine.game_active():
                            break
                        if engine.pending_response(pid):
                            new_rng, d = engine.bot_decide(pid, BOT_DIFFICULTY_HARD, rng[pid])
                            rng[pid] = new_rng
                            if d and d.get("response") == RESP_BLOCK:
                                # Found a block decision
                                self.assertIn("block_claim", d,
                                              "Block decision missing block_claim")
                                self.assertIn("character", d["block_claim"],
                                              "block_claim missing character")
                                char = d["block_claim"]["character"]
                                self.assertIn(char, range(5),
                                              f"Invalid block character: {char}")
                                # Verify the two-step flow works
                                engine.submit_response(d["player_id"], d["response"])
                                if engine.phase() == PHASE_RESOLVING:
                                    result = engine.submit_block_claim(
                                        d["player_id"],
                                        d["block_claim"]["character"])
                                    self.assertGreater(result, 0,
                                                       "Engine rejected block claim from bot")
                                found_block = True
                                break
                            elif d:
                                engine.submit_response(d["player_id"], d["response"])
                    if found_block:
                        break
                    # If still in a window, timeout
                    if engine.game_active() and engine.phase() in (
                            PHASE_CHALLENGE_WINDOW, PHASE_BLOCK_WINDOW,
                            PHASE_BLOCK_CHALLENGE_WINDOW):
                        engine.submit_timeout()

                elif phase == PHASE_WAITING_FOR_INFLUENCE_LOSS:
                    pid = engine.influence_loser()
                    if pid != 0xFF:
                        new_rng, d = engine.bot_decide(pid, BOT_DIFFICULTY_HARD, rng[pid])
                        rng[pid] = new_rng
                        if d:
                            engine.submit_lose_influence(d["player_id"], d["card_idx"])

                elif phase == PHASE_WAITING_FOR_EXCHANGE:
                    pid = engine.exchange_player()
                    if pid != 0xFF:
                        new_rng, d = engine.bot_decide(pid, BOT_DIFFICULTY_HARD, rng[pid])
                        rng[pid] = new_rng
                        if d:
                            engine.submit_exchange(d["player_id"], d["keep0"], d["keep1"])

                elif phase == PHASE_RESOLVING:
                    engine.submit_timeout()

                if found_block:
                    break
            if found_block:
                break

        self.assertTrue(found_block,
                        "No bot block decision found across 200 seeds — bot block logic may be broken")


if __name__ == "__main__":
    unittest.main()
