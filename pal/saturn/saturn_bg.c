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

void saturn_bg_init(void)
{
#ifdef __SATURN__
    volatile uint8_t* vram = (volatile uint8_t*)SATURN_BG_VRAM;
    uint32_t i;

    /* Bitmap pixels -> VDP2 VRAM bank A0. */
    for (i = 0; i < COUP_BG_SIZE; i++) {
        vram[i] = coup_bg_table[i];
    }

    /* Palette -> CRAM 256-colour bank 2. Entry 0 stays transparent. */
    for (i = 0; i < 256u; i++) {
        volatile uint16_t* cram =
            (volatile uint16_t*)saturn_bg_cram_addr((int)i);
        *cram = coup_bg_palette[i];
    }

    /* Arm the layer. Priority 3 sits under the sprites (6) and text (7). */
    slBitMapNbg1(COL_TYPE_256, BM_512x256, (void*)SATURN_BG_VRAM);
    slBMPaletteNbg1(SATURN_BG_PALETTE_BANK);
    slPriorityNbg1(3);
#endif
    s_armed = true;
}
