/**
 * test_coup_lobby.c - Tests for Coup lobby state machine
 *
 * TDD: These tests define the lobby API before implementation.
 * The rule engine's LOBBY phase should handle:
 *   - Adding/removing players and bots
 *   - Ready state management
 *   - Game start validation (2+ ready required)
 *   - Card dealing at game start (not at init)
 */

#include "cui_test_framework.h"
#include "coup_rules.h"

#pragma GCC diagnostic ignored "-Wunused-function"

/* ======================================================================
 * Helpers
 * ====================================================================== */

static int submit(coup_rules_t* r, const coup_input_t* in,
                  coup_event_t* out, int max)
{
    return coup_rules_submit(r, in, out, max);
}

static coup_input_t make_add_player(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_ADD_PLAYER;
    return in;
}

static coup_input_t make_add_bot(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_ADD_BOT;
    return in;
}

static coup_input_t make_remove_player(uint8_t pid)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_REMOVE_PLAYER;
    in.player_id = pid;
    return in;
}

static coup_input_t make_set_ready(uint8_t pid, uint8_t ready)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_SET_READY;
    in.player_id = pid;
    in.data.set_ready.ready = ready;
    return in;
}

static coup_input_t make_start(void)
{
    coup_input_t in;
    memset(&in, 0, sizeof(in));
    in.type = COUP_INPUT_START_GAME;
    return in;
}

static int find_event(const coup_event_t* evts, int n, coup_event_type_t type)
{
    int i;
    for (i = 0; i < n; i++) {
        if (evts[i].type == type) return i;
    }
    return -1;
}

/* Helper: init lobby, add N players, set all ready, start game */
static void lobby_start_game(coup_rules_t* r, int players, uint32_t seed)
{
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    int i;
    coup_rules_init(r, seed);
    for (i = 0; i < players; i++) {
        coup_input_t add = make_add_player();
        submit(r, &add, evts, COUP_RULES_MAX_EVENTS);
        coup_input_t rdy = make_set_ready((uint8_t)i, 1);
        submit(r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    }
    coup_input_t start = make_start();
    submit(r, &start, evts, COUP_RULES_MAX_EVENTS);
}

/* ======================================================================
 * Init
 * ====================================================================== */

CUI_TEST(lobby_init_creates_empty_lobby)
{
    coup_rules_t r;
    coup_rules_init(&r, 42);
    CUI_ASSERT_EQ(0, r.player_count);
    CUI_ASSERT_EQ((int)COUP_TURN_LOBBY, (int)r.phase);
    CUI_ASSERT_FALSE(r.game_active);
}

/* ======================================================================
 * Add Player
 * ====================================================================== */

CUI_TEST(lobby_add_player_increments_count)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t add = make_add_player();
    int n = submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ(1, r.player_count);

    int idx = find_event(evts, n, COUP_EVT_PLAYER_JOINED);
    CUI_ASSERT_GE(idx, 0);
    CUI_ASSERT_EQ(0, (int)evts[idx].data.player_joined.player_id);
    CUI_ASSERT_EQ(0, (int)evts[idx].data.player_joined.is_bot);
}

CUI_TEST(lobby_add_player_defaults_not_ready)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_FALSE(r.players[0].ready);
    CUI_ASSERT_FALSE(r.players[0].is_bot);
}

/* ======================================================================
 * Add Bot
 * ====================================================================== */

CUI_TEST(lobby_add_bot_sets_bot_flag)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t add = make_add_bot();
    int n = submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ(1, r.player_count);
    CUI_ASSERT_TRUE(r.players[0].is_bot);
    CUI_ASSERT_TRUE(r.players[0].ready);

    int idx = find_event(evts, n, COUP_EVT_PLAYER_JOINED);
    CUI_ASSERT_GE(idx, 0);
    CUI_ASSERT_EQ(1, (int)evts[idx].data.player_joined.is_bot);
}

/* ======================================================================
 * Add Multiple
 * ====================================================================== */

