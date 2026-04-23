/**
 * test_coup_bot.c - Tests for shared bot AI library
 */

#include "cui_test_framework.h"
#include "coup_rules.h"
#include "coup_bot.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Helpers
 * ====================================================================== */

static int emit_bot(coup_rules_t* r, const coup_input_t* in,
                    coup_event_t* out, int max)
{
    return coup_rules_submit(r, in, out, max);
}

static void add_ready_players_bot(coup_rules_t* r, int players)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
    for (i = 0; i < players; i++) {
        coup_input_t add;
        memset(&add, 0, sizeof(add));
        add.type = COUP_INPUT_ADD_PLAYER;
        emit_bot(r, &add, evts, COUP_RULES_MAX_EVENTS);

        coup_input_t rdy;
        memset(&rdy, 0, sizeof(rdy));
        rdy.type = COUP_INPUT_SET_READY;
        rdy.player_id = (uint8_t)i;
        rdy.data.set_ready.ready = 1;
        emit_bot(r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    }
}

static void start_game_bot(coup_rules_t* r, int players, uint32_t seed)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t start;
    coup_rules_init(r, seed);
    add_ready_players_bot(r, players);
    memset(&start, 0, sizeof(start));
    start.type = COUP_INPUT_START_GAME;
    emit_bot(r, &start, evts, COUP_RULES_MAX_EVENTS);
}

/* Build a minimal table view for action tests */
static void make_action_view(coup_table_view_t* view, uint8_t bot_id,
                              uint8_t coins,
                              int player_count, uint32_t rng)
{
    int i;
    (void)rng;
    memset(view, 0, sizeof(*view));
    view->viewer_id = bot_id;
    view->seat_count = player_count;
    view->phase = COUP_TURN_WAITING_FOR_ACTION;
    view->current_turn_player = bot_id;
    view->action_target = 0xFF;
    view->blocker_id = 0xFF;
    view->influence_loser = 0xFF;
    view->winner_id = 0xFF;
    view->game_active = true;

    for (i = 0; i < player_count; i++) {
        view->seats[i].alive = true;
        view->seats[i].coins = 2;
        view->seats[i].is_self = (i == (int)bot_id);
        view->seats[i].cards[0] = COUP_RULES_CHAR_DUKE;
        view->seats[i].cards[1] = COUP_RULES_CHAR_CAPTAIN;
    }
    view->seats[bot_id].coins = coins;
}

/* ======================================================================
 * Tests: Determinism
 * ====================================================================== */

CUI_TEST(bot_decide_deterministic)
{
    coup_table_view_t view;
    make_action_view(&view, 1, 5, 4, 12345);

    coup_bot_decision_t d1 = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_MEDIUM, 12345);
    coup_bot_decision_t d2 = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_MEDIUM, 12345);

    CUI_ASSERT_TRUE(d1.valid);
    CUI_ASSERT_TRUE(d2.valid);
    CUI_ASSERT_EQ(d1.input.data.action.action, d2.input.data.action.action);
    CUI_ASSERT_EQ(d1.input.data.action.target_id, d2.input.data.action.target_id);
    CUI_ASSERT_EQ(d1.rng_state, d2.rng_state);
}

/* ======================================================================
 * Tests: No action when not bot's turn
 * ====================================================================== */

CUI_TEST(bot_decide_not_my_turn)
{
    coup_table_view_t view;
    make_action_view(&view, 1, 5, 4, 99999);
    view.current_turn_player = 0;  /* someone else's turn */

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 99999);
    CUI_ASSERT_FALSE(d.valid);
}

/* ======================================================================
 * Tests: Forced coup at 10+ coins
 * ====================================================================== */

CUI_TEST(bot_forced_coup_at_10_coins)
{
    int diff;
    for (diff = 0; diff <= 2; diff++) {
        coup_table_view_t view;
        make_action_view(&view, 1, 10, 4, 42 + (uint32_t)diff);

        coup_bot_decision_t d = coup_bot_decide(&view, (uint8_t)diff, 42 + (uint32_t)diff);
        CUI_ASSERT_TRUE(d.valid);
        CUI_ASSERT_EQ(d.input.type, COUP_INPUT_ACTION);
        CUI_ASSERT_EQ(d.input.data.action.action, COUP_RULES_ACT_COUP);
    }
}

/* ======================================================================
 * Tests: Action distribution per difficulty
 * ====================================================================== */

