/**
 * test_coup_rules.c - Tests for Coup rule engine
 */

#include "cui_test_framework.h"
#include "coup_rules.h"

/* Suppress unused-function for helpers used in later phases */
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

static coup_input_t make_timeout(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_TIMEOUT;
    return in;
}

/* Add N players to a lobby, all ready */
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

/* Start a game and consume the startup events */
static void start_game(coup_rules_t* r, int players, uint32_t seed)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(r, seed);
    add_ready_players(r, players);
    coup_input_t start = make_start();
    emit(r, &start, evts, COUP_RULES_MAX_EVENTS);
}

/* Have all pending players pass */
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

/* Count how many events of a given type appear */
static int count_events(const coup_event_t* evts, int n, coup_event_type_t type)
{
    int count = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (evts[i].type == type) count++;
    }
    return count;
}

/* Find first event of a given type, return index or -1 */
static int find_event(const coup_event_t* evts, int n, coup_event_type_t type)
{
    int i;
    for (i = 0; i < n; i++) {
        if (evts[i].type == type) return i;
    }
    return -1;
}

/* ======================================================================
 * Phase 2: Game Initialization
 * ====================================================================== */

CUI_TEST(coup_rules_init_sets_player_count)
{
    int pc;
    for (pc = 2; pc <= 6; pc++) {
        coup_rules_t r;
        coup_rules_init(&r, 42);
        add_ready_players(&r, pc);
        CUI_ASSERT_EQ(pc, r.player_count);
    }
}

CUI_TEST(coup_rules_init_deals_two_cards_each)
{
    coup_rules_t r;
    start_game(&r, 4, 42);
    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_NEQ(COUP_RULES_CHAR_NONE, (int)r.players[i].cards[0]);
        CUI_ASSERT_NEQ(COUP_RULES_CHAR_NONE, (int)r.players[i].cards[1]);
        CUI_ASSERT_LT((int)r.players[i].cards[0], COUP_RULES_NUM_CHARACTERS);
        CUI_ASSERT_LT((int)r.players[i].cards[1], COUP_RULES_NUM_CHARACTERS);
    }
}

CUI_TEST(coup_rules_init_deck_is_three_of_each)
{
    coup_rules_t r;
    start_game(&r, 4, 42);
    int counts[COUP_RULES_NUM_CHARACTERS];
    int i;
    memset(counts, 0, sizeof(counts));

    /* Count cards in deck */
    for (i = 0; i < r.deck_count; i++) {
        CUI_ASSERT_LT((int)r.deck[i], COUP_RULES_NUM_CHARACTERS);
        counts[r.deck[i]]++;
    }
    /* Count cards dealt to players */
    for (i = 0; i < r.player_count; i++) {
        CUI_ASSERT_LT((int)r.players[i].cards[0], COUP_RULES_NUM_CHARACTERS);
        CUI_ASSERT_LT((int)r.players[i].cards[1], COUP_RULES_NUM_CHARACTERS);
        counts[r.players[i].cards[0]]++;
        counts[r.players[i].cards[1]]++;
    }
    /* Each character should appear exactly 3 times */
    for (i = 0; i < COUP_RULES_NUM_CHARACTERS; i++) {
        CUI_ASSERT_EQ(3, counts[i]);
    }
}

CUI_TEST(coup_rules_init_starting_coins)
{
    coup_rules_t r;
    start_game(&r, 4, 42);
    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ(COUP_RULES_INITIAL_COINS, (int)r.players[i].coins);
    }
}

CUI_TEST(coup_rules_init_deck_size)
{
    int pc;
    for (pc = 2; pc <= 6; pc++) {
        coup_rules_t r;
        start_game(&r, pc, 42);
        CUI_ASSERT_EQ(COUP_RULES_DECK_SIZE - (pc * 2), r.deck_count);
    }
}

CUI_TEST(coup_rules_init_all_alive)
{
    coup_rules_t r;
    start_game(&r, 4, 42);
    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_TRUE(r.players[i].alive);
    }
}

CUI_TEST(coup_rules_init_phase_is_lobby)
{
    coup_rules_t r;
    coup_rules_init(&r, 42);
    CUI_ASSERT_EQ((int)COUP_TURN_LOBBY, (int)r.phase);
}

