/**
 * test_coup_table_view.c - Tests for the shared table view builder
 *
 * Verifies that coup_table_view_from_rules() produces correct, consistent
 * snapshots from any coup_rules_t state. This is the single source of truth
 * for how both bot AI and human rendering see the game.
 */

#include "cui_test_framework.h"
#include "coup_rules.h"
#include "coup_table_view.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Helpers (shared with other coup test files)
 * ====================================================================== */

static int tv_emit(coup_rules_t* r, const coup_input_t* in,
                   coup_event_t* out, int max)
{
    return coup_rules_submit(r, in, out, max);
}

static coup_input_t tv_make_add_player(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_ADD_PLAYER;
    return in;
}

static coup_input_t tv_make_add_bot(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_ADD_BOT;
    return in;
}

static coup_input_t tv_make_set_ready(uint8_t player, uint8_t ready)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_SET_READY;
    in.player_id = player;
    in.data.set_ready.ready = ready;
    return in;
}

static coup_input_t tv_make_start(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_START_GAME;
    return in;
}

static coup_input_t tv_make_action(uint8_t player, uint8_t action, uint8_t target)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_ACTION;
    in.player_id = player;
    in.data.action.action = action;
    in.data.action.target_id = target;
    return in;
}

static coup_input_t tv_make_response(uint8_t player, uint8_t response)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_RESPONSE;
    in.player_id = player;
    in.data.response.response = response;
    return in;
}

static coup_input_t tv_make_block_claim(uint8_t player, uint8_t character)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_BLOCK_CLAIM;
    in.player_id = player;
    in.data.block_claim.character = character;
    return in;
}

/** Set up a started game with N players (player 0 = human, rest = bots) */
static void tv_start_game(coup_rules_t* r, int n, uint32_t seed)
{
    int i;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t in;

    coup_rules_init(r, seed);
    /* Player 0 = human */
    in = tv_make_add_player();
    tv_emit(r, &in, evts, COUP_RULES_MAX_EVENTS);
    in = tv_make_set_ready(0, 1);
    tv_emit(r, &in, evts, COUP_RULES_MAX_EVENTS);
    /* Players 1..n-1 = bots (auto-ready) */
    for (i = 1; i < n; i++) {
        in = tv_make_add_bot();
        tv_emit(r, &in, evts, COUP_RULES_MAX_EVENTS);
    }
    in = tv_make_start();
    tv_emit(r, &in, evts, COUP_RULES_MAX_EVENTS);
}

/** Pass all pending responses */
static void tv_all_pass(coup_rules_t* r)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
    for (i = 0; i < r->player_count; i++) {
        if (r->pending_responses[i]) {
            coup_input_t pass = tv_make_response((uint8_t)i, COUP_RULES_RESP_PASS);
            tv_emit(r, &pass, evts, COUP_RULES_MAX_EVENTS);
        }
    }
}

/* ======================================================================
 * Basic snapshot tests
 * ====================================================================== */

CUI_TEST(table_view_seat_count_matches_player_count)
{
    coup_rules_t r;
    tv_start_game(&r, 4, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ(4, view.seat_count);
}

CUI_TEST(table_view_viewer_id_set)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 1);

    CUI_ASSERT_EQ(1, (int)view.viewer_id);
}

CUI_TEST(table_view_game_active_after_start)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_TRUE(view.game_active);
}

/* ======================================================================
 * Fog of war: card visibility
 * ====================================================================== */

CUI_TEST(table_view_shows_own_cards)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* Viewer's own cards should be visible (not CHAR_NONE) */
    CUI_ASSERT_NEQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[0].cards[0]);
    CUI_ASSERT_NEQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[0].cards[1]);
    /* Should match what's actually in the rules */
    CUI_ASSERT_EQ((int)r.players[0].cards[0], (int)view.seats[0].cards[0]);
    CUI_ASSERT_EQ((int)r.players[0].cards[1], (int)view.seats[0].cards[1]);
}

CUI_TEST(table_view_hides_opponent_unrevealed_cards)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* Opponent's unrevealed cards should be hidden */
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[1].cards[0]);
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[1].cards[1]);
}