CUI_TEST(lobby_add_multiple_players_and_bots)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* 3 humans + 2 bots */
    int i;
    for (i = 0; i < 3; i++) {
        coup_input_t add = make_add_player();
        submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    }
    for (i = 0; i < 2; i++) {
        coup_input_t add = make_add_bot();
        submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    }

    CUI_ASSERT_EQ(5, r.player_count);
    CUI_ASSERT_FALSE(r.players[0].is_bot);
    CUI_ASSERT_FALSE(r.players[1].is_bot);
    CUI_ASSERT_FALSE(r.players[2].is_bot);
    CUI_ASSERT_TRUE(r.players[3].is_bot);
    CUI_ASSERT_TRUE(r.players[4].is_bot);
}

CUI_TEST(lobby_add_player_rejects_when_full)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    int i;
    for (i = 0; i < COUP_RULES_MAX_PLAYERS; i++) {
        coup_input_t add = make_add_player();
        int n = submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
        CUI_ASSERT_GT(n, 0);
    }

    /* One more should fail */
    coup_input_t add = make_add_player();
    int n = submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
    CUI_ASSERT_EQ(COUP_RULES_MAX_PLAYERS, r.player_count);
}

/* ======================================================================
 * Remove Player
 * ====================================================================== */

CUI_TEST(lobby_remove_player_compacts_array)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* Add 3 players */
    int i;
    for (i = 0; i < 3; i++) {
        coup_input_t add = make_add_player();
        submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    }

    /* Set player 1 ready (so we can verify compaction) */
    coup_input_t rdy = make_set_ready(1, 1);
    submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);

    /* Remove player 0 — players 1,2 shift down to 0,1 */
    coup_input_t rem = make_remove_player(0);
    int n = submit(&r, &rem, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ(2, r.player_count);

    /* The player that was at index 1 (ready) is now at index 0 */
    CUI_ASSERT_TRUE(r.players[0].ready);

    int idx = find_event(evts, n, COUP_EVT_PLAYER_LEFT);
    CUI_ASSERT_GE(idx, 0);
    CUI_ASSERT_EQ(0, (int)evts[idx].data.player_left.player_id);
}