CUI_TEST(coup_rules_start_game_emits_events)
{
    coup_rules_t r;
    coup_rules_init(&r, 42);
    add_ready_players(&r, 4);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t start = make_start();
    int n = emit(&r, &start, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ((int)COUP_EVT_GAME_STARTED, (int)evts[0].type);
    CUI_ASSERT_EQ(4, (int)evts[0].data.game_started.player_count);

    /* Should also emit TURN_STARTED */
    int ti = find_event(evts, n, COUP_EVT_TURN_STARTED);
    CUI_ASSERT_GE(ti, 0);
}

CUI_TEST(coup_rules_start_game_phase_waiting)
{
    coup_rules_t r;
    coup_rules_init(&r, 42);
    add_ready_players(&r, 4);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t start = make_start();
    emit(&r, &start, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(coup_rules_deterministic_with_seed)
{
    coup_rules_t r1, r2;

    start_game(&r1, 4, 12345);
    start_game(&r2, 4, 12345);

    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ((int)r1.players[i].cards[0], (int)r2.players[i].cards[0]);
        CUI_ASSERT_EQ((int)r1.players[i].cards[1], (int)r2.players[i].cards[1]);
    }
    CUI_ASSERT_EQ(r1.deck_count, r2.deck_count);
    int j;
    for (j = 0; j < r1.deck_count; j++) {
        CUI_ASSERT_EQ((int)r1.deck[j], (int)r2.deck[j]);
    }
}

/* ======================================================================
 * Phase 3: Basic Actions — Income, Foreign Aid, Coup
 * ====================================================================== */

CUI_TEST(income_adds_one_coin)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)(old_coins + 1), (int)r.players[pid].coins);
}

CUI_TEST(income_emits_coins_changed_and_resolved)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GE(count_events(evts, n, COUP_EVT_COINS_CHANGED), 1);
    CUI_ASSERT_GE(count_events(evts, n, COUP_EVT_ACTION_RESOLVED), 1);

    /* Verify COINS_CHANGED data */
    int ci = find_event(evts, n, COUP_EVT_COINS_CHANGED);
    CUI_ASSERT_EQ((int)pid, (int)evts[ci].data.coins_changed.player_id);
    CUI_ASSERT_EQ(2, (int)evts[ci].data.coins_changed.old_coins);
    CUI_ASSERT_EQ(3, (int)evts[ci].data.coins_changed.new_coins);
}

CUI_TEST(income_advances_turn)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t first = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(first, COUP_RULES_ACT_INCOME, 0xFF);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Should emit TURN_STARTED for the next player */
    int ti = find_event(evts, n, COUP_EVT_TURN_STARTED);
    CUI_ASSERT_GE(ti, 0);

    uint8_t next = coup_rules_current_player(&r);
    CUI_ASSERT_NEQ((int)first, (int)next);
    CUI_ASSERT_EQ((int)evts[ti].data.turn_started.player_id, (int)next);
}

CUI_TEST(income_wrong_player_rejected)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    /* Pick a different player */
    uint8_t wrong = (pid + 1) % 4;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(wrong, COUP_RULES_ACT_INCOME, 0xFF);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(-1, n);
}

CUI_TEST(foreign_aid_opens_block_window)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Should emit ACTION_DECLARED and BLOCK_OPENED */
    CUI_ASSERT_GE(count_events(evts, n, COUP_EVT_ACTION_DECLARED), 1);
    CUI_ASSERT_GE(count_events(evts, n, COUP_EVT_BLOCK_OPENED), 1);

    /* BLOCK_OPENED should indicate Duke can block */
    int bi = find_event(evts, n, COUP_EVT_BLOCK_OPENED);
    CUI_ASSERT_GE(bi, 0);
    CUI_ASSERT_TRUE(evts[bi].data.block_opened.blockable_by &
                     (1 << COUP_RULES_CHAR_DUKE));
    /* Anyone can block (not target-only) */
    CUI_ASSERT_FALSE(evts[bi].data.block_opened.target_only);
}

CUI_TEST(foreign_aid_block_window_all_players_pending)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* All players except the actor should be pending */
    int i;
    for (i = 0; i < 4; i++) {
        if ((uint8_t)i == pid) {
            CUI_ASSERT_FALSE(r.pending_responses[i]);
        } else {
            CUI_ASSERT_TRUE(r.pending_responses[i]);
        }
    }
    CUI_ASSERT_EQ(3, r.pending_count);
}