CUI_TEST(bot_easy_action_distribution)
{
    int counts[COUP_RULES_NUM_ACTIONS];
    int i, total = 1000;
    uint32_t rng = 12345;
    memset(counts, 0, sizeof(counts));

    for (i = 0; i < total; i++) {
        coup_table_view_t view;
        make_action_view(&view, 1, 3, 4, rng);

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, rng);
        CUI_ASSERT_TRUE(d.valid);
        rng = d.rng_state;

        CUI_ASSERT_LT(d.input.data.action.action, COUP_RULES_NUM_ACTIONS);
        counts[d.input.data.action.action]++;
    }

    /* Easy bots with 3 coins should never coup (need 7) */
    CUI_ASSERT_EQ(counts[COUP_RULES_ACT_COUP], 0);
    /* Should see income, foreign aid, tax */
    CUI_ASSERT_GT(counts[COUP_RULES_ACT_INCOME], 0);
    CUI_ASSERT_GT(counts[COUP_RULES_ACT_FOREIGN_AID], 0);
    CUI_ASSERT_GT(counts[COUP_RULES_ACT_TAX], 0);
}

CUI_TEST(bot_hard_prefers_aggressive_actions)
{
    int coup_count = 0;
    int i, total = 1000;
    uint32_t rng = 54321;

    for (i = 0; i < total; i++) {
        coup_table_view_t view;
        make_action_view(&view, 1, 7, 4, rng);

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_HARD, rng);
        CUI_ASSERT_TRUE(d.valid);
        rng = d.rng_state;

        if (d.input.data.action.action == COUP_RULES_ACT_COUP)
            coup_count++;
    }

    /* Hard bots with 7 coins should coup ~70% of the time */
    CUI_ASSERT_GT(coup_count, 500);   /* at least 50% to be safe */
}

/* ======================================================================
 * Tests: Target selection
 * ====================================================================== */

CUI_TEST(bot_steal_targets_richest)
{
    /* Track how steal targets are distributed */
    int target_counts[4];
    int i, total = 100;
    uint32_t rng = 77777;
    memset(target_counts, 0, sizeof(target_counts));

    for (i = 0; i < total; i++) {
        coup_table_view_t view;
        make_action_view(&view, 1, 2, 4, rng);
        /* Make player 2 richest */
        view.seats[0].coins = 3;
        view.seats[2].coins = 8;
        view.seats[3].coins = 1;

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_HARD, rng);
        rng = d.rng_state;

        if (d.valid && d.input.data.action.action == COUP_RULES_ACT_STEAL) {
            int tgt = d.input.data.action.target_id;
            if (tgt < 4) target_counts[tgt]++;
        }
    }

    /* When steal is chosen, it should always target the richest (player 2) */
    CUI_ASSERT_EQ(target_counts[0], 0);
    CUI_ASSERT_EQ(target_counts[3], 0);
}

/* ======================================================================
 * Tests: Response decisions
 * ====================================================================== */

CUI_TEST(bot_response_challenge_rate)
{
    int challenge_count = 0;
    int i, total = 1000;
    uint32_t rng = 11111;

    for (i = 0; i < total; i++) {
        coup_table_view_t view;
        memset(&view, 0, sizeof(view));
        view.viewer_id = 2;
        view.seat_count = 4;
        view.phase = COUP_TURN_CHALLENGE_WINDOW;
        view.pending_response = true;
        view.action_target = 0xFF;
        view.blocker_id = 0xFF;
        view.influence_loser = 0xFF;

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_MEDIUM, rng);
        CUI_ASSERT_TRUE(d.valid);
        rng = d.rng_state;

        if (d.input.data.response.response == COUP_RULES_RESP_CHALLENGE)
            challenge_count++;
    }

    /* Should challenge ~25% of the time (1 in 4) */
    CUI_ASSERT_GT(challenge_count, 150);  /* at least 15% */
    CUI_ASSERT_LT(challenge_count, 400);  /* at most 40% */
}

CUI_TEST(bot_response_block_rate)
{
    int block_count = 0;
    int i, total = 1000;
    uint32_t rng = 22222;

    for (i = 0; i < total; i++) {
        coup_table_view_t view;
        memset(&view, 0, sizeof(view));
        view.viewer_id = 2;
        view.seat_count = 4;
        view.phase = COUP_TURN_BLOCK_WINDOW;
        view.pending_response = true;
        view.blockable_by = (1 << COUP_RULES_CHAR_DUKE);
        view.action_target = 0xFF;
        view.blocker_id = 0xFF;
        view.influence_loser = 0xFF;

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_MEDIUM, rng);
        CUI_ASSERT_TRUE(d.valid);
        rng = d.rng_state;

        if (d.input.data.response.response == COUP_RULES_RESP_BLOCK) {
            block_count++;
            /* When blocking, should have a block claim */
            CUI_ASSERT_TRUE(d.has_block_claim);
            CUI_ASSERT_EQ(d.block_claim_char, COUP_RULES_CHAR_DUKE);
        }
    }

    /* Should block ~33% of the time (1 in 3) */
    CUI_ASSERT_GT(block_count, 200);  /* at least 20% */
    CUI_ASSERT_LT(block_count, 500);  /* at most 50% */
}

