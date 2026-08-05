/**
 * coup_shading.h - Animated gouraud tables.
 *
 * The design doc calls for gradients that MOVE - a sheen sweeping across the
 * logo, a halo breathing under the current player's seat, a ready pulse in
 * the lobby, a spotlight on the winner. The static tables in coup_render.c
 * (COUP_GRD_PANEL, COUP_GRD_RAISED) are uploaded once and never change.
 *
 * Every generator here is a PURE function of a phase counter, so the motion
 * can be asserted on the host: that a highlight actually travels, that a
 * pulse actually returns to where it started, that a hue is actually amber
 * and not just brighter. Animation that is only verified by looking at it is
 * animation nobody can keep working.
 *
 * Gouraud on VDP1 is write-only and costs nothing per draw (ST-013-R3
 * section 5.3), so an animated gradient is as cheap as a flat fill. The only
 * per-frame cost is re-uploading the 8-byte table.
 *
 * Corner order matches saturn_vdp1_gouraud_vshade(): A upper-left,
 * B upper-right, C lower-right, D lower-left.
 */

#ifndef COUP_SHADING_H
#define COUP_SHADING_H

#include <stdbool.h>
#include <stdint.h>

/* Phase counters are taken modulo this, so a caller can pass a free-running
 * frame counter without worrying about overflow or about picking a period. */
#define COUP_SHADING_PERIOD 120

/* Corrections are clamped to the hardware's -16..+15 (SATURN_VDP1_GRD_NEUTRAL
 * is 0x10 and the hardware subtracts it). Nothing here may exceed that or the
 * table silently saturates and the motion flattens out at the extremes. */
#define COUP_SHADING_MAX_CORRECTION 15
#define COUP_SHADING_MIN_CORRECTION (-16)

/**
 * A specular sheen travelling left to right across a quad.
 *
 * Used on the title wordmark. The design doc specifies "table values swept
 * -16..+15"; this sweeps a raised bump rather than the whole table, so the
 * highlight reads as a moving band of light instead of the entire logo
 * getting brighter and darker.
 *
 * @param out    four RGB555 gouraud words
 * @param phase  frame counter; taken modulo COUP_SHADING_PERIOD
 */
void coup_shading_sheen(uint16_t out[4], int phase);

/**
 * An amber halo that breathes, for the seat whose turn it is.
 *
 * Amber is a HUE, not just extra brightness: red is lifted most, green less,
 * blue actually pulled down. A uniform lift would only make the seat lighter
 * and would not read as "highlighted" against the other seats, which are
 * already lit panels.
 *
 * Opaque by design - the design doc section 4.6 items 7-8 rules out a
 * translucent overlay here, so the halo is part of the panel's own gradient.
 */
void coup_shading_halo(uint16_t out[4], int phase);

/**
 * A uniform brightness pulse, for a lobby slot that has readied up.
 *
 * Uniform across all four corners: this one is about the slot flashing for
 * attention, so a directional gradient would fight the painted slot frame in
 * the background art.
 */
void coup_shading_pulse(uint16_t out[4], int phase);

/**
 * The static wash for a lobby slot.
 *
 * @param occupied  true for a seated player (lit), false for an empty slot
 *                  (half luminance, per the design doc's lobby treatment)
 */
void coup_shading_wash(uint16_t out[4], bool occupied);

/**
 * A spotlight for the winner's portrait: bright at the top, falling away
 * below, with a slow bloom so the game-over screen is not static.
 */
void coup_shading_spotlight(uint16_t out[4], int phase);

/**
 * Fixed-point sine, one period over COUP_SHADING_PERIOD steps.
 *
 * Returns -1024..+1024. Integer only - this runs on an SH-2 with no FPU and
 * the build links no libm.
 */
int coup_shading_sin(int phase);

#endif /* COUP_SHADING_H */
