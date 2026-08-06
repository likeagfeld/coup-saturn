/**
 * test_text_wrap.c - Wrapping is how a long label is shown IN FULL.
 *
 * USER REPORT: "cutting a player's name or a log line is not acceptable."
 *
 * The previous pass made the over-long labels fit by removing characters:
 * draw_text_fit() trimmed to the container, "%.10s" trimmed a name to ten,
 * safe_copy() trimmed a phase title to twenty. Every one of those made the
 * measurement green while making the screen wrong - the player is not shown
 * their name, and the log line stops before it says what happened.
 *
 * For the labels that are built from several words, the fix is to wrap them
 * onto a second row rather than to cut them. coup_wrap_row() is that wrap,
 * and it is arithmetic, so it is proved here on the host rather than looked
 * at in an emulator.
 *
 * WHAT THESE TESTS HAVE TO ESTABLISH, not just illustrate:
 *
 *   1. It never breaks inside a word. A word split in half is a truncation
 *      with extra steps; the wrap must not reintroduce the defect it exists
 *      to remove.
 *   2. No character is lost. Re-joining the rows with single spaces must
 *      reproduce the input (modulo runs of spaces at the breaks). This is the
 *      property that actually matters and the one a spot-check would miss.
 *   3. TWO ROWS ARE ALWAYS ENOUGH for every title the game can build. The
 *      layout hard-codes a two-row title bar (coup_render.c, coup_ui.h's
 *      GAME_TITLE_MAX_CHARS = 21), so a third row would be drawn outside its
 *      panel. That is proved EXHAUSTIVELY below over every name length and
 *      every character/action name, not asserted for one example.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "coup_ui.h"

#include <string.h>
#include <stdio.h>

#define WRAP_W GAME_TITLE_MAX_CHARS   /* 21 - see coup_ui.h */

/* Rejoin every row with a single space. */
static void rejoin(const char* src, int width, char* out, int out_sz)
{
    char row[64];
    int r = 0;
    out[0] = '\0';
    while (coup_wrap_row(src, width, r, row, sizeof(row)) > 0) {
        if (r > 0) {
            strncat(out, " ", out_sz - strlen(out) - 1);
        }
        strncat(out, row, out_sz - strlen(out) - 1);
        r++;
        if (r > 32) {
            break;              /* runaway guard - a failure, caught below */
        }
    }
}

/* Input with every run of spaces collapsed to one and the ends trimmed. */
static void squeeze(const char* src, char* out, int out_sz)
{
    int i = 0, o = 0;
    while (src[i] == ' ') i++;
    while (src[i] != '\0' && o < out_sz - 1) {
        if (src[i] == ' ') {
            while (src[i] == ' ') i++;
            if (src[i] == '\0') break;
            out[o++] = ' ';
        } else {
            out[o++] = src[i++];
        }
    }
    out[o] = '\0';
}

static int row_count(const char* src, int width)
{
    char row[64];
    int r = 0;
    while (coup_wrap_row(src, width, r, row, sizeof(row)) > 0) {
        r++;
        if (r > 32) break;
    }
    return r;
}

/*============================================================================
 * 1. The basics
 *============================================================================*/

CUI_TEST(wrap_short_text_is_one_row_and_row_one_is_empty)
{
    char row[64];

    CUI_ASSERT_EQ(11, coup_wrap_row("Waiting for", WRAP_W, 0, row,
                                    sizeof(row)));
    CUI_ASSERT_STR_EQ("Waiting for", row);

    CUI_ASSERT_EQ(0, coup_wrap_row("Waiting for", WRAP_W, 1, row,
                                   sizeof(row)));
    CUI_ASSERT_STR_EQ("", row);
}