CUI_TEST(foreign_aid_actor_cannot_respond)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Actor tries to respond — should be rejected */
    coup_input_t resp = make_response(pid, COUP_RULES_RESP_PASS);
    int n = emit(&r, &resp, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

CUI_TEST(foreign_aid_partial_passes_stay_in_block)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* First non-actor passes */
    uint8_t first_other = (pid + 1) % 4;
    coup_input_t pass1 = make_response(first_other, COUP_RULES_RESP_PASS);
    emit(&r, &pass1, evts, COUP_RULES_MAX_EVENTS);

    /* Still in block window with 2 pending */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_EQ(2, r.pending_count);
    CUI_ASSERT_FALSE(r.pending_responses[first_other]);

    /* Second non-actor passes */
    uint8_t second_other = (pid + 2) % 4;
    coup_input_t pass2 = make_response(second_other, COUP_RULES_RESP_PASS);
    emit(&r, &pass2, evts, COUP_RULES_MAX_EVENTS);

    /* Still in block window with 1 pending */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_EQ(1, r.pending_count);
}

CUI_TEST(foreign_aid_all_pass_resolves)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* All non-actor players pass */
    all_pass(&r);

    CUI_ASSERT_EQ((int)(old_coins + 2), (int)r.players[pid].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
    /* Turn should have advanced */
    CUI_ASSERT_NEQ((int)pid, (int)coup_rules_current_player(&r));
}

CUI_TEST(foreign_aid_blocked_by_duke)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* One player blocks (claims Duke) */
    uint8_t blocker = (pid + 1) % 4;
    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);

    /* Should need block claim */
    coup_input_t claim = make_block_claim(blocker, COUP_RULES_CHAR_DUKE);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* Should open block-challenge window */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);

    /* All pass the block-challenge → block stands, action cancelled */
    all_pass(&r);

    /* No coins gained */
    CUI_ASSERT_EQ((int)old_coins, (int)r.players[pid].coins);

    /* Action cancelled event should have been emitted */
    /* Turn should advance */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
    CUI_ASSERT_NEQ((int)pid, (int)coup_rules_current_player(&r));
}

CUI_TEST(coup_deducts_seven_coins)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    /* Give player enough coins for a coup */
    r.players[pid].coins = 7;

    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(0, (int)r.players[pid].coins);
}

CUI_TEST(coup_requires_seven_coins)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    /* Player has only 2 coins (starting amount) */
    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(-1, n);
}

CUI_TEST(coup_target_loses_influence)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;
    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Should emit INFLUENCE_LOSS_REQUESTED for target */
    int li = find_event(evts, n, COUP_EVT_INFLUENCE_LOSS_REQUESTED);
    CUI_ASSERT_GE(li, 0);
    CUI_ASSERT_EQ((int)target, (int)evts[li].data.influence_loss_requested.player_id);

    /* Phase should be waiting for influence loss */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
}

CUI_TEST(forced_coup_at_ten_coins)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 10;

    uint8_t valid = coup_rules_valid_actions(&r);
    CUI_ASSERT_EQ(1 << COUP_RULES_ACT_COUP, (int)valid);
}

CUI_TEST(turn_advances_round_robin)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    /* Take 4 incomes — each player acts once in order */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int turns_seen[4] = {0, 0, 0, 0};
    int t;
    for (t = 0; t < 4; t++) {
        uint8_t pid = coup_rules_current_player(&r);
        CUI_ASSERT_LT((int)pid, 4);
        turns_seen[pid]++;
        coup_input_t act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
        emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);
    }

    /* Each player should have acted exactly once */
    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ(1, turns_seen[i]);
    }
}

CUI_TEST(turn_wraps_around)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t first = coup_rules_current_player(&r);

    /* Two incomes — should wrap back to first player */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int t;
    for (t = 0; t < 2; t++) {
        uint8_t pid = coup_rules_current_player(&r);
        coup_input_t act = make_action(pid, COUP_RULES_ACT_INCOME, 0xFF);
        int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

        /* After second turn, ROUND_ADVANCED should be emitted */
        if (t == 1) {
            CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ROUND_ADVANCED), 0);
        }
    }

    /* Should be back to first player */
    CUI_ASSERT_EQ((int)first, (int)coup_rules_current_player(&r));
    CUI_ASSERT_EQ(1, r.round_number);
}

