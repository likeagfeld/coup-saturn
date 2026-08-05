/**
 * saturn_distort.c - VDP1 distorted-sprite card-flip + mesh-dissolve
 *
 * See saturn_distort.h for the full hardware/citation background.
 */

#include "saturn_distort.h"

#include <string.h>
#include <stdlib.h> /* abs() */

/*============================================================================
 * Pure geometry
 *============================================================================*/

void saturn_distort_flip_quad(int cx, int cy, int w, int h,
                               int step, int frames,
                               int out_x[4], int out_y[4])
{
    int half_w, half_h;
    int fold;
    int half_width;
    int q;
    int skew;
    int h_left, h_right;

    if (!out_x || !out_y) {
        return;
    }
    if (frames < 1) {
        frames = 1;
    }
    if (step < 0) {
        step = 0;
    }
    if (step > frames) {
        step = frames;
    }

    half_w = w / 2;
    half_h = h / 2;

    /* Width envelope: linear V-shape, full half-width at the two
     * endpoints, zero at the midpoint - the same collapse-then-expand
     * shape a flat rectangle traces on screen when rotated 0->90->180
     * degrees about its vertical axis (S_4_3_1/S_4_3_2). `fold` is the
     * distance (in steps) from the midpoint: frames/2 at either end,
     * 0 exactly at the midpoint. */
    fold = abs(step - frames / 2);
    half_width = (half_w * fold * 2) / frames;

    /* Degenerate-quad clamp (gate G6). At the exact midpoint `fold` is
     * 0, so the unclamped half-width above is 0 - a true zero-width
     * quad. ST-013-R3 permits all-equal-X vertices (the sprite is then
     * drawn as a one-pixel line, VDP1_Manual.txt:5259-5261) but that is
     * not the animation's intended midpoint frame: the design wants a
     * visible "edge-on" sliver, not a hardware degenerate mode. Floor
     * the half-width at SATURN_DISTORT_MIN_HALF_WIDTH so the total quad
     * width never drops below 2px. */
    if (half_width < SATURN_DISTORT_MIN_HALF_WIDTH) {
        half_width = SATURN_DISTORT_MIN_HALF_WIDTH;
    }

    /* Perspective skew: triangle wave built from integer segments
     * (house style - no float/libm, see saturn_fade.c's plain integer
     * lerp). Zero at step=0, +SKEW_MAX at the quarter point, zero at
     * the midpoint, -SKEW_MAX at three-quarters, zero again at
     * step=frames. This traces the same envelope sin(2*theta) would for
     * theta = step/frames * PI, without any trig call. */
    q = frames / 4;
    if (q < 1) {
        q = 1;
    }
    if (step <= q) {
        skew = (SATURN_DISTORT_SKEW_MAX * step) / q;
    } else if (step <= 2 * q) {
        skew = (SATURN_DISTORT_SKEW_MAX * (2 * q - step)) / q;
    } else if (step <= 3 * q) {
        skew = -(SATURN_DISTORT_SKEW_MAX * (step - 2 * q)) / q;
    } else {
        skew = -(SATURN_DISTORT_SKEW_MAX * (frames - step)) / q;
    }
    if (skew > SATURN_DISTORT_SKEW_MAX) {
        skew = SATURN_DISTORT_SKEW_MAX;
    }
    if (skew < -SATURN_DISTORT_SKEW_MAX) {
        skew = -SATURN_DISTORT_SKEW_MAX;
    }

    h_left = half_h - (half_h * skew) / SATURN_DISTORT_SKEW_DIV;
    h_right = half_h + (half_h * skew) / SATURN_DISTORT_SKEW_DIV;
    if (h_left < 1) {
        h_left = 1;
    }
    if (h_right < 1) {
        h_right = 1;
    }

    /* A = top-left, B = top-right, C = bottom-right, D = bottom-left -
     * matches the vertex-order convention in saturn_vdp1.c
     * (saturn_vdp1_encode_polygon, saturn_vdp1_draw_sprite_scaled). */
    out_x[0] = cx - half_width;
    out_y[0] = cy - h_left;

    out_x[1] = cx + half_width;
    out_y[1] = cy - h_right;

    out_x[2] = cx + half_width;
    out_y[2] = cy + h_right;

    out_x[3] = cx - half_width;
    out_y[3] = cy + h_left;
}

/*============================================================================
 * Command encoding
 *============================================================================*/

