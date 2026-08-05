/**
 * test_saturn_distort.c - Host tests for the VDP1 distorted-sprite
 * card-flip and mesh-dissolve encoders.
 *
 * All geometry/encoding here is pure arithmetic (see saturn_distort.h
 * for the ST-013-R3 citations backing every constant); the VRAM write
 * itself is Saturn-only and is not exercised on host.
 *
 * Gate G6 (design doc line 311): "card-flip midpoint frame: quad width
 * <= 2 px at frame N/2, full art restored at frame N - RED fixture:
 * skip the degenerate-quad clamp." The
 * flip_quad_midpoint_collapses_to_clamped_sliver test below is that
 * gate: with SATURN_DISTORT_MIN_HALF_WIDTH's clamp removed from the
 * implementation, frames=12 (even) makes the fold term hit exactly 0 at
 * step=6, so the unclamped half-width is exactly 0 and this test's
 * CUI_ASSERT_GT(width, 0) fails - proving the gate catches the missing
 * clamp rather than passing vacuously.
 */

#include "cui_test_framework.h"
#include "saturn_distort.h"

/*============================================================================
 * saturn_distort_flip_quad - pure geometry
 *============================================================================*/

CUI_TEST(flip_quad_step_zero_is_full_width_flat_rectangle)
{
    int vx[4], vy[4];

    /* 40x56 card centred at (100, 80), 12-frame flip (design doc's own
     * example: "over 12 frames"). */
    saturn_distort_flip_quad(100, 80, 40, 56, 0, 12, vx, vy);

    /* A/D on the left edge, B/C on the right edge, full half-width 20. */
    CUI_ASSERT_EQ(80, vx[0]);   /* A: cx - 20 */
    CUI_ASSERT_EQ(120, vx[1]);  /* B: cx + 20 */
    CUI_ASSERT_EQ(120, vx[2]);  /* C: cx + 20 */
    CUI_ASSERT_EQ(80, vx[3]);   /* D: cx - 20 */

    /* No skew at step 0: both edges span the full height symmetrically. */
    CUI_ASSERT_EQ(vy[0], vy[1]);   /* top edge level */
    CUI_ASSERT_EQ(vy[2], vy[3]);   /* bottom edge level */
    CUI_ASSERT_EQ(80 - 28, vy[0]); /* cy - half_h */
    CUI_ASSERT_EQ(80 + 28, vy[2]); /* cy + half_h */
}

CUI_TEST(flip_quad_step_frames_restores_full_width_flat_rectangle)
{
    int vx[4], vy[4];

    saturn_distort_flip_quad(100, 80, 40, 56, 12, 12, vx, vy);

    CUI_ASSERT_EQ(80, vx[0]);
    CUI_ASSERT_EQ(120, vx[1]);
    CUI_ASSERT_EQ(120, vx[2]);
    CUI_ASSERT_EQ(80, vx[3]);

    /* "Full art restored" - flat again, no residual skew. */
    CUI_ASSERT_EQ(vy[0], vy[1]);
    CUI_ASSERT_EQ(vy[2], vy[3]);
    CUI_ASSERT_EQ(80 - 28, vy[0]);
    CUI_ASSERT_EQ(80 + 28, vy[2]);
}

/* --- Gate G6 --- */
CUI_TEST(flip_quad_midpoint_collapses_to_clamped_sliver)
{
    int vx[4], vy[4];
    int width_top, width_bottom;

    saturn_distort_flip_quad(100, 80, 40, 56, 6, 12, vx, vy);

    width_top = vx[1] - vx[0];
    width_bottom = vx[2] - vx[3];

    /* G6: quad width <= 2 px at frame N/2. */
    CUI_ASSERT(width_top <= 2);
    CUI_ASSERT(width_bottom <= 2);

    /* The clamp's entire purpose: never a true zero-width (or negative/
     * crossed-over) quad, which is what would fire without it. */
    CUI_ASSERT(width_top > 0);
    CUI_ASSERT(width_bottom > 0);

    /* Both edges must still be centred on cx, not off to one side. */
    CUI_ASSERT(vx[0] <= 100 && vx[1] >= 100);
}

CUI_TEST(flip_quad_width_is_monotonic_each_half)
{
    /* First half: width strictly shrinks (or holds at the clamp) toward
     * the midpoint. Second half: it grows back out. This is the
     * "trapezoid collapse... then expand" shape, checked at every step
     * rather than just the three named points. */
    int prev_w = -1;
    int step;

    for (step = 0; step <= 6; step++) {
        int vx[4], vy[4];
        int w;
        saturn_distort_flip_quad(0, 0, 40, 56, step, 12, vx, vy);
        w = vx[1] - vx[0];
        CUI_ASSERT(w >= 1);
        if (prev_w >= 0) {
            CUI_ASSERT(w <= prev_w);
        }
        prev_w = w;
    }

    prev_w = -1;
    for (step = 6; step <= 12; step++) {
        int vx[4], vy[4];
        int w;
        saturn_distort_flip_quad(0, 0, 40, 56, step, 12, vx, vy);
        w = vx[1] - vx[0];
        CUI_ASSERT(w >= 1);
        if (prev_w >= 0) {
            CUI_ASSERT(w >= prev_w);
        }
        prev_w = w;
    }
}

