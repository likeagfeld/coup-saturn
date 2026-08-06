/**
 * saturn_bg.c - VDP2 NBG1 painted background layer.
 *
 * See saturn_bg.h for the VRAM/CRAM placement rationale.
 */

#include "saturn_bg.h"

#include "saturn_cd.h"

#ifdef __SATURN__
#include "sgl_defs.h"
#include "coup_bg_index.h"

/* One scene at a time, staged here on its way from the disc to VRAM.
 *
 * This single buffer replaces what used to be one resident const table PER
 * SCENE. Three scenes cost 215,040 bytes and left 1,032 bytes of slack, which
 * is why the other four could not be added at any colour depth. The staging
 * buffer costs 72,192 and serves all seven. */
static uint8_t s_stage[COUP_BG_FILE_BYTES];
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
/**
 * Stream one scene off the disc, then push it to VRAM and CRAM.
 *
 * The file is a 512-byte big-endian RGB555 palette followed by 224 rows of
 * 320 8bpp pixels - see convert_backgrounds.py emit_binaries(). Big-endian so
 * the palette can be read as uint16_t on this big-endian CPU with no swap.
 *
 * Returns false if the scene could not be read; the caller leaves whatever
 * was already on screen rather than painting garbage.
 */
/**
 * Fill the entire NBG1 plane with the transparent index.
 *
 * Every byte of 512x256 - the visible window AND both margins - so no part
 * of a previous scene can survive anywhere the display reaches.
 */
static void saturn_bg_clear(void)
{
    volatile uint8_t* vram = (volatile uint8_t*)SATURN_BG_VRAM;
    uint32_t i;

    for (i = 0; i < (uint32_t)COUP_BG_W * COUP_BG_H; i++) {
        vram[i] = 0;
    }
}


static bool saturn_bg_upload(int scene)
{
    volatile uint8_t* vram = (volatile uint8_t*)SATURN_BG_VRAM;
    const uint8_t* src;
    const uint16_t* pal;
    uint32_t i, y, x;

    if (saturn_cd_load(coup_bg_files[scene], s_stage,
                       COUP_BG_FILE_BYTES) != 0) {
        return false;
    }
    pal = (const uint16_t*)(const void*)s_stage;
    src = s_stage + COUP_BG_PAL_BYTES;

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
        /* Clear the rest of the row. The plane is 512 px wide (BM_512x256)
         * but a scene only carries the visible 320, so columns 320-511 kept
         * whatever the PREVIOUS scene left there. That residue is what was
         * reported as "a sliver of a neighbouring background image" on the
         * right-hand edge - it is not a neighbour from the source sheet,
         * which is why it survived every seam fix on the asset side. The
         * art is provably clean: convert_backgrounds.py crops the seam and
         * resizes back to a full 320, so nothing reaches the disc with a
         * neighbour column in it.
         *
         * Index 0 is the reserved transparent entry, so the cleared margin
         * shows the back screen rather than stale art. */
        /* EXTEND the last visible column into the margin rather than
         * clearing it to index 0.
         *
         * Index 0 is TRANSPARENT on a scroll screen, so a cleared margin
         * shows whatever is behind NBG1 - and any display or overscan that
         * reaches past column 319 then shows a strip that is not the scene.
         * Reported on the victory screen, whose own 320 columns measure
         * clean, which is what ruled the artwork out and pointed here.
         *
         * Repeating the edge pixel means anything past the window continues
         * the picture instead of punching a hole in it. Same cost. */
        for (x = COUP_BG_VISIBLE_W; x < COUP_BG_W; x++) {
            dst[x] = row[COUP_BG_VISIBLE_W - 1];
        }
    }
    /* Same for the rows below the picture: the plane is 256 tall and a scene
     * is 224, so rows 224-255 also held the previous scene. */
    /* Same for the rows below the picture: repeat the last visible row. */
    for (y = COUP_BG_VISIBLE_H; y < COUP_BG_H; y++) {
        volatile uint8_t* dst = vram + (uint32_t)y * COUP_BG_W;
        const uint8_t* last = src + (uint32_t)(COUP_BG_VISIBLE_H - 1)
                                    * COUP_BG_VISIBLE_W;
        for (x = 0; x < COUP_BG_W; x++) {
            dst[x] = last[(x < COUP_BG_VISIBLE_W) ? x
                                                  : (COUP_BG_VISIBLE_W - 1)];
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
    return true;
}
#endif

void saturn_bg_set_scene(int scene)
{
#ifdef __SATURN__
    if (scene < 0 || scene >= COUP_BG_SCENE_COUNT) {
        return;
    }
    if (scene == s_scene) {
        return;             /* already resident - skip the disc read entirely */
    }
    if (!saturn_bg_upload(scene)) {
        /* BLANK the plane rather than keep the previous scene.
         *
         * "Keep the previous scene" was written to avoid showing garbage, and
         * it trades one artifact for a worse one: if the streamed read fails
         * on the way from the table to VICTORY, the GAME backdrop stays up
         * and the victory screen's panels are drawn on top of it - the old
         * screen visibly nested behind the new one. Reported exactly that
         * way, and it is intermittent because it needs a failed read.
         *
         * A blank backdrop is unambiguous and correct-looking; the wrong
         * screen showing through is neither. The panels and text of the new
         * screen still draw over it, so nothing is lost but the art.
         *
         * saturn_bg_clear() writes the WHOLE 512x256 plane, both margins
         * included, so nothing of the previous scene survives anywhere the
         * display or overscan can reach. */
        saturn_bg_clear();
        s_scene = scene;    /* do not retry-loop on a dead read every frame */
        return;
    }
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
    /* The CD file system must be up before any scene can be read. */
    saturn_cd_init();

    /* Scene 0 is the boot splash, and it earns its place: the caller loads
     * every sprite, font and effect immediately after this returns, which
     * takes visible time. Showing the splash first means that wait is covered
     * by artwork instead of a blank screen, and the load is not wasted - it
     * used to fetch a backdrop that the first rendered frame replaced.
     *
     * Referred to by index rather than by a generated enum name so that
     * adding or renaming scenes in convert_backgrounds.py cannot break it. */
    if (saturn_bg_upload(0)) {
        s_scene = 0;
    }

    /* Arm the layer. Priority 3 sits under the sprites (6) and text (7). */
    slBitMapNbg1(COL_TYPE_256, BM_512x256, (void*)SATURN_BG_VRAM);
    slBMPaletteNbg1(SATURN_BG_PALETTE_BANK);
    slPriorityNbg1(3);
#else
    s_scene = 0;
#endif
    s_armed = true;
}
