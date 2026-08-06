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
#include <stdint.h>

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

/**
 * Texture offset + CRAM bank of one animation frame, for callers that issue
 * their own VDP1 commands rather than going through coup_anim_draw[_scaled]()
 * - the game-over entrance dissolve (saturn_distort_draw_mesh_dissolve())
 * needs the raw texture to build its own Distorted Sprite command, the same
 * way coup_ui_texture() serves the coin-payout animation in coup_fx_loader.h.
 *
 * @return true and fills out_offset and out_bank if loaded and in range,
 *         false (leaving outputs untouched) otherwise
 */
bool coup_anim_texture(int character, int frame,
                        uint32_t* out_offset, int* out_bank);

/**
 * Byte offset just past this loader's VDP1 data, and the first free CRAM
 * bank after its palettes. Loaders CHAIN - never recompute a base from
 * constants. See coup_sprites_vram_end() for why.
 * Valid after coup_anim_load().
 */
uint32_t coup_anim_vram_end(void);
int coup_anim_cram_end_bank(void);

#endif /* COUP_ANIM_LOADER_H */
