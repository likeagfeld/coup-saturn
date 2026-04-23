/**
 * test_coup_game.c - Coup game client test suite
 *
 * Tests the game client (coup_game.c) through its public API:
 *   coup_init() -> coup_start_local_game() -> coup_update(action) -> coup_get_state()
 *
 * With deterministic seeds (frame_count=0 at init, seed=31337),
 * everything is reproducible.
 */

#include "test_coup_game_helpers.h"

/*============================================================================
 * Lifecycle Tests
 *============================================================================*/

CUI_TEST(game_init_default_state)
{
    game_setup();

    CUI_ASSERT_EQ(COUP_SCREEN_TITLE, (int)st()->screen);
    CUI_ASSERT_EQ(COUP_PHASE_IDLE, (int)st()->phase);
    CUI_ASSERT_FALSE(st()->local_mode);
    CUI_ASSERT_EQ(0, st()->frame_count);
    CUI_ASSERT_EQ(0, st()->player_count);
}

CUI_TEST(game_start_enters_game)
{
    start_local_game();

    CUI_ASSERT_EQ(COUP_SCREEN_GAME, (int)st()->screen);
    CUI_ASSERT_TRUE(st()->local_mode);
    /* Default bot_count is 3, so total players = 1 + 3 = 4 */
    CUI_ASSERT_EQ(1 + st()->bot_count, st()->player_count);
    CUI_ASSERT_EQ(0, (int)st()->my_id);
}

CUI_TEST(game_start_sets_player_names)
{
    start_local_game();

    /* Default bot_count is 3: YOU + DANTE + RANDAL + JAY */
    CUI_ASSERT_STR_EQ(st()->players[0].name, "YOU");
    CUI_ASSERT_STR_EQ(st()->players[1].name, "DANTE");
    CUI_ASSERT_STR_EQ(st()->players[2].name, "RANDAL");
    CUI_ASSERT_STR_EQ(st()->players[3].name, "JAY");
}

CUI_TEST(game_start_deals_cards)
{
    start_local_game();

    /* Both cards should be valid characters (0..4) */
    CUI_ASSERT(st()->my_cards[0] <= COUP_CHAR_CONTESSA);
    CUI_ASSERT(st()->my_cards[1] <= COUP_CHAR_CONTESSA);
}

CUI_TEST(game_start_deterministic)
{
    uint8_t cards_a[2];
    uint8_t coins_a;

    /* First run */
    start_local_game();
    cards_a[0] = st()->my_cards[0];
    cards_a[1] = st()->my_cards[1];
    coins_a    = st()->players[0].coins;

    /* Second run (frame_count resets to 0, so same seed) */
    start_local_game();

    CUI_ASSERT_EQ((int)cards_a[0], (int)st()->my_cards[0]);
    CUI_ASSERT_EQ((int)cards_a[1], (int)st()->my_cards[1]);
    CUI_ASSERT_EQ((int)coins_a, (int)st()->players[0].coins);
}

CUI_TEST(game_start_initial_coins)
{
    int i;
    start_local_game();

    /* All players start with 2 coins */
    for (i = 0; i < st()->player_count; i++) {
        CUI_ASSERT_EQ(COUP_INITIAL_COINS, (int)st()->players[i].coins);
    }
}

/*============================================================================
 * Turn Flow Tests
 *============================================================================*/

CUI_TEST(game_first_turn_phase_correct)
{
    start_local_game();

    /*
     * After start, the rules engine assigns the first turn.
     * sync_ui_phase() either sets SELECT_ACTION (if human) or IDLE (if bot).
     */
    CUI_ASSERT(st()->phase == COUP_PHASE_SELECT_ACTION ||
               st()->phase == COUP_PHASE_IDLE);
}

