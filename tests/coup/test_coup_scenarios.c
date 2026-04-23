/**
 * test_coup_scenarios.c - Full game scenario tests for Coup rule engine
 *
 * Scripts complete game sequences and verifies correct event streams.
 *
 * Coverage matrix:
 *
 * STEAL (S1-S11):
 *   S1  - No challenge, no block, steal resolves
 *   S2  - (covered in test_coup_rules.c) Block Captain unchallenged
 *   S3  - (covered in test_coup_rules.c) Block Ambassador unchallenged
 *   S4  - Pass challenge, block Captain, block-challenged, blocker has card
 *   S5  - Pass challenge, block Ambassador, block-challenged, blocker has card
 *   S6  - Pass challenge, block Captain bluff, block-challenged, steal resolves
 *   S7  - Pass challenge, block Ambassador bluff, block-challenged, steal resolves
 *   S8  - Actor bluffs Captain, challenged, caught, action cancelled
 *   S9  - Actor has Captain, challenged, challenger loses, no block, steal resolves
 *   S10 - Challenge fails, block Ambassador unchallenged, action cancelled
 *   S11 - Challenge fails, block bluff, block-challenged, steal resolves
 *
 * ASSASSINATE (A1-A8, A4b, A4c):
 *   A1  - No challenge, no block, target loses influence
 *   A2  - Block Contessa unchallenged, 3 coins gone
 *   A3  - Pass challenge, block Contessa, block-challenged, blocker has card
 *   A4  - Pass challenge, block Contessa bluff, caught, target loses TWICE
 *   A4b - Same as A4, auto-reveal on last card (no choice prompt)
 *   A4c - Target already lost 1 card, block bluff caught, dead target
 *   A5  - Actor bluffs Assassin, challenged, caught, 3 coins not refunded
 *   A6  - Actor has Assassin, challenged, challenger loses, no block, resolves
 *   A7  - Challenge fails, Contessa blocks, block-challenge fails, cancelled
 *   A8  - Challenge fails, Contessa bluff caught, target loses twice
 */

#include "cui_test_framework.h"
#include "coup_rules.h"

/* Suppress unused-function for helpers */
#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Helpers
 * ====================================================================== */

static int emit(coup_rules_t* r, const coup_input_t* in,
                coup_event_t* out, int max)
{
    return coup_rules_submit(r, in, out, max);
}

static coup_input_t make_start(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_START_GAME;
    return in;
}

static coup_input_t make_action(uint8_t player, uint8_t action, uint8_t target)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_ACTION;
    in.player_id = player;
    in.data.action.action = action;
    in.data.action.target_id = target;
    return in;
}

static coup_input_t make_response(uint8_t player, uint8_t response)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_RESPONSE;
    in.player_id = player;
    in.data.response.response = response;
    return in;
}

static coup_input_t make_lose_influence(uint8_t player, uint8_t card_idx)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_LOSE_INFLUENCE;
    in.player_id = player;
    in.data.lose_influence.card_idx = card_idx;
    return in;
}

static coup_input_t make_exchange_choice(uint8_t player, uint8_t keep0, uint8_t keep1)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_EXCHANGE_CHOICE;
    in.player_id = player;
    in.data.exchange_choice.keep[0] = keep0;
    in.data.exchange_choice.keep[1] = keep1;
    return in;
}

static coup_input_t make_block_claim(uint8_t player, uint8_t character)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_BLOCK_CLAIM;
    in.player_id = player;
    in.data.block_claim.character = character;
    return in;
}

static void start_game(coup_rules_t* r, int players, uint32_t seed)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
    coup_rules_init(r, seed);
    for (i = 0; i < players; i++) {
        coup_input_t add;
        memset(&add, 0, sizeof(add));
        add.type = COUP_INPUT_ADD_PLAYER;
        emit(r, &add, evts, COUP_RULES_MAX_EVENTS);
        coup_input_t rdy;
        memset(&rdy, 0, sizeof(rdy));
        rdy.type = COUP_INPUT_SET_READY;
        rdy.player_id = (uint8_t)i;
        rdy.data.set_ready.ready = 1;
        emit(r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    }
    coup_input_t start = make_start();
    emit(r, &start, evts, COUP_RULES_MAX_EVENTS);
}

