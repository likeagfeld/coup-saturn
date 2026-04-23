/**
 * test_coup_edge_cases.c - Edge case and adversarial tests for Coup rule engine
 *
 * Tests for bugs found during adversarial review:
 * - Post-game-over input rejection
 * - Exchange with 1 unrevealed card (card-loss bug)
 * - Invalid block claim character validation
 * - Player count boundary conditions
 * - Concurrent engine instances (static globals removed)
 * - Dead player exclusion from windows
 * - Assassinate → elimination → game over
 * - Actor challenges block
 * - Second block attempt rejected
 * - Block claim from wrong player
 * - Steal from 0-coin player
 * - Seed 0 fallback
 * - Target dies during challenge -> block window skipped
 */

#include "cui_test_framework.h"
#include "coup_rules.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Helpers (shared with other test files)
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

static coup_input_t make_timeout(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_TIMEOUT;
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

static void do_income(coup_rules_t* r)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    uint8_t pid = coup_rules_current_player(r);
    coup_input_t act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    emit(r, &act, evts, COUP_RULES_MAX_EVENTS);
}

/* ======================================================================
 * CRITICAL: Post-game-over input rejection
 * ====================================================================== */

CUI_TEST(post_game_over_rejects_all_inputs)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;
    uint8_t target = (pid + 1) % 2;

    /* Target has one card revealed — coup will eliminate them */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t lose = make_lose_influence(target, 1);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Game should be over */
    CUI_ASSERT_FALSE(r.game_active);

    /* All subsequent inputs should be rejected */
    coup_input_t income = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    CUI_ASSERT_EQ(-1, emit(&r, &income, evts, COUP_RULES_MAX_EVENTS));

    coup_input_t start = make_start();
    CUI_ASSERT_EQ(-1, emit(&r, &start, evts, COUP_RULES_MAX_EVENTS));

    coup_input_t resp = make_response(pid, COUP_RULES_RESP_PASS);
    CUI_ASSERT_EQ(-1, emit(&r, &resp, evts, COUP_RULES_MAX_EVENTS));

    coup_input_t timeout = make_timeout();
    CUI_ASSERT_EQ(-1, emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * CRITICAL: Exchange with 1 unrevealed card
 * ====================================================================== */

CUI_TEST(exchange_with_one_unrevealed_card)
{
    /* Player has lost 1 influence. Exchange should offer 3 cards
     * (1 hand + 2 drawn), player keeps 1, returns 2 to deck. */
    coup_rules_t r;
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);

    /* Reveal one card to simulate lost influence */
    r.players[pid].revealed[0] = true;

    int old_deck = r.deck_count;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);
    /* Should have 3 cards: 1 unrevealed hand + 2 drawn */
    CUI_ASSERT_EQ(3, r.exchange_count);

    /* Deck should have lost 2 cards */
    CUI_ASSERT_EQ(old_deck - 2, r.deck_count);

    /* Keep index 1 (keep0 is the only one used for 1-slot) */
    uint8_t kept_card = r.exchange_cards[1];
    coup_input_t choice = make_exchange_choice(pid, 1, 0);
    int n = emit(&r, &choice, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_EXCHANGE_RESOLVED), 0);

    /* Player's unrevealed card should be the kept card */
    CUI_ASSERT_EQ((int)kept_card, (int)r.players[pid].cards[1]);
    /* Card 0 should still be revealed */
    CUI_ASSERT_TRUE(r.players[pid].revealed[0]);

    /* Deck should have gained 2 back (1 hand + 2 drawn - 1 kept = 2 returned) */
    CUI_ASSERT_EQ(old_deck, r.deck_count);

    /* Total cards in game should still be 15 */
    {
        int total = r.deck_count;
        int pi;
        for (pi = 0; pi < r.player_count; pi++) {
            total += COUP_RULES_CARDS_PER_PLAYER; /* all cards count, revealed or not */
        }
        CUI_ASSERT_EQ(COUP_RULES_DECK_SIZE, total);
    }
}

/* ======================================================================
 * CRITICAL: Block claim character validation
 * ====================================================================== */

CUI_TEST(block_claim_invalid_character_for_action_rejected)
{
    /* Blocking Steal with Duke should be rejected (only Captain/Ambassador) */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    /* Target blocks */
    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Claim Duke — invalid for blocking Steal */
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_DUKE);
    int n = emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);

    /* Should still be in RESOLVING, waiting for valid claim */
    CUI_ASSERT_EQ((int)COUP_TURN_RESOLVING, (int)r.phase);
}

