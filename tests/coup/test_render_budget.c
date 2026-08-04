/**
 * test_render_budget.c - VDP1 draw-call budget gate (G4-host)
 *
 * The Saturn renderer buffers one VDP1 command per draw_rect and one per
 * non-space character (saturn_vdp1.c: saturn_vdp1_draw_rect /
 * saturn_vdp1_draw_text). Command count and total fill area are therefore
 * pure functions of the platform-agnostic render path, and are measured
 * here on the host rather than on hardware.
 *
 * Budget context: the VDP1 draws 1 pixel per 28.6 MHz clock, giving roughly
 * 477,000 pixel-clocks per frame at 60 fps (ST-013-R3 VDP1 manual,
 * txt:1114-1115).
 *
 * NOTE: this file uses the bare CUI_TEST(name) { ... } form. Do not use
 * CUI_TEST_BEGIN/CUI_TEST_END - those macros expand `cui_current_test.name`
 * with the macro parameter substituted into the member name and cannot
 * compile. No test in this suite uses them.
 */

#include "cui_test_framework.h"
#include "mock_pal.h"
#include "cui_pal.h"
#include "coup.h"

#include <stdio.h>
#include <string.h>

/*============================================================================
 * Mock instrumentation self-tests
 *
 * The budget measurements are only trustworthy if the mock actually records
 * per-rect geometry. These two tests pin that contract.
 *============================================================================*/

CUI_TEST(mock_records_rect_geometry)
{
    cui_pal_register(cui_mock_platform());
    mock_pal_reset();

    CUI_DISPLAY()->draw_rect(11, 22, 33, 44, 0x11223344u);

    CUI_ASSERT_EQ(1, mock_pal_get_rect_call_count());

    mock_rect_call_t r = mock_pal_get_rect_call(0);
    CUI_ASSERT_EQ(11, r.x);
    CUI_ASSERT_EQ(22, r.y);
    CUI_ASSERT_EQ(33, r.w);
    CUI_ASSERT_EQ(44, r.h);
}

CUI_TEST(mock_rect_index_out_of_range_is_zeroed)
{
    cui_pal_register(cui_mock_platform());
    mock_pal_reset();

    mock_rect_call_t r = mock_pal_get_rect_call(0);
    CUI_ASSERT_EQ(0, r.x);
    CUI_ASSERT_EQ(0, r.w);
}

/*============================================================================
 * Per-screen VDP1 budget measurement
 *============================================================================*/

/** Sum the pixel area of every rect the renderer emitted this frame. */
static long mock_total_fill_px(void)
{
    long total = 0;
    int n = mock_pal_get_rect_call_count();
    for (int i = 0; i < n; i++) {
        mock_rect_call_t r = mock_pal_get_rect_call(i);
        total += (long)r.w * (long)r.h;
    }
    return total;
}

/** Populate a 6-player mid-game state: the worst case for seat rendering. */
static void build_busy_state(coup_state_t* st, coup_screen_t screen)
{
    memset(st, 0, sizeof(*st));
    st->screen = screen;
    st->phase = COUP_PHASE_SELECT_ACTION;
    st->player_count = 6;
    for (int i = 0; i < 6; i++) {
        st->players[i].id = (uint8_t)i;
        st->players[i].coins = 3;
        st->players[i].cards[0] = 5;
        st->players[i].cards[1] = 5;
        st->players[i].alive = true;
        snprintf(st->players[i].name, sizeof(st->players[i].name), "PLAYER%d", i);
    }
    st->players[0].is_self = true;
    st->my_id = 0;
    st->my_cards[0] = 0;
    st->my_cards[1] = 1;
    st->current_turn_id = 0;
}

/** Render one screen through the mock and report what it cost. */
static void measure(const char* label, coup_screen_t screen,
                    int* out_rects, long* out_fill)
{
    coup_state_t st;
    build_busy_state(&st, screen);

    cui_pal_register(cui_mock_platform());
    mock_pal_reset();
    coup_render_screen(&st);

    *out_rects = mock_pal_get_rect_call_count();
    *out_fill = mock_total_fill_px();
    printf("  [budget] %-10s rects=%3d fill_px=%6ld\n",
           label, *out_rects, *out_fill);
}

/*
 * Per-screen ceilings.
 *
 * MEASURED 2026-08-04 on the pre-facelift build (commit acd7de5), plus ~15%
 * headroom:
 *
 *     title      rects=  3  fill_px= 89280
 *     lobby      rects= 16  fill_px=158848
 *     game_over  rects=  1  fill_px= 71680
 *     game       rects= 16  fill_px=142624
 *
 * A full-screen rect is 320*224 = 71680 px, which is why game_over measures
 * exactly that and why it is half of the game screen's total. Phase 1a moves
 * that background to VDP2 NBG1, so the game and game_over ceilings must be
 * RATCHETED DOWN once it lands - a budget that is never tightened stops being
 * a gate. Raise a ceiling only deliberately, never to make a regression pass.
 */
static const struct {
    const char*    label;
    coup_screen_t  screen;
    int            max_rects;
    long           max_fill;
} k_budgets[] = {
    { "title",     COUP_SCREEN_TITLE,      4, 103000 },
    { "lobby",     COUP_SCREEN_LOBBY,     19, 183000 },
    { "game_over", COUP_SCREEN_GAME_OVER,  2,  82500 },
    { "game",      COUP_SCREEN_GAME,      19, 164000 },
};

CUI_TEST(vdp1_budget_per_screen)
{
    for (size_t i = 0; i < sizeof(k_budgets) / sizeof(k_budgets[0]); i++) {
        int rects;
        long fill;

        measure(k_budgets[i].label, k_budgets[i].screen, &rects, &fill);

        CUI_ASSERT(rects <= k_budgets[i].max_rects);
        CUI_ASSERT(fill <= k_budgets[i].max_fill);
    }
}
