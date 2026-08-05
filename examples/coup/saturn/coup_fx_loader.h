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

/** Byte offset just past this loader's VDP1 data. */
uint32_t coup_fx_vram_end(void);

#endif /* COUP_FX_LOADER_H */
