/**
 * coup_sprite_loader.c - Sprite Loading for Coup on Saturn
 *
 * Copies embedded sprite data to VDP1 VRAM texture area and
 * uploads 16-color palettes to VDP2 CRAM banks 16-24.
 *
 * VDP1 VRAM layout (texture area):
 *   Font glyphs at offset 0 (FONT_DATA_SIZE bytes)
 *   App sprites at APP_TEX_START (after font data)
 *
 * CRAM layout:
 *   Banks 0-15:  Reserved for VDP2 text palettes
 *   Banks 16-24: Sprite palettes (one per sprite)
 */

#include "coup_sprite_loader.h"
#include "coup_sprites.h"
#include "coup_sprite_data.h"
#include "saturn_vdp1.h"
#include "saturn_pal.h"

/*============================================================================
 * CRAM Bank Assignment
 *============================================================================*/

/* First sprite palette bank (banks 0-15 are used by VDP2 text) */
#define SPRITE_CRAM_BASE_BANK  16

/*============================================================================
 * State
 *============================================================================*/

/* VDP1 VRAM offsets for each sprite's texture data (from VDP1 base) */
static uint32_t s_tex_offsets[COUP_SPR_COUNT];
static bool s_loaded = false;

/*============================================================================
 * Public API
 *============================================================================*/

void coup_sprites_load(void)
{
    int i;
    /* Start after all registered fonts (supports multi-font) */
    uint32_t vram_cursor = saturn_vdp1_get_font_end_offset(
        cui_saturn_font_get_registry());

    for (i = 0; i < COUP_SPR_COUNT; i++) {
        const coup_spr_info_t* info = &coup_spr_info[i];

        /* Record the absolute VDP1 VRAM offset (from 0x25C00000) */
        s_tex_offsets[i] = SATURN_VDP1_TEX_OFFSET + vram_cursor;

        /* Copy pixel data to VDP1 VRAM */
        saturn_vdp1_upload_texture(vram_cursor, coup_sprdata_all[i], info->data_size);

        /* Upload palette to CRAM */
        saturn_vdp1_upload_palette(SPRITE_CRAM_BASE_BANK + i, coup_palettes[i]);

        /* Advance VRAM cursor (align to 8-byte boundary for VDP1 SRCA) */
        vram_cursor += (info->data_size + 7) & ~7;
    }

    s_loaded = true;
}

bool coup_sprites_draw(int sprite_id, int x, int y)
{
    if (!s_loaded || sprite_id < 0 || sprite_id >= COUP_SPR_COUNT) {
        return false;
    }

    const coup_spr_info_t* info = &coup_spr_info[sprite_id];

    return saturn_vdp1_draw_sprite(
        x, y,
        info->w, info->h,
        s_tex_offsets[sprite_id],
        SPRITE_CRAM_BASE_BANK + sprite_id
    );
}

bool coup_sprites_loaded(void)
{
    return s_loaded;
}
