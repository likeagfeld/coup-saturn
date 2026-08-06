/**
 * test_winner_char.c - The game-over screen must know the winner's FACE.
 *
 * WHY THIS EXISTS
 *   Design doc 2026-08-04-saturn-visual-facelift-design.md section 4.2
 *   "Game over": "winner portrait scaled up with a gouraud spotlight." The
 *   portrait draw needs a COUP_CHAR_* to look up in coup_anim_loader, and
 *   the only source for "which character is the winner" is their hand at
 *   the moment they won - which coup_pick_winner_char() picks out and
 *   coup_game.c snapshots into st->winner_char at the SAME COUP_EVT_GAME_OVER
 *   moment winner_id/i_won/winner_name are already snapshotted, for the
 *   identical reason (test_game_over_winner.c): a LOBBY_STATE arriving while
 *   this screen is still up rewrites players[].id and corrupts anything
 *   computed live afterwards.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "test_coup_game_helpers.h"

#include <string.h>

/*============================================================================
 * coup_pick_winner_char() - pure
 *============================================================================*/

CUI_TEST(pick_winner_char_null_is_safe)
{
    CUI_ASSERT_EQ(COUP_CHAR_NONE, coup_pick_winner_char(NULL));
}

CUI_TEST(pick_winner_char_picks_the_first_real_character)
{
    uint8_t cards[COUP_CARDS_PER_PLAYER];
    cards[0] = COUP_CHAR_CAPTAIN;
    cards[1] = COUP_CHAR_CONTESSA;

    CUI_ASSERT_EQ(COUP_CHAR_CAPTAIN, coup_pick_winner_char(cards));
}

CUI_TEST(pick_winner_char_skips_a_lost_slot_to_find_the_survivor)
{
    uint8_t cards[COUP_CARDS_PER_PLAYER];
    cards[0] = COUP_CHAR_NONE;       /* lost this influence earlier */
    cards[1] = COUP_CHAR_AMBASSADOR;

    CUI_ASSERT_EQ(COUP_CHAR_AMBASSADOR, coup_pick_winner_char(cards));
}

CUI_TEST(pick_winner_char_skips_facedown_to_find_the_real_card)
{
    /* An opponent's hand is FACEDOWN on the wire even for a winner's
     * final-standing card, in every view except their own my_cards - this
     * proves the picker itself does not treat FACEDOWN as identifiable. */
    uint8_t cards[COUP_CARDS_PER_PLAYER];
    cards[0] = COUP_CHAR_FACEDOWN;
    cards[1] = COUP_CHAR_DUKE;

    CUI_ASSERT_EQ(COUP_CHAR_DUKE, coup_pick_winner_char(cards));
}

CUI_TEST(pick_winner_char_returns_none_when_nothing_is_identifiable)
{
    uint8_t cards[COUP_CARDS_PER_PLAYER];
    cards[0] = COUP_CHAR_FACEDOWN;
    cards[1] = COUP_CHAR_NONE;

    CUI_ASSERT_EQ(COUP_CHAR_NONE, coup_pick_winner_char(cards));
}

/*============================================================================
 * End to end: a real game snapshots a real, identifiable winner_char
 *============================================================================*/

#define MAX_FRAMES 20000

static bool play_to_game_over(void)
{
    int f;
    start_local_game();
    for (f = 0; f < MAX_FRAMES; f++) {
        if (st()->screen == COUP_SCREEN_GAME_OVER) {
            return true;
        }
        if ((f % 3) == 0) {
            press(CUI_INPUT_CONFIRM);
        }
        tick_frames(1);
    }
    return false;
}

CUI_TEST(winner_char_is_a_real_character_after_a_finished_game)
{
    CUI_ASSERT_TRUE(play_to_game_over());

    /* Not FACEDOWN, not NONE - the portrait loader needs an index into its
     * 5-character table (coup_anim_loader.h: 0=Duke..4=Contessa). */
    CUI_ASSERT(st()->winner_char < COUP_NUM_CHARACTERS);
}

CUI_TEST(winner_char_survives_a_lobby_state_arriving_on_the_game_over_screen)
{
    /* Same regression shape as
     * the_result_survives_a_lobby_state_arriving_on_the_game_over_screen in
     * test_game_over_winner.c: winner_char must already be a snapshot, not
     * something recomputed from players[] after a LOBBY_STATE has rewritten
     * their ids. */
    coup_state_t* m;
    uint8_t char_before;
    int i;

    CUI_ASSERT_TRUE(play_to_game_over());

    m = st_mut();
    char_before = m->winner_char;

    for (i = 0; i < m->player_count; i++) {
        m->players[i].id = (uint8_t)(100 + i);
        m->players[i].is_self = (m->players[i].id == m->my_id);
        /* A LOBBY_STATE zero-fills cards too - the field that would corrupt
         * a LIVE recomputation, which is exactly what this test rules out. */
        memset(m->players[i].cards, COUP_CHAR_DUKE, sizeof(m->players[i].cards));
    }

    CUI_ASSERT_EQ(char_before, m->winner_char);
}
