/**
 * test_coup_timeout.c - Tests for Coup rule engine timeout handling
 *
 * Covers COUP_INPUT_TIMEOUT in every phase where it applies:
 *   - CHALLENGE_WINDOW (blockable action -> block window; unblockable -> resolve)
 *   - BLOCK_WINDOW (resolve action)
 *   - BLOCK_CHALLENGE_WINDOW (block stands, action cancelled)
 *   - RESOLVING (blocker never claimed char, action resolves)
 */

#include "cui_test_framework.h"
#include "coup_rules.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Helpers (mirror test_coup_rules.c)
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

static coup_input_t make_block_claim(uint8_t player, uint8_t character)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_BLOCK_CLAIM;
    in.player_id = player;
    in.data.block_claim.character = character;
    return in;
}

static coup_input_t make_timeout(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_TIMEOUT;
    return in;
}

static void add_ready_players(coup_rules_t* r, int players)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
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
}

static void start_game(coup_rules_t* r, int players, uint32_t seed)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(r, seed);
    add_ready_players(r, players);
    coup_input_t start = make_start();
    emit(r, &start, evts, COUP_RULES_MAX_EVENTS);
}

static int find_event(const coup_event_t* evts, int n, coup_event_type_t type)
{
    int i;
    for (i = 0; i < n; i++) {
        if (evts[i].type == type) return i;
    }
    return -1;
}

/* ======================================================================
 * CHALLENGE_WINDOW Timeout - blockable action (Steal)
 * ====================================================================== */

CUI_TEST(timeout_challenge_window_blockable_opens_block_window)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 3, 100);

    /* Player 0's turn — steal from player 1 (blockable) */
    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid == 0) ? 1 : 0;
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    /* Timeout instead of explicit passes */
    coup_input_t timeout = make_timeout();
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    /* Should move to block window (steal is blockable) */
    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
}

/* ======================================================================
 * CHALLENGE_WINDOW Timeout - unblockable action (Tax)
 * ====================================================================== */

CUI_TEST(timeout_challenge_window_unblockable_resolves)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 2, 100);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    /* Tax: character claim (Duke) but not blockable */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    /* Timeout */
    coup_input_t timeout = make_timeout();
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    /* Tax resolves: +3 coins, advance to next turn */
    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)(old_coins + 3), (int)r.players[pid].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * BLOCK_WINDOW Timeout - no one blocks, action resolves
 * ====================================================================== */

CUI_TEST(timeout_block_window_resolves_action)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 3, 100);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid == 0) ? 1 : 0;
    uint8_t target_coins_before = r.players[target].coins;

    /* Steal from target */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge window via timeout */
    coup_input_t timeout = make_timeout();
    emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Now timeout the block window */
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    /* Steal resolves — target loses coins */
    CUI_ASSERT_GT(n, 0);
    uint8_t stolen = (target_coins_before >= 2) ? 2 : target_coins_before;
    CUI_ASSERT_EQ((int)(target_coins_before - stolen), (int)r.players[target].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * BLOCK_CHALLENGE_WINDOW Timeout - block stands, action cancelled
 * ====================================================================== */

CUI_TEST(timeout_block_challenge_window_cancels_action)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 3, 100);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid == 0) ? 1 : 0;
    uint8_t target_coins_before = r.players[target].coins;

    /* Steal from target */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge window */
    coup_input_t timeout = make_timeout();
    emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Target declares block */
    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Target claims Captain for block */
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_CAPTAIN);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);

    /* Timeout the block-challenge window — no one challenges the block */
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    /* Block stands, action cancelled — target keeps their coins */
    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)target_coins_before, (int)r.players[target].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * RESOLVING Timeout - blocker never sent block claim, action resolves
 * ====================================================================== */

CUI_TEST(timeout_resolving_phase_resolves_action)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 3, 100);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid == 0) ? 1 : 0;
    uint8_t target_coins_before = r.players[target].coins;

    /* Steal from target */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge window */
    coup_input_t timeout = make_timeout();
    emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Target says BLOCK but never sends the block claim character */
    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_RESOLVING, (int)r.phase);

    /* Timeout during RESOLVING — blocker didn't claim, action resolves */
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    uint8_t stolen = (target_coins_before >= 2) ? 2 : target_coins_before;
    CUI_ASSERT_EQ((int)(target_coins_before - stolen), (int)r.players[target].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * Timeout in invalid phase returns 0 (no events, no crash)
 * ====================================================================== */

CUI_TEST(timeout_waiting_for_action_is_noop)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 2, 100);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);

    coup_input_t timeout = make_timeout();
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    /* Should produce 0 events and not change phase */
    CUI_ASSERT_EQ(0, n);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * Timeout during Assassinate challenge -> opens block window for Contessa
 * ====================================================================== */

CUI_TEST(timeout_challenge_assassinate_opens_block_window)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 2, 100);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid == 0) ? 1 : 0;

    /* Give actor enough coins for assassinate */
    r.players[pid].coins = 5;

    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    /* Timeout challenge window — assassinate is blockable, so opens block window */
    coup_input_t timeout = make_timeout();
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
}
