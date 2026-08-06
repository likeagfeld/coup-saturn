/**
 * saturn_rbg0.c - VDP2 RBG0 rotation background, title-screen fly-in only.
 *
 * See saturn_rbg0.h for the full citation trail (bank/offset plan, the
 * per-bank RDBSx finding, the 16-vs-256-colour bitmap palette hardware
 * constraint, why NBG1 is not armed alongside RBG0). This file has two
 * independent halves, matching saturn_linescroll.c's established shape:
 *
 *   1. Pure integer curve/pattern math - compiles and runs identically on
 *      the host test build and on SH-2.
 *   2. Saturn-only register/VRAM/CRAM control, #ifdef __SATURN__.
 */

#include "saturn_rbg0.h"

#include <stddef.h>

/*============================================================================
 * Pure math (host-testable)
 *============================================================================*/

int32_t saturn_rbg0_flyin_recip_q16(int frame, int total_frames)
{
    int64_t start = (int64_t)SATURN_RBG0_FLYIN_START_RECIP_Q16;
    int64_t end   = (int64_t)SATURN_RBG0_FLYIN_END_RECIP_Q16;
    int64_t value;

    if (total_frames <= 0 || frame >= total_frames) {
        return (int32_t)end;
    }
    if (frame <= 0) {
        return (int32_t)start;
    }

    value = start + (end - start) * (int64_t)frame / (int64_t)total_frames;
    return (int32_t)value;
}

int16_t saturn_rbg0_flyin_angle(int frame, int total_frames)
{
    int32_t start = SATURN_RBG0_FLYIN_START_ANGLE;
    int32_t value;

    if (total_frames <= 0 || frame >= total_frames) {
        return 0;
    }
    if (frame <= 0) {
        return (int16_t)start;
    }

    value = start * (int32_t)(total_frames - frame) / (int32_t)total_frames;
    return (int16_t)value;
}

bool saturn_rbg0_flyin_is_done(int frame, int total_frames)
{
    return (total_frames <= 0) || (frame >= total_frames);
}

uint8_t saturn_rbg0_pattern_pixel(int x, int y)
{
    int cell_x, cell_y;

    if (x < 0 || x >= SATURN_RBG0_BITMAP_W ||
        y < 0 || y >= SATURN_RBG0_BITMAP_H) {
        return 0;
    }

    cell_x = x / 32;
    cell_y = y / 32;
    return ((cell_x + cell_y) & 1) == 0 ? 1u : 0u;
}

/*============================================================================
 * Saturn-only: VDP2 register / VRAM / CRAM control
 *============================================================================*/

#ifdef __SATURN__
#include "sgl_defs.h"

/* Two placeholder colours (RGB555, 0BBBBBGGGGGRRRRR) - arbitrary, chosen
 * only to be clearly distinguishable in a capture. Not final art; the
 * Approach-B asset pipeline (owned by a concurrent change) supplies real
 * title art for NBG1. This module exists to prove the RBG0 mechanism. */
#define SATURN_RBG0_COLOR_BG    0x0421u   /* dark navy */
#define SATURN_RBG0_COLOR_INK   0x03FFu   /* bright gold-ish */

/* NOT static - deliberately external linkage, same as saturn_cd.h's
 * g_saturn_cd_stats, so the linker map lists its address for
 * qa_rbg0_witness.py's symbol_addr() to find (a `static` witness would not
 * appear in the map's global symbol table at all). */
saturn_rbg0_witness_t g_saturn_rbg0_witness = { 0, 0, 0, 0, 0, 0 };
static bool s_armed = false;
static int  s_frame = 0;

static void saturn_rbg0_upload_palette(void)
{
    volatile uint16_t* cram =
        (volatile uint16_t*)(SATURN_RBG0_CRAM_BASE + SATURN_RBG0_CRAM_OFFSET);
    int i;

    /* CRAM permits word/long-word access only, never bytes (ST-58-R2 VDP2
     * manual) - matches saturn_bg.c's own palette upload loop shape. */
    for (i = 0; i < 256; i++) {
        cram[i] = (i == 1) ? SATURN_RBG0_COLOR_INK : SATURN_RBG0_COLOR_BG;
    }
}

static void saturn_rbg0_upload_bitmap(void)
{
    volatile uint8_t* vram = (volatile uint8_t*)SATURN_RBG0_BITMAP_VRAM;
    int x, y;

    for (y = 0; y < SATURN_RBG0_BITMAP_H; y++) {
        volatile uint8_t* row = vram + (uint32_t)y * SATURN_RBG0_BITMAP_W;
        for (x = 0; x < SATURN_RBG0_BITMAP_W; x++) {
            row[x] = saturn_rbg0_pattern_pixel(x, y);
        }
    }
}