CUI_TEST(block_claim_out_of_range_character_rejected)
{
    /* Blocking with character=255 should be rejected */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t blocker = (pid + 1) % 4;
    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Claim character 255 — out of range */
    coup_input_t claim = make_block_claim(blocker, 255);
    int n = emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

CUI_TEST(block_claim_contessa_on_foreign_aid_rejected)
{
    /* Contessa cannot block Foreign Aid (only Duke can) */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t blocker = (pid + 1) % 4;
    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t claim = make_block_claim(blocker, COUP_RULES_CHAR_CONTESSA);
    int n = emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

CUI_TEST(block_claim_valid_characters_accepted)
{
    /* Captain can block Steal — should be accepted */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    all_pass(&r);

    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Captain is valid for blocking Steal */
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_CAPTAIN);
    int n = emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);
}

/* ======================================================================
 * CRITICAL: Player count boundaries
 * ====================================================================== */

CUI_TEST(player_count_capped_at_max)
{
    /* Adding more than MAX_PLAYERS should be rejected */
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
    coup_rules_init(&r, 42);
    for (i = 0; i < COUP_RULES_MAX_PLAYERS; i++) {
        coup_input_t add;
        memset(&add, 0, sizeof(add));
        add.type = COUP_INPUT_ADD_PLAYER;
        int n = emit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
        CUI_ASSERT_GT(n, 0);
    }
    CUI_ASSERT_EQ(COUP_RULES_MAX_PLAYERS, r.player_count);

    /* 8th add should fail */
    coup_input_t add;
    memset(&add, 0, sizeof(add));
    add.type = COUP_INPUT_ADD_PLAYER;
    CUI_ASSERT_EQ(-1, emit(&r, &add, evts, COUP_RULES_MAX_EVENTS));
}

CUI_TEST(start_requires_two_ready)
{
    /* 0 or 1 ready players can't start */
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* 0 players */
    coup_input_t start = make_start();
    CUI_ASSERT_EQ(-1, emit(&r, &start, evts, COUP_RULES_MAX_EVENTS));

    /* 1 player, not ready */
    coup_input_t add;
    memset(&add, 0, sizeof(add));
    add.type = COUP_INPUT_ADD_PLAYER;
    emit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, emit(&r, &start, evts, COUP_RULES_MAX_EVENTS));

    /* 1 player ready — still only 1 */
    coup_input_t rdy;
    memset(&rdy, 0, sizeof(rdy));
    rdy.type = COUP_INPUT_SET_READY;
    rdy.player_id = 0;
    rdy.data.set_ready.ready = 1;
    emit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, emit(&r, &start, evts, COUP_RULES_MAX_EVENTS));
}

CUI_TEST(seven_player_game_starts_correctly)
{
    coup_rules_t r;
    start_game(&r, 7, 42);

    CUI_ASSERT_EQ(7, r.player_count);
    CUI_ASSERT_EQ(COUP_RULES_DECK_SIZE - 14, r.deck_count); /* 15 - 14 = 1 */
    CUI_ASSERT_TRUE(r.game_active);

    /* All 7 players alive */
    int i;
    for (i = 0; i < 7; i++) {
        CUI_ASSERT_TRUE(r.players[i].alive);
    }
}

/* ======================================================================
 * CRITICAL: Concurrent engine instances (no static globals)
 * ====================================================================== */

CUI_TEST(two_engine_instances_independent)
{
    coup_rules_t r1, r2;
    start_game(&r1, 2, 100);
    start_game(&r2, 4, 200);

    /* Submit income to r1 */
    coup_event_t evts1[COUP_RULES_MAX_EVENTS];
    uint8_t p1 = coup_rules_current_player(&r1);
    coup_input_t act1 = make_action(p1, COUP_RULES_ACT_INCOME, 0xFF);
    int n1 = emit(&r1, &act1, evts1, COUP_RULES_MAX_EVENTS);

    /* Submit income to r2 */
    coup_event_t evts2[COUP_RULES_MAX_EVENTS];
    uint8_t p2 = coup_rules_current_player(&r2);
    coup_input_t act2 = make_action(p2, COUP_RULES_ACT_INCOME, 0xFF);
    int n2 = emit(&r2, &act2, evts2, COUP_RULES_MAX_EVENTS);

    /* Both should succeed independently */
    CUI_ASSERT_GT(n1, 0);
    CUI_ASSERT_GT(n2, 0);

    /* r1 events should reference r1's player */
    int ci1 = find_event(evts1, n1, COUP_EVT_COINS_CHANGED);
    CUI_ASSERT_GE(ci1, 0);
    CUI_ASSERT_EQ((int)p1, (int)evts1[ci1].data.coins_changed.player_id);

    /* r2 events should reference r2's player */
    int ci2 = find_event(evts2, n2, COUP_EVT_COINS_CHANGED);
    CUI_ASSERT_GE(ci2, 0);
    CUI_ASSERT_EQ((int)p2, (int)evts2[ci2].data.coins_changed.player_id);

    /* States should be independent */
    CUI_ASSERT_EQ(2, r1.player_count);
    CUI_ASSERT_EQ(4, r2.player_count);
}