/* ======================================================================
 * Phase 4: Character Actions — Tax, Steal, Assassinate, Exchange
 * ====================================================================== */

CUI_TEST(tax_claims_duke_opens_challenge)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_OPENED);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_EQ((int)pid, (int)evts[ci].data.challenge_opened.defender_id);
    CUI_ASSERT_EQ(COUP_RULES_CHAR_DUKE,
                   (int)evts[ci].data.challenge_opened.claimed_char);

    /* All players except actor should be pending */
    int i;
    for (i = 0; i < 4; i++) {
        if ((uint8_t)i == pid) {
            CUI_ASSERT_FALSE(r.pending_responses[i]);
        } else {
            CUI_ASSERT_TRUE(r.pending_responses[i]);
        }
    }
}

CUI_TEST(tax_no_challenge_adds_three)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* All pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)(old_coins + 3), (int)r.players[pid].coins);
    /* Turn should have advanced */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
    CUI_ASSERT_NEQ((int)pid, (int)coup_rules_current_player(&r));
}

CUI_TEST(tax_challenge_immediately_resolves)
{
    /* In 4-player game: player 0 declares Tax, player 1 challenges.
     * Phase should advance immediately — players 2 and 3 should NOT
     * need to respond. */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(3, r.pending_count);

    /* One player challenges */
    uint8_t challenger = (pid + 1) % 4;
    coup_input_t challenge = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &challenge, evts, COUP_RULES_MAX_EVENTS);

    /* Should have challenge result */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_CHALLENGE_RESULT), 0);

    /* All pending cleared — no one else needs to respond */
    CUI_ASSERT_EQ(0, r.pending_count);

    /* Phase should NOT still be CHALLENGE_WINDOW */
    CUI_ASSERT_NEQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);
}

CUI_TEST(steal_claims_captain_opens_challenge)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_OPENED);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_EQ(COUP_RULES_CHAR_CAPTAIN,
                   (int)evts[ci].data.challenge_opened.claimed_char);
}

CUI_TEST(steal_no_challenge_opens_block)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* All pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);

    /* Only the target should be pending (Captain/Ambassador block) */
    CUI_ASSERT_EQ(1, r.pending_count);
    CUI_ASSERT_TRUE(r.pending_responses[target]);
    CUI_ASSERT_FALSE(r.pending_responses[pid]);
}

CUI_TEST(steal_takes_min_two_target_coins)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;

    /* Target has only 1 coin */
    r.players[target].coins = 1;
    uint8_t old_actor = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);
    /* Pass block */
    all_pass(&r);

    /* Should steal only 1 (min of 2 and target's 1) */
    CUI_ASSERT_EQ(0, (int)r.players[target].coins);
    CUI_ASSERT_EQ((int)(old_actor + 1), (int)r.players[pid].coins);
}

CUI_TEST(steal_no_block_resolves)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    uint8_t old_actor = r.players[pid].coins;
    uint8_t old_target = r.players[target].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);
    /* Pass block */
    all_pass(&r);

    /* Actor gains 2, target loses 2 */
    CUI_ASSERT_EQ((int)(old_actor + 2), (int)r.players[pid].coins);
    CUI_ASSERT_EQ((int)(old_target - 2), (int)r.players[target].coins);

    /* Turn advanced */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(assassinate_costs_three_upfront)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    r.players[pid].coins = 3;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Coins deducted immediately on declaration */
    CUI_ASSERT_EQ(0, (int)r.players[pid].coins);
}

CUI_TEST(assassinate_opens_challenge_then_block)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 4;
    r.players[pid].coins = 3;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* First: challenge window */
    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    /* All pass challenge */
    all_pass(&r);

    /* Then: block window for target only (Contessa) */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_EQ(1, r.pending_count);
    CUI_ASSERT_TRUE(r.pending_responses[target]);
}

CUI_TEST(assassinate_target_loses_influence)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    r.players[pid].coins = 3;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);
    /* Pass block */
    all_pass(&r);

    /* Target should be asked to lose influence */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ((int)target, (int)r.influence_loser);
}