CUI_TEST(flip_quad_quarter_point_is_a_trapezoid_not_a_squash)
{
    /* At the quarter point the card is still visibly wide but should
     * already show perspective skew: the two vertical edges must NOT
     * be the same height, or this is just a symmetric squash, not a
     * rotating card (design doc: "trapezoid collapse", not "shrink"). */
    int vx[4], vy[4];
    int left_edge_h, right_edge_h;

    saturn_distort_flip_quad(0, 0, 40, 56, 3, 12, vx, vy);

    left_edge_h = vy[3] - vy[0];   /* D.y - A.y */
    right_edge_h = vy[2] - vy[1];  /* C.y - B.y */

    CUI_ASSERT_NEQ(left_edge_h, right_edge_h);
}

CUI_TEST(flip_quad_skew_direction_flips_across_the_midpoint)
{
    /* The edge that was taller before the midpoint must be the shorter
     * one after it - a continuously turning card, not a symmetric
     * "pinch". */
    int vx[4], vy[4];
    int left_h_before, right_h_before;
    int left_h_after, right_h_after;

    saturn_distort_flip_quad(0, 0, 40, 56, 3, 12, vx, vy);
    left_h_before = vy[3] - vy[0];
    right_h_before = vy[2] - vy[1];

    saturn_distort_flip_quad(0, 0, 40, 56, 9, 12, vx, vy);
    left_h_after = vy[3] - vy[0];
    right_h_after = vy[2] - vy[1];

    CUI_ASSERT((left_h_before > right_h_before) == (left_h_after < right_h_after));
}

CUI_TEST(flip_quad_survives_odd_frame_counts_and_out_of_range_step)
{
    int vx[4], vy[4];

    /* frames not a clean multiple of 4 - must not divide by zero or
     * crash, and must still respect the clamp. */
    saturn_distort_flip_quad(0, 0, 40, 56, 3, 7, vx, vy);
    CUI_ASSERT(vx[1] - vx[0] >= 1);

    /* Out-of-range step values must clamp into [0, frames], not read
     * garbage or go negative: step=-5 clamps to step=0 (full width). */
    saturn_distort_flip_quad(0, 0, 40, 56, -5, 12, vx, vy);
    CUI_ASSERT_EQ(40, vx[1] - vx[0]);

    saturn_distort_flip_quad(0, 0, 40, 56, 999, 12, vx, vy);
    CUI_ASSERT(vx[1] - vx[0] >= 1);

    /* frames <= 0 must not divide by zero. */
    saturn_distort_flip_quad(0, 0, 40, 56, 0, 0, vx, vy);
    CUI_ASSERT(vx[1] - vx[0] >= 1);
}

/*============================================================================
 * saturn_distort_encode_quad / encode_flip / encode_mesh_dissolve
 *============================================================================*/

CUI_TEST(encode_quad_sets_distorted_sprite_command_type)
{
    saturn_vdp1_cmd_t cmd;
    int vx[4] = { 10, 30, 30, 10 };
    int vy[4] = { 10, 10, 20, 20 };

    saturn_distort_encode_quad(&cmd, vx, vy, 32, 32, 0x1000, 20, false);

    /* Comm field = 0010B, ST-013-R3 section 7.6 (VDP1_Manual.txt:5169). */
    CUI_ASSERT_EQ(VDP1_CMD_DISTORTED_SPRITE, cmd.ctrl);
}

CUI_TEST(encode_quad_copies_all_four_vertices_independently)
{
    saturn_vdp1_cmd_t cmd;
    int vx[4] = { 1, 2, 3, 4 };
    int vy[4] = { 5, 6, 7, 8 };

    saturn_distort_encode_quad(&cmd, vx, vy, 32, 32, 0x1000, 20, false);

    CUI_ASSERT_EQ(1, cmd.xa); CUI_ASSERT_EQ(5, cmd.ya);
    CUI_ASSERT_EQ(2, cmd.xb); CUI_ASSERT_EQ(6, cmd.yb);
    CUI_ASSERT_EQ(3, cmd.xc); CUI_ASSERT_EQ(7, cmd.yc);
    CUI_ASSERT_EQ(4, cmd.xd); CUI_ASSERT_EQ(8, cmd.yd);
}

