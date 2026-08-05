/**
 * test_saturn_coinfx.c - Host tests for the coin payout arc/scale animation
 * and the timer-bar gouraud sweep (pal/saturn/saturn_coinfx.h).
 *
 * All math under test is pure integer arithmetic; the VRAM-touching draw
 * path is Saturn-only and is not exercised here (see saturn_coinfx.h's
 * #ifdef __SATURN__ guard on saturn_coinfx_draw).
 */

#include "cui_test_framework.h"
#include "saturn_coinfx.h"
#include "saturn_vdp1.h"

/*============================================================================
 * saturn_coinfx_clamp_scale_pct - the RED-firing gate for the VDP1
 * zero/negative-zoom precaution (ST-013-R3 p.74, VDP1_Manual.txt:3143-3144).
 *============================================================================*/

CUI_TEST(clamp_scale_never_reaches_zero_or_negative)
{
    /* Adversarial inputs: exactly zero, mildly negative, wildly negative. */
    CUI_ASSERT_GT(saturn_coinfx_clamp_scale_pct(0), 0);
    CUI_ASSERT_GT(saturn_coinfx_clamp_scale_pct(-1), 0);
    CUI_ASSERT_GT(saturn_coinfx_clamp_scale_pct(-500), 0);
    CUI_ASSERT_GT(saturn_coinfx_clamp_scale_pct(-999999), 0);

    CUI_ASSERT_EQ(SATURN_COINFX_MIN_SCALE_PCT, saturn_coinfx_clamp_scale_pct(0));
    CUI_ASSERT_EQ(SATURN_COINFX_MIN_SCALE_PCT, saturn_coinfx_clamp_scale_pct(-500));
}

CUI_TEST(clamp_scale_ceilings_at_peak)
{
    CUI_ASSERT_EQ(SATURN_COINFX_PEAK_SCALE_PCT, saturn_coinfx_clamp_scale_pct(101));
    CUI_ASSERT_EQ(SATURN_COINFX_PEAK_SCALE_PCT, saturn_coinfx_clamp_scale_pct(999999));
}

CUI_TEST(clamp_scale_passes_through_legal_range)
{
    CUI_ASSERT_EQ(1, saturn_coinfx_clamp_scale_pct(1));
    CUI_ASSERT_EQ(50, saturn_coinfx_clamp_scale_pct(50));
    CUI_ASSERT_EQ(100, saturn_coinfx_clamp_scale_pct(100));
}

/*============================================================================
 * saturn_coinfx_point - trajectory + scale
 *============================================================================*/

CUI_TEST(point_scale_never_zero_or_negative_across_a_step_fuzz_sweep)
{
    /* MEASURED regression target: step far beyond frames (a caller that
     * forgets to stop ticking after arrival) must not be allowed to drive
     * the scale formula negative. Sweep step from well before to well past
     * the flight window for several frame counts. */
    int frames_cases[3] = { 1, 30, 120 };
    int fi;

    for (fi = 0; fi < 3; fi++) {
        int frames = frames_cases[fi];
        int step;

        for (step = -50; step <= frames + 200; step++) {
            int x, y, scale;
            saturn_coinfx_point(step, frames, 0, 0, 100, 100, &x, &y, &scale);

            CUI_ASSERT_GT(scale, 0);
            CUI_ASSERT_LE(scale, SATURN_COINFX_PEAK_SCALE_PCT);
        }
    }
}

CUI_TEST(point_survives_zero_and_negative_frames)
{
    int x, y, scale;

    /* frames <= 0 must not divide by zero or crash. */
    saturn_coinfx_point(0, 0, 0, 0, 50, 50, &x, &y, &scale);
    CUI_ASSERT_GT(scale, 0);

    saturn_coinfx_point(5, -10, 0, 0, 50, 50, &x, &y, &scale);
    CUI_ASSERT_GT(scale, 0);
}

CUI_TEST(point_x_is_linear_interpolation)
{
    int x, y, scale;
    (void)y; (void)scale;

    saturn_coinfx_point(0, 10, 0, 0, 100, 0, &x, &y, &scale);
    CUI_ASSERT_EQ(0, x);

    saturn_coinfx_point(10, 10, 0, 0, 100, 0, &x, &y, &scale);
    CUI_ASSERT_EQ(100, x);

    saturn_coinfx_point(5, 10, 0, 0, 100, 0, &x, &y, &scale);
    CUI_ASSERT_EQ(50, x);
}

CUI_TEST(point_y_arcs_above_the_straight_line_at_the_midpoint)
{
    /* Coins must lift and fall, not slide in a straight line (task spec). */
    int x, y, scale;
    int mid_y, straight_line_y;

    saturn_coinfx_point(5, 10, 0, 100, 100, 100, &x, &y, &scale);
    mid_y = y;

    /* Straight-line Y at the midpoint would just be 100 (both endpoints are
     * y=100). The arced Y must be strictly ABOVE that (smaller y = higher
     * on a top-left-origin screen). */
    straight_line_y = 100;
    CUI_ASSERT_LT(mid_y, straight_line_y);

    /* At both endpoints, the arc contributes nothing - Y must land exactly
     * on the endpoints. */
    saturn_coinfx_point(0, 10, 0, 100, 100, 100, &x, &y, &scale);
    CUI_ASSERT_EQ(100, y);

    saturn_coinfx_point(10, 10, 0, 100, 100, 100, &x, &y, &scale);
    CUI_ASSERT_EQ(100, y);
}

