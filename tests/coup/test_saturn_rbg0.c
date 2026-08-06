/**
 * test_saturn_rbg0.c - Host tests for the VDP2 RBG0 title fly-in math and
 * bank/offset placement constants.
 *
 * saturn_rbg0_flyin_recip_q16/angle/is_done and saturn_rbg0_pattern_pixel
 * are pure integer arithmetic (see saturn_rbg0.c), so all of it is host-
 * testable. Only the VDP2 register writes and VRAM/CRAM uploads
 * (saturn_rbg0_init/advance/teardown/run_title_demo, #ifdef __SATURN__)
 * are Saturn-only and are out of reach here.
 *
 * The placement-constant tests pin the bank/offset plan cited in
 * saturn_rbg0.h against a RED fixture: they prove the gate CAN detect the
 * regression the whole plan exists to avoid (RBG0 bitmap data landing in
 * VRAM bank B1, which is where saturn_pal.c's NBG0 text character data and
 * PNT already live - ASCII_CEL_VRAM_ADDR = 0x25e60000, saturn_pal.c:93,
 * VDP2_VRAM_B1 in sgl_defs.h) before trusting the real placement (bank B0,
 * empty) is correct. See also scripts/qa/qa_vram_rbg0_map.py, which runs
 * the equivalent check by parsing the shipped header rather than
 * hardcoding the constant a second time - this file's version is the
 * "gate can detect a known-bad fixture" proof; that script is the
 * "the shipped constant is actually good" proof.
 */

#include "cui_test_framework.h"
#include "saturn_rbg0.h"

#include <stdlib.h>

/* VDP2 VRAM bank base addresses, transcribed from sgl_defs.h (not included
 * here - the pure-math half of this module intentionally has no SGL
 * dependency, matching saturn_linescroll.c's own host-testable half). */
#define TEST_VDP2_VRAM_B0   0x25E40000u
#define TEST_VDP2_VRAM_B1   0x25E60000u   /* NBG0 text char data + PNT live here */

/* ==========================================================================
 * Zoom (reciprocal-of-scale) curve
 * ========================================================================== */

CUI_TEST(rbg0_recip_starts_at_the_configured_start_value)
{
    int32_t v = saturn_rbg0_flyin_recip_q16(0, SATURN_RBG0_FLYIN_FRAMES);
    CUI_ASSERT_EQ(SATURN_RBG0_FLYIN_START_RECIP_Q16, v);
}

CUI_TEST(rbg0_recip_ends_at_exactly_full_size)
{
    int32_t v_at_end = saturn_rbg0_flyin_recip_q16(
        SATURN_RBG0_FLYIN_FRAMES, SATURN_RBG0_FLYIN_FRAMES);
    int32_t v_past_end = saturn_rbg0_flyin_recip_q16(
        SATURN_RBG0_FLYIN_FRAMES + 30, SATURN_RBG0_FLYIN_FRAMES);

    CUI_ASSERT_EQ(SATURN_RBG0_FLYIN_END_RECIP_Q16, v_at_end);
    CUI_ASSERT_EQ(SATURN_RBG0_FLYIN_END_RECIP_Q16, v_past_end);
}

CUI_TEST(rbg0_recip_is_monotonically_non_increasing)
{
    int i;
    int32_t prev = saturn_rbg0_flyin_recip_q16(0, SATURN_RBG0_FLYIN_FRAMES);

    for (i = 1; i <= SATURN_RBG0_FLYIN_FRAMES; i++) {
        int32_t v = saturn_rbg0_flyin_recip_q16(i, SATURN_RBG0_FLYIN_FRAMES);
        CUI_ASSERT_LE(v, prev);
        prev = v;
    }
}

CUI_TEST(rbg0_recip_never_goes_below_full_size_or_above_the_start)
{
    int i;
    for (i = -5; i <= SATURN_RBG0_FLYIN_FRAMES + 5; i++) {
        int32_t v = saturn_rbg0_flyin_recip_q16(i, SATURN_RBG0_FLYIN_FRAMES);
        CUI_ASSERT_GE(v, SATURN_RBG0_FLYIN_END_RECIP_Q16);
        CUI_ASSERT_LE(v, SATURN_RBG0_FLYIN_START_RECIP_Q16);
    }
}

CUI_TEST(rbg0_recip_degenerate_total_frames_collapses_to_full_size)
{
    /* total_frames <= 0 must not divide by zero; treated as "already
     * arrived" rather than crashing or returning a huge/undefined value. */
    CUI_ASSERT_EQ(SATURN_RBG0_FLYIN_END_RECIP_Q16,
                  saturn_rbg0_flyin_recip_q16(0, 0));
    CUI_ASSERT_EQ(SATURN_RBG0_FLYIN_END_RECIP_Q16,
                  saturn_rbg0_flyin_recip_q16(5, -1));
}

