/**
 * test_game_over_winner.c - The game-over screen must know who actually won.
 *
 * WHY THIS TEST EXISTS
 *   The VICTORY banner never appeared. A winning player was shown DEFEAT.
 *
 *   The COUP_EVT_GAME_OVER handler sets screen, phase and winner_name, but
 *   never assigns g_state.winner_id - and winner_id is precisely what the
 *   renderer tests to choose between the two banners:
 *
 *       won = (me && me->id == st->winner_id);
 *
 *   So the banner was decided by whatever winner_id held from init, which is
 *   0. The only player who could ever see VICTORY was the one whose id was 0,
 *   and only by coincidence.
 *
 *   Nothing caught this because winner_NAME was set correctly from the same
 *   event, so every log line and every heading said the right thing while the
 *   banner said the opposite. The two fields disagreed and nothing compared
 *   them.
 *
 *   That comparison is this test: at game over, the player identified by
 *   winner_id must be the same player named by winner_name. One field is
 *   checkable against the other, so neither can drift alone again.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "test_coup_game_helpers.h"

#include <string.h>

/* A local game against bots always reaches a winner. 20,000 frames is far
 * more than a game needs and still bounds a hang. */
#define MAX_FRAMES 20000

static const coup_player_t* player_by_id(const coup_state_t* s, uint8_t id)
{
    int i;
    for (i = 0; i < s->player_count; i++) {
        if (s->players[i].id == id) {
            return &s->players[i];
        }
    }
    return NULL;
}

static bool play_to_game_over(void)
{
    int f;
    start_local_game();
    for (f = 0; f < MAX_FRAMES; f++) {
        if (st()->screen == COUP_SCREEN_GAME_OVER) {
            return true;
        }
        /* The human seat has to act or the game never advances - a bare tick
         * loop sits forever on our own turn. Confirming the highlighted
         * option every few frames plays a valid, if unambitious, game; which
         * action is chosen does not matter here, only that a game finishes
         * and produces a winner. */
        if ((f % 3) == 0) {
            press(CUI_INPUT_CONFIRM);
        }
        tick_frames(1);
    }
    return false;
}

CUI_TEST(game_over_winner_id_identifies_a_real_player)
{
    const coup_player_t* w;

    CUI_ASSERT_TRUE(play_to_game_over());

    /* Not merely "some value" - it must name a seat that exists. A stale 0
     * would pass a null check on most tables, which is why the identity test
     * below is the one that matters. */
    w = player_by_id(st(), st()->winner_id);
    CUI_ASSERT(w != NULL);
}

CUI_TEST(game_over_winner_id_and_winner_name_agree)
{
    const coup_player_t* w;

    CUI_ASSERT_TRUE(play_to_game_over());

    w = player_by_id(st(), st()->winner_id);
    CUI_ASSERT(w != NULL);

    /* THE gate. winner_name is snapshotted straight from the event and was
     * always right; winner_id was never assigned and was always 0. Comparing
     * them is what exposes the disagreement the banner was built on. */
    CUI_ASSERT_EQ(0, strcmp(w->name, st()->winner_name));
}

CUI_TEST(the_result_survives_a_lobby_state_arriving_on_the_game_over_screen)
{
    /* THE regression test, and the one the first three miss.
     *
     * coup_start_game() normalises players[i].id to i, so during a game seat
     * index and id agree and every naive test passes. The failure needs a
     * LOBBY_STATE to arrive while the game-over screen is still up - which
     * really happens, when other players back out and the server re-broadcasts
     * - because that rewrites players[].id back to WIRE ids while my_id stays
     * a seat index until the player confirms.
     *
     * coup_game.c:946 and coup_render.c already carried comments about this
     * hazard for winner_name, which was snapshotted and stayed correct. The
     * win/lose flag was recomputed live from find_self(), so it did not, and
     * a winning player was shown DEFEAT beside their own name.
     *
     * This reproduces the documented corruption directly: take a finished
     * game, then rewrite the ids the way LOBBY_STATE does, and require the
     * recorded result to be unchanged. */
    coup_state_t* m;
    bool won_before;
    int i;

    CUI_ASSERT_TRUE(play_to_game_over());

    m = st_mut();
    won_before = m->i_won;

    /* Exactly what the LOBBY_STATE path does: ids come off the wire and no
     * longer equal seat indices, and is_self is recomputed against my_id -
     * which has NOT been restored to a wire id yet. */
    for (i = 0; i < m->player_count; i++) {
        m->players[i].id = (uint8_t)(100 + i);
        m->players[i].is_self = (m->players[i].id == m->my_id);
    }

    /* find_self() now finds nobody at all, which is what made the live
     * predicate collapse to "lost". */
    CUI_ASSERT_EQ(won_before, m->i_won);
}

CUI_TEST(game_over_winner_is_the_last_player_standing)
{
    /* Independent of both fields: Coup ends when one player still has
     * influence. Whoever that is must be the one winner_id points at, which
     * catches a winner_id that is set but set to the wrong seat - something
     * the name comparison alone would miss if the name were also wrong. */
    const coup_player_t* w;
    int i, alive = 0;

    CUI_ASSERT_TRUE(play_to_game_over());

    for (i = 0; i < st()->player_count; i++) {
        if (st()->players[i].alive) {
            alive++;
        }
    }
    CUI_ASSERT_EQ(1, alive);

    w = player_by_id(st(), st()->winner_id);
    CUI_ASSERT(w != NULL);
    CUI_ASSERT_TRUE(w->alive);
}