CUI_TEST(exchange_claims_ambassador)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    int n = emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_CHALLENGE_WINDOW, (int)r.phase);

    int ci = find_event(evts, n, COUP_EVT_CHALLENGE_OPENED);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_EQ(COUP_RULES_CHAR_AMBASSADOR,
                   (int)evts[ci].data.challenge_opened.claimed_char);
}

CUI_TEST(exchange_offers_four_cards)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_EXCHANGE, (int)r.phase);

    /* Find the EXCHANGE_OFFERED event from the last batch of events
     * The all_pass triggered it, so we need to check the rule state */
    CUI_ASSERT_EQ(4, r.exchange_count);
    CUI_ASSERT_EQ((int)pid, (int)r.exchange_player);

    /* All 4 cards should be valid characters */
    int i;
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_LT((int)r.exchange_cards[i], COUP_RULES_NUM_CHARACTERS);
    }
}

CUI_TEST(exchange_keeps_two_returns_two)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    int old_deck = r.deck_count;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_EXCHANGE, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    /* Deck should have lost 2 cards for the exchange offer */
    CUI_ASSERT_EQ(old_deck - 2, r.deck_count);

    /* Keep cards at indices 0 and 1 */
    uint8_t kept0 = r.exchange_cards[0];
    uint8_t kept1 = r.exchange_cards[1];

    coup_input_t choice = make_exchange_choice(pid, 0, 1);
    int n = emit(&r, &choice, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);

    /* Player's unrevealed cards should be the kept ones */
    int slot = 0;
    int ci;
    uint8_t hand[2];
    for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
        if (!r.players[pid].revealed[ci]) {
            hand[slot++] = r.players[pid].cards[ci];
        }
    }
    CUI_ASSERT_EQ((int)kept0, (int)hand[0]);
    CUI_ASSERT_EQ((int)kept1, (int)hand[1]);

    /* Deck should have 2 cards returned (back to original count) */
    CUI_ASSERT_EQ(old_deck, r.deck_count);

    /* Turn advanced */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * Phase 5: Challenge Resolution
 * ====================================================================== */

CUI_TEST(challenge_defender_has_card_challenger_loses)
{
    /* Force player 0 to have a Duke, then Tax → challenged → defender wins */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    /* Force a Duke into hand */
    r.players[pid].cards[0] = COUP_RULES_CHAR_DUKE;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Challenger */
    uint8_t challenger = (pid + 1) % 4;
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Challenge result: defender had card */
    int ri = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ri, 0);
    CUI_ASSERT_TRUE(evts[ri].data.challenge_result.defender_had_card);
    CUI_ASSERT_EQ((int)COUP_RULES_CHAR_DUKE,
                   (int)evts[ri].data.challenge_result.revealed_char);

    /* Defender's card should have been replaced */
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_CARD_REPLACED), 0);

    /* Challenger must lose influence */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ((int)challenger, (int)r.influence_loser);
}

CUI_TEST(challenge_defender_bluffing_loses_influence)
{
    /* Force player 0 to NOT have Duke, then Tax → challenged → caught */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    /* Force hand to be non-Duke */
    r.players[pid].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    r.players[pid].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t challenger = (pid + 1) % 4;
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Defender was bluffing */
    int ri = find_event(evts, n, COUP_EVT_CHALLENGE_RESULT);
    CUI_ASSERT_GE(ri, 0);
    CUI_ASSERT_FALSE(evts[ri].data.challenge_result.defender_had_card);

    /* Defender must lose influence */
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_INFLUENCE_LOSS, (int)r.phase);
    CUI_ASSERT_EQ((int)pid, (int)r.influence_loser);

    /* After losing influence, action should be cancelled */
    coup_input_t lose = make_lose_influence(pid, 0);
    n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_CANCELLED), 0);

    /* No coins gained */
    CUI_ASSERT_EQ(COUP_RULES_INITIAL_COINS, (int)r.players[pid].coins);
}