CUI_TEST(encode_quad_mesh_flag_sets_cmdpmod_bit_8)
{
    saturn_vdp1_cmd_t cmd_off, cmd_on;
    int vx[4] = { 0, 8, 8, 0 };
    int vy[4] = { 0, 0, 8, 8 };

    saturn_distort_encode_quad(&cmd_off, vx, vy, 8, 8, 0x1000, 20, false);
    saturn_distort_encode_quad(&cmd_on, vx, vy, 8, 8, 0x1000, 20, true);

    /* Mesh Enable Bit: bit 8 (ST-013-R3 section 6.3, VDP1_Manual.txt:
     * 3338-3343; confirmed for the distorted-sprite command specifically
     * at VDP1_Manual.txt:5232-5233). */
    CUI_ASSERT_EQ(0, cmd_off.pmod & 0x0100);
    CUI_ASSERT_EQ(0x0100, cmd_on.pmod & 0x0100);

    /* Mesh must be the ONLY difference - both still 4bpp bank-mode
     * sprites with the same colour bank and texture. */
    CUI_ASSERT_EQ(cmd_off.colr, cmd_on.colr);
    CUI_ASSERT_EQ(cmd_off.srca, cmd_on.srca);
    CUI_ASSERT_EQ((uint16_t)(cmd_off.pmod | 0x0100), cmd_on.pmod);
}

CUI_TEST(encode_quad_colour_bank_matches_existing_sprite_encoder_convention)
{
    saturn_vdp1_cmd_t cmd;
    int vx[4] = { 0, 8, 8, 0 };
    int vy[4] = { 0, 0, 8, 8 };

    /* pal/saturn/saturn_vdp1.c:180 - saturn_vdp1_encode_sprite() packs
     * the CRAM bank as `cram_bank << 4`. This module must match so the
     * same palette-upload path (saturn_vdp1_upload_palette) works for
     * flip textures. */
    saturn_distort_encode_quad(&cmd, vx, vy, 8, 8, 0x1000, 20, false);
    CUI_ASSERT_EQ((uint16_t)(20 << 4), cmd.colr);
}

CUI_TEST(encode_quad_texture_address_is_byte_offset_divided_by_eight)
{
    saturn_vdp1_cmd_t cmd;
    int vx[4] = { 0, 8, 8, 0 };
    int vy[4] = { 0, 0, 8, 8 };

    saturn_distort_encode_quad(&cmd, vx, vy, 32, 40, 0x2000, 20, false);

    CUI_ASSERT_EQ(0x2000 / 8, cmd.srca);
    /* CMDSIZE: high byte = width/8, low byte = height. */
    CUI_ASSERT_EQ((uint16_t)(((32 / 8) << 8) | 40), cmd.size);
}

CUI_TEST(encode_flip_swaps_texture_at_the_midpoint)
{
    saturn_vdp1_cmd_t cmd;

    /* Design doc line 125: "texture swap at the midpoint". */
    saturn_distort_encode_flip(&cmd, 0, 0, 32, 32, 5, 12,
                                0x1000, 0x2000, 20, false);
    CUI_ASSERT_EQ(0x1000 / 8, cmd.srca);

    saturn_distort_encode_flip(&cmd, 0, 0, 32, 32, 6, 12,
                                0x1000, 0x2000, 20, false);
    CUI_ASSERT_EQ(0x2000 / 8, cmd.srca);

    saturn_distort_encode_flip(&cmd, 0, 0, 32, 32, 12, 12,
                                0x1000, 0x2000, 20, false);
    CUI_ASSERT_EQ(0x2000 / 8, cmd.srca);
}

CUI_TEST(encode_flip_produces_the_same_vertices_as_flip_quad)
{
    saturn_vdp1_cmd_t cmd;
    int vx[4], vy[4];

    saturn_distort_flip_quad(50, 40, 32, 48, 4, 12, vx, vy);
    saturn_distort_encode_flip(&cmd, 50, 40, 32, 48, 4, 12,
                                0x1000, 0x2000, 20, false);

    CUI_ASSERT_EQ(vx[0], cmd.xa); CUI_ASSERT_EQ(vy[0], cmd.ya);
    CUI_ASSERT_EQ(vx[1], cmd.xb); CUI_ASSERT_EQ(vy[1], cmd.yb);
    CUI_ASSERT_EQ(vx[2], cmd.xc); CUI_ASSERT_EQ(vy[2], cmd.yc);
    CUI_ASSERT_EQ(vx[3], cmd.xd); CUI_ASSERT_EQ(vy[3], cmd.yd);
}

CUI_TEST(encode_mesh_dissolve_is_undistorted_and_mesh_forced_on)
{
    saturn_vdp1_cmd_t cmd;

    saturn_distort_encode_mesh_dissolve(&cmd, 100, 80, 40, 56, 0x1000, 20);

    /* Plain rectangle - no flip skew applied to the dissolve quad. */
    CUI_ASSERT_EQ(80, cmd.xa);
    CUI_ASSERT_EQ(120, cmd.xb);
    CUI_ASSERT_EQ(120, cmd.xc);
    CUI_ASSERT_EQ(80, cmd.xd);
    CUI_ASSERT_EQ(cmd.ya, cmd.yb);
    CUI_ASSERT_EQ(cmd.yc, cmd.yd);

    /* Mesh Enable bit forced on. */
    CUI_ASSERT_EQ(0x0100, cmd.pmod & 0x0100);
}