CUI_TEST(wrap_breaks_on_a_space_never_inside_a_word)
{
    /* 15-char name + " claims " + "Ambassador" = 33 chars, wrap width 21.
     * A width-based cut would give "Bartholomew1234 claim" - the defect. */
    const char* title = "Bartholomew1234 claims Ambassador";
    char row[64];

    CUI_ASSERT_EQ(15, coup_wrap_row(title, WRAP_W, 0, row, sizeof(row)));
    CUI_ASSERT_STR_EQ("Bartholomew1234", row);

    CUI_ASSERT_EQ(17, coup_wrap_row(title, WRAP_W, 1, row, sizeof(row)));
    CUI_ASSERT_STR_EQ("claims Ambassador", row);

    CUI_ASSERT_EQ(0, coup_wrap_row(title, WRAP_W, 2, row, sizeof(row)));
}

CUI_TEST(wrap_never_starts_a_row_with_a_space)
{
    char row[64];
    int r;

    for (r = 0; r < 4; r++) {
        if (coup_wrap_row("a    b    c    d    e", 6, r, row,
                          sizeof(row)) <= 0) {
            break;
        }
        CUI_ASSERT(row[0] != ' ');
    }
}

CUI_TEST(wrap_emits_an_over_long_token_whole_rather_than_splitting_it)
{
    /* No caller can produce this, but if one ever does the token must survive
     * intact and be visibly too wide - not be silently cut in half, which is
     * the exact failure this whole change exists to remove. */
    char row[64];

    CUI_ASSERT_EQ(27, coup_wrap_row("Supercalifragilisticexpiali docious",
                                    10, 0, row, sizeof(row)));
    CUI_ASSERT_STR_EQ("Supercalifragilisticexpiali", row);
    CUI_ASSERT_EQ(7, coup_wrap_row("Supercalifragilisticexpiali docious",
                                   10, 1, row, sizeof(row)));
    CUI_ASSERT_STR_EQ("docious", row);
}

CUI_TEST(wrap_is_safe_on_null_and_on_a_zero_length_buffer)
{
    char row[8];

    CUI_ASSERT_EQ(0, coup_wrap_row(NULL, WRAP_W, 0, row, sizeof(row)));
    CUI_ASSERT_EQ(0, coup_wrap_row("text", WRAP_W, 0, NULL, 8));
    CUI_ASSERT_EQ(0, coup_wrap_row("text", WRAP_W, 0, row, 0));
    CUI_ASSERT_EQ(0, coup_wrap_row("text", 0, 0, row, sizeof(row)));
    CUI_ASSERT_EQ(0, coup_wrap_row("text", WRAP_W, -1, row, sizeof(row)));
}

/*============================================================================
 * 2. The property that matters: nothing is lost
 *============================================================================*/

CUI_TEST(wrap_loses_no_characters_for_any_width)
{
    static const char* inputs[] = {
        "Bartholomew1234 claims Ambassador",
        "Bartholomew1234 blocks w/ Ambassador",
        "Bartholomew1234: Foreign Aid",
        "Bartholomew1234: Assassinate",
        "Waiting for Bartholomew1234...",
        "Waiting for decision...",
        "Al blocks w/ Duke",
        "X: Tax",
    };
    const int n = (int)(sizeof(inputs) / sizeof(inputs[0]));
    char joined[128], expect[128];
    int i, w;

    for (i = 0; i < n; i++) {
        squeeze(inputs[i], expect, sizeof(expect));
        /* Every width from "one short word" up to "the whole thing fits". */
        for (w = 4; w <= 48; w++) {
            rejoin(inputs[i], w, joined, sizeof(joined));
            if (strcmp(expect, joined) != 0) {
                printf("      width %d: %s\n         in  %s\n         out %s\n",
                       w, inputs[i], expect, joined);
            }
            CUI_ASSERT_STR_EQ(expect, joined);
        }
    }
}

/*============================================================================
 * 3. Two rows are ENOUGH - exhaustively, not by example
 *
 * The response-phase title bar is drawn two rows tall. If any reachable title
 * needed three, the third would be painted outside the panel and over the
 * action list. So this walks EVERY name length the protocol allows against
 * EVERY character and action name, for all four title formats the game
 * builds, and requires <= 2 rows with every row within the wrap width.
 *============================================================================*/

