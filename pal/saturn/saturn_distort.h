/**
 * saturn_distort.h - VDP1 distorted-sprite card-flip + mesh-dissolve
 *
 * Implements the "card reveal/loss" effect from the visual-facelift design
 * doc (docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md,
 * section 4.2 "Game": "card reveal/loss = distorted-sprite Y-axis flip
 * (trapezoid collapse to the centre line over 12 frames, texture swap at
 * the midpoint, then expand - ST-013-R3 section 7.6, samples S_4_3_1/2);
 * influence-loss = card flip to back + mesh-dissolve out") and gate G6
 * (design doc line 311): "card-flip midpoint frame: quad width <= 2 px at
 * frame N/2, full art restored at frame N - RED fixture: skip the
 * degenerate-quad clamp".
 *
 * -- Hardware background --
 *
 * The VDP1 Distorted Sprite draw command (Comm field = 0010B, ST-013-R3
 * "VDP1 User's Manual" section 7.6, p.124) draws a textured quad from four
 * INDEPENDENT vertex pairs (CMDXA/YA..CMDXD/YD). Unlike the Scaled Sprite
 * command it has no single "zoom point" - each corner moves on its own,
 * which is exactly what is needed to fake a card rotating about its
 * vertical axis: the left/right edges sweep toward the centre line as the
 * card turns "edge-on" and the top/bottom edges converge unevenly (a
 * trapezoid, not a plain squash) because one edge is nearer the viewer
 * than the other during the turn. The classic reference for this
 * envelope is a flat polygon rotated about its Y axis, demonstrated by
 * SGL samples S_4_3_1 (Y-axis only) and S_4_3_2 (X+Y axis)
 * (NOV96_DTS/LIBRARY/SDK_10J/SGL302/SAMPLE/S_4_3_1/MAIN.C,
 * S_4_3_2/MAIN.C): as the rotation angle sweeps through 90 degrees the
 * on-screen width collapses toward zero and then re-expands as the far
 * face comes into view. This module reproduces that width/skew envelope
 * with integer arithmetic (no libm sin/cos - SH-2 on Saturn has no FPU,
 * and no other file under pal/saturn/ uses float/math.h; saturn_fade.c
 * uses a plain integer lerp, and this module follows the same
 * convention) instead of driving SGL's 3D matrix pipeline, because the
 * PAL talks to bare VDP1 commands directly (see saturn_vdp1.c).
 *
 * VDP1 precautions checked against ST-013-R3 (sega_saturn_docs/
 * VDP1_Manual.txt):
 *   - Distorted-sprite vertex degeneracy is a DOCUMENTED mode, not a
 *     crash: "When two vertices are set at the same coordinates, the
 *     sprite is drawn as a triangle. When four vertices are set at the
 *     same coordinates, the sprite is drawn as one pixel." (VDP1_Manual
 *     .txt:5259-5261, section 7.6). That is a legal but visually wrong
 *     outcome for an *animation midpoint* we do not want to be a single
 *     pixel or a triangle - the design calls for a visible collapsed
 *     sliver, so this module clamps the half-width away from zero
 *     instead of relying on the hardware's degenerate-vertex behaviour.
 *     This clamp is gate G6's entire subject.
 *   - "Distorted sprites are created by skewing the character pattern...
 *     there may be some pixels that are written twice" (VDP1_Manual
 *     .txt:758-761, p.8) - expected artifact of any distorted sprite,
 *     not specific to this effect, noted for completeness.
 *   - Mesh Enable is CMDPMOD bit 8 (VDP1_Manual.txt:3338-3343, section
 *     6.3 "Mesh Enable Bit: bit 8" - "When it is '1,' the part is drawn
 *     with mesh processing" i.e. only pixels where
 *     (X coordinate + Y coordinate) is even are drawn, a fixed 50%
 *     checkerboard). The distorted-sprite command definition explicitly
 *     lists mesh as one of its configured bits (VDP1_Manual.txt:
 *     5232-5233, section 7.6: "Mesh is enabled, end code is disabled,
 *     and transparent pixel is disabled."). Confirmed identically in
 *     the CMDPMOD field table at VDP1_Manual.txt:2821 and :6132
 *     ("...Clip Cmod Mesh ECD SPD...").
 */

#ifndef SATURN_DISTORT_H
#define SATURN_DISTORT_H

#include <stdint.h>
#include <stdbool.h>

#include "saturn_vdp1.h"

/*============================================================================
 * Tunables
 *============================================================================*/

/**
 * Minimum HALF-width (px) the flip quad is allowed to collapse to.
 *
 * At the true geometric midpoint the left/right edges meet exactly on
 * the centre line, giving a full quad width of 0px. ST-013-R3 permits
 * this (a distorted sprite with all-equal X vertices degenerates to a
 * one-pixel or triangle draw per VDP1_Manual.txt:5259-5261) but that is
 * not what the design wants at the flip's midpoint - it wants a visible
 * "edge-on" sliver, gate G6: "quad width <= 2 px at frame N/2". A
 * half-width floor of 1px yields a total width floor of 2px, satisfying
 * G6 exactly while staying comfortably clear of the hardware's zero-
 * vertex degenerate modes.
 */