CUI_TEST(challenge_card_replaced_from_shuffled_deck)
{
    /* After defender proves card, the card goes back to deck, deck is
     * shuffled, and a new card is drawn */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].cards[0] = COUP_RULES_CHAR_DUKE;
    int old_deck_count = r.deck_count;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t challenger = (pid + 1) % 2;
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* CARD_REPLACED event should exist */
    int ci = find_event(evts, n, COUP_EVT_CARD_REPLACED);
    CUI_ASSERT_GE(ci, 0);
    CUI_ASSERT_EQ((int)pid, (int)evts[ci].data.card_replaced.player_id);

    /* Deck count should be unchanged (put one back, drew one) */
    CUI_ASSERT_EQ(old_deck_count, r.deck_count);

    /* Defender's card at that slot should be a valid character */
    uint8_t new_card = r.players[pid].cards[evts[ci].data.card_replaced.card_idx];
    CUI_ASSERT_LT((int)new_card, COUP_RULES_NUM_CHARACTERS);
}

CUI_TEST(challenge_failed_action_still_proceeds)
{
    /* Tax challenged, defender has Duke → challenger loses influence,
     * then Tax still resolves (+3 coins) */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].cards[0] = COUP_RULES_CHAR_DUKE;
    uint8_t old_coins = r.players[pid].coins;

    uint8_t challenger = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_TAX, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Challenger loses influence */
    coup_input_t lose = make_lose_influence(challenger, 0);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Tax should have resolved — actor gained 3 coins */
    CUI_ASSERT_EQ((int)(old_coins + 3), (int)r.players[pid].coins);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_ACTION_RESOLVED), 0);
}

CUI_TEST(challenge_failed_blockable_action_still_gets_block_window)
{
    /* Steal challenged, defender has Captain → challenger loses influence,
     * then block window still opens for the target */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    uint8_t target = (pid + 1) % 4;
    uint8_t challenger = (pid + 2) % 4;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* A different player (not target) challenges */
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Challenger loses influence */
    coup_input_t lose = make_lose_influence(challenger, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Should now be in block window for the target */
    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_WINDOW, (int)r.phase);
    CUI_ASSERT_TRUE(r.pending_responses[target]);
}

CUI_TEST(challenge_succeeded_assassinate_coins_not_refunded)
{
    /* Player pays 3 for Assassinate, bluffs Assassin, gets caught.
     * The 3 coins are still gone. */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    r.players[pid].coins = 5;
    /* Force hand to NOT have Assassin */
    r.players[pid].cards[0] = COUP_RULES_CHAR_DUKE;
    r.players[pid].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Should have paid 3 already */
    CUI_ASSERT_EQ(2, (int)r.players[pid].coins);

    /* Target challenges */
    coup_input_t chal = make_response(target, COUP_RULES_RESP_CHALLENGE);
    emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Defender (actor) loses influence for bluffing */
    coup_input_t lose = make_lose_influence(pid, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Coins NOT refunded — still 2 */
    CUI_ASSERT_EQ(2, (int)r.players[pid].coins);
}

/* ======================================================================
 * Phase 6: Block Resolution
 * ====================================================================== */

CUI_TEST(steal_blocked_by_captain)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    uint8_t old_actor = r.players[pid].coins;
    uint8_t old_target = r.players[target].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Pass challenge */
    all_pass(&r);

    /* Target blocks with Captain */
    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_CAPTAIN);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* All pass block-challenge */
    all_pass(&r);

    /* No coins transferred */
    CUI_ASSERT_EQ((int)old_actor, (int)r.players[pid].coins);
    CUI_ASSERT_EQ((int)old_target, (int)r.players[target].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(steal_blocked_by_ambassador)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    uint8_t old_target = r.players[target].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_STEAL, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    all_pass(&r);

    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_AMBASSADOR);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    all_pass(&r);

    CUI_ASSERT_EQ((int)old_target, (int)r.players[target].coins);
}

CUI_TEST(assassinate_blocked_by_contessa)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t target = (pid + 1) % 2;
    r.players[pid].coins = 3;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_ASSASSINATE, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* 3 coins paid upfront */
    CUI_ASSERT_EQ(0, (int)r.players[pid].coins);

    /* Pass challenge */
    all_pass(&r);

    /* Target blocks with Contessa */
    coup_input_t block = make_response(target, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(target, COUP_RULES_CHAR_CONTESSA);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* All pass block-challenge */
    all_pass(&r);

    /* Target still alive — both cards unrevealed */
    CUI_ASSERT_TRUE(r.players[target].alive);
    CUI_ASSERT_FALSE(r.players[target].revealed[0]);
    CUI_ASSERT_FALSE(r.players[target].revealed[1]);

    /* Coins still gone */
    CUI_ASSERT_EQ(0, (int)r.players[pid].coins);
}