CUI_TEST(every_reachable_phase_title_wraps_into_at_most_two_rows)
{
    char name[COUP_MAX_NAME];
    char title[64];
    char row[64];
    int len, ci, r, rows;
    int checked = 0;

    for (len = 1; len < COUP_MAX_NAME; len++) {
        int k;
        for (k = 0; k < len; k++) {
            name[k] = (char)('A' + (k % 26));
        }
        name[len] = '\0';

        for (ci = 0; ci < COUP_NUM_CHARACTERS; ci++) {
            const char* cname = coup_char_names[ci];
            int f;

            for (f = 0; f < 2; f++) {
                if (f == 0) {
                    snprintf(title, sizeof(title), "%s claims %s",
                             name, cname);
                } else {
                    snprintf(title, sizeof(title), "%s blocks w/ %s",
                             name, cname);
                }
                rows = row_count(title, WRAP_W);
                if (rows > 2) {
                    printf("      %d rows: '%s'\n", rows, title);
                }
                CUI_ASSERT(rows >= 1 && rows <= 2);
                for (r = 0; r < rows; r++) {
                    int n = coup_wrap_row(title, WRAP_W, r, row, sizeof(row));
                    if (n > WRAP_W) {
                        printf("      row %d is %d chars (max %d): '%s'\n",
                               r, n, WRAP_W, row);
                    }
                    CUI_ASSERT(n <= WRAP_W);
                }
                checked++;
            }
        }

        for (ci = 0; ci < COUP_NUM_ACTIONS; ci++) {
            snprintf(title, sizeof(title), "%s: %s", name,
                     coup_action_names[ci]);
            rows = row_count(title, WRAP_W);
            if (rows > 2) {
                printf("      %d rows: '%s'\n", rows, title);
            }
            CUI_ASSERT(rows >= 1 && rows <= 2);
            for (r = 0; r < rows; r++) {
                int n = coup_wrap_row(title, WRAP_W, r, row, sizeof(row));
                CUI_ASSERT(n <= WRAP_W);
            }
            checked++;
        }

        /* The idle/resolving line, same two-row budget. */
        snprintf(title, sizeof(title), "Waiting for %s...", name);
        rows = row_count(title, WRAP_W);
        if (rows > 2) {
            printf("      %d rows: '%s'\n", rows, title);
        }
        CUI_ASSERT(rows >= 1 && rows <= 2);
        checked++;
    }

    printf("      %d reachable titles checked, all <= 2 rows of <= %d\n",
           checked, WRAP_W);
    CUI_ASSERT(checked > 200);
}

/*============================================================================
 * 4. The name is never touched
 *
 * The specific user complaint. Whatever the wrap does with the surrounding
 * words, the player's name must appear in the output complete and contiguous.
 *============================================================================*/

CUI_TEST(the_players_whole_name_survives_every_title_format)
{
    char name[COUP_MAX_NAME];
    char title[64];
    char joined[128];
    int len;

    for (len = 1; len < COUP_MAX_NAME; len++) {
        int k;
        for (k = 0; k < len; k++) {
            name[k] = (char)('a' + (k % 26));
        }
        name[len] = '\0';

        snprintf(title, sizeof(title), "%s claims Ambassador", name);
        rejoin(title, WRAP_W, joined, sizeof(joined));
        CUI_ASSERT(strstr(joined, name) != NULL);

        snprintf(title, sizeof(title), "%s blocks w/ Contessa", name);
        rejoin(title, WRAP_W, joined, sizeof(joined));
        CUI_ASSERT(strstr(joined, name) != NULL);

        snprintf(title, sizeof(title), "Waiting for %s...", name);
        rejoin(title, WRAP_W, joined, sizeof(joined));
        CUI_ASSERT(strstr(joined, name) != NULL);
    }
}