CUI_TEST(lobby_remove_bot)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* Human + bot */
    coup_input_t add_p = make_add_player();
    submit(&r, &add_p, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t add_b = make_add_bot();
    submit(&r, &add_b, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_EQ(2, r.player_count);

    /* Remove bot at index 1 */
    coup_input_t rem = make_remove_player(1);
    int n = submit(&r, &rem, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_EQ(1, r.player_count);
    CUI_ASSERT_FALSE(r.players[0].is_bot);
}

CUI_TEST(lobby_remove_invalid_returns_error)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* Empty lobby — any remove should fail */
    coup_input_t rem = make_remove_player(0);
    int n = submit(&r, &rem, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);

    /* Add one player, remove pid 5 */
    coup_input_t add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    rem = make_remove_player(5);
    n = submit(&r, &rem, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

/* ======================================================================
 * Set Ready
 * ====================================================================== */

CUI_TEST(lobby_set_ready_on)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t rdy = make_set_ready(0, 1);
    int n = submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_TRUE(r.players[0].ready);

    int idx = find_event(evts, n, COUP_EVT_READY_CHANGED);
    CUI_ASSERT_GE(idx, 0);
    CUI_ASSERT_EQ(0, (int)evts[idx].data.ready_changed.player_id);
    CUI_ASSERT_EQ(1, (int)evts[idx].data.ready_changed.ready);
}

CUI_TEST(lobby_set_ready_off)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    /* Set ready, then unset */
    coup_input_t rdy_on = make_set_ready(0, 1);
    submit(&r, &rdy_on, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_TRUE(r.players[0].ready);

    coup_input_t rdy_off = make_set_ready(0, 0);
    int n = submit(&r, &rdy_off, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_FALSE(r.players[0].ready);
}

CUI_TEST(lobby_set_ready_rejects_bot)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t add = make_add_bot();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_TRUE(r.players[0].ready); /* auto-ready */

    /* Trying to unready a bot should fail */
    coup_input_t rdy = make_set_ready(0, 0);
    int n = submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
    CUI_ASSERT_TRUE(r.players[0].ready); /* still ready */
}

CUI_TEST(lobby_set_ready_invalid_player)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t rdy = make_set_ready(0, 1);
    int n = submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

/* ======================================================================
 * Start Game
 * ====================================================================== */

CUI_TEST(lobby_start_rejects_insufficient_ready)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* 2 players but only 1 ready */
    coup_input_t add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t rdy = make_set_ready(0, 1);
    submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t start = make_start();
    int n = submit(&r, &start, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
    CUI_ASSERT_EQ((int)COUP_TURN_LOBBY, (int)r.phase);
}

CUI_TEST(lobby_start_rejects_zero_players)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    coup_input_t start = make_start();
    int n = submit(&r, &start, evts, COUP_RULES_MAX_EVENTS);
    CUI_ASSERT_EQ(-1, n);
}

CUI_TEST(lobby_start_with_two_ready)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* Add 2 players, both ready */
    int i;
    for (i = 0; i < 2; i++) {
        coup_input_t add = make_add_player();
        submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
        coup_input_t rdy = make_set_ready((uint8_t)i, 1);
        submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    }

    coup_input_t start = make_start();
    int n = submit(&r, &start, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_TRUE(r.game_active);
    CUI_ASSERT_EQ((int)COUP_TURN_WAITING_FOR_ACTION, (int)r.phase);

    int gs = find_event(evts, n, COUP_EVT_GAME_STARTED);
    CUI_ASSERT_GE(gs, 0);
    CUI_ASSERT_EQ(2, (int)evts[gs].data.game_started.player_count);

    int ts = find_event(evts, n, COUP_EVT_TURN_STARTED);
    CUI_ASSERT_GE(ts, 0);
}

CUI_TEST(lobby_start_with_bot_and_human)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* 1 human (ready) + 1 bot (auto-ready) = 2 ready */
    coup_input_t add_p = make_add_player();
    submit(&r, &add_p, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t rdy = make_set_ready(0, 1);
    submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS);
    coup_input_t add_b = make_add_bot();
    submit(&r, &add_b, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t start = make_start();
    int n = submit(&r, &start, evts, COUP_RULES_MAX_EVENTS);

    CUI_ASSERT_GT(n, 0);
    CUI_ASSERT_TRUE(r.game_active);
}

CUI_TEST(lobby_start_deals_cards)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_rules_init(&r, 42);

    /* Before start: no cards dealt */
    coup_input_t add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);
    add = make_add_player();
    submit(&r, &add, evts, COUP_RULES_MAX_EVENTS);

    /* Cards should be unset (CHAR_NONE or 0) before start */
    /* (memset to 0 means cards[0]=0=DUKE, but coins should be INITIAL) */

    coup_input_t rdy0 = make_set_ready(0, 1);
    coup_input_t rdy1 = make_set_ready(1, 1);
    submit(&r, &rdy0, evts, COUP_RULES_MAX_EVENTS);
    submit(&r, &rdy1, evts, COUP_RULES_MAX_EVENTS);

    coup_input_t start = make_start();
    submit(&r, &start, evts, COUP_RULES_MAX_EVENTS);

    /* After start: each player has 2 valid cards */
    int i;
    for (i = 0; i < 2; i++) {
        CUI_ASSERT_LT((int)r.players[i].cards[0], COUP_RULES_NUM_CHARACTERS);
        CUI_ASSERT_LT((int)r.players[i].cards[1], COUP_RULES_NUM_CHARACTERS);
        CUI_ASSERT_FALSE(r.players[i].revealed[0]);
        CUI_ASSERT_FALSE(r.players[i].revealed[1]);
        CUI_ASSERT_EQ(COUP_RULES_INITIAL_COINS, (int)r.players[i].coins);
        CUI_ASSERT_TRUE(r.players[i].alive);
    }

    /* Deck should be reduced by 4 cards (2 players * 2 cards) */
    CUI_ASSERT_EQ(COUP_RULES_DECK_SIZE - 4, r.deck_count);
}

