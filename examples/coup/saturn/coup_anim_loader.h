/**
 * coup_anim_loader.h - Animated Sprite Loader for Coup on Saturn
 *
 * Loads animated sprite frames (24 frames per character, 5 characters)
 * into VDP1 VRAM and uploads shared palettes to CRAM.
 *
 * Usage:
 *   coup_anim_load();                          // Once at startup
 *   coup_anim_draw(COUP_CHAR_DUKE, frame, x, y);  // Each frame
 */

#ifndef COUP_ANIM_LOADER_H
#define COUP_ANIM_LOADER_H

#include <stdbool.h>

/**
 * Load all animated sprite frame data into VDP1 VRAM and palettes to CRAM.
 * Call once after coup_gameover_load() at startup.
 */
void coup_anim_load(void);

/**
 * Draw one animation frame of a character.
 *
 * @param character  Character index (0=Duke, 1=Assassin, 2=Captain,
 *                   3=Ambassador, 4=Contessa) — same as COUP_CHAR_*
 * @param frame      Frame index (0 to COUP_ANIM_FRAMES-1)
 * @param x          X position (pixels, top-left)
 * @param y          Y position (pixels, top-left)
 * @return true if drawn, false if invalid args or VDP1 budget exceeded
 */
bool coup_anim_draw(int character, int frame, int x, int y);

/**
 * Draw one animation frame of a character, scaled to a custom size.
 *
 * @param character  Character index (0-4)
 * @param frame      Frame index (0 to COUP_ANIM_FRAMES-1)
 * @param x          X position (pixels, top-left)
 * @param y          Y position (pixels, top-left)
 * @param dst_w      Display width (scaled)
 * @param dst_h      Display height (scaled)
 * @return true if drawn, false if invalid args or VDP1 budget exceeded
 */
bool coup_anim_draw_scaled(int character, int frame, int x, int y,
                            int dst_w, int dst_h);

/**
 * Check if animated sprites have been loaded.
 */
bool coup_anim_loaded(void);

#endif /* COUP_ANIM_LOADER_H */