CUI_TEST(table_view_shows_opponent_revealed_cards)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    /* Reveal opponent's first card */
    r.players[1].revealed[0] = true;

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* Revealed card should be visible */
    CUI_ASSERT_EQ((int)r.players[1].cards[0], (int)view.seats[1].cards[0]);
    CUI_ASSERT_TRUE(view.seats[1].revealed[0]);
    /* Unrevealed card still hidden */
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[1].cards[1]);
    CUI_ASSERT_FALSE(view.seats[1].revealed[1]);
}

CUI_TEST(table_view_is_self_flag)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 1);

    CUI_ASSERT_FALSE(view.seats[0].is_self);
    CUI_ASSERT_TRUE(view.seats[1].is_self);
    CUI_ASSERT_FALSE(view.seats[2].is_self);
}

/* ======================================================================
 * Coins and alive status
 * ====================================================================== */

CUI_TEST(table_view_coins_match_rules)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    r.players[0].coins = 5;
    r.players[1].coins = 8;
    r.players[2].coins = 0;

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ(5, (int)view.seats[0].coins);
    CUI_ASSERT_EQ(8, (int)view.seats[1].coins);
    CUI_ASSERT_EQ(0, (int)view.seats[2].coins);
}

CUI_TEST(table_view_alive_matches_rules)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    r.players[2].alive = false;

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_TRUE(view.seats[0].alive);
    CUI_ASSERT_TRUE(view.seats[1].alive);
    CUI_ASSERT_FALSE(view.seats[2].alive);
}

/* ======================================================================
 * Phase and turn
 * ====================================================================== */

CUI_TEST(table_view_phase_matches_rules)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ((int)r.phase, (int)view.phase);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)view.phase);
}

CUI_TEST(table_view_current_turn_player)
{
    coup_rules_t r;
    tv_start_game(&r, 4, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ((int)coup_rules_current_player(&r),
                  (int)view.current_turn_player);
}

CUI_TEST(table_view_valid_actions_at_start)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, pid);

    /* At 2 coins: income, foreign aid, tax, steal, exchange available */
    CUI_ASSERT_EQ((int)coup_rules_valid_actions(&r), (int)view.valid_actions);
    CUI_ASSERT_TRUE(view.valid_actions & (1 << COUP_RULES_ACT_INCOME));
    CUI_ASSERT_FALSE(view.valid_actions & (1 << COUP_RULES_ACT_COUP));
}

CUI_TEST(table_view_valid_actions_forced_coup)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 10;

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, pid);

    CUI_ASSERT_EQ((int)(1 << COUP_RULES_ACT_COUP), (int)view.valid_actions);
}

/* ======================================================================
 * Action context
 * ====================================================================== */

CUI_TEST(table_view_action_context_during_challenge)
{
    coup_rules_t r;
    tv_start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Declare steal → opens challenge window */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_STEAL, target);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)view.phase);
    CUI_ASSERT_EQ((int)pid, (int)view.action_actor);
    CUI_ASSERT_EQ((int)COUP_RULES_ACT_STEAL, (int)view.action_type);
    CUI_ASSERT_EQ((int)target, (int)view.action_target);
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_CAPTAIN, (int)view.action_claim);
}

CUI_TEST(table_view_no_action_context_when_idle)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* No action in progress — target should be 0xFF */
    CUI_ASSERT_EQ(0xFF, (int)view.action_target);
}

/* ======================================================================
 * Pending response
 * ====================================================================== */

CUI_TEST(table_view_pending_response_in_challenge_window)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    uint8_t pid = coup_rules_current_player(&r);
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Tax → challenge window opens for all except actor */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Non-actor should be pending */
    uint8_t non_actor = (pid + 1) % 3;
    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, non_actor);
    CUI_ASSERT_TRUE(view.pending_response);

    /* Actor should NOT be pending */
    coup_table_view_from_rules(&view, &r, pid);
    CUI_ASSERT_FALSE(view.pending_response);
}

/* ======================================================================
 * Block context
 * ====================================================================== */

