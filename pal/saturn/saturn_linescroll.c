/**
 * saturn_linescroll.c - VDP2 NBG1 line-scroll shimmer.
 *
 * See saturn_linescroll.h for the full citation trail (hardware legality,
 * table format, VRAM placement/budget). This file has two independent
 * halves:
 *
 *   1. saturn_linescroll_build() - pure integer math, compiles and runs
 *      identically on the host test build and on SH-2.
 *   2. saturn_linescroll_arm/disarm/advance() - Saturn-only register and
 *      VRAM access, #ifdef __SATURN__.
 */

#include "saturn_linescroll.h"

#include <stddef.h>

/*============================================================================
 * Pure table math
 *============================================================================*/

/*
 * Bhaskara I's sine approximation formula (7th-century, rational, integer-
 * friendly): for 0 <= x <= 180 (degrees),
 *
 *   sin(x) ~= 4x(180-x) / (40500 - x(180-x))
 *
 * Maximum relative error is about 0.0016 (well documented as "Bhaskara I's
 * sine approximation formula"). It is used here specifically BECAUSE it
 * needs only integer multiply/divide - no trig library, no floating
 * point - which is the "no floating point and no libm" requirement for
 * the SH-2 target, and it is why this whole function can be host-tested:
 * it does not touch SGL's slSin() (Saturn-only, FIXED-typed, no host
 * build).
 *
 * SATURN_LINESCROLL_SINE_SCALE fixes the output's integer precision:
 * sine_bhaskara_scaled() returns sin(x) * SATURN_LINESCROLL_SINE_SCALE,
 * an integer in roughly [-SCALE, +SCALE]. 4096 gives ~13 bits of
 * resolution, comfortably finer than Bhaskara's own ~0.16% error term and
 * more than enough for a table whose final output is clamped to a
 * handful of pixels.
 */
#define SATURN_LINESCROLL_SINE_SCALE 4096

static int32_t saturn_linescroll_sine_scaled(int x_deg)
{
    int sign = 1;
    int32_t num;
    int32_t den;

    /* Reduce to [0, 360) - the % operator's result takes the sign of the
     * dividend in C, so a second +360 pass is needed to fix up negative
     * input before the final mod. */
    x_deg = ((x_deg % 360) + 360) % 360;

    /* Bhaskara I's formula is stated for [0,180]; mirror the second half
     * of the circle using sin(x) = -sin(x-180) for x in (180,360). */
    if (x_deg > 180) {
        sign = -1;
        x_deg -= 180;
    }

    num = 4 * (int32_t)x_deg * (180 - x_deg);
    den = 40500 - ((int32_t)x_deg * (180 - x_deg));
    /* den is always in (32400, 40500] for x_deg in [0,180] (minimum at
     * x_deg=90, where x_deg*(180-x_deg)=8100), so this never divides by
     * zero. */

    return sign * (int32_t)(((int64_t)num * SATURN_LINESCROLL_SINE_SCALE) / den);
}

void saturn_linescroll_build(int16_t *table, int lines, int phase,
                              int amplitude, int wavelength)
{
    int i;

    if (table == NULL || lines <= 0) {
        return;
    }
    if (wavelength <= 0) {
        wavelength = 1;
    }
    if (amplitude > SATURN_LINESCROLL_MAX_AMPLITUDE_PX) {
        amplitude = SATURN_LINESCROLL_MAX_AMPLITUDE_PX;
    }
    if (amplitude < -SATURN_LINESCROLL_MAX_AMPLITUDE_PX) {
        amplitude = -SATURN_LINESCROLL_MAX_AMPLITUDE_PX;
    }

    for (i = 0; i < lines; i++) {
        int t = (i + phase) % wavelength;
        int x_deg;
        int32_t sine_scaled;
        int32_t pixel;

        /* C's % can return a negative result when phase is negative; fold
         * it back into [0, wavelength) so table[] stays periodic for
         * negative phase too (test: linescroll_negative_phase_wraps_...). */
        if (t < 0) {
            t += wavelength;
        }

        x_deg = (t * 360) / wavelength;
        sine_scaled = saturn_linescroll_sine_scaled(x_deg);
        pixel = (sine_scaled * amplitude) / SATURN_LINESCROLL_SINE_SCALE;

        table[i] = (int16_t)pixel;
    }
}