static void all_pass(coup_rules_t* r)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
    for (i = 0; i < r->player_count; i++) {
        if (r->pending_responses[i]) {
            coup_input_t pass = make_response((uint8_t)i, COUP_RULES_RESP_PASS);
            emit(r, &pass, evts, COUP_RULES_MAX_EVENTS);
        }
    }
}

static int find_event(const coup_event_t* evts, int n, coup_event_type_t type)
{
    int i;
    for (i = 0; i < n; i++) {
        if (evts[i].type == type) return i;
    }
    return -1;
}

/* Do income for current player */
static void do_income(coup_rules_t* r)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    uint8_t pid = coup_rules_current_player(r);
    coup_input_t act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    emit(r, &act, evts, COUP_RULES_MAX_EVENTS);
}

/**
 * Assert a player's coins, alive status, and revealed cards in one call.
 */
static void assert_player_state(const coup_rules_t* r, int pid,
                                int coins, bool alive,
                                bool rev0, bool rev1)
{
    CUI_ASSERT_EQ(coins, (int)r->players[pid].coins);
    CUI_ASSERT_EQ(alive ? 1 : 0, r->players[pid].alive ? 1 : 0);
    CUI_ASSERT_EQ(rev0 ? 1 : 0, r->players[pid].revealed[0] ? 1 : 0);
    CUI_ASSERT_EQ(rev1 ? 1 : 0, r->players[pid].revealed[1] ? 1 : 0);
}

/* ======================================================================
 * General Scenarios
 * ====================================================================== */