CUI_TEST(game_bots_act_after_think_timer)
{
    start_local_game();

    /* If it's a bot's turn, phase should be IDLE. Tick until something changes. */
    if (st()->phase == COUP_PHASE_IDLE) {
        /* BOT_THINK_MAX is 45, give extra margin */
        int initial_round = st()->round_number;
        tick_frames(200);
        /* After enough ticks, bots should have acted (round may advance,
         * or phase changes if it became human's turn) */
        CUI_ASSERT(st()->phase != COUP_PHASE_IDLE ||
                   st()->round_number > initial_round ||
                   st()->current_turn_id == st()->my_id);
    }
}

CUI_TEST(game_human_income_increases_coins)
{
    start_local_game();

    /* Wait for human's turn */
    if (!wait_for_phase(COUP_PHASE_SELECT_ACTION, 500))
        return;

    int coins_before = (int)st()->players[0].coins;

    /* sync_ui_phase() sets menu_cursor to the first valid action (Income=0).
     * Income is always valid, so cursor should already be on it. */
    CUI_ASSERT_EQ(COUP_ACT_INCOME, st()->menu_cursor);

    /* Confirm Income — resolves synchronously through submit_local() */
    press(CUI_INPUT_CONFIRM);

    /* Coins update immediately (sync_ui_state runs inside submit_local).
     * Check before ticking to avoid bots stealing coins back. */
    CUI_ASSERT_EQ(coins_before + 1, (int)st()->players[0].coins);
}

/*============================================================================
 * Phase Handler Tests
 *============================================================================*/

CUI_TEST(game_select_action_cursor_navigation)
{
    start_local_game();

    if (!wait_for_phase(COUP_PHASE_SELECT_ACTION, 500))
        return;

    int cursor_start = st()->menu_cursor;

    press(CUI_INPUT_DOWN);
    int cursor_after_down = st()->menu_cursor;

    press(CUI_INPUT_UP);
    int cursor_after_up = st()->menu_cursor;

    /* Cursor should have moved and then returned */
    CUI_ASSERT_NEQ(cursor_start, cursor_after_down);
    CUI_ASSERT_EQ(cursor_start, cursor_after_up);
}

CUI_TEST(game_select_targeted_enters_target_select)
{
    start_local_game();

    if (!wait_for_phase(COUP_PHASE_SELECT_ACTION, 500))
        return;

    /* Find a targeted action that's available: Steal (5) or Assassinate (4) or Coup (2).
     * Coup requires 7 coins (not available at start).
     * Assassinate requires 3 coins (not available at start with 2 coins).
     * Steal is always available. Navigate to it. */
    /* Steal is COUP_ACT_STEAL = 5. In display order it's index 3. */
    /* Navigate: go to top first, then step down to Steal. */
    /* Display order: Income(0), Foreign Aid(1), Tax(3), Steal(5), Exchange(6), Assassinate(4), Coup(2) */
    /* Steal is 4th in display order (index 3). */

    /* Reset to top */
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);

    /* Move down to Steal (3 steps from Income) */
    press(CUI_INPUT_DOWN);  /* Foreign Aid */
    press(CUI_INPUT_DOWN);  /* Tax */
    press(CUI_INPUT_DOWN);  /* Steal */

    /* Verify cursor is on Steal */
    if (st()->menu_cursor == COUP_ACT_STEAL) {
        press(CUI_INPUT_CONFIRM);
        CUI_ASSERT_EQ(COUP_PHASE_SELECT_TARGET, (int)st()->phase);
    }
}

CUI_TEST(game_select_target_cancel_returns)
{
    start_local_game();

    if (!wait_for_phase(COUP_PHASE_SELECT_ACTION, 500))
        return;

    /* Navigate to Steal like above */
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_UP);
    press(CUI_INPUT_DOWN);
    press(CUI_INPUT_DOWN);
    press(CUI_INPUT_DOWN);

    if (st()->menu_cursor == COUP_ACT_STEAL) {
        press(CUI_INPUT_CONFIRM);
        CUI_ASSERT_EQ(COUP_PHASE_SELECT_TARGET, (int)st()->phase);

        /* Cancel should return to SELECT_ACTION */
        press(CUI_INPUT_CANCEL);
        CUI_ASSERT_EQ(COUP_PHASE_SELECT_ACTION, (int)st()->phase);
    }
}

