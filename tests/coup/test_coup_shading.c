/**
 * test_coup_shading.c - Proves the animated gradients actually animate.
 *
 * The failure mode these guard against is not a crash - it is a gradient that
 * compiles, uploads, and sits perfectly still, or one that "pulses" by
 * saturating at both ends so the middle of the motion is invisible. Both look
 * like working code and neither can be caught by reading the source.
 *
 * So each generator is asserted on the property that makes it worth having:
 * the sheen must MOVE, the halo must be AMBER and not merely brighter, the
 * pulse must RETURN, the empty wash must be DARKER than the occupied one.
 */

#include "cui_test_framework.h"
#include "coup_shading.h"
#include "saturn_vdp1.h"

/* Saturn RGB555 is 0BBBBBGGGGGRRRRR (saturn_vdp1.c:591). The hardware
 * subtracts SATURN_VDP1_GRD_NEUTRAL, so a channel decodes to a signed
 * correction. */
static int chan_r(uint16_t w) { return (int)(w & 0x1F) - SATURN_VDP1_GRD_NEUTRAL; }
static int chan_g(uint16_t w) { return (int)((w >> 5) & 0x1F) - SATURN_VDP1_GRD_NEUTRAL; }
static int chan_b(uint16_t w) { return (int)((w >> 10) & 0x1F) - SATURN_VDP1_GRD_NEUTRAL; }

/* Mean luminance-ish weight of one corner. Equal weights: these corrections
 * are applied to whatever colour the polygon already is, so there is no
 * source luma to weight against. */
static int corner_level(uint16_t w)
{
    return chan_r(w) + chan_g(w) + chan_b(w);
}

/*============================================================================
 * The sine primitive everything else is built on
 *============================================================================*/

CUI_TEST(shading_sin_is_bounded_and_periodic)
{
    int i;
    for (i = 0; i < COUP_SHADING_PERIOD * 3; i++) {
        int s = coup_shading_sin(i);
        CUI_ASSERT(s >= -1024);
        CUI_ASSERT(s <= 1024);
    }
    /* One period returns to the start - otherwise a free-running frame
     * counter would make every animation drift. */
    CUI_ASSERT_EQ(coup_shading_sin(0), coup_shading_sin(COUP_SHADING_PERIOD));
    CUI_ASSERT_EQ(coup_shading_sin(7), coup_shading_sin(COUP_SHADING_PERIOD + 7));
}

CUI_TEST(shading_sin_actually_reaches_both_extremes)
{
    /* A table that never gets near +/-1024 would make every animation built
     * on it a barely-visible wobble. */
    int i, hi = -2048, lo = 2048;
    for (i = 0; i < COUP_SHADING_PERIOD; i++) {
        int s = coup_shading_sin(i);
        if (s > hi) hi = s;
        if (s < lo) lo = s;
    }
    CUI_ASSERT(hi >= 900);
    CUI_ASSERT(lo <= -900);
}

/*============================================================================
 * Every generator must stay inside the hardware's range
 *============================================================================*/

CUI_TEST(shading_never_exceeds_the_hardware_correction_range)
{
    /* Out-of-range corrections do not error - they silently clamp, which
     * flattens the ends of the motion. Assert the generators stay inside the
     * range so the clamp is never what shapes the animation. */
    int i, c;
    uint16_t t[4];
    for (i = 0; i < COUP_SHADING_PERIOD; i++) {
        coup_shading_sheen(t, i);
        for (c = 0; c < 4; c++) {
            CUI_ASSERT(chan_r(t[c]) >= COUP_SHADING_MIN_CORRECTION);
            CUI_ASSERT(chan_r(t[c]) <= COUP_SHADING_MAX_CORRECTION);
            CUI_ASSERT(chan_g(t[c]) >= COUP_SHADING_MIN_CORRECTION);
            CUI_ASSERT(chan_g(t[c]) <= COUP_SHADING_MAX_CORRECTION);
            CUI_ASSERT(chan_b(t[c]) >= COUP_SHADING_MIN_CORRECTION);
            CUI_ASSERT(chan_b(t[c]) <= COUP_SHADING_MAX_CORRECTION);
        }
        coup_shading_halo(t, i);
        for (c = 0; c < 4; c++) {
            CUI_ASSERT(chan_r(t[c]) <= COUP_SHADING_MAX_CORRECTION);
            CUI_ASSERT(chan_b(t[c]) >= COUP_SHADING_MIN_CORRECTION);
        }
        coup_shading_pulse(t, i);
        for (c = 0; c < 4; c++) {
            CUI_ASSERT(chan_r(t[c]) <= COUP_SHADING_MAX_CORRECTION);
            CUI_ASSERT(chan_r(t[c]) >= COUP_SHADING_MIN_CORRECTION);
        }
        coup_shading_spotlight(t, i);
        for (c = 0; c < 4; c++) {
            CUI_ASSERT(chan_r(t[c]) <= COUP_SHADING_MAX_CORRECTION);
            CUI_ASSERT(chan_r(t[c]) >= COUP_SHADING_MIN_CORRECTION);
        }
    }
}

/*============================================================================
 * Sheen: the highlight must travel
 *============================================================================*/