CUI_TEST(lobby_start_deterministic_with_seed)
{
    coup_rules_t r1, r2;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    /* Same seed, same player setup → same cards */
    int i;
    for (i = 0; i < 2; i++) {
        coup_rules_t* r = (i == 0) ? &r1 : &r2;
        coup_rules_init(r, 12345);
        coup_input_t add = make_add_player();
        submit(r, &add, evts, COUP_RULES_MAX_EVENTS);
        add = make_add_player();
        submit(r, &add, evts, COUP_RULES_MAX_EVENTS);
        coup_input_t rdy0 = make_set_ready(0, 1);
        coup_input_t rdy1 = make_set_ready(1, 1);
        submit(r, &rdy0, evts, COUP_RULES_MAX_EVENTS);
        submit(r, &rdy1, evts, COUP_RULES_MAX_EVENTS);
        coup_input_t start = make_start();
        submit(r, &start, evts, COUP_RULES_MAX_EVENTS);
    }

    CUI_ASSERT_EQ((int)r1.players[0].cards[0], (int)r2.players[0].cards[0]);
    CUI_ASSERT_EQ((int)r1.players[0].cards[1], (int)r2.players[0].cards[1]);
    CUI_ASSERT_EQ((int)r1.players[1].cards[0], (int)r2.players[1].cards[0]);
    CUI_ASSERT_EQ((int)r1.players[1].cards[1], (int)r2.players[1].cards[1]);
}

/* ======================================================================
 * Lobby inputs rejected during game
 * ====================================================================== */

CUI_TEST(lobby_inputs_rejected_during_game)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];

    lobby_start_game(&r, 2, 42);
    CUI_ASSERT_TRUE(r.game_active);

    /* ADD_PLAYER during game → -1 */
    coup_input_t add = make_add_player();
    CUI_ASSERT_EQ(-1, submit(&r, &add, evts, COUP_RULES_MAX_EVENTS));

    /* ADD_BOT during game → -1 */
    coup_input_t bot = make_add_bot();
    CUI_ASSERT_EQ(-1, submit(&r, &bot, evts, COUP_RULES_MAX_EVENTS));

    /* SET_READY during game → -1 */
    coup_input_t rdy = make_set_ready(0, 0);
    CUI_ASSERT_EQ(-1, submit(&r, &rdy, evts, COUP_RULES_MAX_EVENTS));

    /* REMOVE_PLAYER during game → -1 */
    coup_input_t rem = make_remove_player(0);
    CUI_ASSERT_EQ(-1, submit(&r, &rem, evts, COUP_RULES_MAX_EVENTS));
}

/* ======================================================================
 * Full game via lobby flow
 * ====================================================================== */

CUI_TEST(lobby_full_game_income_to_coup)
{
    coup_rules_t r;
    coup_event_t evts[COUP_RULES_MAX_EVENTS];
    coup_input_t in;

    /* Start a 2-player game through lobby */
    lobby_start_game(&r, 2, 42);
    CUI_ASSERT_TRUE(r.game_active);
    CUI_ASSERT_EQ(2, r.player_count);

    /* Play income repeatedly until someone can coup */
    int turns;
    for (turns = 0; turns < 100 && r.game_active; turns++) {
        uint8_t pid = coup_rules_current_player(&r);
        uint8_t valid = coup_rules_valid_actions(&r);

        if (valid == (1 << COUP_RULES_ACT_COUP)) {
            /* Forced coup — target the other player */
            uint8_t target = (pid == 0) ? 1 : 0;
            memset(&in, 0, sizeof(in));
            in.type = COUP_INPUT_ACTION;
            in.player_id = pid;
            in.data.action.action = COUP_RULES_ACT_COUP;
            in.data.action.target_id = target;
            int n = submit(&r, &in, evts, COUP_RULES_MAX_EVENTS);
            CUI_ASSERT_GT(n, 0);

            /* Handle influence loss if requested */
            if (r.phase == COUP_TURN_WAITING_FOR_INFLUENCE_LOSS) {
                memset(&in, 0, sizeof(in));
                in.type = COUP_INPUT_LOSE_INFLUENCE;
                in.player_id = target;
                in.data.lose_influence.card_idx = 0;
                submit(&r, &in, evts, COUP_RULES_MAX_EVENTS);
            }
        } else {
            /* Income */
            memset(&in, 0, sizeof(in));
            in.type = COUP_INPUT_ACTION;
            in.player_id = pid;
            in.data.action.action = COUP_RULES_ACT_INCOME;
            in.data.action.target_id = 0xFF;
            submit(&r, &in, evts, COUP_RULES_MAX_EVENTS);
        }
    }

    /* Game should have ended (one player eliminated) */
    CUI_ASSERT_FALSE(r.game_active);
}