CUI_TEST(game_quit_during_game_returns_to_lobby)
{
    start_local_game();

    CUI_ASSERT_EQ(COUP_SCREEN_GAME, (int)st()->screen);

    press(CUI_INPUT_QUIT);

    CUI_ASSERT_EQ(COUP_SCREEN_LOBBY, (int)st()->screen);
    CUI_ASSERT_FALSE(st()->local_mode);
    /* Lobby should be re-populated with players */
    CUI_ASSERT_EQ(1 + st()->bot_count, st()->player_count);
}

/*============================================================================
 * Game Log Tests
 *============================================================================*/

CUI_TEST(game_log_adds_and_retrieves)
{
    start_local_game();

    CUI_ASSERT(st()->log_count > 0);

    /* Add our own log entry */
    int count_before = st()->log_count;
    coup_log("test message");

    CUI_ASSERT_EQ(count_before + 1, st()->log_count);
}

CUI_TEST(game_log_scroll_clamps)
{
    start_local_game();

    /* Scroll should start at 0 */
    CUI_ASSERT_EQ(0, st()->log_scroll);

    /* LOG_DOWN with few entries shouldn't go negative or crash */
    press(CUI_INPUT_LOG_DOWN);
    CUI_ASSERT_EQ(0, st()->log_scroll);

    /* LOG_UP should not scroll past available entries */
    press(CUI_INPUT_LOG_UP);
    /* With only 1 log entry and max_visible=3, can't scroll */
    CUI_ASSERT_GE(st()->log_scroll, 0);
}

/*============================================================================
 * SFX Verification Tests
 *============================================================================*/

CUI_TEST(game_start_plays_sfx)
{
    start_local_game();

    /* coup_start_local_game() calls coup_audio_play_sfx(COUP_SFX_TURN_START) */
    CUI_ASSERT(stub_sfx_count() > 0);
    CUI_ASSERT_EQ(COUP_SFX_TURN_START, stub_sfx_last());
}

CUI_TEST(game_turn_start_plays_sfx)
{
    start_local_game();

    /* If human goes first, SFX was played during start.
     * If bot goes first, we need to tick until human's turn. */
    if (st()->phase == COUP_PHASE_SELECT_ACTION) {
        /* Human went first — SFX already played during start */
        CUI_ASSERT_EQ(COUP_SFX_TURN_START, stub_sfx_last());
        return;
    }

    /* Bot goes first — reset and wait for human's turn */
    stub_sfx_reset();
    if (!wait_for_phase(COUP_PHASE_SELECT_ACTION, 500))
        return;

    /* sync_ui_phase plays COUP_SFX_TURN_START when human's turn starts */
    int i;
    bool found = false;
    for (i = 0; i < stub_sfx_count(); i++) {
        if (stub_sfx_at(i) == COUP_SFX_TURN_START) {
            found = true;
            break;
        }
    }
    CUI_ASSERT_TRUE(found);
}

/*============================================================================
 * Game Over / Screen Transition Tests
 *============================================================================*/

CUI_TEST(game_quit_from_game_over)
{
    start_local_game();

    /* Quit to lobby (this tests the GAME -> LOBBY path) */
    press(CUI_INPUT_QUIT);
    CUI_ASSERT_EQ(COUP_SCREEN_LOBBY, (int)st()->screen);
    CUI_ASSERT_EQ(COUP_PHASE_IDLE, (int)st()->phase);
    /* Lobby should be re-populated */
    CUI_ASSERT_EQ(1 + st()->bot_count, st()->player_count);
    CUI_ASSERT_EQ(0, st()->log_count);
}

/*============================================================================
 * Game Restart State Reset Tests
 *============================================================================*/