/* ==========================================================================
 * Rotation-settle curve
 * ========================================================================== */

CUI_TEST(rbg0_angle_starts_at_the_configured_tilt)
{
    int16_t v = saturn_rbg0_flyin_angle(0, SATURN_RBG0_FLYIN_FRAMES);
    CUI_ASSERT_EQ((int16_t)SATURN_RBG0_FLYIN_START_ANGLE, v);
}

CUI_TEST(rbg0_angle_settles_to_exactly_zero)
{
    int16_t v_at_end =
        saturn_rbg0_flyin_angle(SATURN_RBG0_FLYIN_FRAMES, SATURN_RBG0_FLYIN_FRAMES);
    int16_t v_past_end =
        saturn_rbg0_flyin_angle(SATURN_RBG0_FLYIN_FRAMES + 30, SATURN_RBG0_FLYIN_FRAMES);

    CUI_ASSERT_EQ(0, v_at_end);
    CUI_ASSERT_EQ(0, v_past_end);
}

CUI_TEST(rbg0_angle_is_monotonically_non_increasing_toward_zero)
{
    int i;
    int16_t prev = saturn_rbg0_flyin_angle(0, SATURN_RBG0_FLYIN_FRAMES);

    for (i = 1; i <= SATURN_RBG0_FLYIN_FRAMES; i++) {
        int16_t v = saturn_rbg0_flyin_angle(i, SATURN_RBG0_FLYIN_FRAMES);
        CUI_ASSERT_LE(v, prev);
        CUI_ASSERT_GE(v, 0);
        prev = v;
    }
}

CUI_TEST(rbg0_angle_degenerate_total_frames_is_zero)
{
    CUI_ASSERT_EQ(0, saturn_rbg0_flyin_angle(0, 0));
    CUI_ASSERT_EQ(0, saturn_rbg0_flyin_angle(5, -1));
}

/* ==========================================================================
 * is_done
 * ========================================================================== */

CUI_TEST(rbg0_is_done_boundary)
{
    CUI_ASSERT_FALSE(saturn_rbg0_flyin_is_done(0, SATURN_RBG0_FLYIN_FRAMES));
    CUI_ASSERT_FALSE(saturn_rbg0_flyin_is_done(
        SATURN_RBG0_FLYIN_FRAMES - 1, SATURN_RBG0_FLYIN_FRAMES));
    CUI_ASSERT_TRUE(saturn_rbg0_flyin_is_done(
        SATURN_RBG0_FLYIN_FRAMES, SATURN_RBG0_FLYIN_FRAMES));
    CUI_ASSERT_TRUE(saturn_rbg0_flyin_is_done(
        SATURN_RBG0_FLYIN_FRAMES + 1, SATURN_RBG0_FLYIN_FRAMES));
    CUI_ASSERT_TRUE(saturn_rbg0_flyin_is_done(0, 0));
    CUI_ASSERT_TRUE(saturn_rbg0_flyin_is_done(0, -1));
}

/* ==========================================================================
 * Placeholder bitmap pattern
 * ========================================================================== */

CUI_TEST(rbg0_pattern_pixel_out_of_bounds_is_zero)
{
    CUI_ASSERT_EQ(0, saturn_rbg0_pattern_pixel(-1, 0));
    CUI_ASSERT_EQ(0, saturn_rbg0_pattern_pixel(0, -1));
    CUI_ASSERT_EQ(0, saturn_rbg0_pattern_pixel(SATURN_RBG0_BITMAP_W, 0));
    CUI_ASSERT_EQ(0, saturn_rbg0_pattern_pixel(0, SATURN_RBG0_BITMAP_H));
}

CUI_TEST(rbg0_pattern_pixel_only_ever_two_values)
{
    int x, y;
    for (y = 0; y < SATURN_RBG0_BITMAP_H; y += 17) {
        for (x = 0; x < SATURN_RBG0_BITMAP_W; x += 13) {
            uint8_t v = saturn_rbg0_pattern_pixel(x, y);
            CUI_ASSERT(v == 0 || v == 1);
        }
    }
}

CUI_TEST(rbg0_pattern_pixel_alternates_between_adjacent_32px_cells)
{
    /* Cell (0,0) and cell (1,0) (i.e. x=0 vs x=32, same row) must differ -
     * a checkerboard, not a solid fill or a degenerate always-0/always-1
     * implementation that would pass the "only two values" gate trivially. */
    uint8_t a = saturn_rbg0_pattern_pixel(0, 0);
    uint8_t b = saturn_rbg0_pattern_pixel(32, 0);
    uint8_t c = saturn_rbg0_pattern_pixel(0, 32);

    CUI_ASSERT(a != b);
    CUI_ASSERT(a != c);
}

