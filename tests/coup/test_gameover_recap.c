/**
 * test_gameover_recap.c - The game-over recap must read back in the order
 * the actions actually happened.
 *
 * USER REPORT: "the game-over recap shows actions out of order."
 *
 * WHAT WAS RULED OUT FIRST
 *   The ring APPEND (coup_game.c:394-417) is a textbook head/count ring and
 *   is correct: while log_count < COUP_LOG_LINES entries land at
 *   [0..count-1] with head pinned at 0; once full, each append overwrites
 *   slot `head` and advances it, so the OLDEST entry is always at `head` and
 *   chronological order is (head + n) % COUP_LOG_LINES.
 *
 *   The "three visible-row constants disagree" theory was also checked and
 *   is NOT the fault: COUP_GAMEOVER_RECAP_ROWS (coup.h:460) is 5 and the
 *   game screen's GL->max_visible (coup_ui.h:715) is 5, and each is used
 *   consistently by its own scroll clamp (coup_game.c:238 for the game
 *   screen, coup_game.c:2062 for the recap). The connecting screen's
 *   log_max_visible of 4 (coup_ui.h:616) belongs to a screen with no
 *   scrolling at all.
 *
 * THE ACTUAL FAULT
 *   The recap indexed the ring LINEARLY - `st->log[total - shown - scroll + i]`
 *   - i.e. it treated a chronological entry number as a slot number. That is
 *   only the same thing while the ring has never wrapped. With
 *   COUP_LOG_LINES == 6 a match produces far more than six log lines, so by
 *   game over the ring has always wrapped and every recap row is reading
 *   somebody else's entry. The game log on the game screen
 *   (coup_render.c:2136-2141) got this right; the recap did not.
 *
 * These tests drive real coup_log() appends and assert the recap's own
 * row->slot mapping, so they fail on the linear version and pass on the ring
 * version.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "test_coup_game_helpers.h"

#include <stdio.h>
#include <string.h>

/* Append n entries named E1..En through the real client-side logger. */
static void log_entries(int n)
{
    int i;
    char buf[16];
    for (i = 1; i <= n; i++) {
        snprintf(buf, sizeof(buf), "E%d", i);
        coup_log(buf);
    }
}

/** The text the recap would print on row `row`, or NULL for an empty row. */
static const char* recap_row_text(int row)
{
    const coup_state_t* s = st();
    int slot = coup_log_ring_index(s->log_head, s->log_count,
                                   COUP_GAMEOVER_RECAP_ROWS,
                                   s->log_scroll, row);
    return (slot < 0) ? NULL : s->log[slot];
}

static void fresh_log(void)
{
    game_setup();
    st_mut()->log_count = 0;
    st_mut()->log_head = 0;
    st_mut()->log_scroll = 0;
}

/*============================================================================
 * The buffer is 6 deep and the recap shows 5. Below the wrap the linear
 * index and the ring index coincide, so these two pass either way - they are
 * here to prove the ring mapping did not BREAK the un-wrapped case.
 *============================================================================*/

CUI_TEST(recap_shows_nothing_when_no_actions_were_logged)
{
    fresh_log();
    CUI_ASSERT_EQ(-1, coup_log_ring_index(0, 0, COUP_GAMEOVER_RECAP_ROWS, 0, 0));
}

CUI_TEST(recap_before_the_ring_wraps_is_chronological)
{
    fresh_log();
    log_entries(4);

    CUI_ASSERT_STR_EQ(recap_row_text(0), "E1");
    CUI_ASSERT_STR_EQ(recap_row_text(1), "E2");
    CUI_ASSERT_STR_EQ(recap_row_text(2), "E3");
    CUI_ASSERT_STR_EQ(recap_row_text(3), "E4");
    CUI_ASSERT_NULL(recap_row_text(4));   /* only four exist */
}

CUI_TEST(recap_exactly_full_buffer_drops_only_the_oldest)
{
    fresh_log();
    log_entries(6);   /* buffer holds all six; recap shows the newest five */

    CUI_ASSERT_STR_EQ(recap_row_text(0), "E2");
    CUI_ASSERT_STR_EQ(recap_row_text(4), "E6");
}

/*============================================================================
 * Past the wrap - this is the reported bug.
 *============================================================================*/