CUI_TEST(block_opens_challenge_window)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Someone blocks with Duke */
    uint8_t blocker = (pid + 1) % 4;
    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(blocker, COUP_RULES_CHAR_DUKE);
    int n = emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)COUP_TURN_BLOCK_CHALLENGE_WINDOW, (int)r.phase);

    /* BLOCK_CHALLENGE_OPENED event */
    int bci = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_OPENED);
    CUI_ASSERT_GE(bci, 0);
    CUI_ASSERT_EQ((int)blocker,
                   (int)evts[bci].data.block_challenge_opened.blocker_id);
    CUI_ASSERT_EQ(COUP_RULES_CHAR_DUKE,
                   (int)evts[bci].data.block_challenge_opened.claimed_char);

    /* Everyone except blocker can challenge the block */
    int i;
    for (i = 0; i < 4; i++) {
        if ((uint8_t)i == blocker) {
            CUI_ASSERT_FALSE(r.pending_responses[i]);
        } else {
            CUI_ASSERT_TRUE(r.pending_responses[i]);
        }
    }
}

CUI_TEST(block_challenge_blocker_has_card_block_stands)
{
    /* Foreign Aid → blocked by Duke → challenger challenges block →
     * blocker has Duke → challenger loses influence, action cancelled */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t blocker = (pid + 1) % 4;
    r.players[blocker].cards[0] = COUP_RULES_CHAR_DUKE;

    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(blocker, COUP_RULES_CHAR_DUKE);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* A third player challenges the block */
    uint8_t challenger = (pid + 2) % 4;
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Blocker had the card */
    int bri = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bri, 0);
    CUI_ASSERT_TRUE(evts[bri].data.block_challenge_result.blocker_had_card);

    /* Challenger loses influence */
    CUI_ASSERT_EQ((int)challenger, (int)r.influence_loser);

    /* After losing influence, block stands → action cancelled */
    coup_input_t lose = make_lose_influence(challenger, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ((int)old_coins, (int)r.players[pid].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

CUI_TEST(block_challenge_blocker_bluffing_action_proceeds)
{
    /* Foreign Aid → blocked by Duke → challenger challenges →
     * blocker doesn't have Duke → blocker loses influence, action proceeds */
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t blocker = (pid + 1) % 4;
    /* Blocker does NOT have Duke */
    r.players[blocker].cards[0] = COUP_RULES_CHAR_CAPTAIN;
    r.players[blocker].cards[1] = COUP_RULES_CHAR_CONTESSA;

    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(blocker, COUP_RULES_CHAR_DUKE);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    uint8_t challenger = (pid + 2) % 4;
    coup_input_t chal = make_response(challenger, COUP_RULES_RESP_CHALLENGE);
    int n = emit(&r, &chal, evts, COUP_RULES_MAX_EVENTS);

    /* Blocker was bluffing */
    int bri = find_event(evts, n, COUP_EVT_BLOCK_CHALLENGE_RESULT);
    CUI_ASSERT_GE(bri, 0);
    CUI_ASSERT_FALSE(evts[bri].data.block_challenge_result.blocker_had_card);

    /* Blocker loses influence */
    CUI_ASSERT_EQ((int)blocker, (int)r.influence_loser);

    coup_input_t lose = make_lose_influence(blocker, 0);
    emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Action proceeds — actor gained 2 coins */
    CUI_ASSERT_EQ((int)(old_coins + 2), (int)r.players[pid].coins);
}

CUI_TEST(block_unchallenged_cancels_action)
{
    coup_rules_t r;
    start_game(&r, 4, 42);

    uint8_t pid = coup_rules_current_player(&r);
    uint8_t old_coins = r.players[pid].coins;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_FOREIGN_AID, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    uint8_t blocker = (pid + 1) % 4;
    coup_input_t block = make_response(blocker, COUP_RULES_RESP_BLOCK);
    emit(&r, &block, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t claim = make_block_claim(blocker, COUP_RULES_CHAR_DUKE);
    emit(&r, &claim, evts, COUP_RULES_MAX_EVENTS);

    /* All pass the block-challenge */
    all_pass(&r);

    /* Action cancelled, no coins */
    CUI_ASSERT_EQ((int)old_coins, (int)r.players[pid].coins);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);
}

/* ======================================================================
 * Phase 7: Influence Loss + Elimination
 * ====================================================================== */

CUI_TEST(lose_influence_reveals_chosen_card)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;
    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Target chooses to lose card 1 */
    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_TRUE(r.players[target].revealed[1]);
    CUI_ASSERT_FALSE(r.players[target].revealed[0]);

    int li = find_event(evts, n, COUP_EVT_INFLUENCE_LOST);
    CUI_ASSERT_GE(li, 0);
    CUI_ASSERT_EQ((int)target, (int)evts[li].data.influence_lost.player_id);
    CUI_ASSERT_EQ(1, (int)evts[li].data.influence_lost.card_idx);
    CUI_ASSERT_EQ((int)r.players[target].cards[1],
                   (int)evts[li].data.influence_lost.revealed_char);
}

CUI_TEST(lose_influence_auto_if_one_card)
{
    /* If player has only one unrevealed card, auto-reveal it */
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;
    uint8_t target = (pid + 1) % 2;

    /* Reveal target's card 0 already */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Target submits card 0 (already revealed) — engine should auto-pick card 1 */
    coup_input_t lose = make_lose_influence(target, 0);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    int li = find_event(evts, n, COUP_EVT_INFLUENCE_LOST);
    CUI_ASSERT_GE(li, 0);
    CUI_ASSERT_EQ(1, (int)evts[li].data.influence_lost.card_idx);
    CUI_ASSERT_TRUE(r.players[target].revealed[1]);
}

CUI_TEST(lose_influence_invalid_index_auto)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;
    uint8_t target = (pid + 1) % 2;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Invalid index 5 — engine should auto-pick card 0 */
    coup_input_t lose = make_lose_influence(target, 5);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    int li = find_event(evts, n, COUP_EVT_INFLUENCE_LOST);
    CUI_ASSERT_GE(li, 0);
    CUI_ASSERT_EQ(0, (int)evts[li].data.influence_lost.card_idx);
}

