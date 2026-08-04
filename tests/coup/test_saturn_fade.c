/**
 * test_saturn_fade.c - Host tests for the VDP2 colour-offset fade module.
 *
 * The ramp arithmetic and state machine are platform-independent; only the
 * register write is Saturn-only. Gate G3 covers the on-screen result.
 */

#include "cui_test_framework.h"
#include "saturn_fade.h"

CUI_TEST(fade_interp_hits_both_ends)
{
    CUI_ASSERT_EQ(0,   saturn_fade_interp(0, 255, 0, 16));
    CUI_ASSERT_EQ(255, saturn_fade_interp(0, 255, 16, 16));
    /* Past the end stays clamped at the target, never overshoots. */
    CUI_ASSERT_EQ(255, saturn_fade_interp(0, 255, 99, 16));
}

CUI_TEST(fade_interp_is_monotonic)
{
    int prev = -1;
    for (int step = 0; step <= 16; step++) {
        int v = saturn_fade_interp(0, 255, step, 16);
        CUI_ASSERT(v >= prev);
        prev = v;
    }
}

CUI_TEST(fade_interp_handles_reverse_ramp)
{
    CUI_ASSERT_EQ(255, saturn_fade_interp(255, 0, 0, 16));
    CUI_ASSERT_EQ(0,   saturn_fade_interp(255, 0, 16, 16));
    CUI_ASSERT(saturn_fade_interp(255, 0, 8, 16) < 200);
}

CUI_TEST(fade_interp_clamps_and_survives_zero_frames)
{
    /* frames < 1 must not divide by zero. */
    CUI_ASSERT_EQ(255, saturn_fade_interp(0, 255, 1, 0));
    CUI_ASSERT(saturn_fade_interp(-500, 255, 0, 16) >= 0);
    CUI_ASSERT(saturn_fade_interp(0, 9999, 16, 16) <= 255);
}

CUI_TEST(fade_ramp_completes_and_reports_inactive)
{
    saturn_fade_start(0, 255, 8, false);
    CUI_ASSERT(saturn_fade_active());

    for (int i = 0; i < 8; i++) {
        saturn_fade_tick();
    }

    CUI_ASSERT(!saturn_fade_active());
    CUI_ASSERT_EQ(255, saturn_fade_level());

    saturn_fade_clear();
    CUI_ASSERT_EQ(0, saturn_fade_level());
}

CUI_TEST(fade_tick_is_a_noop_when_idle)
{
    saturn_fade_clear();
    saturn_fade_tick();
    saturn_fade_tick();
    CUI_ASSERT_EQ(0, saturn_fade_level());
    CUI_ASSERT(!saturn_fade_active());
}
