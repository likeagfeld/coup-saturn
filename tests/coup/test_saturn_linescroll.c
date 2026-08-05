/**
 * test_saturn_linescroll.c - Host tests for the NBG1 line-scroll shimmer
 * table math.
 *
 * saturn_linescroll_build() is pure integer arithmetic (Bhaskara I's
 * rational sine approximation - no floating point, no libm; see
 * saturn_linescroll.c), so all of it is host-testable. Only the VDP2
 * register writes and the VRAM upload (saturn_linescroll_arm/advance/
 * disarm, #ifdef __SATURN__) are Saturn-only and are out of reach here.
 *
 * The three RED-firing gates called for by the task:
 *   1. amplitude is clamped - a future change cannot make the shimmer
 *      "seasick" by cranking the amplitude up.
 *   2. the wave is continuous - no adjacent-line jump bigger than the
 *      measured bound at the shipped defaults.
 *   3. the wave is periodic in phase - phase 0 and phase == wavelength
 *      produce identical tables.
 *
 * Numbers below are MEASURED (see pal-saturn-agent report), not guessed:
 * with the shipped defaults (amplitude=2px, wavelength=64 lines) the
 * table's actual range is [-2, +2] and the actual max adjacent-line delta
 * is 1px.
 */

#include "cui_test_framework.h"
#include "saturn_linescroll.h"

#include <stdlib.h>

/* ==========================================================================
 * Gate 1 - amplitude clamp (anti-seasickness)
 * ========================================================================== */

CUI_TEST(linescroll_amplitude_is_clamped_to_the_hard_ceiling)
{
    int16_t table[SATURN_LINESCROLL_LINES];
    int i;

    /* A caller asking for a wildly excessive amplitude (as a future
     * "make it punchier" change might) must never see more than the hard
     * ceiling - this is the gate that keeps the shimmer subtle even if
     * someone changes SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX carelessly. */
    saturn_linescroll_build(table, SATURN_LINESCROLL_LINES, 0, 500,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        CUI_ASSERT(table[i] <= SATURN_LINESCROLL_MAX_AMPLITUDE_PX);
        CUI_ASSERT(table[i] >= -SATURN_LINESCROLL_MAX_AMPLITUDE_PX);
    }
}

CUI_TEST(linescroll_default_amplitude_is_within_the_ceiling)
{
    /* The shipped default itself must already be a legal, subtle value -
     * not just clamped at the last second. */
    CUI_ASSERT(SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX <=
               SATURN_LINESCROLL_MAX_AMPLITUDE_PX);
    CUI_ASSERT(SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX > 0);
}

CUI_TEST(linescroll_negative_amplitude_request_is_also_clamped)
{
    int16_t table[SATURN_LINESCROLL_LINES];
    int i;

    saturn_linescroll_build(table, SATURN_LINESCROLL_LINES, 0, -500,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        CUI_ASSERT(table[i] <= SATURN_LINESCROLL_MAX_AMPLITUDE_PX);
        CUI_ASSERT(table[i] >= -SATURN_LINESCROLL_MAX_AMPLITUDE_PX);
    }
}

/* ==========================================================================
 * Gate 2 - continuity (no jarring per-line jump)
 * ========================================================================== */

