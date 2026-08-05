/**
 * saturn_bg.c - VDP2 NBG1 painted background layer.
 *
 * See saturn_bg.h for the VRAM/CRAM placement rationale.
 */

#include "saturn_bg.h"

#ifdef __SATURN__
#include "sgl_defs.h"
#include "coup_bg_data.h"
#endif

static bool s_armed = false;
static int s_scene = -1;

uint32_t saturn_bg_cram_addr(int index)
{
    if (index < 0) {
        index = 0;
    }
    if (index > 255) {
        index = 255;
    }
    return SATURN_BG_CRAM_BASE + SATURN_BG_CRAM_OFFSET + (uint32_t)index * 2u;
}

bool saturn_bg_is_armed(void)
{
    return s_armed;
}

int saturn_bg_current_scene(void)
{
    return s_scene;
}

#ifdef __SATURN__
/** Copy one scene's pixels into VRAM and its palette into CRAM. */
static void saturn_bg_upload(int scene)
{
    volatile uint8_t* vram = (volatile uint8_t*)SATURN_BG_VRAM;
    const uint8_t* src = coup_bg_tables[scene];
    const uint16_t* pal = coup_bg_palettes[scene];
    uint32_t i, y, x;

    /* Scenes are stored as the visible window only (320x224), not the full
     * 512x256 plane, because 45% of that plane is never displayed. Expand row
     * by row into the 512-wide VRAM bitmap.
     *
     * The same number of bytes reaches VRAM either way, so this costs nothing
     * at upload time; the saving is 59,392 bytes of WRAM per scene, which is
     * what lets more than two backgrounds be resident. */
    for (y = 0; y < COUP_BG_VISIBLE_H; y++) {
        volatile uint8_t* dst = vram + (uint32_t)y * COUP_BG_W;
        const uint8_t* row = src + (uint32_t)y * COUP_BG_VISIBLE_W;
        for (x = 0; x < COUP_BG_VISIBLE_W; x++) {
            dst[x] = row[x];
        }
    }
    (void)i;

    /* CRAM permits word and long-word access only; byte access is not allowed
     * (ST-58-R2, VDP2 manual: "Access in bytes is not allowed"). These are
     * 16-bit writes. VRAM above does permit bytes. */
    for (i = 0; i < 256u; i++) {
        volatile uint16_t* cram =
            (volatile uint16_t*)saturn_bg_cram_addr((int)i);
        *cram = pal[i];
    }
}
#endif

void saturn_bg_set_scene(int scene)
{
#ifdef __SATURN__
    if (scene < 0 || scene >= COUP_BG_SCENE_COUNT) {
        return;
    }
    if (scene == s_scene) {
        return;             /* already resident - skip the 128 KB copy */
    }
    saturn_bg_upload(scene);
#else
    if (scene < 0) {
        return;
    }
#endif
    s_scene = scene;
}

void saturn_bg_init(void)
{
#ifdef __SATURN__
    /* Scene 0 is the default backdrop. Referred to by index rather than by a
     * generated enum name so that adding or renaming scenes in
     * convert_backgrounds.py cannot break this call. */
    saturn_bg_upload(0);
    s_scene = 0;

    /* Arm the layer. Priority 3 sits under the sprites (6) and text (7). */
    slBitMapNbg1(COL_TYPE_256, BM_512x256, (void*)SATURN_BG_VRAM);
    slBMPaletteNbg1(SATURN_BG_PALETTE_BANK);
    slPriorityNbg1(3);
#else
    s_scene = 0;
#endif
    s_armed = true;
}