CUI_TEST(table_view_block_context_during_block_window)
{
    coup_rules_t r;
    tv_start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Steal → pass challenge → block window */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_STEAL, target);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    tv_all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, target);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)view.phase);
    /* Blockable by Captain or Ambassador */
    uint8_t expected = (1 << COUP_RULES_CHAR_CAPTAIN) |
                       (1 << COUP_RULES_CHAR_AMBASSADOR);
    CUI_ASSERT_EQ((int)expected, (int)view.blockable_by);
    CUI_ASSERT_TRUE(view.pending_response);
}

CUI_TEST(table_view_blocker_context_during_block_challenge)
{
    coup_rules_t r;
    tv_start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Steal → pass challenge → target blocks with Captain */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_STEAL, target);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    tv_all_pass(&r);

    coup_input_t block = tv_make_response(target, COUP_RULES_RESP_BLOCK);
    tv_emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t claim = tv_make_block_claim(target, COUP_RULES_CHAR_CAPTAIN);
    tv_emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, pid);

    CUI_ASSERT_EQ((int)target, (int)view.blocker_id);
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_CAPTAIN, (int)view.block_char);
}

/* ======================================================================
 * Exchange visibility
 * ====================================================================== */

CUI_TEST(table_view_exchange_visible_to_exchanger)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Exchange → pass challenge → exchange offered */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    tv_all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, pid);

    CUI_ASSERT_EQ(4, view.exchange_count);
    /* Cards should not all be zero/NONE */
    CUI_ASSERT_TRUE(view.exchange_count > 0);
}

CUI_TEST(table_view_exchange_hidden_from_non_exchanger)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t other = (pid + 1) % 2;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Exchange → pass challenge → exchange offered */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    tv_all_pass(&r);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, other);

    /* Non-exchanger should not see exchange cards */
    CUI_ASSERT_EQ(0, view.exchange_count);
}

/* ======================================================================
 * Influence loss context
 * ====================================================================== */

CUI_TEST(table_view_influence_loser_set)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    r.players[pid].coins = 7;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Coup → target must lose influence */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_COUP, target);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ((int)target, (int)view.influence_loser);
}

/* ======================================================================
 * Game metadata
 * ====================================================================== */

CUI_TEST(table_view_round_number)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ(r.round_number, view.round_number);
}

/* ======================================================================
 * Consistency: same rules state → same view regardless of viewer
 * (except fog of war and pending_response)
 * ====================================================================== */

CUI_TEST(table_view_consistent_game_state_across_viewers)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    coup_table_view_t v0, v1;
    coup_table_view_from_rules(&v0, &r, 0);
    coup_table_view_from_rules(&v1, &r, 1);

    /* Phase, turn, action context should be identical */
    CUI_ASSERT_EQ((int)v0.phase, (int)v1.phase);
    CUI_ASSERT_EQ((int)v0.current_turn_player, (int)v1.current_turn_player);
    CUI_ASSERT_EQ((int)v0.action_actor, (int)v1.action_actor);
    CUI_ASSERT_EQ((int)v0.action_type, (int)v1.action_type);
    CUI_ASSERT_EQ((int)v0.game_active, (int)v1.game_active);
    CUI_ASSERT_EQ((int)v0.round_number, (int)v1.round_number);

    /* Coins and alive should be identical */
    CUI_ASSERT_EQ((int)v0.seats[0].coins, (int)v1.seats[0].coins);
    CUI_ASSERT_EQ((int)v0.seats[2].alive, (int)v1.seats[2].alive);

    /* But viewer_id and is_self differ */
    CUI_ASSERT_EQ(0, (int)v0.viewer_id);
    CUI_ASSERT_EQ(1, (int)v1.viewer_id);
    CUI_ASSERT_TRUE(v0.seats[0].is_self);
    CUI_ASSERT_FALSE(v1.seats[0].is_self);
}

/* ======================================================================
 * Adversarial review: lobby phase
 * ====================================================================== */

CUI_TEST(table_view_lobby_phase)
{
    coup_rules_t r;
    coup_rules_init(&r, 42);

    /* Add two players but don't start */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t in = tv_make_add_player();
    tv_emit(&r, &in, evts, COUP_RULES_MAX_EVENTS);
    in = tv_make_add_bot();
    tv_emit(&r, &in, evts, COUP_RULES_MAX_EVENTS);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ((int)COUP_TURN_LOBBY, (int)view.phase);
    CUI_ASSERT_FALSE(view.game_active);
    CUI_ASSERT_EQ(2, view.seat_count);
    CUI_ASSERT_EQ(0xFF, (int)view.winner_id);
}