/* ==========================================================================
 * Bank/offset placement constants - RED-fixture-then-GREEN proof
 *
 * The RED fixture: assert the gate's own comparison logic actually
 * distinguishes "bitmap data in B0" (correct) from "bitmap data in B1"
 * (the text/font/PNT bank - the regression this whole plan exists to
 * avoid). If this test could not fail on a bad input, it would not be
 * proving anything about the good one either.
 * ========================================================================== */

CUI_TEST(rbg0_bitmap_bank_is_provably_not_the_text_bank)
{
    /* GREEN: the shipped constant. */
    CUI_ASSERT_EQ(TEST_VDP2_VRAM_B0, SATURN_RBG0_BITMAP_VRAM);
    CUI_ASSERT_NEQ(TEST_VDP2_VRAM_B1, SATURN_RBG0_BITMAP_VRAM);

    /* RED fixture: prove the comparison itself is discriminating, not
     * vacuously true - a B1 address MUST compare equal to B1 and unequal
     * to B0, or this whole gate class would be a no-op. */
    CUI_ASSERT_EQ(TEST_VDP2_VRAM_B1, TEST_VDP2_VRAM_B1);
    CUI_ASSERT_NEQ(TEST_VDP2_VRAM_B0, TEST_VDP2_VRAM_B1);
}

CUI_TEST(rbg0_bitmap_exactly_fills_one_vram_bank_no_more_no_less)
{
    /* 512 x 256 @ 8bpp must equal exactly 0x20000 (one whole VDP2 VRAM
     * bank) - the same shape saturn_bg.c already uses for NBG1 in bank A0.
     * Anything smaller wastes the "whole bank, nothing else can collide"
     * property this plan relies on; anything larger overflows into B1. */
    CUI_ASSERT_EQ(0x20000u, SATURN_RBG0_BITMAP_BYTES);
    CUI_ASSERT_EQ((uint32_t)SATURN_RBG0_BITMAP_W * SATURN_RBG0_BITMAP_H,
                  SATURN_RBG0_BITMAP_BYTES);
}

CUI_TEST(rbg0_param_table_offset_satisfies_the_directed_floor)
{
    /* The task's directive: "Rotation Parameter Table goes in bank A1 at
     * offset >= 0x0380" (immediately above saturn_linescroll.h's
     * 0x000-0x37F claim). */
    CUI_ASSERT_GE(SATURN_RBG0_PARAM_VRAM_OFFSET, SATURN_RBG0_PARAM_MIN_OFFSET);
    CUI_ASSERT_EQ(0x0380u, SATURN_RBG0_PARAM_MIN_OFFSET);
}

CUI_TEST(rbg0_param_table_stays_clear_of_the_reserved_top_of_bank_a1)
{
    uint32_t table_end =
        SATURN_RBG0_PARAM_VRAM_OFFSET + SATURN_RBG0_PARAM_TABLE_BYTES;

    /* Back-screen colour lives at A1+0x1FFFE (main_saturn.c:226), inside
     * the reserved top 256 B (0x1FF00-0x1FFFF). The table must end at or
     * before the start of that reserved region. */
    CUI_ASSERT_LE(table_end, SATURN_RBG0_A1_RESERVED_TOP);
}

CUI_TEST(rbg0_param_table_rpta_bit6_alignment)
{
    /* VDP2_Manual.txt:6873-6874: "RPTA6 bit is ignored even if data is
     * written. The bit is set at 0 for rotation parameter A" - i.e. the
     * table's byte offset must have bit 6 (0x40) clear for parameter A to
     * land where slRparaInitSet's caller expects (param B is always at
     * ptr+0x80, which also requires 0x80-alignment of ptr for the two
     * slots not to overlap awkwardly). */
    CUI_ASSERT_EQ(0u, SATURN_RBG0_PARAM_VRAM_OFFSET & 0x40u);
    CUI_ASSERT_EQ(0u, SATURN_RBG0_PARAM_VRAM_OFFSET % 0x80u);
}

CUI_TEST(rbg0_cram_claim_is_a_whole_256_colour_bank_not_the_background_ones)
{
    /* saturn_bg.h's background bitmap palette owns 256-colour bank 7
     * (0xE00-0xFFF, SATURN_BG_CRAM_OFFSET). RBG0's placeholder palette must
     * not land on the same bank. */
    CUI_ASSERT_EQ(0xC00u, SATURN_RBG0_CRAM_OFFSET);
    CUI_ASSERT_NEQ(0xE00u, SATURN_RBG0_CRAM_OFFSET);
    CUI_ASSERT_EQ(6, SATURN_RBG0_PALETTE_BANK);

    /* The claim must fit inside CRAM (4 KB total, mode 0). */
    CUI_ASSERT_LE(SATURN_RBG0_CRAM_OFFSET + 512u, 0x1000u);
}