/* ======================================================================
 * HIGH: Assassinate → elimination → game over
 * ====================================================================== */

CUI_TEST(assassinate_eliminates_target_game_over)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    r.players[pid].coins = 3;

    /* Target has only 1 card left */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);
    /* Pass block */
    all_pass(&r);

    /* Target should be asked to lose influence */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);

    /* Target loses last card */
    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Should be eliminated and game over */
    CUI_ASSERT_FALSE(r.players[target].alive);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_GAME_OVER), 0);
    CUI_ASSERT_FALSE(r.game_active);

    int gi = find_event(evts, n, COUP_EVT_GAME_OVER);
    CUI_ASSERT_EQ((int)pid, (int)evts[gi].data.game_over.winner_id);
}

/* ======================================================================
 * HIGH: Actor challenges a block
 * ====================================================================== */

CUI_TEST(actor_can_challenge_block)
{
    /* 2-player: Player 0 Foreign Aid, Player 1 blocks with Duke.
     * Player 0 challenges the block. Player 1 doesn't have Duke.
     * Player 1 loses influence, Foreign Aid resolves. */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    /* Force target NOT to have Duke */
    r.players[target].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    r.players[target].cards[1] = COUP_RULES_CHAR_CONTESSA;

    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Target blocks with Duke */
    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_DUKE);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* Actor should be in pending (can challenge the block) */
    CUI_ASSERT_TRUE(r.pending_responses[pid]);

    /* Actor challenges the block */
    coup_input_t chal = make_response(pid, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Blocker was bluffing */
    int bri = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bri, 0);
    CUI_ASSERT_FALSE(evts[bri].data.block_challenge_result.blocker_had_card);

    /* Blocker loses influence */
    CUI_ASSERT_EQ((int)target, (int)r.influence_loser);

    coup_input_t lose = make_lose_influence(target, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Foreign Aid resolves — actor gained 2 coins */
    CUI_ASSERT_EQ((int)(old_coins + 2), (int)r.players[pid].coins);
}

/* ======================================================================
 * HIGH: Second block attempt rejected
 * ====================================================================== */

CUI_TEST(second_block_attempt_rejected)
{
    /* After one player blocks Foreign Aid, the phase moves to RESOLVING.
     * A second block attempt should be rejected. */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Player 1 blocks */
    uint8_t blocker1 = (pid + 1) % 4;
    coup_input_t block = make_response(blocker1, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Phase is now RESOLVING — player 2's block should be rejected */
    CUI_ASSERT_EQ((int)COUP_TURN_RESOLVING, (int)r.phase);

    uint8_t blocker2 = (pid + 2) % 4;
    coup_input_t block2 = make_response(blocker2, COUP_RULES_RESP_BLOCK);
    int n = emit(&r, &block2, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

/* ======================================================================
 * HIGH: Block claim from wrong player
 * ====================================================================== */

CUI_TEST(block_claim_from_wrong_player_rejected)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Player 1 blocks */
    uint8_t blocker = (pid + 1) % 4;
    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Player 2 (not the blocker) tries to send block claim */
    uint8_t wrong = (pid + 2) % 4;
    coup_input_t claim = make_block_claim(wrong, COUP_RULES_CHAR_DUKE);
    int n = emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);

    /* Correct blocker should still work */
    coup_input_t real_claim = make_block_claim(blocker, COUP_RULES_CHAR_DUKE);
    n = emit(&r, &real_claim, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_GT(n, 0);
}

/* ======================================================================
 * HIGH: Dead player excluded from challenge window
 * ====================================================================== */

CUI_TEST(dead_player_excluded_from_challenge_window)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    /* Kill player 2 */
    r.players[2].revealed[0] = true;
    r.players[2].revealed[1] = true;
    r.players[2].alive = false;

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Dead player 2 should NOT be in pending */
    CUI_ASSERT_FALSE(r.pending_responses[2]);

    /* Only alive non-actor players should be pending */
    int expected_pending = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if ((uint8_t)i != pid && r.players[i].alive) expected_pending++;
    }
    CUI_ASSERT_EQ(expected_pending, r.pending_count);
}

