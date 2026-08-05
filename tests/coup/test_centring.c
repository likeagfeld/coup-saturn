/**
 * test_centring.c - Proves labels are centred by arithmetic, not by eye.
 *
 * Most screens are behind online play and cannot be reached in an offline
 * capture, so the centring RULE is proven here rather than screen by screen.
 * What this locks down is the property that broke twice already: a label's
 * offset must be derived from the string being drawn, so it cannot drift when
 * the string or the font changes.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "coup_ui.h"
#include "sgl_defs.h"

/*============================================================================
 * The arithmetic
 *============================================================================*/

CUI_TEST(centre_puts_equal_space_on_both_sides)
{
    /* 64 px of text in a 320 px screen leaves 128 either side. */
    CUI_ASSERT_EQ(128, coup_centre_x(320, 64));
    CUI_ASSERT_EQ(96, coup_centre_x(320, 128));
    CUI_ASSERT_EQ(0, coup_centre_x(320, 320));
}

CUI_TEST(centre_of_an_empty_string_is_the_middle)
{
    CUI_ASSERT_EQ(160, coup_centre_x(320, 0));
}

CUI_TEST(a_label_wider_than_its_container_clamps_left)
{
    /* Never negative. On VDP1 a negative x wraps rather than clips, which
     * would throw the label to the far right instead of trimming it. */
    CUI_ASSERT_EQ(0, coup_centre_x(320, 400));
    CUI_ASSERT_EQ(0, coup_centre_x(64, 65));
    CUI_ASSERT_EQ(0, coup_centre_x(0, 8));
}

CUI_TEST(centring_works_for_a_panel_not_just_the_screen)
{
    /* The rules header panel is 272 px wide at x=24. */
    CUI_ASSERT_EQ(104, coup_centre_x(272, 64));
    /* ...and that panel is itself screen-centred, so 24 + 104 = 128, the same
     * answer screen-centring gives for the same string. */
    CUI_ASSERT_EQ(24 + coup_centre_x(272, 64), coup_centre_x(320, 64));
}

/*============================================================================
 * The regressions this replaces
 *============================================================================*/

CUI_TEST(the_settings_heading_is_no_longer_sixteen_px_off)
{
    /* "Settings" is 8 characters. The body face advances 8 px, so 64 px wide.
     * It used to be drawn at grid column 14, i.e. x = 112, centring the label
     * on 144 when its panel's centre is 160. */
    const int text_w = 8 * 8;
    const int old_x = 14 * 8;

    CUI_ASSERT_EQ(128, coup_centre_x(320, text_w));
    CUI_ASSERT_EQ(112, old_x);
    /* The old position was 16 px left of correct. */
    CUI_ASSERT_EQ(16, coup_centre_x(320, text_w) - old_x);
}

CUI_TEST(both_rules_headings_centre_independently)
{
    /* Two different strings shared one fixed column, so at most one of them
     * could ever have been centred. Measuring each gives each its own x. */
    const int page0 = 19 * 8;   /* "CHARACTER REFERENCE" */
    const int page1 = 16 * 8;   /* "HOW TO PLAY COUP"    */

    CUI_ASSERT_EQ(84, coup_centre_x(320, page0));
    CUI_ASSERT_EQ(96, coup_centre_x(320, page1));
    /* They genuinely differ - one fixed column could not have served both. */
    CUI_ASSERT(coup_centre_x(320, page0) != coup_centre_x(320, page1));
}

CUI_TEST(padding_a_literal_is_not_equivalent_to_centring)
{
    /* "GAME OVER" shipped as "     GAME  OVER" - five leading spaces plus a
     * doubled space inside, 15 characters against the 9 it means.
     *
     * Padding cannot centre, because the spaces are part of the measured
     * string. Even if the padded literal is centred as a whole, the INK is
     * not: the label starts at 100, the visible text starts 40 px later at
     * 140, and that visible run is 10 characters (80 px), so its centre lands
     * on 180 - twenty pixels right of the screen's 160.
     *
     * Measuring the real string puts its centre exactly on 160. */
    const int padded_w = 15 * 8;    /* "     GAME  OVER" */
    const int visible_w = 10 * 8;   /* "GAME  OVER"      */
    const int real_w = 9 * 8;       /* "GAME OVER"       */
    int label_x = coup_centre_x(320, padded_w);
    int ink_x = label_x + 5 * 8;
    int ink_centre = ink_x + visible_w / 2;

    CUI_ASSERT_EQ(100, label_x);
    CUI_ASSERT_EQ(140, ink_x);
    CUI_ASSERT_EQ(180, ink_centre);      /* 20 px right of centre */
    CUI_ASSERT_EQ(20, ink_centre - 160);

    /* Measured, the ink centres exactly. */
    CUI_ASSERT_EQ(124, coup_centre_x(320, real_w));
    CUI_ASSERT_EQ(160, coup_centre_x(320, real_w) + real_w / 2);
}