CUI_TEST(point_scale_peaks_at_midflight_and_rests_at_the_endpoints)
{
    int x, y, start_scale, mid_scale, end_scale;
    (void)x; (void)y;

    saturn_coinfx_point(0, 10, 0, 0, 100, 0, &x, &y, &start_scale);
    saturn_coinfx_point(5, 10, 0, 0, 100, 0, &x, &y, &mid_scale);
    saturn_coinfx_point(10, 10, 0, 0, 100, 0, &x, &y, &end_scale);

    CUI_ASSERT_EQ(SATURN_COINFX_REST_SCALE_PCT, start_scale);
    CUI_ASSERT_EQ(SATURN_COINFX_REST_SCALE_PCT, end_scale);
    CUI_ASSERT_EQ(SATURN_COINFX_PEAK_SCALE_PCT, mid_scale);
    CUI_ASSERT_GT(mid_scale, start_scale);
}

/*============================================================================
 * saturn_coinfx_center_to_rect - ZP=0xA anchor emulation
 *============================================================================*/

CUI_TEST(center_to_rect_keeps_the_centre_fixed_as_scale_grows)
{
    /* ST-013-R3 p.73: ZP=0xA (Center-Center) keeps the sprite's centre
     * fixed while it grows/shrinks. saturn_vdp1_draw_sprite_scaled is
     * top-left anchored (saturn_vdp1.c:227-235), so this helper must
     * recompute the top-left corner from a FIXED centre as size changes. */
    int x_small, y_small, w_small, h_small;
    int x_big, y_big, w_big, h_big;
    int cx_small, cy_small, cx_big, cy_big;

    saturn_coinfx_center_to_rect(160, 112, 16, 16, 25,
                                  &x_small, &y_small, &w_small, &h_small);
    saturn_coinfx_center_to_rect(160, 112, 16, 16, 100,
                                  &x_big, &y_big, &w_big, &h_big);

    CUI_ASSERT_GT(w_big, w_small);
    CUI_ASSERT_GT(h_big, h_small);

    /* Recover the centre implied by each rectangle; it must not move. */
    cx_small = x_small + w_small / 2;
    cy_small = y_small + h_small / 2;
    cx_big   = x_big + w_big / 2;
    cy_big   = y_big + h_big / 2;

    CUI_ASSERT_EQ(cx_small, cx_big);
    CUI_ASSERT_EQ(cy_small, cy_big);
}

CUI_TEST(center_to_rect_at_100_percent_matches_base_size)
{
    int x, y, w, h;

    saturn_coinfx_center_to_rect(200, 150, 16, 24, 100, &x, &y, &w, &h);

    CUI_ASSERT_EQ(16, w);
    CUI_ASSERT_EQ(24, h);
    CUI_ASSERT_EQ(200 - 16 / 2, x);
    CUI_ASSERT_EQ(150 - 24 / 2, y);
}

/*============================================================================
 * Multi-coin pool - static allocation, staggered starts
 *============================================================================*/

CUI_TEST(payout_spawns_the_requested_coin_count)
{
    int spawned;

    saturn_coinfx_reset();
    spawned = saturn_coinfx_payout(10, 10, 200, 10, 3, 30);

    CUI_ASSERT_EQ(3, spawned);
    CUI_ASSERT_EQ(3, saturn_coinfx_active_count());
}

CUI_TEST(payout_is_capped_at_pool_capacity_and_never_allocates)
{
    int spawned;

    saturn_coinfx_reset();
    /* Request far more than SATURN_COINFX_MAX_COINS. */
    spawned = saturn_coinfx_payout(0, 0, 100, 0, 999, 30);

    CUI_ASSERT_EQ(SATURN_COINFX_MAX_COINS, spawned);
    CUI_ASSERT_EQ(SATURN_COINFX_MAX_COINS, saturn_coinfx_active_count());
}

CUI_TEST(payout_reads_as_three_coins_not_one_via_staggered_starts)
{
    /* At frame 0, only the first coin (no stagger delay) should be visible;
     * the other two are still waiting out their stagger offset. By the
     * time the last stagger delay has elapsed, all three are visible. */
    int i, x, y, scale;
    int visible_at_frame_0 = 0;
    int visible_at_last_stagger = 0;

    saturn_coinfx_reset();
    saturn_coinfx_payout(0, 0, 100, 0, 3, 30);

    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        if (saturn_coinfx_get(i, &x, &y, &scale)) {
            visible_at_frame_0++;
        }
    }
    CUI_ASSERT_EQ(1, visible_at_frame_0);

    /* Advance to just past the second stagger delay (2 * STAGGER frames). */
    for (i = 0; i < 2 * SATURN_COINFX_STAGGER_FRAMES; i++) {
        saturn_coinfx_tick();
    }

    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        if (saturn_coinfx_get(i, &x, &y, &scale)) {
            visible_at_last_stagger++;
        }
    }
    CUI_ASSERT_EQ(3, visible_at_last_stagger);
}

