/**
 * coup_sprite_loader.h - Sprite Loading and Drawing for Coup on Saturn
 *
 * Manages VDP1 texture upload and provides a simple API to draw
 * character portraits, card backs, title logo, and icons.
 *
 * Usage:
 *   coup_sprites_load();         // Once at startup (after VDP1 init)
 *   coup_sprites_draw(COUP_SPR_DUKE, 100, 50);  // Each frame
 */

#ifndef COUP_SPRITE_LOADER_H
#define COUP_SPRITE_LOADER_H

#include <stdbool.h>
#include <stdint.h>

/* Sprite indices (from coup_sprites.h) */
#include "coup_sprites.h"

/**
 * Load all sprite data into VDP1 VRAM and upload palettes to CRAM.
 * Call once after saturn_vdp1_init() and cui_saturn_init().
 */
void coup_sprites_load(void);

/**
 * Draw a sprite at the given screen position.
 *
 * @param sprite_id   One of COUP_SPR_* enum values
 * @param x           X position (pixels, top-left)
 * @param y           Y position (pixels, top-left)
 * @return true if drawn, false if VDP1 budget exceeded or invalid ID
 */
bool coup_sprites_draw(int sprite_id, int x, int y);

/**
 * Check if sprites have been loaded.
 */
bool coup_sprites_loaded(void);

/**
 * Byte offset (within the VDP1 texture area) just past this loader's data.
 *
 * Loaders must CHAIN their allocations. Recomputing a base from constants such
 * as SATURN_VDP1_APP_TEX_START + COUP_SPR_TOTAL_SIZE silently assumes exactly
 * one registered font, and breaks the moment another is added: the sprites
 * move up and the later loaders overwrite them.
 *
 * Valid only after coup_sprites_load().
 */
uint32_t coup_sprites_vram_end(void);

#endif /* COUP_SPRITE_LOADER_H */