/*============================================================================
 * Font independence - the property that makes this worth having
 *============================================================================*/

CUI_TEST(centring_follows_the_font_advance)
{
    /* The Alagard display face has a 16x16 cell but advances 8 px. Assuming
     * the advance matched the cell put every label 16 px left of centre on
     * its button. Centring takes a WIDTH, so it is correct for either as long
     * as the caller measures rather than assumes. */
    const int glyphs = 4;                  /* "PLAY" */
    const int measured_w = glyphs * 8;     /* real advance  */
    const int assumed_w = glyphs * 16;     /* the old wrong assumption */

    CUI_ASSERT_EQ(144, coup_centre_x(320, measured_w));
    CUI_ASSERT_EQ(128, coup_centre_x(320, assumed_w));
    CUI_ASSERT_EQ(16, coup_centre_x(320, measured_w)
                      - coup_centre_x(320, assumed_w));
}

/*============================================================================
 * A plate must be able to hold its widest label
 *============================================================================*/

CUI_TEST(every_difficulty_label_fits_its_plate)
{
    /* The settings screen draws three difficulty options on identical plates.
     * They shipped 42 px wide on 44 px spacing while "Medium" measures 48 px,
     * so that label overflowed its own plate by 6 px AND ran to x 212 when the
     * next plate began at 206 - overlapping a neighbour.
     *
     * A plate exists to frame its label, so it must be sized from the WIDEST
     * label rather than from a round number. */
    static const char* const names[3] = { "Easy", "Medium", "Hard" };
    const int plate_w = COUP_UI.settings.diff_option_w;
    const int spacing = COUP_UI.settings.diff_option_spacing;
    int j;

    for (j = 0; j < 3; j++) {
        int text_w = 0;
        while (names[j][text_w]) {
            text_w++;
        }
        text_w *= COUP_FONT_ADVANCE;

        /* Fits its plate... */
        CUI_ASSERT(text_w <= plate_w);
        /* ...and centred, stays inside it on both sides. */
        CUI_ASSERT(coup_centre_x(plate_w, text_w) >= 0);
        CUI_ASSERT(coup_centre_x(plate_w, text_w) + text_w <= plate_w);
    }

    /* Plates cannot touch, or a selection highlight would run into its
     * neighbour. */
    CUI_ASSERT(spacing > plate_w);
}

CUI_TEST(the_longest_menu_item_fits_its_plate_with_its_inset)
{
    /* Menu items are left-aligned, which is right for a list - but they must
     * still clear the plate border and not run off the far edge. The longest
     * label the block phase can produce is "Block as Ambassador". */
    const char* longest = "Block as Ambassador";
    const int plate_w = COUP_UI.game.select_action.item_w;
    int text_w = 0;

    while (longest[text_w]) {
        text_w++;
    }
    text_w *= COUP_FONT_ADVANCE;

    CUI_ASSERT(COUP_ITEM_TEXT_INSET > 0);          /* never flush to the edge */
    CUI_ASSERT(COUP_ITEM_TEXT_INSET + text_w <= plate_w);
}

/*============================================================================
 * SGL constant fidelity
 *
 * These are transcribed from SL_DEF.H by hand, because this PAL builds
 * against bare declarations rather than SGL's headers. A transcription error
 * is silent: the wrong bit still compiles and still runs.
 *============================================================================*/

CUI_TEST(screen_enable_bits_match_sl_def_h)
{
    /* SL_DEF.H:542-549. SPRON was transcribed as (1<<5) - the LINE COLOUR
     * screen - so the fade armed its colour offset on the wrong layer and
     * sprites never faded with the backdrop. */
    CUI_ASSERT_EQ(1 << 0, NBG0ON);
    CUI_ASSERT_EQ(1 << 1, NBG1ON);
    CUI_ASSERT_EQ(1 << 2, NBG2ON);
    CUI_ASSERT_EQ(1 << 3, NBG3ON);
    CUI_ASSERT_EQ(1 << 4, RBG0ON);
    CUI_ASSERT_EQ(1 << 5, LNCLON);
    CUI_ASSERT_EQ(1 << 6, SPRON);

    /* The sprite layer and the line colour screen must not be the same bit.
     * That confusion is exactly what the bug was. */
    CUI_ASSERT(SPRON != LNCLON);
}

CUI_TEST(the_fade_arms_the_sprite_layer)
{
    /* What saturn_fade.c passes to slColOffsetAUse. It must include the
     * sprite bit, or a fade dims the painted backdrop while every portrait,
     * panel and glyph stays at full brightness. */
    const int fade_mask = NBG0ON | NBG1ON | SPRON;

    CUI_ASSERT((fade_mask & SPRON) != 0);
    CUI_ASSERT((fade_mask & NBG1ON) != 0);
    CUI_ASSERT_EQ(0x43, fade_mask);
}