#define SATURN_DISTORT_MIN_HALF_WIDTH 1

/** Peak perspective skew numerator/denominator (skew fraction = MAX/DIV
 * of the half-height, applied to make one edge taller and the other
 * shorter as the card turns - see saturn_distort_flip_quad() below). */
#define SATURN_DISTORT_SKEW_MAX 4
#define SATURN_DISTORT_SKEW_DIV 16

/*============================================================================
 * Pure geometry (host-testable, no hardware access)
 *============================================================================*/

/**
 * Compute the four quad vertices for a Y-axis card-flip at animation
 * step `step` of `frames` total steps.
 *
 * Envelope (matches design doc line 124-126 and gate G6):
 *   step == 0                : full width w, flat rectangle (front face)
 *   step == frames / 2       : collapsed to a clamped sliver (<= 2px wide)
 *   step == frames           : full width w restored, flat rectangle
 *
 * Between those points the half-width follows a linear V-shape (the
 * same shape saturn_fade_interp() uses for its ramp - integer lerp, no
 * trig) and the top/bottom edges pick up an antisymmetric "skew" that
 * peaks at the quarter and three-quarter points and is zero at 0,
 * frames/2 and frames. That skew is what makes the quad a TRAPEZOID
 * (one vertical edge taller than the other) rather than a symmetric
 * squash, matching the perspective a real rotating rectangle produces
 * when one edge is briefly nearer the viewer than the other (the S_4_3_1
 * /S_4_3_2 Y-axis-rotation samples show the same width-collapse envelope
 * driven through SGL's 3D pipeline instead of by hand).
 *
 * Output vertex order matches the house convention used throughout
 * pal/saturn/saturn_vdp1.c (saturn_vdp1_encode_polygon,
 * saturn_vdp1_draw_sprite_scaled): index 0 = A (top-left), 1 = B
 * (top-right), 2 = C (bottom-right), 3 = D (bottom-left).
 *
 * Pure function - no globals, no hardware access, fully host-testable.
 *
 * @param cx,cy      Centre of the card, in screen pixels
 * @param w,h         Full (undistorted) card width/height, in pixels
 * @param step        Current animation step, clamped to [0, frames]
 * @param frames      Total steps in the flip (>= 1; clamped internally)
 * @param out_x,out_y Four-element arrays receiving vertex coordinates
 */
void saturn_distort_flip_quad(int cx, int cy, int w, int h,
                               int step, int frames,
                               int out_x[4], int out_y[4]);

/**
 * Encode a VDP1 Distorted Sprite command from four explicit vertices.
 *
 * Low-level, reusable builder shared by the flip and mesh-dissolve
 * encoders below - mirrors the house style of
 * saturn_vdp1_encode_polygon() (pal/saturn/saturn_vdp1.c:34-84): zeroes
 * the struct, fills CMDCTRL/CMDPMOD/CMDCOLR/CMDSRCA/CMDSIZE and the four
 * vertex pairs, does not touch VRAM.
 *
 * CMDCTRL is set to VDP1_CMD_DISTORTED_SPRITE (0x0002), reusing the
 * constant already declared in saturn_vdp1.h - Comm field = 0010B per
 * ST-013-R3 section 7.6 (VDP1_Manual.txt:5169, 5175).
 *
 * CMDPMOD reuses SATURN_VDP1_SPR_PMOD (ECD disable + 16-colour bank
 * mode, transparent index 0 - saturn_vdp1.h:107), the same base every
 * other 4bpp textured sprite command in this codebase uses
 * (saturn_vdp1_draw_sprite, saturn_vdp1_draw_sprite_scaled), OR'd with
 * the Mesh Enable bit (CMDPMOD bit 8, 0x0100) when `mesh` is true - see
 * the file-header citation above for the exact manual passage.
 *
 * CMDCOLR uses `cram_bank << 4`, identical to the existing sprite
 * encoder (pal/saturn/saturn_vdp1.c:180, saturn_vdp1_encode_sprite).
 *
 * @param cmd        Output command structure
 * @param vx,vy      Four vertex coordinates, A/B/C/D order
 * @param src_w,src_h Source texture dimensions (src_w must be a
 *                    multiple of 8, matching CMDSIZE's X/8 field)
 * @param tex_offset Byte offset of the texture within VDP1 VRAM
 * @param cram_bank  CRAM palette bank (0-127)
 * @param mesh       Set CMDPMOD Mesh Enable bit (50% checkerboard)
 */
void saturn_distort_encode_quad(saturn_vdp1_cmd_t* cmd,
                                 const int vx[4], const int vy[4],
                                 int src_w, int src_h,
                                 uint32_t tex_offset,
                                 int cram_bank,
                                 bool mesh);

