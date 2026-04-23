/**
 * coup_anim_loader.c - Animated Sprite Loading for Coup on Saturn
 *
 * Copies animated sprite frame data to VDP1 VRAM texture area and
 * uploads per-character 16-color palettes to VDP2 CRAM.
 *
 * VDP1 VRAM: Animation frames placed after gameover strip data.
 * CRAM: Palettes use banks 32-36 (after gameover banks 25-31).
 *
 * Layout: 5 characters x 24 frames x 768 bytes = 92,160 bytes total.
 * Each character's 24 frames are stored contiguously in VRAM.
 */

#include "coup_anim_loader.h"
#include "coup_anim_sprites.h"
#include "coup_anim_sprite_data.h"
#include "coup_sprites.h"
#include "saturn_vdp1.h"

/* Include gameover data for VRAM offset calculation */
#include "coup_gameover_data.h"

/*============================================================================
 * CRAM Bank Assignment
 *============================================================================*/

/* Animation palettes start after gameover strip palettes.
 * Sprites: banks 16-24 (9 banks)
 * Gameover: banks 25-31 (7 banks)
 * Animation: banks 32-36 (5 banks, one per character) */
#define ANIM_CRAM_BASE_BANK  (16 + COUP_SPR_COUNT + GAMEOVER_STRIP_COUNT)

/*============================================================================
 * State
 *============================================================================*/

/* VDP1 VRAM offsets for each character's first frame.
 * Subsequent frames are at offset + frame_index * COUP_ANIM_FRAME_SIZE. */
static uint32_t s_char_base_offsets[COUP_ANIM_CHARS];
static bool s_loaded = false;

/*============================================================================
 * Public API
 *============================================================================*/

void coup_anim_load(void)
{
    int ci, fi;

    /* Calculate VRAM cursor: start after font + sprites + gameover strips.
     * This mirrors how coup_gameover_loader.c calculates its start. */
    uint32_t vram_cursor;
    uint32_t gameover_total;
    int i;

    /* Start after existing sprites (same as gameover loader does) */
    vram_cursor = (SATURN_VDP1_APP_TEX_START + COUP_SPR_TOTAL_SIZE + 7) & ~7;

    /* Skip past gameover strip data */
    gameover_total = 0;
    for (i = 0; i < GAMEOVER_STRIP_COUNT; i++) {
        gameover_total += (gameover_strip_sizes[i] + 7) & ~7;
    }
    vram_cursor += gameover_total;

    /* Upload animation frames for each character */
    for (ci = 0; ci < COUP_ANIM_CHARS; ci++) {
        /* Record the base VRAM offset for this character's first frame */
        s_char_base_offsets[ci] = SATURN_VDP1_TEX_OFFSET + vram_cursor;

        /* Upload all frames for this character contiguously */
        for (fi = 0; fi < COUP_ANIM_FRAMES; fi++) {
            saturn_vdp1_upload_texture(vram_cursor,
                                        coup_animdata_all[ci][fi],
                                        COUP_ANIM_FRAME_SIZE);
            vram_cursor += (COUP_ANIM_FRAME_SIZE + 7) & ~7;
        }

        /* Upload this character's palette to CRAM */
        saturn_vdp1_upload_palette(ANIM_CRAM_BASE_BANK + ci,
                                   coup_anim_palettes[ci]);
    }

    s_loaded = true;
}

bool coup_anim_draw(int character, int frame, int x, int y)
{
    uint32_t tex_offset;

    if (!s_loaded) return false;
    if (character < 0 || character >= COUP_ANIM_CHARS) return false;
    if (frame < 0 || frame >= COUP_ANIM_FRAMES) return false;

    /* Calculate the VRAM offset for this specific frame */
    tex_offset = s_char_base_offsets[character]
                 + (uint32_t)frame * ((COUP_ANIM_FRAME_SIZE + 7) & ~7);

    return saturn_vdp1_draw_sprite(
        x, y,
        COUP_ANIM_W, COUP_ANIM_H,
        tex_offset,
        ANIM_CRAM_BASE_BANK + character
    );
}

bool coup_anim_draw_scaled(int character, int frame, int x, int y,
                            int dst_w, int dst_h)
{
    uint32_t tex_offset;

    if (!s_loaded) return false;
    if (character < 0 || character >= COUP_ANIM_CHARS) return false;
    if (frame < 0 || frame >= COUP_ANIM_FRAMES) return false;

    tex_offset = s_char_base_offsets[character]
                 + (uint32_t)frame * ((COUP_ANIM_FRAME_SIZE + 7) & ~7);

    return saturn_vdp1_draw_sprite_scaled(
        x, y,
        COUP_ANIM_W, COUP_ANIM_H,
        dst_w, dst_h,
        tex_offset,
        ANIM_CRAM_BASE_BANK + character
    );
}

bool coup_anim_loaded(void)
{
    return s_loaded;
}