CUI_TEST(coins_deactivate_after_their_flight_completes)
{
    int i;

    saturn_coinfx_reset();
    saturn_coinfx_payout(0, 0, 100, 0, 1, 10);
    CUI_ASSERT_EQ(1, saturn_coinfx_active_count());

    /* 10 frames of flight; the coin should be gone well before frame 50. */
    for (i = 0; i < 50; i++) {
        saturn_coinfx_tick();
    }

    CUI_ASSERT_EQ(0, saturn_coinfx_active_count());
}

CUI_TEST(reset_clears_every_slot)
{
    int i, x, y, scale;

    saturn_coinfx_payout(0, 0, 100, 0, SATURN_COINFX_MAX_COINS, 30);
    CUI_ASSERT_EQ(SATURN_COINFX_MAX_COINS, saturn_coinfx_active_count());

    saturn_coinfx_reset();
    CUI_ASSERT_EQ(0, saturn_coinfx_active_count());

    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        CUI_ASSERT_FALSE(saturn_coinfx_get(i, &x, &y, &scale));
    }
}

/*============================================================================
 * saturn_coinfx_timer_colors - green -> amber -> red gouraud sweep
 *============================================================================*/

static int coinfx_test_red_channel(uint16_t w)   { return w & 0x1F; }
static int coinfx_test_green_channel(uint16_t w) { return (w >> 5) & 0x1F; }
static int coinfx_test_blue_channel(uint16_t w)  { return (w >> 10) & 0x1F; }

CUI_TEST(timer_colors_full_time_is_pure_green)
{
    uint16_t out[4];
    int i;

    saturn_coinfx_timer_colors(100, out);

    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ(0x00, coinfx_test_red_channel(out[i]));
        CUI_ASSERT_EQ(0x1F, coinfx_test_green_channel(out[i]));
        CUI_ASSERT_EQ(0x00, coinfx_test_blue_channel(out[i]));
    }
}

CUI_TEST(timer_colors_expired_is_pure_red)
{
    uint16_t out[4];
    int i;

    saturn_coinfx_timer_colors(0, out);

    for (i = 0; i < 4; i++) {
        CUI_ASSERT_EQ(0x1F, coinfx_test_red_channel(out[i]));
        CUI_ASSERT_EQ(0x00, coinfx_test_green_channel(out[i]));
        CUI_ASSERT_EQ(0x00, coinfx_test_blue_channel(out[i]));
    }
}

CUI_TEST(timer_colors_midpoint_is_amber)
{
    uint16_t out[4];

    saturn_coinfx_timer_colors(50, out);

    /* Amber = red and green both near their maximum, no blue. */
    CUI_ASSERT_EQ(0x1F, coinfx_test_red_channel(out[0]));
    CUI_ASSERT_EQ(0x1F, coinfx_test_green_channel(out[0]));
    CUI_ASSERT_EQ(0x00, coinfx_test_blue_channel(out[0]));
}

CUI_TEST(timer_colors_all_four_corners_match)
{
    /* This is a flat colour sweep, not a directional shade - pair with
     * saturn_vdp1_gouraud_vshade separately for lighting if wanted. */
    uint16_t out[4];

    saturn_coinfx_timer_colors(37, out);

    CUI_ASSERT_EQ(out[0], out[1]);
    CUI_ASSERT_EQ(out[0], out[2]);
    CUI_ASSERT_EQ(out[0], out[3]);
}

CUI_TEST(timer_colors_red_channel_is_monotonic_as_time_runs_out)
{
    /* The bar must only ever get redder as remaining_pct falls from 100
     * to 0 - never flicker back toward green. */
    int pct;
    int prev_red = -1;

    for (pct = 100; pct >= 0; pct--) {
        uint16_t out[4];
        int red;

        saturn_coinfx_timer_colors(pct, out);
        red = coinfx_test_red_channel(out[0]);

        CUI_ASSERT_GE(red, prev_red);
        prev_red = red;
    }
}

CUI_TEST(timer_colors_clamps_out_of_range_input)
{
    uint16_t out[4];

    /* Must not crash or produce channel values outside 0..0x1F. */
    saturn_coinfx_timer_colors(-50, out);
    CUI_ASSERT_EQ(0x1F, coinfx_test_red_channel(out[0]));
    CUI_ASSERT_EQ(0x00, coinfx_test_green_channel(out[0]));

    saturn_coinfx_timer_colors(500, out);
    CUI_ASSERT_EQ(0x00, coinfx_test_red_channel(out[0]));
    CUI_ASSERT_EQ(0x1F, coinfx_test_green_channel(out[0]));
}