CUI_TEST(bot_response_no_action_when_not_pending)
{
    coup_table_view_t view;
    memset(&view, 0, sizeof(view));
    view.viewer_id = 2;
    view.seat_count = 4;
    view.phase = COUP_TURN_CHALLENGE_WINDOW;
    view.pending_response = false;
    view.action_target = 0xFF;
    view.blocker_id = 0xFF;
    view.influence_loser = 0xFF;

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 33333);
    CUI_ASSERT_FALSE(d.valid);
}

/* ======================================================================
 * Tests: Influence loss
 * ====================================================================== */

CUI_TEST(bot_influence_loss_valid)
{
    coup_table_view_t view;
    memset(&view, 0, sizeof(view));
    view.viewer_id = 3;
    view.seat_count = 4;
    view.phase = COUP_TURN_WAITING_FOR_INFLUENCE_LOSS;
    view.influence_loser = 3;
    view.action_target = 0xFF;
    view.blocker_id = 0xFF;
    view.seats[3].alive = true;
    view.seats[3].cards[0] = COUP_RULES_CHAR_DUKE;
    view.seats[3].cards[1] = COUP_RULES_CHAR_CAPTAIN;

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 44444);
    CUI_ASSERT_TRUE(d.valid);
    CUI_ASSERT_EQ(d.input.type, COUP_INPUT_LOSE_INFLUENCE);
    CUI_ASSERT_EQ(d.input.player_id, 3);
    CUI_ASSERT_EQ(d.input.data.lose_influence.card_idx, 0);
}

CUI_TEST(bot_influence_loss_not_my_loss)
{
    coup_table_view_t view;
    memset(&view, 0, sizeof(view));
    view.viewer_id = 3;
    view.seat_count = 4;
    view.phase = COUP_TURN_WAITING_FOR_INFLUENCE_LOSS;
    view.influence_loser = 1;  /* someone else loses */
    view.action_target = 0xFF;
    view.blocker_id = 0xFF;

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 55555);
    CUI_ASSERT_FALSE(d.valid);
}

/* ======================================================================
 * Tests: Exchange
 * ====================================================================== */

CUI_TEST(bot_exchange_keeps_first_two)
{
    coup_table_view_t view;
    memset(&view, 0, sizeof(view));
    view.viewer_id = 2;
    view.seat_count = 4;
    view.phase = COUP_TURN_WAITING_FOR_EXCHANGE;
    view.exchange_count = 4;
    view.exchange_cards[0] = COUP_RULES_CHAR_DUKE;
    view.exchange_cards[1] = COUP_RULES_CHAR_CAPTAIN;
    view.exchange_cards[2] = COUP_RULES_CHAR_ASSASSIN;
    view.exchange_cards[3] = COUP_RULES_CHAR_CONTESSA;
    view.action_target = 0xFF;
    view.blocker_id = 0xFF;
    view.influence_loser = 0xFF;

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 66666);
    CUI_ASSERT_TRUE(d.valid);
    CUI_ASSERT_EQ(d.input.type, COUP_INPUT_EXCHANGE_CHOICE);
    CUI_ASSERT_EQ(d.input.data.exchange_choice.keep[0], 0);
    CUI_ASSERT_EQ(d.input.data.exchange_choice.keep[1], 1);
}

/* ======================================================================
 * Tests: Integration — bot decides using table view from rules engine
 * ====================================================================== */

CUI_TEST(bot_view_from_rules_basic)
{
    coup_rules_t rules;
    coup_table_view_t view;
    start_game_bot(&rules, 4, 42);

    coup_table_view_from_rules(&view, &rules, 1);

    CUI_ASSERT_EQ(view.viewer_id, 1);
    CUI_ASSERT_EQ(view.seat_count, 4);
    CUI_ASSERT_TRUE(view.seats[1].is_self);
    CUI_ASSERT_FALSE(view.seats[0].is_self);

    /* Bot should see its own cards */
    CUI_ASSERT_NEQ((int)view.seats[1].cards[0], COUP_RULES_CHAR_NONE);

    /* Bot should NOT see opponents' unrevealed cards */
    CUI_ASSERT_EQ((int)view.seats[0].cards[0], COUP_RULES_CHAR_NONE);
    CUI_ASSERT_EQ((int)view.seats[0].cards[1], COUP_RULES_CHAR_NONE);
}