CUI_TEST(dead_player_excluded_from_block_window)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    /* Kill player 2 */
    r.players[2].revealed[0] = true;
    r.players[2].revealed[1] = true;
    r.players[2].alive = false;

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Dead player 2 should NOT be pending in block window */
    CUI_ASSERT_FALSE(r.pending_responses[2]);

    /* Alive non-actor players should be pending */
    int expected = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if ((uint8_t)i != pid && r.players[i].alive) expected++;
    }
    CUI_ASSERT_EQ(expected, r.pending_count);
}

/* ======================================================================
 * HIGH: Timeout during non-window phases
 * ====================================================================== */

CUI_TEST(timeout_during_waiting_for_action_returns_zero)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t timeout = make_timeout();
    int n = emit(&r, &timeout, evts, COUP_RULES_MAX_EVENTS);

    /* Timeout in non-window phase does nothing — returns 0 events */
    CUI_ASSERT_EQ(0, n);
    /* Phase unchanged */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * MEDIUM: Steal from player with 0 coins
 * ====================================================================== */

CUI_TEST(steal_from_zero_coins)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    /* Target has 0 coins */
    r.players[target].coins = 0;
    uint8_t old_actor = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);
    /* Pass block */
    all_pass(&r);

    /* No coins transferred */
    CUI_ASSERT_EQ(0, (int)r.players[target].coins);
    CUI_ASSERT_EQ((int)old_actor, (int)r.players[pid].coins);
}

/* ======================================================================
 * MEDIUM: Seed 0 fallback
 * ====================================================================== */

CUI_TEST(seed_zero_fallback)
{
    coup_rules_t r;
    coup_rules_init(&r, 0);

    /* rng_state should not be 0 (the degenerate xorshift state).
     * Init sets it to 1. */
    CUI_ASSERT_NEQ(0, (int)r.rng_state);

    /* Seed 0 and seed 1 should produce identical games */
    coup_rules_t r1, r2;
    start_game(&r1, 4, 0);
    start_game(&r2, 4, 1);

    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_LT((int)r1.players[i].cards[0], COUP_RULES_NUM_CHARACTERS);
        CUI_ASSERT_LT((int)r1.players[i].cards[1], COUP_RULES_NUM_CHARACTERS);
        CUI_ASSERT_EQ((int)r1.players[i].cards[0], (int)r2.players[i].cards[0]);
        CUI_ASSERT_EQ((int)r1.players[i].cards[1], (int)r2.players[i].cards[1]);
    }
}

/* ======================================================================
 * MEDIUM: Exchange after challenge defended
 * ====================================================================== */

CUI_TEST(exchange_after_challenge_defended)
{
    /* Player declares Exchange, gets challenged, proves Ambassador.
     * Card is replaced from deck. Then exchange resolves with NEW card. */
    coup_rules_t r;
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t challenger = (pid + 1) % 2;

    /* Force player to have Ambassador */
    r.players[pid].cards[0] = COUP_RULES_CHAR_AMBASSADOR;
    int old_deck = r.deck_count;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenger challenges */
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Defender had card — challenger loses influence */
    coup_input_t lose = make_lose_influence(challenger, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Exchange should now be in progress */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);
    CUI_ASSERT_EQ(4, r.exchange_count);

    /* All exchange cards should be valid */
    int i;
    for (i = 0; i < r.exchange_count; i++) {
        CUI_ASSERT_LT((int)r.exchange_cards[i], COUP_RULES_NUM_CHARACTERS);
    }

    /* Keep 0 and 1 */
    coup_input_t choice = make_exchange_choice(pid, 0, 1);
    int n = emit(&r, &choice, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);

    /* Deck should be restored (card replaced + 2 drawn + 2 returned) */
    CUI_ASSERT_EQ(old_deck, r.deck_count);
}

/* ======================================================================
 * MEDIUM: Eliminated player mid-turn excluded from block window
 * ====================================================================== */

