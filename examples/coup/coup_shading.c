/**
 * coup_shading.c - Animated gouraud tables. See coup_shading.h.
 *
 * All integer. This runs on an SH-2 with no FPU, and the Saturn build links
 * no libm, so every curve here comes off a fixed-point table.
 */

#include "coup_shading.h"

#include "saturn_vdp1.h"

/* Quarter-wave sine, sin(2*pi*i/COUP_SHADING_PERIOD) * 1024 for i = 0..30,
 * i.e. 0 to 90 degrees in 3-degree steps. A quarter is all that needs
 * storing; the other three are reflections. 31 entries, 62 bytes. */
static const int16_t s_sin_q[31] = {
       0,   54,  107,  160,  213,  265,  316,  367,
     416,  465,  512,  558,  602,  644,  685,  724,
     761,  796,  828,  859,  887,  912,  935,  956,
     974,  989, 1002, 1011, 1018, 1023, 1024
};

#define QUARTER (COUP_SHADING_PERIOD / 4)      /* 30 */

int coup_shading_sin(int phase)
{
    int q, r;

    phase %= COUP_SHADING_PERIOD;
    if (phase < 0) {
        phase += COUP_SHADING_PERIOD;
    }
    q = phase / QUARTER;
    r = phase % QUARTER;

    switch (q) {
    case 0:  return  s_sin_q[r];
    case 1:  return  s_sin_q[QUARTER - r];
    case 2:  return -s_sin_q[r];
    default: return -s_sin_q[QUARTER - r];
    }
}

/* 0..256, a breathing curve that never goes negative: the sine lifted and
 * halved. Used wherever something should swell and subside rather than swing
 * to both sides of neutral. */
static int breathe(int phase)
{
    return (coup_shading_sin(phase) + 1024) / 8;
}

/*============================================================================
 * Sheen
 *============================================================================*/

/* The highlight is a linear bump one quad-width across, swept from before the
 * left edge to past the right. BASE darkens everything outside it, so the
 * band reads as light travelling over a surface rather than the whole shape
 * brightening - the difference between a sheen and a flash. */
#define SHEEN_BASE   (-3)
#define SHEEN_PEAK   14
#define SHEEN_SPAN   256          /* quad width in sweep units */

static int sheen_corner(int scan, int corner_pos)
{
    int d = corner_pos - scan;
    int w;

    if (d < 0) {
        d = -d;
    }
    w = SHEEN_SPAN - d;
    if (w < 0) {
        w = 0;
    }
    return SHEEN_BASE + (SHEEN_PEAK * w) / SHEEN_SPAN;
}

void coup_shading_sheen(uint16_t out[4], int phase)
{
    int scan, l, r;

    if (!out) {
        return;
    }
    phase %= COUP_SHADING_PERIOD;
    if (phase < 0) {
        phase += COUP_SHADING_PERIOD;
    }
    scan = (phase * SHEEN_SPAN) / COUP_SHADING_PERIOD;

    l = sheen_corner(scan, 0);
    r = sheen_corner(scan, SHEEN_SPAN);

    out[0] = saturn_vdp1_gouraud_word(l, l, l);   /* A upper left  */
    out[1] = saturn_vdp1_gouraud_word(r, r, r);   /* B upper right */
    out[2] = saturn_vdp1_gouraud_word(r, r, r);   /* C lower right */
    out[3] = saturn_vdp1_gouraud_word(l, l, l);   /* D lower left  */
}

/*============================================================================
 * Halo
 *============================================================================*/

/* Amber by RATIO, not by brightness: red lifted hardest, green about half as
 * much, blue pulled down the same amount green is lifted. Lifting all three
 * equally would read as a white light on the seat, which is the thing this is
 * meant to be distinguishable from - the other seats are already lit panels.
 *
 * The bottom corners stay neutral, so the band ramps from amber at the top
 * into the panel's own colour, per the design doc's game-screen treatment. */
#define HALO_R  14
#define HALO_G   7
#define HALO_B  (-7)

void coup_shading_halo(uint16_t out[4], int phase)
{
    int k, dr, dg, db;

    if (!out) {
        return;
    }
    k = breathe(phase);
    dr = (HALO_R * k) / 256;
    dg = (HALO_G * k) / 256;
    db = (HALO_B * k) / 256;

    out[0] = saturn_vdp1_gouraud_word(dr, dg, db);
    out[1] = saturn_vdp1_gouraud_word(dr, dg, db);
    out[2] = saturn_vdp1_gouraud_word(0, 0, 0);
    out[3] = saturn_vdp1_gouraud_word(0, 0, 0);
}

/*============================================================================
 * Pulse and washes
 *============================================================================*/

#define PULSE_PEAK 10

void coup_shading_pulse(uint16_t out[4], int phase)
{
    uint16_t w;
    int k;

    if (!out) {
        return;
    }
    k = (PULSE_PEAK * breathe(phase)) / 256;
    w = saturn_vdp1_gouraud_word(k, k, k);

    out[0] = w;
    out[1] = w;
    out[2] = w;
    out[3] = w;
}

/* An occupied slot is lit from above like the other panels. An empty one is
 * pushed well below neutral - the design doc calls for half luminance, and
 * the gap has to survive being drawn over painted slot chrome in the
 * background art, so it is a wide gap rather than a subtle one. */
void coup_shading_wash(uint16_t out[4], bool occupied)
{
    int top = occupied ? 8 : -6;
    int bot = occupied ? 2 : -10;

    if (!out) {
        return;
    }
    out[0] = saturn_vdp1_gouraud_word(top, top, top);
    out[1] = saturn_vdp1_gouraud_word(top, top, top);
    out[2] = saturn_vdp1_gouraud_word(bot, bot, bot);
    out[3] = saturn_vdp1_gouraud_word(bot, bot, bot);
}

/*============================================================================
 * Spotlight
 *============================================================================*/

/* Top is capped at 15 - the hardware maximum - so the bloom is sized to reach
 * it exactly rather than clamp into it. A correction that saturates would
 * flatten the top of the bloom and the light would appear to stall. */
#define SPOT_TOP_BASE   9
#define SPOT_BLOOM      6
#define SPOT_BOTTOM    (-6)

void coup_shading_spotlight(uint16_t out[4], int phase)
{
    int bloom, top, bot;

    if (!out) {
        return;
    }
    bloom = (SPOT_BLOOM * breathe(phase)) / 256;
    top = SPOT_TOP_BASE + bloom;
    bot = SPOT_BOTTOM + bloom / 2;

    out[0] = saturn_vdp1_gouraud_word(top, top, top);
    out[1] = saturn_vdp1_gouraud_word(top, top, top);
    out[2] = saturn_vdp1_gouraud_word(bot, bot, bot);
    out[3] = saturn_vdp1_gouraud_word(bot, bot, bot);
}