CUI_TEST(sheen_highlight_travels_from_left_to_right)
{
    /* Corners are A=UL, B=UR, C=LR, D=LL. Left side is A+D, right is B+C.
     * Early in the period the left must be the brighter side, and later the
     * right must be - that difference changing SIGN is what proves the
     * highlight moved rather than the whole quad brightening. */
    uint16_t t[4];
    int left_early, right_early, left_late, right_late;

    coup_shading_sheen(t, COUP_SHADING_PERIOD / 8);
    left_early = corner_level(t[0]) + corner_level(t[3]);
    right_early = corner_level(t[1]) + corner_level(t[2]);

    coup_shading_sheen(t, (COUP_SHADING_PERIOD * 5) / 8);
    left_late = corner_level(t[0]) + corner_level(t[3]);
    right_late = corner_level(t[1]) + corner_level(t[2]);

    CUI_ASSERT(left_early > right_early);
    CUI_ASSERT(right_late > left_late);
}

CUI_TEST(sheen_is_periodic)
{
    uint16_t a[4], b[4];
    int c;
    coup_shading_sheen(a, 3);
    coup_shading_sheen(b, COUP_SHADING_PERIOD + 3);
    for (c = 0; c < 4; c++) {
        CUI_ASSERT_EQ(a[c], b[c]);
    }
}

CUI_TEST(sheen_top_is_never_uniformly_flat)
{
    /* If every corner were equal at every phase, the "sheen" would just be a
     * brightness oscillation. Require at least one phase with real spread. */
    int i, best = 0;
    uint16_t t[4];
    for (i = 0; i < COUP_SHADING_PERIOD; i++) {
        int lo = 999, hi = -999, c;
        coup_shading_sheen(t, i);
        for (c = 0; c < 4; c++) {
            int v = corner_level(t[c]);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (hi - lo > best) best = hi - lo;
    }
    CUI_ASSERT(best >= 9);
}

/*============================================================================
 * Halo: amber is a hue, not a brightness
 *============================================================================*/

CUI_TEST(halo_is_amber_and_not_merely_brighter)
{
    /* Amber: red lifted most, green less, blue pushed DOWN. A uniform lift
     * would pass a brightness test while looking like plain white light. */
    uint16_t t[4];
    int c, checked = 0;
    coup_shading_halo(t, COUP_SHADING_PERIOD / 4);   /* near the peak */
    for (c = 0; c < 4; c++) {
        if (corner_level(t[c]) <= 0) {
            continue;                 /* the falling-off corners */
        }
        CUI_ASSERT(chan_r(t[c]) > chan_g(t[c]));
        CUI_ASSERT(chan_g(t[c]) > chan_b(t[c]));
        checked++;
    }
    CUI_ASSERT(checked > 0);
}

CUI_TEST(halo_breathes_and_returns)
{
    /* It must come back to where it started, and it must actually move a
     * meaningful amount in between. */
    uint16_t a[4], b[4];
    int i, lo = 999, hi = -999;
    coup_shading_halo(a, 0);
    coup_shading_halo(b, COUP_SHADING_PERIOD);
    CUI_ASSERT_EQ(a[0], b[0]);

    for (i = 0; i < COUP_SHADING_PERIOD; i++) {
        int v;
        coup_shading_halo(a, i);
        v = corner_level(a[0]);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    CUI_ASSERT(hi - lo >= 12);
}

/*============================================================================
 * Pulse and washes
 *============================================================================*/

CUI_TEST(pulse_is_uniform_across_the_quad)
{
    uint16_t t[4];
    coup_shading_pulse(t, 17);
    CUI_ASSERT_EQ(t[0], t[1]);
    CUI_ASSERT_EQ(t[1], t[2]);
    CUI_ASSERT_EQ(t[2], t[3]);
}

CUI_TEST(pulse_returns_to_its_starting_level)
{
    uint16_t a[4], b[4];
    coup_shading_pulse(a, 0);
    coup_shading_pulse(b, COUP_SHADING_PERIOD);
    CUI_ASSERT_EQ(a[0], b[0]);
}

CUI_TEST(an_empty_lobby_slot_is_darker_than_an_occupied_one)
{
    /* The whole point of the lobby treatment: a seated player should read as
     * lit and an empty chair as recessed, at a glance, on every corner. */
    uint16_t occ[4], emp[4];
    int c;
    coup_shading_wash(occ, true);
    coup_shading_wash(emp, false);
    for (c = 0; c < 4; c++) {
        CUI_ASSERT(corner_level(emp[c]) < corner_level(occ[c]));
    }
}

CUI_TEST(spotlight_is_brighter_at_the_top_than_the_bottom)
{
    /* A spotlight falling on the winner from above. If top and bottom were
     * equal it would be a flood, not a spot. */
    uint16_t t[4];
    coup_shading_spotlight(t, 0);
    CUI_ASSERT(corner_level(t[0]) > corner_level(t[3]));
    CUI_ASSERT(corner_level(t[1]) > corner_level(t[2]));
}

CUI_TEST(spotlight_blooms_over_time)
{
    int i, lo = 999, hi = -999;
    uint16_t t[4];
    for (i = 0; i < COUP_SHADING_PERIOD; i++) {
        int v;
        coup_shading_spotlight(t, i);
        v = corner_level(t[0]);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    CUI_ASSERT(hi - lo >= 6);
}