CUI_TEST(scenario_simple_income_game)
{
    /* 2 players take income repeatedly until one can coup.
     * Starting coins: 2 each. Need 7 for coup.
     * 5 incomes each = 7 coins, then coup. */
    coup_rules_t r;
    start_game(&r, 2, 100);

    int round;
    for (round = 0; round < 5; round++) {
        /* Player 0 income */
        do_income(&r);
        /* Player 1 income */
        do_income(&r);
    }

    /* Both should have 7 coins */
    CUI_ASSERT_EQ(7, (int)r.players[0].coins);
    CUI_ASSERT_EQ(7, (int)r.players[1].coins);

    /* Player 0 coups player 1 */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_COUP, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(0, (int)r.players[0].coins);

    /* Player 1 loses influence */
    coup_input_t lose = make_lose_influence(1, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_TRUE(r.players[1].revealed[0]);
    CUI_ASSERT_TRUE(r.players[1].alive); /* still has card 1 */

    /* Player 1's turn — takes income */
    do_income(&r);
    CUI_ASSERT_EQ(8, (int)r.players[1].coins);
}

CUI_TEST(scenario_successful_challenge)
{
    /* Player 0 claims Tax (Duke), player 1 challenges.
     * Player 0 does NOT have Duke -> caught bluffing. */
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    r.players[0].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_FALSE(evts[ci].data.challenge_result.defender_had_card);

    /* Player 0 loses influence */
    coup_input_t lose = make_lose_influence(0, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Action cancelled */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);
    CUI_ASSERT_EQ(COUP_RULES_INITIAL_COINS, (int)r.players[0].coins);

    /* Now player 1's turn */
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

CUI_TEST(scenario_failed_challenge)
{
    /* Player 0 claims Tax (Duke), player 1 challenges.
     * Player 0 HAS Duke -> challenger loses. Tax still resolves. */
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_DUKE;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_TRUE(evts[ci].data.challenge_result.defender_had_card);

    /* Player 1 loses influence */
    coup_input_t lose = make_lose_influence(1, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Tax resolved — player 0 gained 3 coins */
    CUI_ASSERT_EQ(COUP_RULES_INITIAL_COINS + 3, (int)r.players[0].coins);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
}

CUI_TEST(scenario_block_and_block_challenge)
{
    /* Player 0 Foreign Aid -> player 1 blocks (Duke) ->
     * player 0 challenges block -> player 1 has Duke ->
     * player 0 loses influence, action cancelled */
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Foreign Aid */
    coup_input_t act = make_action(0, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Player 1 blocks */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(1, COUP_RULES_CHAR_DUKE);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* Player 0 challenges the block */
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_TRUE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* Player 0 loses influence */
    coup_input_t lose = make_lose_influence(0, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* No coins gained */
    CUI_ASSERT_EQ(COUP_RULES_INITIAL_COINS, (int)r.players[0].coins);
}

CUI_TEST(scenario_two_player_game_to_completion)
{
    /* Full 2-player game played to completion via income + coup. */
    coup_rules_t r;
    start_game(&r, 2, 200);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int turns = 0;
    int max_turns = 100; /* safety limit */

    while (r.game_active && turns < max_turns) {
        uint8_t pid = coup_rules_current_player(&r);
        uint8_t target = (pid + 1) % 2;
        coup_input_t act;

        if (r.players[pid].coins >= COUP_RULES_COUP_COST) {
            /* Coup the opponent */
            act = make_action(pid, COUP_RULES_ACT_COUP, target);
            emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

            if (r.phase == COUP_TURN_WAITING_FOR_INFLUENCE_LOSS) {
                coup_input_t lose = make_lose_influence(target, 0);
                emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);
            }
        } else {
            /* Income */
            act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
            emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
        }
        turns++;
    }

    /* Game should have ended */
    CUI_ASSERT_FALSE(r.game_active);

    /* Exactly one player alive */
    int alive = 0;
    int i;
    for (i = 0; i < 2; i++) {
        if (r.players[i].alive) alive++;
    }
    CUI_ASSERT_EQ(1, alive);
}

CUI_TEST(scenario_exchange_full_flow)
{
    /* Player 0 declares Exchange (Ambassador).
     * No challenge. Gets 4 cards to choose from. Keeps 2.
     * Deck count restored. Turn advances. */
    coup_rules_t r;
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);
    int old_deck = r.deck_count;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);
    CUI_ASSERT_EQ(4, r.exchange_count);

    /* Remember offered cards */
    uint8_t offered[4];
    int i;
    for (i = 0; i < 4; i++) {
        offered[i] = r.exchange_cards[i];
    }

    /* Keep indices 1 and 3 */
    coup_input_t choice = make_exchange_choice(pid, 1, 3);
    int n = emit(&r, &choice, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_EXCHANGE_RESOLVED), 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);

    /* Player's hand should be the kept cards */
    int slot = 0;
    uint8_t hand[2];
    int ci;
    for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
        if (!r.players[pid].revealed[ci]) {
            hand[slot++] = r.players[pid].cards[ci];
        }
    }
    CUI_ASSERT_EQ((int)offered[1], (int)hand[0]);
    CUI_ASSERT_EQ((int)offered[3], (int)hand[1]);

    /* Deck restored (put 2 back, drew 2 earlier) */
    CUI_ASSERT_EQ(old_deck, r.deck_count);

    /* Turn advanced */
    CUI_ASSERT_NEQ((int)pid, (int)coup_rules_current_player(&r));
}

/* ======================================================================
 * STEAL Scenarios (S1-S11)
 *
 * Steal flow: challenge window -> block window -> block-challenge window
 * Actor claims Captain. Target can block with Captain or Ambassador.
 * ====================================================================== */

/* S1: No challenge, no block, steal resolves.
 * Coins transfer: target -2, actor +2. */
CUI_TEST(scenario_steal_pass_challenge_no_block_resolves)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: all pass */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    all_pass(&r);

    /* Block window: target passes */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);
    coup_input_t pass = make_response(1, COUP_RULES_RESP_PASS);
    int n = emit(&r, &pass, evts, COUP_RULES_MAX_EVENTS);

    /* Steal resolves */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS + 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS - 2, true, false, false);

    /* Turn advanced */
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S4: Pass challenge, block Captain, block-challenged, blocker has card.
 * Challenger loses influence, block stands, action cancelled. */