bool saturn_rbg0_init(void)
{
    int32_t recip0 = saturn_rbg0_flyin_recip_q16(0, SATURN_RBG0_FLYIN_FRAMES);
    int16_t angle0 = saturn_rbg0_flyin_angle(0, SATURN_RBG0_FLYIN_FRAMES);

    saturn_rbg0_upload_palette();
    saturn_rbg0_upload_bitmap();

    /* Rotation parameter table first - S_8_9_1/MAIN.C:25 calls
     * slRparaInitSet() before slCharRbg0/slPageRbg0; this module's bitmap
     * equivalent follows the same order. */
    slRparaInitSet((ROTSCROLL*)SATURN_RBG0_PARAM_VRAM);

    /* Bitmap pattern data + palette - the same two-call shape saturn_bg.c
     * already uses successfully for NBG1 (slBitMapNbg1 + slBMPaletteNbg1),
     * mirrored onto RBG0. */
    slBitMapRbg0(COL_TYPE_256, BM_512x256, (void*)SATURN_RBG0_BITMAP_VRAM);
    slBMPaletteRbg0(SATURN_RBG0_PALETTE_BANK);

    /* Screen-over mode 2: "outside of the display area, leave entire area
     * clear" - mode 1 (repeat a fixed character) is invalid in bitmap
     * format (ST-238-R1 slOverRA remarks), and mode 0 (repeat the display
     * area image) would tile visibly while zoomed far out at the start of
     * the fly-in. */
    slOverRA(2);

    slCurRpara(RA);
    slRparaMode(RA);

    /* Screen display centre = the 320x224 screen centre (S_8_9_1/MAIN.C:35
     * pattern, `slDispCenterR(toFIXED(160.0), toFIXED(112.0))` for its own
     * TV_320x224 mode). Look-at centre = the bitmap's own centre. */
    slDispCenterR(toFIXED(160), toFIXED(112));
    slLookR(toFIXED(SATURN_RBG0_BITMAP_W / 2), toFIXED(SATURN_RBG0_BITMAP_H / 2));

    /* Same priority slot NBG1 normally occupies (saturn_bg.c:165) - RBG0
     * stands in for NBG1's background role while NBG1 is off. */
    slPriorityRbg0(3);

    s_frame = 0;
    slZoomR(recip0, recip0);
    slZrotR(angle0);

    /* NBG0 (text) + RBG0 only - matches S_8_9_1/MAIN.C:38's proven
     * `slScrAutoDisp(NBG0ON | RBG0ON)`. Falls back to NBG0-only on NG,
     * mirroring main_saturn.c:243-245's existing NBG0+NBG1 fallback idiom
     * so a failed arbiter never loses the text layer either. */
    s_armed = (slScrAutoDisp(NBG0ON | RBG0ON) == OK);
    if (!s_armed) {
        slScrAutoDisp(NBG0ON);
    }

    g_saturn_rbg0_witness.magic     = SATURN_RBG0_MAGIC;
    g_saturn_rbg0_witness.armed     = s_armed ? 1 : 0;
    g_saturn_rbg0_witness.frame     = 0;
    g_saturn_rbg0_witness.angle     = angle0;
    g_saturn_rbg0_witness.recip_q16 = recip0;
    g_saturn_rbg0_witness.finished  = 0;

    return s_armed;
}

void saturn_rbg0_advance(void)
{
    int16_t angle;
    int32_t recip;

    if (!s_armed) {
        return;
    }

    s_frame++;

    recip = saturn_rbg0_flyin_recip_q16(s_frame, SATURN_RBG0_FLYIN_FRAMES);
    angle = saturn_rbg0_flyin_angle(s_frame, SATURN_RBG0_FLYIN_FRAMES);

    slZoomR(recip, recip);
    slZrotR(angle);

    g_saturn_rbg0_witness.frame     = s_frame;
    g_saturn_rbg0_witness.angle     = angle;
    g_saturn_rbg0_witness.recip_q16 = recip;
    g_saturn_rbg0_witness.finished  =
        saturn_rbg0_flyin_is_done(s_frame, SATURN_RBG0_FLYIN_FRAMES) ? 1 : 0;
}

void saturn_rbg0_teardown(void)
{
    /* The same call already used by main_saturn.c's own NBG0+NBG1 fallback
     * (main_saturn.c:244) - restores exactly the scroll-screen state the
     * surrounding boot code already expects to be in at this point. */
    slScrAutoDisp(NBG0ON);
    s_armed = false;
    g_saturn_rbg0_witness.armed = 0;
}

bool saturn_rbg0_is_armed(void)
{
    return s_armed;
}

bool saturn_rbg0_is_finished(void)
{
    return saturn_rbg0_flyin_is_done(s_frame, SATURN_RBG0_FLYIN_FRAMES);
}

const saturn_rbg0_witness_t* saturn_rbg0_witness(void)
{
    return &g_saturn_rbg0_witness;
}

void saturn_rbg0_run_title_demo(void)
{
    if (!saturn_rbg0_init()) {
        return;
    }
    while (!saturn_rbg0_is_finished()) {
        saturn_rbg0_advance();
        slSynch();
    }
    saturn_rbg0_teardown();
}

#endif /* __SATURN__ */