void saturn_distort_encode_quad(saturn_vdp1_cmd_t* cmd,
                                 const int vx[4], const int vy[4],
                                 int src_w, int src_h,
                                 uint32_t tex_offset,
                                 int cram_bank,
                                 bool mesh)
{
    if (!cmd || !vx || !vy) {
        return;
    }

    memset(cmd, 0, sizeof(*cmd));

    /* Distorted Sprite: Comm field = 0010B (ST-013-R3 section 7.6,
     * VDP1_Manual.txt:5169, 5175). */
    cmd->ctrl = VDP1_CMD_DISTORTED_SPRITE;
    cmd->link = 0x0000;

    /* Same 4bpp bank-mode base every other textured sprite command in
     * this codebase uses (saturn_vdp1.h:107, SATURN_VDP1_SPR_PMOD),
     * OR'd with Mesh Enable (CMDPMOD bit 8, ST-013-R3 section 6.3,
     * VDP1_Manual.txt:3338-3343) when requested. */
    cmd->pmod = (uint16_t)(SATURN_VDP1_SPR_PMOD
                            | (mesh ? 0x0100u : 0x0000u));

    /* Colour bank, matching saturn_vdp1_encode_sprite()'s convention
     * (pal/saturn/saturn_vdp1.c:180): bank number in bits [10:4]. */
    cmd->colr = (uint16_t)(cram_bank << 4);

    /* Texture address in VDP1 VRAM, divided by 8 (CMDSRCA). */
    cmd->srca = (uint16_t)(tex_offset / 8);

    /* CMDSIZE: high byte = width/8, low byte = height. */
    cmd->size = (uint16_t)(((src_w / 8) << 8) | src_h);

    cmd->xa = (int16_t)vx[0];
    cmd->ya = (int16_t)vy[0];
    cmd->xb = (int16_t)vx[1];
    cmd->yb = (int16_t)vy[1];
    cmd->xc = (int16_t)vx[2];
    cmd->yc = (int16_t)vy[2];
    cmd->xd = (int16_t)vx[3];
    cmd->yd = (int16_t)vy[3];

    cmd->grda = 0x0000;
}

void saturn_distort_encode_flip(saturn_vdp1_cmd_t* cmd,
                                 int cx, int cy, int w, int h,
                                 int step, int frames,
                                 uint32_t tex_offset_front,
                                 uint32_t tex_offset_back,
                                 int cram_bank,
                                 bool mesh)
{
    int vx[4], vy[4];
    uint32_t tex_offset;

    if (!cmd) {
        return;
    }
    if (frames < 1) {
        frames = 1;
    }

    saturn_distort_flip_quad(cx, cy, w, h, step, frames, vx, vy);

    /* "texture swap at the midpoint" (design doc line 125). */
    tex_offset = (step < frames / 2) ? tex_offset_front : tex_offset_back;

    saturn_distort_encode_quad(cmd, vx, vy, w, h, tex_offset, cram_bank, mesh);
}

void saturn_distort_encode_mesh_dissolve(saturn_vdp1_cmd_t* cmd,
                                          int cx, int cy, int w, int h,
                                          uint32_t tex_offset,
                                          int cram_bank)
{
    int vx[4], vy[4];
    int half_w, half_h;

    if (!cmd) {
        return;
    }

    half_w = w / 2;
    half_h = h / 2;

    vx[0] = cx - half_w; vy[0] = cy - half_h;
    vx[1] = cx + half_w; vy[1] = cy - half_h;
    vx[2] = cx + half_w; vy[2] = cy + half_h;
    vx[3] = cx - half_w; vy[3] = cy + half_h;

    saturn_distort_encode_quad(cmd, vx, vy, w, h, tex_offset, cram_bank, true);
}

/*============================================================================
 * Hardware draw entry points
 *============================================================================*/

#ifdef __SATURN__
static void saturn_distort_write_cmd(uint32_t cmd_vram_offset,
                                      const saturn_vdp1_cmd_t* cmd)
{
    /* Same raw per-word copy saturn_vdp1_upload_texture()/
     * saturn_vdp1_upload_palette() use for out-of-band VRAM writes, and
     * saturn_vdp1_flush_cmds() uses to publish its own buffered
     * commands (pal/saturn/saturn_vdp1.c:138-163, 367-375). */
    volatile uint16_t* dst = (volatile uint16_t*)(uintptr_t)(
        SATURN_VDP1_VRAM + cmd_vram_offset);
    const uint16_t* src = (const uint16_t*)cmd;
    uint32_t w;

    for (w = 0; w < sizeof(*cmd) / 2; w++) {
        dst[w] = src[w];
    }
}
#endif

bool saturn_distort_draw_flip(uint32_t cmd_vram_offset,
                               int cx, int cy, int w, int h,
                               int step, int frames,
                               uint32_t tex_offset_front,
                               uint32_t tex_offset_back,
                               int cram_bank,
                               bool mesh)
{
    saturn_vdp1_cmd_t cmd;

    if (w <= 0 || h <= 0) {
        return false;
    }

    saturn_distort_encode_flip(&cmd, cx, cy, w, h, step, frames,
                                tex_offset_front, tex_offset_back,
                                cram_bank, mesh);

#ifdef __SATURN__
    saturn_distort_write_cmd(cmd_vram_offset, &cmd);
#else
    (void)cmd_vram_offset;
#endif

    return true;
}

bool saturn_distort_draw_mesh_dissolve(uint32_t cmd_vram_offset,
                                        int cx, int cy, int w, int h,
                                        uint32_t tex_offset,
                                        int cram_bank)
{
    saturn_vdp1_cmd_t cmd;

    if (w <= 0 || h <= 0) {
        return false;
    }

    saturn_distort_encode_mesh_dissolve(&cmd, cx, cy, w, h, tex_offset, cram_bank);

#ifdef __SATURN__
    saturn_distort_write_cmd(cmd_vram_offset, &cmd);
#else
    (void)cmd_vram_offset;
#endif

    return true;
}