CUI_TEST(eliminated_during_challenge_excluded_from_block)
{
    /* 4 players. Player 0 steals from player 1.
     * Player 2 challenges → player 0 has Captain → player 2 loses both cards.
     * Block window opens. Player 2 should NOT be in pending. */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    uint8_t target = (pid + 1) % 4;
    uint8_t challenger = (pid + 2) % 4;

    /* Challenger has only 1 card left */
    r.players[challenger].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Player 2 challenges */
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Challenger loses last card → eliminated */
    coup_input_t lose = make_lose_influence(challenger, 1);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_FALSE(r.players[challenger].alive);

    /* Block window should be open for target only (Steal is target-only block) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_FALSE(r.pending_responses[challenger]);
    CUI_ASSERT_TRUE(r.pending_responses[target]);
}

/* ======================================================================
 * HIGH: Target dies during challenge -> block window skipped
 * ====================================================================== */

CUI_TEST(assassinate_target_one_inf_challenges_dies_no_block)
{
    /* 3 players. P0 assassinates P1 (1 influence). P1 challenges,
     * P0 has Assassin. P1 loses last card, dies.
     * Block window should be skipped since target is dead. */
    coup_rules_t r;
    start_game(&r, 3, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 3;

    /* Give actor enough coins and the Assassin card */
    r.players[pid].coins = 3;
    r.players[pid].cards[0] = COUP_RULES_CHAR_ASSASSIN;

    /* Target has only 1 influence left */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* P0 declares Assassinate on P1 */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P1 (target) challenges the Assassin claim */
    coup_input_t chal = make_response(target, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 must lose influence — loses last card, dies */
    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Target should be dead */
    CUI_ASSERT_FALSE(r.players[target].alive);

    /* No block window should have opened */
    CUI_ASSERT_EQ(-1, find_event(evts, n, COUP_EVT_BLOCK_OPENED));

    /* Dead target should not be pending */
    CUI_ASSERT_FALSE(r.pending_responses[target]);

    /* Action should have resolved (dead target, nothing to block) */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);

    /* Game should still be active (3 players, 2 remain) */
    CUI_ASSERT_TRUE(r.game_active);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(assassinate_target_one_inf_challenges_dies_game_over)
{
    /* 2 players. P0 assassinates P1 (1 influence). P1 challenges,
     * P0 has Assassin. P1 loses last card, dies -> game over. */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    r.players[pid].coins = 3;
    r.players[pid].cards[0] = COUP_RULES_CHAR_ASSASSIN;

    /* Target has only 1 influence left */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* P0 declares Assassinate on P1 */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P1 challenges */
    coup_input_t chal = make_response(target, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 loses last card -> dies */
    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Target should be dead */
    CUI_ASSERT_FALSE(r.players[target].alive);

    /* Game over — only 1 player remains */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_GAME_OVER), 0);
    CUI_ASSERT_FALSE(r.game_active);

    /* No block window should have opened */
    CUI_ASSERT_EQ(-1, find_event(evts, n, COUP_EVT_BLOCK_OPENED));

    /* Dead target not pending */
    CUI_ASSERT_FALSE(r.pending_responses[target]);
}

CUI_TEST(steal_target_one_inf_challenges_dies_no_block)
{
    /* 3 players. P0 steals from P1 (1 influence). P1 challenges,
     * P0 has Captain. P1 loses last card, dies.
     * Block window should be skipped. */
    coup_rules_t r;
    start_game(&r, 3, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 3;

    r.players[pid].cards[0] = COUP_RULES_CHAR_CAPTAIN;

    /* Target has only 1 influence left */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* P0 declares Steal on P1 */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P1 challenges the Captain claim */
    coup_input_t chal = make_response(target, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 loses last card -> dies */
    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Target should be dead */
    CUI_ASSERT_FALSE(r.players[target].alive);

    /* No block window */
    CUI_ASSERT_EQ(-1, find_event(evts, n, COUP_EVT_BLOCK_OPENED));

    /* Dead target not pending */
    CUI_ASSERT_FALSE(r.pending_responses[target]);

    /* Action resolved */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);

    /* Game still active (2 of 3 players remain) */
    CUI_ASSERT_TRUE(r.game_active);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(steal_target_one_inf_challenges_dies_game_over)
{
    /* 2 players. P0 steals from P1 (1 influence). P1 challenges,
     * P0 has Captain. P1 loses last card, dies -> game over. */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    r.players[pid].cards[0] = COUP_RULES_CHAR_CAPTAIN;

    /* Target has only 1 influence left */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* P0 declares Steal on P1 */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P1 challenges */
    coup_input_t chal = make_response(target, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P1 loses last card -> dies */
    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Target dead */
    CUI_ASSERT_FALSE(r.players[target].alive);

    /* Game over */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_GAME_OVER), 0);
    CUI_ASSERT_FALSE(r.game_active);

    /* No block window */
    CUI_ASSERT_EQ(-1, find_event(evts, n, COUP_EVT_BLOCK_OPENED));

    /* Dead target not pending */
    CUI_ASSERT_FALSE(r.pending_responses[target]);
}

CUI_TEST(assassinate_non_target_challenges_dies_block_still_opens)
{
    /* 4 players. P0 assassinates P1. P2 (1 influence, non-target) challenges,
     * P0 has Assassin. P2 dies. Block window DOES open — P1 (target) can
     * still block, but P2 (dead) should NOT be pending. */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    uint8_t challenger = (pid + 2) % 4;

    r.players[pid].coins = 3;
    r.players[pid].cards[0] = COUP_RULES_CHAR_ASSASSIN;

    /* Challenger has only 1 influence */
    r.players[challenger].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* P0 declares Assassinate on P1 */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* P2 (non-target) challenges */
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* P2 loses last card -> dies */
    coup_input_t lose = make_lose_influence(challenger, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Challenger should be dead */
    CUI_ASSERT_FALSE(r.players[challenger].alive);

    /* Block window SHOULD open (target is alive) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_BLOCK_OPENED), 0);

    /* Target (P1) should be pending */
    CUI_ASSERT_TRUE(r.pending_responses[target]);

    /* Dead challenger should NOT be pending */
    CUI_ASSERT_FALSE(r.pending_responses[challenger]);
}

/* ======================================================================
 * Scenario: Full 4-player game with mixed actions
 * ====================================================================== */

CUI_TEST(scenario_four_player_mixed_game)
{
    /* A 4-player game exercising multiple action types, ensuring
     * the engine handles all transitions correctly. */
    coup_rules_t r;
    start_game(&r, 4, 500);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Round 1: All take income */
    int i;
    for (i = 0; i < 4; i++) {
        do_income(&r);
    }

    /* All should have 3 coins */
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ(3, (int)r.players[i].coins);
    }

    /* Round 2: Player 0 attempts Tax, everyone passes */
    {
        uint8_t pid = coup_rules_current_player(&r);
        CUI_ASSERT_EQ(0, (int)pid);
        coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
        emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
        all_pass(&r);
        CUI_ASSERT_EQ(6, (int)r.players[0].coins); /* 3 + 3 */
    }

    /* Player 1, 2, 3 take income */
    do_income(&r); /* p1: 4 */
    do_income(&r); /* p2: 4 */
    do_income(&r); /* p3: 4 */

    /* Round 3: Player 0 has 6 coins, takes income to get 7 */
    do_income(&r);
    CUI_ASSERT_EQ(7, (int)r.players[0].coins);

    /* Player 1 takes income */
    do_income(&r);

    /* Player 2 takes income */
    do_income(&r);

    /* Player 3 takes income */
    do_income(&r);

    /* Player 0 coups player 3 */
    {
        CUI_ASSERT_EQ(0, (int)coup_rules_current_player(&r));
        coup_input_t act = make_action(0, COUP_RULES_ACT_COUP, 3);
        emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
        CUI_ASSERT_EQ(0, (int)r.players[0].coins);

        coup_input_t lose = make_lose_influence(3, 0);
        emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

        CUI_ASSERT_TRUE(r.players[3].revealed[0]);
        CUI_ASSERT_TRUE(r.players[3].alive); /* still has 1 card */
    }

    /* Game should still be active with 4 alive */
    CUI_ASSERT_TRUE(r.game_active);
}

/* ======================================================================
 * Forced coup at 10+ coins blocks all other actions
 * ====================================================================== */

CUI_TEST(forced_coup_blocks_all_other_actions)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 10;

    /* Valid actions should be ONLY Coup */
    uint8_t valid = coup_rules_valid_actions(&r);
    CUI_ASSERT_EQ((int)(1 << COUP_RULES_ACT_COUP), (int)valid);

    /* Attempting Income should be rejected */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t income = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    CUI_ASSERT_EQ(-1, emit(&r, &income, evts, COUP_RULES_MAX_EVENTS));

    /* Attempting Tax should be rejected */
    coup_input_t tax = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    CUI_ASSERT_EQ(-1, emit(&r, &tax, evts, COUP_RULES_MAX_EVENTS));

    /* Attempting Foreign Aid should be rejected */
    coup_input_t fa = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    CUI_ASSERT_EQ(-1, emit(&r, &fa, evts, COUP_RULES_MAX_EVENTS));

    /* Coup should succeed */
    uint8_t target = (pid + 1) % 2;
    coup_input_t coup = make_action(pid, COUP_RULES_ACT_COUP, target);
    CUI_ASSERT_GT(emit(&r, &coup, evts, COUP_RULES_MAX_EVENTS), 0);
}

/* ======================================================================
 * Targeted actions reject self as target
 * ====================================================================== */

CUI_TEST(targeted_action_rejects_self_as_target)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Coup self */
    coup_input_t coup_self = make_action(pid, COUP_RULES_ACT_COUP, pid);
    CUI_ASSERT_EQ(-1, emit(&r, &coup_self, evts, COUP_RULES_MAX_EVENTS));

    /* Steal from self */
    r.players[pid].coins = 2; /* reset so not forced to coup */
    coup_input_t steal_self = make_action(pid, COUP_RULES_ACT_STEAL, pid);
    CUI_ASSERT_EQ(-1, emit(&r, &steal_self, evts, COUP_RULES_MAX_EVENTS));

    /* Assassinate self */
    r.players[pid].coins = 3;
    coup_input_t assn_self = make_action(pid, COUP_RULES_ACT_ASSASSINATE, pid);
    CUI_ASSERT_EQ(-1, emit(&r, &assn_self, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * Targeted actions reject dead target
 * ====================================================================== */

CUI_TEST(targeted_action_rejects_dead_target)
{
    coup_rules_t r;
    start_game(&r, 3, 42);

    /* Kill player 2 */
    r.players[2].revealed[0] = true;
    r.players[2].revealed[1] = true;
    r.players[2].alive = false;

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Coup dead player */
    coup_input_t coup_dead = make_action(pid, COUP_RULES_ACT_COUP, 2);
    CUI_ASSERT_EQ(-1, emit(&r, &coup_dead, evts, COUP_RULES_MAX_EVENTS));

    /* Steal from dead player */
    r.players[pid].coins = 2;
    coup_input_t steal_dead = make_action(pid, COUP_RULES_ACT_STEAL, 2);
    CUI_ASSERT_EQ(-1, emit(&r, &steal_dead, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * Targeted actions reject out-of-bounds target
 * ====================================================================== */

CUI_TEST(targeted_action_rejects_out_of_bounds_target)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Coup target_id = player_count (out of bounds) */
    coup_input_t coup_oob = make_action(pid, COUP_RULES_ACT_COUP, 2);
    CUI_ASSERT_EQ(-1, emit(&r, &coup_oob, evts, COUP_RULES_MAX_EVENTS));

    /* Coup target_id = 255 */
    coup_input_t coup_255 = make_action(pid, COUP_RULES_ACT_COUP, 255);
    CUI_ASSERT_EQ(-1, emit(&r, &coup_255, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * Action from out-of-bounds player_id rejected
 * ====================================================================== */

CUI_TEST(action_rejects_out_of_bounds_player_id)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* player_id = 255 */
    coup_input_t act = make_action(255, COUP_RULES_ACT_INCOME, 0xFF);
    CUI_ASSERT_EQ(-1, emit(&r, &act, evts, COUP_RULES_MAX_EVENTS));

    /* player_id = player_count */
    coup_input_t act2 = make_action(2, COUP_RULES_ACT_INCOME, 0xFF);
    CUI_ASSERT_EQ(-1, emit(&r, &act2, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * Only steal target can block steal (non-target rejected)
 * ====================================================================== */

CUI_TEST(steal_non_target_cannot_block)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    uint8_t bystander = (pid + 2) % 4;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Bystander should NOT be pending */
    CUI_ASSERT_FALSE(r.pending_responses[bystander]);

    /* Bystander trying to block should fail */
    coup_input_t bad_block = make_response(bystander, COUP_RULES_RESP_BLOCK);
    CUI_ASSERT_EQ(-1, emit(&r, &bad_block, evts, COUP_RULES_MAX_EVENTS));

    /* Target should be able to block */
    CUI_ASSERT_TRUE(r.pending_responses[target]);
}

/* ======================================================================
 * Only assassinate target can block with Contessa (non-target rejected)
 * ====================================================================== */

CUI_TEST(assassinate_non_target_cannot_block)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    uint8_t bystander = (pid + 2) % 4;
    r.players[pid].coins = 3;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Bystander should NOT be pending */
    CUI_ASSERT_FALSE(r.pending_responses[bystander]);

    /* Bystander trying to block should fail */
    coup_input_t bad_block = make_response(bystander, COUP_RULES_RESP_BLOCK);
    CUI_ASSERT_EQ(-1, emit(&r, &bad_block, evts, COUP_RULES_MAX_EVENTS));

    /* Target should be pending */
    CUI_ASSERT_TRUE(r.pending_responses[target]);
}

/* ======================================================================
 * Dead player cannot submit challenge response
 * ====================================================================== */

CUI_TEST(dead_player_challenge_response_rejected)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    /* Kill player 2 */
    r.players[2].revealed[0] = true;
    r.players[2].revealed[1] = true;
    r.players[2].alive = false;

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    /* Dead player 2 tries to challenge */
    coup_input_t chal = make_response(2, COUP_RULES_RESP_CHALLENGE);
    CUI_ASSERT_EQ(-1, emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS));

    /* Phase should be unchanged */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
}

/* ======================================================================
 * Exchange choice: duplicate indices rejected
 * ====================================================================== */

CUI_TEST(exchange_choice_rejects_duplicate_indices)
{
    coup_rules_t r;
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);
    CUI_ASSERT_EQ(4, r.exchange_count);

    /* Keeping same index twice should be rejected */
    coup_input_t dup = make_exchange_choice(pid, 1, 1);
    CUI_ASSERT_EQ(-1, emit(&r, &dup, evts, COUP_RULES_MAX_EVENTS));

    /* Phase should be unchanged — still waiting for valid choice */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);
}

/* ======================================================================
 * Exchange choice: out-of-range indices rejected
 * ====================================================================== */

CUI_TEST(exchange_choice_rejects_out_of_range_indices)
{
    coup_rules_t r;
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);

    /* Index 4 is out of range (only 4 cards, indices 0-3) */
    coup_input_t oob = make_exchange_choice(pid, 0, 4);
    CUI_ASSERT_EQ(-1, emit(&r, &oob, evts, COUP_RULES_MAX_EVENTS));

    /* Index 255 should also be rejected */
    coup_input_t oob2 = make_exchange_choice(pid, 0, 255);
    CUI_ASSERT_EQ(-1, emit(&r, &oob2, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * Exchange with depleted deck
 * ====================================================================== */

CUI_TEST(exchange_with_empty_deck_offers_only_hand_cards)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);

    /* Drain the deck completely */
    r.deck_count = 0;

    /* Count unrevealed cards */
    int unrevealed = 0;
    int ci;
    for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
        if (!r.players[pid].revealed[ci]) unrevealed++;
    }
    CUI_ASSERT_EQ(2, unrevealed); /* fresh game, both cards unrevealed */

    /* Exchange action */
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge window */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);

    /* Exchange should only offer hand cards (2), no drawn cards */
    CUI_ASSERT_EQ(2, r.exchange_count);
    CUI_ASSERT_EQ((int)r.players[pid].cards[0], (int)r.exchange_cards[0]);
    CUI_ASSERT_EQ((int)r.players[pid].cards[1], (int)r.exchange_cards[1]);

    /* Player must keep both (only 2 offered, 2 unrevealed) */
    coup_input_t choice = make_exchange_choice(pid, 0, 1);
    int n = emit(&r, &choice, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(exchange_with_one_card_in_deck_offers_three)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    start_game(&r, 2, 300);

    uint8_t pid = coup_rules_current_player(&r);

    /* Leave only 1 card in deck */
    r.deck_count = 1;
    r.deck[0] = COUP_RULES_CHAR_CONTESSA;

    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);

    /* 2 hand cards + 1 drawn = 3 offered */
    CUI_ASSERT_EQ(3, r.exchange_count);
    CUI_ASSERT_EQ(0, r.deck_count); /* deck fully drained */

    /* Keep first two */
    coup_input_t choice = make_exchange_choice(pid, 0, 1);
    int n = emit(&r, &choice, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}