/* ======================================================================
 * Adversarial review: game over and winner
 * ====================================================================== */

CUI_TEST(table_view_game_over_winner_id)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    /* Eliminate player 1 by revealing both cards */
    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Give actor enough coins for coup */
    r.players[pid].coins = 7;

    /* Coup the target */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_COUP, target);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Target loses first influence */
    coup_input_t lose;
    memset(&lose, 0, sizeof(lose));
    lose.type = COUP_INPUT_LOSE_INFLUENCE;
    lose.player_id = target;
    lose.data.lose_influence.card_idx = 0;
    tv_emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Now target has one card left. Need another coup. Advance turn. */
    /* The turn should have advanced to target (but target is still alive with 1 card).
     * We need to get back to pid's turn and coup again. */
    /* Let target take income (skip if not their turn) */
    if (r.game_active && coup_rules_current_player(&r) == target) {
        coup_input_t income = tv_make_action(target, COUP_RULES_ACT_INCOME, 0xFF);
        tv_emit(&r, &income, evts, COUP_RULES_MAX_EVENTS);
    }

    /* If pid's turn, coup again */
    if (r.game_active && coup_rules_current_player(&r) == pid) {
        r.players[pid].coins = 7;
        coup_input_t act2 = tv_make_action(pid, COUP_RULES_ACT_COUP, target);
        tv_emit(&r, &act2, evts, COUP_RULES_MAX_EVENTS);

        if (r.phase == COUP_TURN_WAITING_FOR_INFLUENCE_LOSS) {
            lose.data.lose_influence.card_idx = 1;
            tv_emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);
        }
    }

    /* Game should be over now */
    CUI_ASSERT_FALSE(r.game_active);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_FALSE(view.game_active);
    CUI_ASSERT_EQ((int)pid, (int)view.winner_id);
}

/* ======================================================================
 * Adversarial review: dead player cards visibility
 * ====================================================================== */

CUI_TEST(table_view_dead_player_both_cards_revealed)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    /* Manually mark player 1 as dead with both cards revealed */
    r.players[1].revealed[0] = true;
    r.players[1].revealed[1] = true;
    r.players[1].alive = false;

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* Dead player's revealed cards should be visible to everyone */
    CUI_ASSERT_TRUE(view.seats[1].revealed[0]);
    CUI_ASSERT_TRUE(view.seats[1].revealed[1]);
    CUI_ASSERT_EQ((int)r.players[1].cards[0], (int)view.seats[1].cards[0]);
    CUI_ASSERT_EQ((int)r.players[1].cards[1], (int)view.seats[1].cards[1]);
    CUI_ASSERT_FALSE(view.seats[1].alive);
}

/* ======================================================================
 * Adversarial review: valid_actions when not viewer's turn
 * ====================================================================== */

CUI_TEST(table_view_valid_actions_not_viewers_turn)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    uint8_t current = coup_rules_current_player(&r);
    uint8_t other = (current + 1) % 3;

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, other);

    /* valid_actions is a global field from coup_rules_valid_actions(),
     * but it's only meaningful when it's the viewer's turn.
     * Verify it reflects the current player's options regardless of viewer. */
    CUI_ASSERT_EQ((int)coup_rules_valid_actions(&r), (int)view.valid_actions);
}

/* ======================================================================
 * Adversarial review: action_claim for non-character actions
 * ====================================================================== */

CUI_TEST(table_view_action_claim_none_for_income)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Income has no character claim */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* After income resolves immediately, we're on next turn.
     * Check that the idle state has no claim. */
    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* action_claim should be 0xFF when action_type has no character claim */
    CUI_ASSERT_EQ(0xFF, (int)view.action_claim);
}

CUI_TEST(table_view_action_claim_duke_for_tax)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Tax claims Duke */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_DUKE, (int)view.action_claim);
}

/* ======================================================================
 * Adversarial review: fog-of-war symmetry
 * ====================================================================== */

