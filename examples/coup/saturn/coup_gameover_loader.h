/**
 * coup_gameover_loader.h - Game Over Background Image Loader
 *
 * Loads the full-screen game over background image into VDP1 VRAM
 * as horizontal strips (each with its own 15-color palette for
 * maximum color quality).
 *
 * Usage:
 *   coup_gameover_load();   // Once at startup (after coup_sprites_load())
 *   coup_gameover_draw();   // Each frame during GAME_OVER screen
 */

#ifndef COUP_GAMEOVER_LOADER_H
#define COUP_GAMEOVER_LOADER_H

#include <stdbool.h>

/**
 * Load game over background strip data into VDP1 VRAM and palettes to CRAM.
 * Call once after coup_sprites_load() (uses VRAM/CRAM space after sprites).
 */
void coup_gameover_load(void);

/**
 * Draw the full-screen game over background.
 * Draws all 7 horizontal strips to cover the 320x224 screen.
 * Uses 7 VDP1 sprite commands.
 *
 * @return true if all strips drawn, false if VDP1 budget exceeded
 */
bool coup_gameover_draw(void);

/**
 * Check if game over background has been loaded.
 */
bool coup_gameover_loaded(void);

#endif /* COUP_GAMEOVER_LOADER_H */
