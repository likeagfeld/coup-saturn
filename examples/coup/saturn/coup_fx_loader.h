/**
 * coup_fx_loader.h - Action effects and UI sprites.
 *
 * Loads the magenta-keyed effect sequences (coup, assassinate, steal, tax,
 * exchange, block, challenge) and UI icons (coin stacks, banners, markers)
 * into VDP1 VRAM.
 *
 * Allocation CHAINS from the animated portraits, which chain from the
 * game-over strips, which chain from the sprites, which chain from the fonts.
 * Never recompute a base from constants - that assumption has broken twice in
 * this codebase (see coup_sprites_vram_end).
 */

#ifndef COUP_FX_LOADER_H
#define COUP_FX_LOADER_H

#include <stdbool.h>
#include <stdint.h>

/** Upload every effect and UI sprite. Call after coup_anim_load(). */
void coup_fx_load(void);

/** True once coup_fx_load() has run. */
bool coup_fx_loaded(void);

/**
 * Draw one frame of an effect sequence.
 *
 * @param fx     COUP_FX_* index
 * @param frame  frame number; wrapped into range
 * @param x,y    top-left in screen pixels
 * @return true if a command was queued
 */
bool coup_fx_draw(int fx, int frame, int x, int y);

/**
 * Draw an effect frame scaled about the point (cx, cy).
 *
 * The authored sizes (32x32 to 64x64) are small against 320x224 and read as a
 * flicker rather than an event. Scaling is done by VDP1 at no CPU cost.
 */
bool coup_fx_draw_scaled(int fx, int frame, int cx, int cy, int scale_num,
                         int scale_den);

/** Frame count for an effect, so callers can time a sequence. */
int coup_fx_frames(int fx);

/**
 * Draw a UI sprite (COUP_UI_* index) at its native size.
 */
bool coup_ui_draw(int ui, int x, int y);

/**
 * Draw the coin stack that best represents `coins`.
 * Picks among the 1/2/3/5/10 stacks rather than repeating a single coin.
 */
bool coup_ui_draw_coins(int coins, int x, int y);

/* Texture offset + CRAM bank of a UI sprite, for callers that issue
 * their own VDP1 commands (the coin-payout animation). */
bool coup_ui_texture(int ui, uint32_t* out_offset, int* out_bank);

/**
 * Deliberately NOT implemented: an RGB555 (colour mode 5) copy of the
 * wordmark for saturn_vdp1_draw_sprite_gouraud() gouraud lighting.
 *
 * ST-013-R3 p.94 (VDP1_Manual.txt:4091-4094) says colour calculation on the
 * wordmark's existing 4bpp colour-BANK texture "cannot be guaranteed", so a
 * sheen effect would need a second, RGB-mode copy of the same art.
 * examples/coup/assets/convert_effects.py already has this capability
 * (wordmark_rgb555_bytes(), behind its opt-in --wordmark-rgb555 flag,
 * default OFF) - but it is not wired here.
 *
 * MEASURED 2026-08-06 (facelift task): that texture is 32,768 B, and this
 * bare-SGL build's whole program (.text + .rodata, not just .data/.bss)
 * loads into WRAM-H at boot, so a VDP1 texture asset costs WRAM-H headroom
 * too, not only VDP1 VRAM. VDP1 VRAM had 192,608 B free at the time
 * (comfortable) but WRAM-H's slack under SGL's SortList was only 30,188 B
 * - the texture alone is bigger than that budget, and with the title
 * carousel's own small addition the combined build overran SortList by
 * 2,900 B (scripts/qa/verify_facelift.py gate F). Wiring this needs the
 * sprite streamed from CD, the way the background scenes already are
 * (coup_bg_index.h / gate A), rather than linked into the resident binary.
 */

#endif /* COUP_FX_LOADER_H */