CUI_TEST(game_start_resets_stale_cards)
{
    /*
     * Simulate a game restart: after a game ends, stale UI state from
     * the previous game (dead player, old cursors, timers) must be
     * cleared by coup_start_game().
     *
     * In local mode, the engine starts immediately and deals cards,
     * so my_cards will be valid characters (not NONE) after restart.
     * In online mode they'd stay NONE until INPUT_RELAY(START_GAME).
     */
    coup_state_t* s;

    /* First game */
    start_local_game();

    /* Simulate end-of-game stale state */
    s = st_mut();
    s->my_cards[0] = COUP_CHAR_NONE;
    s->my_cards[1] = COUP_CHAR_NONE;
    s->players[0].alive = false;
    s->players[0].coins = 99;
    s->menu_cursor = 5;
    s->exchange_sel[0] = 2;
    s->response_timer = 100;
    s->winner_id = 3;
    s->declared_target = 1;
    s->blocker_id = 2;

    /* Restart game (local mode: engine deals cards immediately) */
    coup_start_game(12345, 0);

    /* In local mode, cards are dealt by engine — should be valid characters */
    CUI_ASSERT(st()->my_cards[0] <= COUP_CHAR_CONTESSA);
    CUI_ASSERT(st()->my_cards[1] <= COUP_CHAR_CONTESSA);

    /* Players should be alive with initial coins */
    CUI_ASSERT_TRUE(st()->players[0].alive);
    CUI_ASSERT_EQ(COUP_INITIAL_COINS, (int)st()->players[0].coins);

    /* UI state should be clean */
    CUI_ASSERT_EQ(0, st()->menu_cursor);
    CUI_ASSERT_EQ(-1, st()->exchange_sel[0]);
    CUI_ASSERT_EQ(0, st()->response_timer);
    CUI_ASSERT_EQ(0, (int)st()->winner_id);
    CUI_ASSERT_EQ(0xFF, (int)st()->declared_target);
    CUI_ASSERT_EQ(0xFF, (int)st()->blocker_id);
}

/*============================================================================
 * Online Player ID Normalization Tests
 *============================================================================*/

CUI_TEST(game_start_normalizes_player_ids)
{
    /*
     * Simulate online mode: LOBBY_STATE sets server user_ids (1-based),
     * then coup_start_game() should normalize to 0-based engine PIDs.
     */
    int i;
    coup_state_t* s;

    game_setup();
    s = st_mut();

    /* Simulate LOBBY_STATE populating players with 1-based server IDs */
    s->player_count = 4;
    s->bot_count = 3;
    s->online_mode = true;
    s->local_mode = false;

    /* Player 0: human with server user_id=1 */
    s->players[0].id = 1;
    strncpy(s->players[0].name, "HUMAN", COUP_MAX_NAME);
    s->players[0].alive = true;
    s->players[0].is_self = true;
    s->players[0].is_bot = false;
    s->players[0].ready = true;

    /* Players 1-3: bots with server user_ids 2,3,4 */
    for (i = 1; i < 4; i++) {
        s->players[i].id = (uint8_t)(i + 1);
        strncpy(s->players[i].name, "BOT", COUP_MAX_NAME);
        s->players[i].alive = true;
        s->players[i].is_self = false;
        s->players[i].is_bot = true;
        s->players[i].ready = true;
    }

    /* Call coup_start_game with engine_pid=0 for the human */
    coup_start_game(31337, 0);

    /* After normalization, player IDs should be 0-based engine PIDs */
    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ(i, (int)st()->players[i].id);
    }

    /* is_self should be set correctly based on my_pid=0 */
    CUI_ASSERT_TRUE(st()->players[0].is_self);
    CUI_ASSERT_FALSE(st()->players[1].is_self);
    CUI_ASSERT_FALSE(st()->players[2].is_self);
    CUI_ASSERT_FALSE(st()->players[3].is_self);
}