CUI_TEST(recap_after_the_ring_wraps_is_still_chronological)
{
    /* Nine appends into a six-slot ring. Slots end up
     *   [E7, E8, E9, E4, E5, E6]  with head == 3.
     * The newest five, in order, are E5 E6 E7 E8 E9 - which live in slots
     * 4, 5, 0, 1, 2. The linear index read slots 1..5 and produced
     * E8 E9 E4 E5 E6: two entries out of order, one (E7) missing, and a
     * three-appends-stale E4 shown as if it were recent. */
    fresh_log();
    log_entries(9);

    CUI_ASSERT_EQ(3, st()->log_head);
    CUI_ASSERT_EQ(COUP_LOG_LINES, st()->log_count);

    CUI_ASSERT_STR_EQ(recap_row_text(0), "E5");
    CUI_ASSERT_STR_EQ(recap_row_text(1), "E6");
    CUI_ASSERT_STR_EQ(recap_row_text(2), "E7");
    CUI_ASSERT_STR_EQ(recap_row_text(3), "E8");
    CUI_ASSERT_STR_EQ(recap_row_text(4), "E9");
}

CUI_TEST(recap_last_row_is_the_newest_entry_at_every_wrap_offset)
{
    /* The final row carries the gold ">" marker and is described in the
     * renderer as "the winning action", so it has to be the most recent
     * entry no matter where the ring head happens to be sitting. */
    int n;
    for (n = 1; n <= 20; n++) {
        char expect[16];
        fresh_log();
        log_entries(n);
        snprintf(expect, sizeof(expect), "E%d", n);

        {
            int shown = (st()->log_count < COUP_GAMEOVER_RECAP_ROWS)
                        ? st()->log_count : COUP_GAMEOVER_RECAP_ROWS;
            CUI_ASSERT_STR_EQ(recap_row_text(shown - 1), expect);
        }
    }
}

CUI_TEST(recap_scrolled_back_one_row_shows_the_previous_window)
{
    /* Scrolling is what the [UP/DOWN] hint offers. One step back must show
     * the five entries ending one earlier - still in order. */
    fresh_log();
    log_entries(9);
    st_mut()->log_scroll = 1;

    CUI_ASSERT_STR_EQ(recap_row_text(0), "E4");
    CUI_ASSERT_STR_EQ(recap_row_text(4), "E8");
}

CUI_TEST(recap_scroll_beyond_the_buffer_is_clamped_not_wrapped)
{
    /* coup_game.c clamps log_scroll, but a stale value can survive a state
     * reset, and reading slot (head - 99) would silently show garbage. */
    fresh_log();
    log_entries(9);
    st_mut()->log_scroll = 99;

    CUI_ASSERT_STR_EQ(recap_row_text(0), "E4");
    CUI_ASSERT_STR_EQ(recap_row_text(4), "E8");

    st_mut()->log_scroll = -5;
    CUI_ASSERT_STR_EQ(recap_row_text(4), "E9");
}

CUI_TEST(recap_row_outside_the_visible_window_is_empty)
{
    fresh_log();
    log_entries(9);

    CUI_ASSERT_EQ(-1, coup_log_ring_index(st()->log_head, st()->log_count,
                                          COUP_GAMEOVER_RECAP_ROWS, 0, -1));
    CUI_ASSERT_EQ(-1, coup_log_ring_index(st()->log_head, st()->log_count,
                                          COUP_GAMEOVER_RECAP_ROWS, 0,
                                          COUP_GAMEOVER_RECAP_ROWS));
}

/*============================================================================
 * The same helper now serves the game screen's log and the connecting
 * screen's list, which had their own copies of this arithmetic. Assert it
 * reproduces what those two were already doing correctly, so the
 * de-duplication cannot regress them.
 *============================================================================*/

CUI_TEST(ring_index_matches_the_game_logs_own_window_arithmetic)
{
    const int max_visible = 5;
    int head, count, scroll, i;

    for (count = 0; count <= COUP_LOG_LINES; count++) {
        for (head = 0; head < COUP_LOG_LINES; head++) {
            for (scroll = 0; scroll <= 3; scroll++) {
                int shown = (count < max_visible) ? count : max_visible;
                int clamped = scroll;
                int max_scroll = count - shown;

                if (clamped > max_scroll) clamped = max_scroll;
                if (clamped < 0) clamped = 0;

                for (i = 0; i < shown; i++) {
                    int expect = (head + count - shown - clamped + i)
                                 % COUP_LOG_LINES;
                    CUI_ASSERT_EQ(expect,
                        coup_log_ring_index(head, count, max_visible,
                                            scroll, i));
                }
            }
        }
    }
}