/*============================================================================
 * Saturn-only: NBG1 line-scroll register control
 *============================================================================*/

#ifdef __SATURN__
#include "sgl_defs.h"

/*
 * Not declared in sgl_defs.h (which only carries what the rest of the PAL
 * needs). Both come from SGL 3.02j SL_DEF.H:
 *
 *   SL_DEF.H:1065  extern void slLineScrollMode(Uint16, Uint16);
 *   SL_DEF.H:1069  extern void slLineScrollTable1(void *);
 *
 * slLineScrollMode's first parameter is the SGL screen index (scnNBG0/
 * scnNBG1, already defined in sgl_defs.h) - SL_DEF.H:1066-1067 define
 * slLineScrollModeNbg0/1 as thin wrappers over exactly that; this file
 * calls slLineScrollMode(scnNBG1, ...) directly rather than redefining
 * that convenience macro.
 */
extern void slLineScrollMode(Uint16 screen, Uint16 mode);
extern void slLineScrollTable1(void *addr);

/* Line-scroll control bits (SL_DEF.H:688,695). Not present in sgl_defs.h;
 * declared locally with the same citation SEGA2D_1/MAIN.C:40 relies on. */
#define SATURN_LS_LINE_SZ1        0x00u  /* 1-line read interval */
#define SATURN_LS_LINE_H_SCROLL   0x02u  /* horizontal scroll value enabled */

static int16_t s_pixels[SATURN_LINESCROLL_LINES];
static int     s_phase = 0;
static bool    s_armed = false;

static void saturn_linescroll_upload(void)
{
    volatile Uint32 *dst =
        (volatile Uint32 *)(VDP2_VRAM_A1 + SATURN_LINESCROLL_VRAM_OFFSET);
    int i;

    for (i = 0; i < SATURN_LINESCROLL_LINES; i++) {
        /* toFIXED() on a whole-pixel integer produces a 16.16 value whose
         * upper 16 bits are the integer part and lower 16 bits are zero
         * fraction - exactly the "+00H integer / +02H fractional" layout
         * ST-058-R2 Figure 5.4 specifies for one line-scroll-table entry,
         * matching SEGA2D_1/MAIN.C:43's `16 * slSin(...)` FIXED value,
         * just generated by saturn_linescroll_build() instead of slSin(). */
        dst[i] = (Uint32)toFIXED((FIXED)s_pixels[i]);
    }
}

void saturn_linescroll_arm(void)
{
    saturn_linescroll_build(s_pixels, SATURN_LINESCROLL_LINES, 0,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);
    saturn_linescroll_upload();

    slLineScrollMode(scnNBG1, SATURN_LS_LINE_SZ1 | SATURN_LS_LINE_H_SCROLL);
    slLineScrollTable1((void *)(VDP2_VRAM_A1 + SATURN_LINESCROLL_VRAM_OFFSET));

    s_phase = 0;
    s_armed = true;
}

void saturn_linescroll_disarm(void)
{
    slLineScrollMode(scnNBG1, 0);
    s_armed = false;
}

void saturn_linescroll_advance(void)
{
    if (!s_armed) {
        return;
    }

    s_phase = (s_phase + 1) % SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES;

    saturn_linescroll_build(s_pixels, SATURN_LINESCROLL_LINES, s_phase,
                             SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX,
                             SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES);
    saturn_linescroll_upload();
}

bool saturn_linescroll_is_armed(void)
{
    return s_armed;
}

#endif /* __SATURN__ */