/**
 * Encode one frame of the card-flip animation.
 *
 * Combines saturn_distort_flip_quad() for the vertices with a texture
 * swap at the midpoint (design doc line 125: "texture swap at the
 * midpoint"): steps [0, frames/2) use `tex_offset_front`, steps
 * [frames/2, frames] use `tex_offset_back`. `mesh` is threaded through
 * unchanged so a caller can run the flip and a mesh-dissolve
 * simultaneously if a future effect wants that (not exercised by the v1
 * "influence-loss" sequence below, which runs them one after another).
 *
 * Pure function - safe to unit test without hardware.
 */
void saturn_distort_encode_flip(saturn_vdp1_cmd_t* cmd,
                                 int cx, int cy, int w, int h,
                                 int step, int frames,
                                 uint32_t tex_offset_front,
                                 uint32_t tex_offset_back,
                                 int cram_bank,
                                 bool mesh);

/**
 * Encode a plain, undistorted quad with Mesh Enable forced on.
 *
 * This is the "mesh-dissolve out" half of "influence-loss = card flip
 * to back + mesh-dissolve out" (design doc line 126): after the flip
 * finishes on the back face, this draws that same back face with the
 * VDP1 mesh bit set so every other pixel (X+Y even, VDP1_Manual.txt:
 * 3549-3551) is skipped, producing the fixed 50% checkerboard
 * "dissolve" look. Mesh processing is chosen over half-transparency
 * because it is free (no framebuffer read, VDP1_Manual.txt:3538-3554)
 * and, per the design doc's own constraint sweep (section 4.6 item 1),
 * it "works on palette text too" - unlike half-transparency/shadow,
 * which silently replace rather than blend colour-bank (MSB=0) pixels
 * such as this codebase's 4bpp sprites and font.
 *
 * Pure function - safe to unit test without hardware.
 */
void saturn_distort_encode_mesh_dissolve(saturn_vdp1_cmd_t* cmd,
                                          int cx, int cy, int w, int h,
                                          uint32_t tex_offset,
                                          int cram_bank);

/*============================================================================
 * Hardware draw entry points (Saturn-only VRAM writes)
 *============================================================================*/

/**
 * Draw one frame of the card-flip animation directly to VDP1 VRAM.
 *
 * IMPORTANT - integration note: saturn_vdp1.c's per-frame command queue
 * (g_cmd_buffer / g_cmd_buffer_count / saturn_vdp1_write_cmd) is `static`
 * to that file and is not exposed by saturn_vdp1.h, so this module
 * cannot enqueue into it without modifying saturn_vdp1.c/.h (out of
 * scope for pal-saturn-agent's write access on this task - see the
 * task's file-write boundary). Instead this function writes the encoded
 * command directly to an explicit VDP1 VRAM command-table byte offset
 * supplied by the caller, using the same raw word-copy technique
 * saturn_vdp1_upload_texture()/saturn_vdp1_upload_palette() already use
 * for out-of-band VRAM writes (pal/saturn/saturn_vdp1.c:138-163), and
 * the same per-word copy saturn_vdp1_flush_cmds() uses to publish its
 * own buffered commands (pal/saturn/saturn_vdp1.c:367-375).
 *
 * The caller (integration owner) is responsible for choosing a
 * `cmd_vram_offset` that is actually reachable by VDP1's command chain
 * for the current frame (a reserved slot linked in via CMDLINK/JP, or a
 * dedicated slot range carved out of saturn_vdp1.c's queue) - this
 * function only performs the raw write, it does not manage the chain.
 *
 * @param cmd_vram_offset Byte offset within VDP1 VRAM (from
 *                        SATURN_VDP1_VRAM) to write the 32-byte command
 *                        table entry to. Caller-owned; must be 32-byte
 *                        aligned per ST-013-R3 command-table boundary
 *                        rules (VDP1_Manual.txt precaution "Store
 *                        command tables to address 000000H of VRAM" /
 *                        "lower 2 bits of CMDLINK become 00H",
 *                        VDP1_Manual.txt:6877, 3297).
 * @return true if the command was written, false if w/h were degenerate
 *         (<= 0) and nothing was drawn
 */
bool saturn_distort_draw_flip(uint32_t cmd_vram_offset,
                               int cx, int cy, int w, int h,
                               int step, int frames,
                               uint32_t tex_offset_front,
                               uint32_t tex_offset_back,
                               int cram_bank,
                               bool mesh);

/**
 * Draw the mesh-dissolve quad directly to VDP1 VRAM. Same integration
 * caveat as saturn_distort_draw_flip() above - the caller must place
 * `cmd_vram_offset` on a live command chain.
 *
 * @return true if the command was written, false if w/h were degenerate
 */
bool saturn_distort_draw_mesh_dissolve(uint32_t cmd_vram_offset,
                                        int cx, int cy, int w, int h,
                                        uint32_t tex_offset,
                                        int cram_bank);

#endif /* SATURN_DISTORT_H */
