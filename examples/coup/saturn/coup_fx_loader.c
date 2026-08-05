/**
 * coup_fx_loader.c - Action effects and UI sprites.
 *
 * See coup_fx_loader.h for the allocation-chaining rule.
 */

#include "coup_fx_loader.h"
#include "coup_fx_data.h"
#include "coup_anim_loader.h"
#include "saturn_vdp1.h"

/*============================================================================
 * State
 *============================================================================*/

static uint32_t s_fx_offsets[COUP_FX_COUNT][16];   /* per sequence, per frame */
static uint32_t s_ui_offsets[COUP_UI_COUNT];
static int      s_fx_bank[COUP_FX_COUNT];
static int      s_ui_bank[COUP_UI_COUNT];
static uint32_t s_vram_end = 0;
static bool     s_loaded = false;

/*============================================================================
 * Load
 *============================================================================*/

void coup_fx_load(void)
{
    uint32_t cursor = (coup_anim_vram_end() + 7) & ~(uint32_t)7;
    int cram = coup_anim_cram_end_bank();
    int i, f;

    for (i = 0; i < COUP_FX_COUNT; i++) {
        const coup_fx_info_t* info = &coup_fx_info[i];

        for (f = 0; f < info->frames && f < 16; f++) {
            s_fx_offsets[i][f] = SATURN_VDP1_TEX_OFFSET + cursor;
            saturn_vdp1_upload_texture(cursor, coup_fx_all[i][f],
                                       info->frame_bytes);
            cursor += (info->frame_bytes + 7) & ~(uint32_t)7;
        }

        s_fx_bank[i] = cram;
        saturn_vdp1_upload_palette(cram, coup_fx_palettes[i]);
        cram++;
    }

    for (i = 0; i < COUP_UI_COUNT; i++) {
        const coup_fx_info_t* info = &coup_ui_info[i];

        s_ui_offsets[i] = SATURN_VDP1_TEX_OFFSET + cursor;
        saturn_vdp1_upload_texture(cursor, coup_ui_all[i][0],
                                   info->frame_bytes);
        cursor += (info->frame_bytes + 7) & ~(uint32_t)7;

        s_ui_bank[i] = cram;
        saturn_vdp1_upload_palette(cram, coup_ui_palettes[i]);
        cram++;
    }

    s_vram_end = cursor;
    s_loaded = true;
}

bool coup_fx_loaded(void)
{
    return s_loaded;
}

uint32_t coup_fx_vram_end(void)
{
    return s_vram_end;
}

/*============================================================================
 * Draw
 *============================================================================*/

int coup_fx_frames(int fx)
{
    if (fx < 0 || fx >= COUP_FX_COUNT) {
        return 0;
    }
    return coup_fx_info[fx].frames;
}

bool coup_fx_draw(int fx, int frame, int x, int y)
{
    const coup_fx_info_t* info;

    if (!s_loaded || fx < 0 || fx >= COUP_FX_COUNT) {
        return false;
    }

    info = &coup_fx_info[fx];
    if (info->frames <= 0) {
        return false;
    }

    frame %= info->frames;
    if (frame < 0) {
        frame += info->frames;
    }
    if (frame >= 16) {
        frame = 15;
    }

    return saturn_vdp1_draw_sprite(x, y, info->w, info->h,
                                   s_fx_offsets[fx][frame], s_fx_bank[fx]);
}

bool coup_ui_draw(int ui, int x, int y)
{
    const coup_fx_info_t* info;

    if (!s_loaded || ui < 0 || ui >= COUP_UI_COUNT) {
        return false;
    }

    info = &coup_ui_info[ui];
    return saturn_vdp1_draw_sprite(x, y, info->w, info->h,
                                   s_ui_offsets[ui], s_ui_bank[ui]);
}

bool coup_ui_draw_coins(int coins, int x, int y)
{
    int ui;

    /* Pick the densest stack that does not overstate the count. The art has
     * 1/2/3/5/10 tiers, so a player holding 7 shows the 5-stack rather than
     * five separate coins - fewer commands and it reads faster. */
    if (coins >= 10) {
        ui = COUP_UI_COIN10;
    } else if (coins >= 5) {
        ui = COUP_UI_COIN5;
    } else if (coins >= 3) {
        ui = COUP_UI_COIN3;
    } else if (coins >= 2) {
        ui = COUP_UI_COIN2;
    } else if (coins >= 1) {
        ui = COUP_UI_COIN1;
    } else {
        return false;       /* nothing to show at zero */
    }

    return coup_ui_draw(ui, x, y);
}