CUI_TEST(bot_view_from_rules_reveals_shown_cards)
{
    coup_rules_t rules;
    coup_table_view_t view;
    start_game_bot(&rules, 4, 42);

    /* Reveal player 0's first card */
    rules.players[0].revealed[0] = true;

    coup_table_view_from_rules(&view, &rules, 1);

    /* Revealed card should be visible to the bot */
    CUI_ASSERT_NEQ((int)view.seats[0].cards[0], COUP_RULES_CHAR_NONE);
    /* Unrevealed card should still be hidden */
    CUI_ASSERT_EQ((int)view.seats[0].cards[1], COUP_RULES_CHAR_NONE);
}

/* ======================================================================
 * Tests: Integration — bot can play a full turn via rules engine
 * ====================================================================== */

CUI_TEST(bot_plays_action_through_engine)
{
    coup_rules_t rules;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    uint8_t cur;
    coup_table_view_t view;
    coup_bot_decision_t d;
    int n;

    start_game_bot(&rules, 3, 42);

    cur = coup_rules_current_player(&rules);
    CUI_ASSERT_EQ(rules.phase, COUP_TURN_WAITING_FOR_ACTION);

    /* Build view for the current player and get a decision */
    coup_table_view_from_rules(&view, &rules, cur);

    d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_MEDIUM, 42);
    CUI_ASSERT_TRUE(d.valid);
    CUI_ASSERT_EQ(d.input.type, COUP_INPUT_ACTION);
    CUI_ASSERT_EQ(d.input.player_id, cur);

    /* Submit to the engine — should succeed */
    n = emit_bot(&rules, &d.input, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_GT(n, 0);
}

/* ======================================================================
 * Tests: Steal fallback to tax when target has 0 coins
 * ====================================================================== */

CUI_TEST(bot_steal_fallback_when_zero_coins)
{
    int steal_count = 0;
    int i, total = 200;
    uint32_t rng = 88888;

    for (i = 0; i < total; i++) {
        coup_table_view_t view;
        make_action_view(&view, 1, 2, 4, rng);
        /* All opponents have 0 coins */
        view.seats[0].coins = 0;
        view.seats[2].coins = 0;
        view.seats[3].coins = 0;

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_HARD, rng);
        rng = d.rng_state;

        if (d.valid && d.input.data.action.action == COUP_RULES_ACT_STEAL)
            steal_count++;
    }

    /* Should never steal from 0-coin targets */
    CUI_ASSERT_EQ(steal_count, 0);
}

/* ======================================================================
 * Tests: Lobby phase produces no decision
 * ====================================================================== */

CUI_TEST(bot_decide_noop_in_lobby)
{
    coup_table_view_t view;
    memset(&view, 0, sizeof(view));
    view.viewer_id = 1;
    view.phase = COUP_TURN_LOBBY;

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 12345);
    CUI_ASSERT_FALSE(d.valid);
}

/* ======================================================================
 * Tests: Resolving phase produces no decision
 * ====================================================================== */

CUI_TEST(bot_decide_noop_in_resolving)
{
    coup_table_view_t view;
    memset(&view, 0, sizeof(view));
    view.viewer_id = 1;
    view.phase = COUP_TURN_RESOLVING;

    coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, 12345);
    CUI_ASSERT_FALSE(d.valid);
}

/* ======================================================================
 * Tests: Block claim picks from blockable_by mask
 * ====================================================================== */

CUI_TEST(bot_block_claim_uses_mask)
{
    int i;
    uint32_t rng = 55555;
    uint8_t mask = (1 << COUP_RULES_CHAR_CAPTAIN) |
                   (1 << COUP_RULES_CHAR_AMBASSADOR);

    for (i = 0; i < 100; i++) {
        coup_table_view_t view;
        memset(&view, 0, sizeof(view));
        view.viewer_id = 2;
        view.seat_count = 4;
        view.phase = COUP_TURN_BLOCK_WINDOW;
        view.pending_response = true;
        view.blockable_by = mask;
        view.action_target = 0xFF;
        view.blocker_id = 0xFF;
        view.influence_loser = 0xFF;

        coup_bot_decision_t d = coup_bot_decide(&view, COUP_BOT_DIFFICULTY_EASY, rng);
        rng = d.rng_state;

        if (d.valid && d.has_block_claim) {
            /* Block claim should be Captain (first set bit in mask) */
            CUI_ASSERT_EQ(d.block_claim_char, COUP_RULES_CHAR_CAPTAIN);
            return;
        }
    }

    /* If we got here, we never blocked in 100 tries — very unlikely but ok */
}