CUI_TEST(both_cards_lost_player_eliminated)
{
    coup_rules_t r;
    start_game(&r, 2, 42);

    uint8_t pid = coup_rules_current_player(&r);
    r.players[pid].coins = 7;
    uint8_t target = (pid + 1) % 2;

    /* Target already has one card revealed */
    r.players[target].revealed[0] = true;

    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t act = make_action(pid, COUP_RULES_ACT_COUP, target);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t lose = make_lose_influence(target, 1);
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_FALSE(r.players[target].alive);
    CUI_ASSERT_GE(find_event(evts, n, COUP_EVT_PLAYER_ELIMINATED), 0);
}

CUI_TEST(eliminated_player_skipped_in_turns)
{
    coup_rules_t r;
    start_game(&r, 3, 42);

    /* Kill player 1 by revealing both cards */
    r.players[1].revealed[0] = true;
    r.players[1].revealed[1] = true;
    r.players[1].alive = false;

    /* Player 0 takes income */
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    uint8_t p0 = coup_rules_current_player(&r);
    CUI_ASSERT_EQ(0, (int)p0);
    coup_input_t act = make_action(0, COUP_RULES_ACT_INCOME, 0xFF);
    emit(&r, &act, evts, COUP_RULES_MAX_EVENTS);

    /* Should skip player 1, go to player 2 */
    CUI_ASSERT_EQ(2, (int)coup_rules_current_player(&r));
}

CUI_TEST(last_player_standing_wins)
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
    int n = emit(&r, &lose, evts, COUP_RULES_MAX_EVENTS);

    /* Game over */
    int gi = find_event(evts, n, COUP_EVT_GAME_OVER);
    CUI_ASSERT_GE(gi, 0);
    CUI_ASSERT_EQ((int)pid, (int)evts[gi].data.game_over.winner_id);
    CUI_ASSERT_FALSE(r.game_active);
}