CUI_TEST(linescroll_default_table_has_no_jump_bigger_than_one_pixel)
{
    int16_t table[SATURN_LINESCROLL_LINES];
    int i;

    saturn_linescroll_build(table, SATURN_LINESCROLL_LINES, 0,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    /* MEASURED: at amplitude=2px, wavelength=64 lines, the true max
     * adjacent-line delta is 1px. Bounding at 1 catches any future
     * amplitude/wavelength change that would make the shimmer visibly
     * "step" instead of glide. */
    for (i = 1; i < SATURN_LINESCROLL_LINES; i++) {
        int delta = table[i] - table[i - 1];
        if (delta < 0) {
            delta = -delta;
        }
        CUI_ASSERT_LE(delta, 1);
    }
}

CUI_TEST(linescroll_table_actually_oscillates)
{
    /* A degenerate always-zero (or always-clamped) implementation would
     * pass the continuity and amplitude gates trivially. Guard against
     * that: the shipped defaults must actually swing through the full
     * measured range. */
    int16_t table[SATURN_LINESCROLL_LINES];
    int i;
    int seen_positive = 0, seen_negative = 0;

    saturn_linescroll_build(table, SATURN_LINESCROLL_LINES, 0,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        if (table[i] > 0) {
            seen_positive = 1;
        }
        if (table[i] < 0) {
            seen_negative = 1;
        }
    }

    CUI_ASSERT(seen_positive);
    CUI_ASSERT(seen_negative);
}

/* ==========================================================================
 * Gate 3 - periodicity in phase
 * ========================================================================== */

CUI_TEST(linescroll_phase_zero_and_phase_full_period_are_identical)
{
    int16_t table_phase0[SATURN_LINESCROLL_LINES];
    int16_t table_phase_full[SATURN_LINESCROLL_LINES];
    int i;

    saturn_linescroll_build(table_phase0, SATURN_LINESCROLL_LINES, 0,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);
    saturn_linescroll_build(table_phase_full, SATURN_LINESCROLL_LINES,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        CUI_ASSERT_EQ(table_phase0[i], table_phase_full[i]);
    }
}

CUI_TEST(linescroll_phase_two_periods_out_still_matches)
{
    int16_t table_phase0[SATURN_LINESCROLL_LINES];
    int16_t table_phase_2x[SATURN_LINESCROLL_LINES];
    int i;

    saturn_linescroll_build(table_phase0, SATURN_LINESCROLL_LINES, 0,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);
    saturn_linescroll_build(table_phase_2x, SATURN_LINESCROLL_LINES,
                             2 * SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        CUI_ASSERT_EQ(table_phase0[i], table_phase_2x[i]);
    }
}

CUI_TEST(linescroll_negative_phase_wraps_like_a_positive_equivalent)
{
    int16_t table_neg[SATURN_LINESCROLL_LINES];
    int16_t table_wrapped[SATURN_LINESCROLL_LINES];
    int i;

    /* phase = -1 must behave like phase = wavelength - 1 (mod wraps
     * correctly for negative input, not just positive). */
    saturn_linescroll_build(table_neg, SATURN_LINESCROLL_LINES, -1,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);
    saturn_linescroll_build(table_wrapped, SATURN_LINESCROLL_LINES,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES - 1,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        CUI_ASSERT_EQ(table_neg[i], table_wrapped[i]);
    }
}

/* ==========================================================================
 * Robustness / degenerate inputs
 * ========================================================================== */

CUI_TEST(linescroll_build_tolerates_null_and_non_positive_lines)
{
    int16_t table[4] = { 7, 7, 7, 7 };

    /* Must not crash; NULL table or lines <= 0 is a no-op. */
    saturn_linescroll_build(NULL, 4, 0, 2, 64);
    saturn_linescroll_build(table, 0, 0, 2, 64);
    saturn_linescroll_build(table, -3, 0, 2, 64);

    /* The non-positive-lines calls must not have touched `table`. */
    CUI_ASSERT_EQ(7, table[0]);
    CUI_ASSERT_EQ(7, table[1]);
    CUI_ASSERT_EQ(7, table[2]);
    CUI_ASSERT_EQ(7, table[3]);
}

CUI_TEST(linescroll_build_tolerates_zero_wavelength)
{
    /* wavelength <= 0 must not divide by zero; treated as wavelength=1. */
    int16_t table[SATURN_LINESCROLL_LINES];

    saturn_linescroll_build(table, SATURN_LINESCROLL_LINES, 0, 2, 0);

    /* wavelength=1 collapses every line to the same sample (x_deg=0),
     * which is sin(0) == 0. */
    CUI_ASSERT_EQ(0, table[0]);
    CUI_ASSERT_EQ(0, table[SATURN_LINESCROLL_LINES - 1]);
}

/* ==========================================================================
 * VRAM/table budget - pins the design doc's cited "896 B - 2 KB" range
 * (docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md:152)
 * ========================================================================== */

CUI_TEST(linescroll_table_byte_budget_matches_the_design_doc_range)
{
    CUI_ASSERT_EQ(224, SATURN_LINESCROLL_LINES);
    CUI_ASSERT_EQ(4, SATURN_LINESCROLL_BYTES_PER_LINE);
    CUI_ASSERT_EQ(896, SATURN_LINESCROLL_TABLE_BYTES);

    CUI_ASSERT_GE(SATURN_LINESCROLL_TABLE_BYTES, 896);
    CUI_ASSERT_LE(SATURN_LINESCROLL_TABLE_BYTES, 2048);
}
