/**
 * coup_gameover_loader.c - Game Over Background Image Loader
 *
 * Uploads the game over screen background image to VDP1 VRAM as
 * 7 horizontal strips (320x32 each), each with its own 15-color
 * palette. This approach gives far better color quality than a
 * single 15-color palette for the entire detailed image.
 *
 * VDP1 VRAM: Strips are placed after existing sprite data.
 * CRAM: Strip palettes use banks 25-31 (after sprite banks 16-24).
 */

#include "coup_gameover_loader.h"
#include "coup_gameover_data.h"
#include "coup_sprites.h"
#include "saturn_vdp1.h"

/*============================================================================
 * CRAM Bank Assignment
 *============================================================================*/

/* Game over strip palettes start after sprite palettes.
 * Sprites use banks 16-24 (9 banks for COUP_SPR_COUNT sprites).
 * Gameover strips use banks 25-31 (7 banks). */
#define GAMEOVER_CRAM_BASE_BANK  (16 + COUP_SPR_COUNT)

/*============================================================================
 * State
 *============================================================================*/

/* VDP1 VRAM offsets for each strip's texture data */
static uint32_t s_strip_tex_offsets[GAMEOVER_STRIP_COUNT];
static bool s_loaded = false;

/*============================================================================
 * Public API
 *============================================================================*/

void coup_gameover_load(void)
{
    int i;
    /* Start after font + sprite data (8-byte aligned) */
    uint32_t vram_cursor = (SATURN_VDP1_APP_TEX_START + COUP_SPR_TOTAL_SIZE + 7) & ~7;

    for (i = 0; i < GAMEOVER_STRIP_COUNT; i++) {
        uint16_t data_size = gameover_strip_sizes[i];

        /* Record absolute VDP1 VRAM offset */
        s_strip_tex_offsets[i] = SATURN_VDP1_TEX_OFFSET + vram_cursor;

        /* Copy strip pixel data to VDP1 VRAM */
        saturn_vdp1_upload_texture(vram_cursor, gameover_strip_data[i], data_size);

        /* Upload strip palette to CRAM */
        saturn_vdp1_upload_palette(GAMEOVER_CRAM_BASE_BANK + i,
                                   gameover_strip_palettes[i]);

        /* Advance cursor (8-byte aligned) */
        vram_cursor += (data_size + 7) & ~7;
    }

    s_loaded = true;
}

bool coup_gameover_draw(void)
{
    int i;

    if (!s_loaded) return false;

    for (i = 0; i < GAMEOVER_STRIP_COUNT; i++) {
        int y = i * GAMEOVER_STRIP_H;

        if (!saturn_vdp1_draw_sprite(
                0, y,
                GAMEOVER_STRIP_W, GAMEOVER_STRIP_H,
                s_strip_tex_offsets[i],
                GAMEOVER_CRAM_BASE_BANK + i)) {
            return false;  /* VDP1 budget exceeded */
        }
    }

    return true;
}

bool coup_gameover_loaded(void)
{
    return s_loaded;
}