CUI_TEST(scenario_steal_pass_challenge_block_captain_challenged_has_card)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    r.players[1].cards[0] = COUP_RULES_CHAR_CAPTAIN; /* target has Captain */

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: all pass */
    all_pass(&r);

    /* Block window: P1 blocks with Captain */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CAPTAIN);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 has Captain -> block challenge fails */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_TRUE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P0 loses influence */
    CUI_ASSERT_EQ(0, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(0, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Block stands -> action cancelled, no coins transferred */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS, true, true, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S5: Pass challenge, block Ambassador, block-challenged, blocker has card.
 * (Renamed from scenario_steal_blocked_with_ambassador_challenged) */
CUI_TEST(scenario_steal_pass_challenge_block_ambassador_challenged_has_card)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    /* P0: Ambassador (for blocking, but actually P0 is the actor here) */
    /* Wait - this test is: P1 steals from P0, P0 blocks with Ambassador */
    r.players[0].cards[0] = COUP_RULES_CHAR_AMBASSADOR;
    r.players[0].cards[1] = COUP_RULES_CHAR_CONTESSA;
    r.players[1].cards[0] = COUP_RULES_CHAR_CAPTAIN;

    /* Skip P0's turn with income */
    do_income(&r);

    /* P1 steals from P0 */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(1, COUP_RULES_ACT_STEAL, 0);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: P0 passes */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t pass_chal = make_response(0, COUP_RULES_RESP_PASS);
    emit(&r, &pass_chal, evts, COUP_RULES_MAX_EVENTS);

    /* Block window: P0 blocks with Ambassador */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(0, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(0, COUP_RULES_CHAR_AMBASSADOR);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P1 challenges the block */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 HAD Ambassador -> block challenge fails */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_TRUE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses influence */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(1, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Block stands -> action cancelled, no coins stolen */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS + 1, true, false, false); /* +1 from income */
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, true, false);
    CUI_ASSERT_EQ(0, (int)coup_rules_current_player(&r));
}

/* S6: Pass challenge, block Captain bluff, block-challenged, steal resolves.
 * P0 steals from P1. P1 blocks Captain (bluff). P0 challenges. P1 loses. */
CUI_TEST(scenario_steal_pass_challenge_block_captain_challenged_bluff)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    /* P1 does NOT have Captain */
    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[1].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: all pass */
    all_pass(&r);

    /* Block window: P1 blocks with Captain (bluff) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CAPTAIN);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 does NOT have Captain -> block bluff caught */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_FALSE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses influence for the bluff */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(1, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Steal resolves: P0 +2, P1 -2 */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS + 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS - 2, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S7: Pass challenge, block Ambassador bluff, block-challenged, steal resolves.
 * Same as S6 but blocker claims Ambassador instead of Captain. */
CUI_TEST(scenario_steal_pass_challenge_block_ambassador_challenged_bluff)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    /* P1 does NOT have Ambassador */
    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[1].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: all pass */
    all_pass(&r);

    /* Block window: P1 blocks with Ambassador (bluff) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_AMBASSADOR);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 does NOT have Ambassador -> block bluff caught */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_FALSE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses influence for the bluff */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(1, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Steal resolves: P0 +2, P1 -2 */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS + 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS - 2, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S8: Actor bluffs Captain, challenged by target, caught.
 * Action cancelled, actor loses influence. */
CUI_TEST(scenario_steal_challenge_succeeds_actor_bluffing)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    /* P0 does NOT have Captain */
    r.players[0].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[0].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: P1 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 was bluffing -> challenge succeeds */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_FALSE(evts[ci].data.challenge_result.defender_had_card);

    /* P0 loses influence */
    CUI_ASSERT_EQ(0, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(0, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Action cancelled */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);

    /* No coins transferred */
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS, true, true, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S9: Actor has Captain, challenged, challenger loses, no block, steal resolves.
 * After failed challenge, block window opens. Target passes. */
CUI_TEST(scenario_steal_challenge_fails_no_block_resolves)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge window: P1 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 has Captain -> challenge fails, P1 loses influence */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_TRUE(evts[ci].data.challenge_result.defender_had_card);

    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(1, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Block window opens for target (P1 still alive with 1 card) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);

    /* P1 passes block */
    coup_input_t pass = make_response(1, COUP_RULES_RESP_PASS);
    n = emit(&r, &pass, evts, COUP_RULES_MAX_EVENTS);

    /* Steal resolves */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS + 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS - 2, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S10: Challenge fails, block Ambassador unchallenged, action cancelled.
 * (Renamed from scenario_steal_challenged_then_blocked)
 * 4 players. P0 steals from P1. P2 challenges -> P0 has Captain ->
 * P2 loses influence. Block window -> P1 blocks with Ambassador ->
 * unchallenged -> action cancelled. */
CUI_TEST(scenario_steal_challenge_fails_block_ambassador_unchallenged)
{
    coup_rules_t r;
    start_game(&r, 4, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P2 challenges */
    coup_input_t chal = make_response(2, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 had Captain -> P2 loses influence */
    coup_input_t lose = make_lose_influence(2, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Block window opens for target (P1) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);

    /* P1 blocks with Ambassador */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_AMBASSADOR);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Nobody challenges the block */
    all_pass(&r);

    /* No coins transferred */
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);
    assert_player_state(&r, 2, COUP_RULES_INITIAL_COINS, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* S11: Challenge fails, block bluff, block-challenged, steal resolves.
 * 3 players. P0 steals from P1 (has Captain). P2 challenges ->
 * P0 has Captain -> P2 loses influence. Block window -> P1 blocks
 * with Captain (bluff). P0 challenges block -> P1 caught -> steal resolves. */
CUI_TEST(scenario_steal_challenge_fails_block_challenged_bluff)
{
    coup_rules_t r;
    start_game(&r, 3, 100);

    r.players[0].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    /* P1 does NOT have Captain or Ambassador */
    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[1].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_STEAL, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P2 challenges the steal */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(2, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 has Captain -> challenge fails */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_TRUE(evts[ci].data.challenge_result.defender_had_card);

    /* P2 loses influence */
    CUI_ASSERT_EQ(2, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(2, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Block window opens for P1 */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);

    /* P1 blocks with Captain (bluff) */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CAPTAIN);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges the block */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t bchal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    n = emit(&r, &bchal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 doesn't have Captain -> block bluff caught */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_FALSE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses influence for block bluff */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose2 = make_lose_influence(1, 0);
    n = emit(&r, &lose2, evts, COUP_RULES_MAX_EVENTS);

    /* Steal resolves: P0 +2, P1 -2 */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
    assert_player_state(&r, 0, COUP_RULES_INITIAL_COINS + 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS - 2, true, true, false);
    assert_player_state(&r, 2, COUP_RULES_INITIAL_COINS, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* ======================================================================
 * ASSASSINATE Scenarios (A1-A8, A4b, A4c)
 *
 * Assassinate flow: 3 coins deducted upfront -> challenge window ->
 *   block window (Contessa only) -> block-challenge window
 * Actor claims Assassin. Target can block with Contessa.
 * Coins are NEVER refunded, even if action is cancelled.
 * ====================================================================== */

/* A1: No challenge, no block, target loses influence.
 * 3 coins deducted, target picks card to lose. */
CUI_TEST(scenario_assassinate_pass_challenge_no_block)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* 3 coins deducted immediately */
    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Challenge window: all pass */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    all_pass(&r);

    /* Block window: P1 passes */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);
    coup_input_t pass = make_response(1, COUP_RULES_RESP_PASS);
    emit(&r, &pass, evts, COUP_RULES_MAX_EVENTS);

    /* Waiting for P1 to lose influence */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ(1, (int)r.influence_loser);

    coup_input_t lose = make_lose_influence(1, 0);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Action resolved, P1 lost a card */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
    assert_player_state(&r, 0, 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* A2: Block Contessa unchallenged. 3 coins gone, target keeps both cards.
 * (Renamed from scenario_assassinate_blocked_by_contessa) */
CUI_TEST(scenario_assassinate_pass_challenge_block_contessa_unchallenged)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].coins = 5;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Pass challenge */
    all_pass(&r);

    /* P1 blocks with Contessa */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* P0 doesn't challenge the block */
    all_pass(&r);

    /* P1 alive with both cards, 3 coins gone from P0 */
    assert_player_state(&r, 0, 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);

    /* Turn advanced to P1 */
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* A3: Pass challenge, block Contessa, block-challenged, blocker has card.
 * 3 players. P0 assassinates P1. P2 challenges -> P0 has Assassin ->
 * P2 loses. Block window -> P1 blocks Contessa. P0 challenges block ->
 * P1 has Contessa -> P0 loses influence. Block stands, action cancelled. */
CUI_TEST(scenario_assassinate_pass_challenge_block_contessa_challenged_has_card)
{
    coup_rules_t r;
    start_game(&r, 3, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;
    r.players[1].cards[0] = COUP_RULES_CHAR_CONTESSA; /* target has Contessa */

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Challenge window: all pass */
    all_pass(&r);

    /* Block window: P1 blocks with Contessa */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges the block */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 HAS Contessa -> block challenge fails */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_TRUE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P0 loses influence (challenger penalty) */
    CUI_ASSERT_EQ(0, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(0, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Block stands -> action cancelled. 3 coins still gone. */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);
    assert_player_state(&r, 0, 2, true, true, false); /* lost 3 coins + 1 card */
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* A4: Pass challenge, block Contessa bluff, caught, target loses TWICE.
 * P0 assassinates P1. P1 blocks Contessa (bluff). P0 challenges block.
 * P1 caught -> loses influence #1. Assassinate resolves -> P1 loses
 * influence #2 -> eliminated (in 2-player game). */
CUI_TEST(scenario_assassinate_pass_challenge_block_contessa_challenged_bluff)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;
    /* P1 does NOT have Contessa */
    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[1].cards[1] = COUP_RULES_CHAR_CAPTAIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Challenge window: all pass */
    all_pass(&r);

    /* Block window: P1 blocks with Contessa (bluff) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 does NOT have Contessa -> block bluff caught */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_FALSE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses influence #1 (block bluff penalty) */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose1 = make_lose_influence(1, 0);
    n = emit(&r, &lose1, evts, COUP_RULES_MAX_EVENTS);

    /* Assassinate resolves -> P1 must lose influence #2 */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ(1, (int)r.influence_loser);

    coup_input_t lose2 = make_lose_influence(1, 1);
    n = emit(&r, &lose2, evts, COUP_RULES_MAX_EVENTS);

    /* P1 eliminated, game over in 2-player */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_GAME_OVER), 0);
    assert_player_state(&r, 0, 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, false, true, true);
    CUI_ASSERT_FALSE(r.game_active);
}

/* A4b: Same as A4, but verifies auto-reveal on last card.
 * After P1 loses card 0 from bluff, only card 1 remains.
 * Engine auto-reveals card 1 regardless of chosen index. */
CUI_TEST(scenario_assassinate_block_bluff_target_has_two_cards_auto_reveals_last)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;
    /* P1 does NOT have Contessa */
    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[1].cards[1] = COUP_RULES_CHAR_CAPTAIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    /* P1 blocks with Contessa (bluff) */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* P0 challenges block */
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 loses card 0 from bluff */
    coup_input_t lose1 = make_lose_influence(1, 0);
    emit(&r, &lose1, evts, COUP_RULES_MAX_EVENTS);

    /* Assassinate resolves -> P1 loses influence again.
     * P1 now has only card 1 unrevealed. Send card_idx=0 (already revealed)
     * to verify auto-pick selects card 1. */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    coup_input_t lose2 = make_lose_influence(1, 0); /* deliberately wrong index */
    int n = emit(&r, &lose2, evts, COUP_RULES_MAX_EVENTS);

    /* Engine should auto-reveal card 1 (the only unrevealed card) */
    int ili = find_event(evts, n, COUP_EVT_INFLUENCE_LOST);
    CUI_ASSERT_GE(ili, 0);
    CUI_ASSERT_EQ(1, (int)evts[ili].data.influence_lost.card_idx);
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_CAPTAIN,
                   (int)evts[ili].data.influence_lost.revealed_char);

    /* P1 eliminated */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, false, true, true);
    CUI_ASSERT_FALSE(r.game_active);
}

/* A4c: Target already lost 1 card, blocks assassinate with Contessa bluff,
 * caught -> loses LAST card -> eliminated from bluff.
 * Assassinate resolves but target is dead.
 * 3-player game so game doesn't end on P1's elimination.
 * Tests that engine handles gracefully (no stuck state). */
CUI_TEST(scenario_assassinate_block_bluff_target_has_one_card_already_dead)
{
    coup_rules_t r;
    start_game(&r, 3, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;
    /* P1 already lost 1 card */
    r.players[1].revealed[0] = true;
    /* P1's remaining card is NOT Contessa */
    r.players[1].cards[1] = COUP_RULES_CHAR_DUKE;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Challenge window: all pass */
    all_pass(&r);

    /* Block window: P1 blocks with Contessa (bluff) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* Block-challenge window: P0 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 doesn't have Contessa -> caught */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_FALSE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses last card -> eliminated */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(1, 1);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* P1 should be eliminated */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
    CUI_ASSERT_FALSE(r.players[1].alive);

    /* Engine should NOT get stuck waiting for dead player.
     * After block-challenge resolves with after_challenge_result=1,
     * resolve_action is called. With the dead-target guard, it should
     * skip influence loss and emit ACTION_RESOLVED directly. */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);

    /* Game still active (P2 alive) */
    CUI_ASSERT_TRUE(r.game_active);
    assert_player_state(&r, 0, 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, false, true, true);

    /* Turn should advance past dead P1 */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* A5: Actor bluffs Assassin, challenged by target, caught.
 * 3 coins already deducted, NOT refunded. Actor loses influence. */
CUI_TEST(scenario_assassinate_challenge_succeeds_actor_bluffing)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].coins = 5;
    /* P0 does NOT have Assassin */
    r.players[0].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[0].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* 3 coins deducted immediately */
    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Challenge window: P1 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 was bluffing -> challenge succeeds */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_FALSE(evts[ci].data.challenge_result.defender_had_card);

    /* P0 loses influence */
    CUI_ASSERT_EQ(0, (int)r.influence_loser);
    coup_input_t lose = make_lose_influence(0, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Action cancelled. 3 coins NOT refunded. */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);
    assert_player_state(&r, 0, 2, true, true, false); /* 5 - 3 = 2, still alive */
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* A6: Actor has Assassin, challenged, challenger loses, no block, resolves.
 * P0 assassinates P1. P1 challenges -> P0 has Assassin -> P1 loses influence.
 * Block window -> P1 passes -> assassinate resolves -> P1 loses again. */
CUI_TEST(scenario_assassinate_challenge_fails_no_block)
{
    coup_rules_t r;
    start_game(&r, 2, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* Challenge window: P1 challenges */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(1, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 has Assassin -> challenge fails */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_TRUE(evts[ci].data.challenge_result.defender_had_card);

    /* P1 loses influence #1 (challenge penalty) */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose1 = make_lose_influence(1, 0);
    emit(&r, &lose1, evts, COUP_RULES_MAX_EVENTS);

    /* Block window opens for P1 (still alive with 1 card) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);

    /* P1 passes block */
    coup_input_t pass = make_response(1, COUP_RULES_RESP_PASS);
    emit(&r, &pass, evts, COUP_RULES_MAX_EVENTS);

    /* Assassinate resolves -> P1 loses influence #2 */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ(1, (int)r.influence_loser);

    coup_input_t lose2 = make_lose_influence(1, 1);
    n = emit(&r, &lose2, evts, COUP_RULES_MAX_EVENTS);

    /* P1 eliminated, game over */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_GAME_OVER), 0);
    assert_player_state(&r, 0, 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, false, true, true);
    CUI_ASSERT_FALSE(r.game_active);
}

/* A7: Challenge fails, Contessa blocks, block-challenge fails, cancelled.
 * 3 players. P0 assassinates P1 (has Assassin). P2 challenges -> P0 has
 * Assassin -> P2 loses. Block window -> P1 blocks Contessa (has it).
 * P0 challenges block -> P1 has Contessa -> P0 loses. Block stands. */
CUI_TEST(scenario_assassinate_challenge_fails_block_contessa_challenged_has_card)
{
    coup_rules_t r;
    start_game(&r, 3, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;
    r.players[1].cards[0] = COUP_RULES_CHAR_CONTESSA; /* target has Contessa */

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* P2 challenges the assassinate */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(2, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 has Assassin -> challenge fails */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_TRUE(evts[ci].data.challenge_result.defender_had_card);

    /* P2 loses influence */
    CUI_ASSERT_EQ(2, (int)r.influence_loser);
    coup_input_t lose1 = make_lose_influence(2, 0);
    emit(&r, &lose1, evts, COUP_RULES_MAX_EVENTS);

    /* Block window opens for P1 */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[1]);

    /* P1 blocks with Contessa (has it) */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* P0 challenges the block */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t bchal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    n = emit(&r, &bchal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 HAS Contessa -> block challenge fails */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_TRUE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P0 loses influence (challenger penalty) */
    CUI_ASSERT_EQ(0, (int)r.influence_loser);
    coup_input_t lose2 = make_lose_influence(0, 0);
    n = emit(&r, &lose2, evts, COUP_RULES_MAX_EVENTS);

    /* Block stands -> action cancelled */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);
    assert_player_state(&r, 0, 2, true, true, false); /* 3 coins gone + 1 card */
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, true, false, false);
    assert_player_state(&r, 2, COUP_RULES_INITIAL_COINS, true, true, false);
    CUI_ASSERT_EQ(1, (int)coup_rules_current_player(&r));
}

/* A8: Challenge fails, Contessa bluff caught, target loses twice.
 * 3 players. P0 assassinates P1 (has Assassin). P2 challenges -> P0 has
 * Assassin -> P2 loses. Block window -> P1 blocks Contessa (bluff).
 * P0 challenges block -> P1 caught -> P1 loses influence.
 * Assassinate resolves -> P1 loses again -> eliminated. */
CUI_TEST(scenario_assassinate_challenge_fails_block_contessa_challenged_bluff)
{
    coup_rules_t r;
    start_game(&r, 3, 100);

    r.players[0].coins = 5;
    r.players[0].cards[0] = COUP_RULES_CHAR_ASSASSIN;
    /* P1 does NOT have Contessa */
    r.players[1].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[1].cards[1] = COUP_RULES_CHAR_CAPTAIN;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(0, COUP_RULES_ACT_ASSASSINATE, 1);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, (int)r.players[0].coins);

    /* P2 challenges the assassinate */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t chal = make_response(2, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P0 has Assassin -> challenge fails */
    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_TRUE(evts[ci].data.challenge_result.defender_had_card);

    /* P2 loses influence */
    CUI_ASSERT_EQ(2, (int)r.influence_loser);
    coup_input_t lose1 = make_lose_influence(2, 0);
    emit(&r, &lose1, evts, COUP_RULES_MAX_EVENTS);

    /* Block window opens for P1 */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* P1 blocks with Contessa (bluff) */
    coup_input_t block = make_response(1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t bclaim = make_block_claim(1, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &bclaim, evts, COUP_RULES_MAX_EVENTS);

    /* P0 challenges the block */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
    coup_input_t bchal = make_response(0, COUP_RULES_RESP_CHALLENGE);
    n = emit(&r, &bchal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 does NOT have Contessa -> block bluff caught */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_FALSE(evts[bci].data.block_challenge_result.blocker_had_card);

    /* P1 loses influence #1 (block bluff penalty) */
    CUI_ASSERT_EQ(1, (int)r.influence_loser);
    coup_input_t lose2 = make_lose_influence(1, 0);
    n = emit(&r, &lose2, evts, COUP_RULES_MAX_EVENTS);

    /* Assassinate resolves -> P1 loses influence #2 */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ(1, (int)r.influence_loser);

    coup_input_t lose3 = make_lose_influence(1, 1);
    n = emit(&r, &lose3, evts, COUP_RULES_MAX_EVENTS);

    /* P1 eliminated */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
    assert_player_state(&r, 0, 2, true, false, false);
    assert_player_state(&r, 1, COUP_RULES_INITIAL_COINS, false, true, true);
    assert_player_state(&r, 2, COUP_RULES_INITIAL_COINS, true, true, false);

    /* Game still active (P0 and P2 alive) */
    CUI_ASSERT_TRUE(r.game_active);
}