CUI_TEST(table_view_fog_of_war_symmetric)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    /* Each viewer should see their own cards but not others' */
    int i;
    for (i = 0; i < 3; i++) {
        coup_table_view_t view;
        coup_table_view_from_rules(&view, &r, (uint8_t)i);

        /* Own cards visible */
        CUI_ASSERT_EQ((int)r.players[i].cards[0], (int)view.seats[i].cards[0]);
        CUI_ASSERT_EQ((int)r.players[i].cards[1], (int)view.seats[i].cards[1]);

        /* Other players' cards hidden */
        int j;
        for (j = 0; j < 3; j++) {
            if (j == i) continue;
            if (!r.players[j].revealed[0]) {
                CUI_ASSERT_EQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[j].cards[0]);
            }
            if (!r.players[j].revealed[1]) {
                CUI_ASSERT_EQ((int)COUP_RULES_CHAR_NONE, (int)view.seats[j].cards[1]);
            }
        }
    }
}

/* ======================================================================
 * Adversarial review: foreign aid block window
 * ====================================================================== */

CUI_TEST(table_view_foreign_aid_block_window)
{
    coup_rules_t r;
    tv_start_game(&r, 3, 42);

    uint8_t pid = coup_rules_current_player(&r);
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Foreign aid → no challenge window (no claim) → block window */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Foreign aid goes straight to block window (Duke can block) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    uint8_t non_actor = (pid + 1) % 3;
    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, non_actor);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)view.phase);
    CUI_ASSERT_TRUE(view.pending_response);
    /* Duke can block foreign aid */
    CUI_ASSERT_TRUE(view.blockable_by & (1 << COUP_RULES_CHAR_DUKE));
}

/* ======================================================================
 * Adversarial review: exchange card content verification
 * ====================================================================== */

CUI_TEST(table_view_exchange_cards_are_valid_characters)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Exchange → pass challenge → exchange offered */
    coup_input_t act = tv_make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    tv_all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, pid);

    CUI_ASSERT_EQ(4, view.exchange_count);

    /* All exchange cards should be valid character IDs (0-4) */
    int i;
    for (i = 0; i < view.exchange_count; i++) {
        CUI_ASSERT_TRUE(view.exchange_cards[i] <= COUP_RULES_CHAR_CONTESSA);
    }
}

/* ======================================================================
 * Adversarial review: strengthen weak tests
 * ====================================================================== */

CUI_TEST(table_view_no_action_context_when_idle_full)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    /* Full check: no action in progress at game start */
    CUI_ASSERT_EQ(0xFF, (int)view.action_target);
    CUI_ASSERT_EQ(0xFF, (int)view.blocker_id);
    CUI_ASSERT_EQ(0xFF, (int)view.influence_loser);
    CUI_ASSERT_EQ(0, view.exchange_count);
    CUI_ASSERT_FALSE(view.pending_response);
}

CUI_TEST(table_view_round_number_advances)
{
    coup_rules_t r;
    tv_start_game(&r, 2, 42);

    coup_table_view_t view;
    coup_table_view_from_rules(&view, &r, 0);

    int initial_round = view.round_number;

    /* Play a full round: both players take income */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    uint8_t p0 = coup_rules_current_player(&r);
    coup_input_t act = tv_make_action(p0, COUP_RULES_ACT_INCOME, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t p1 = coup_rules_current_player(&r);
    act = tv_make_action(p1, COUP_RULES_ACT_INCOME, 0xFF);
    tv_emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_table_view_from_rules(&view, &r, 0);
    CUI_ASSERT_TRUE(view.round_number > initial_round);
}

CUI_TEST(table_view_viewer_id_set_for_each_player)
{
    coup_rules_t r;
    tv_start_game(&r, 4, 42);

    /* Each viewer_id should match what was requested */
    int i;
    for (i = 0; i < 4; i++) {
        coup_table_view_t view;
        coup_table_view_from_rules(&view, &r, (uint8_t)i);
        CUI_ASSERT_EQ(i, (int)view.viewer_id);
        CUI_ASSERT_TRUE(view.seats[i].is_self);
    }
}
