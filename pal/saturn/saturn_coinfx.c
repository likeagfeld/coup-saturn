/**
 * saturn_coinfx.c - Coin payout fly animation + timer-bar gouraud sweep.
 *
 * See saturn_coinfx.h for design-doc citation, hardware citations (ST-013-
 * R3 zoom-point table + zero/negative-zoom precaution), and the ZP=0xA
 * centre-anchor workaround this file implements on top of saturn_vdp1.c's
 * top-left-anchored saturn_vdp1_draw_sprite_scaled().
 */

#include "saturn_coinfx.h"
#include "saturn_vdp1.h"

/*============================================================================
 * Pure trajectory / scale math
 *============================================================================*/

int saturn_coinfx_clamp_scale_pct(int scale_pct)
{
    /* ST-013-R3 p.74 (VDP1_Manual.txt:3143-3144): "A negative value cannot
     * be specified for the display width. Drawing cannot be guaranteed
     * when a negative value is specified for the display width." Never
     * hand VDP1 a zero or negative zoom, for any input. */
    if (scale_pct < SATURN_COINFX_MIN_SCALE_PCT) {
        scale_pct = SATURN_COINFX_MIN_SCALE_PCT;
    }
    if (scale_pct > SATURN_COINFX_PEAK_SCALE_PCT) {
        scale_pct = SATURN_COINFX_PEAK_SCALE_PCT;
    }
    return scale_pct;
}

void saturn_coinfx_point(int step, int frames,
                          int x0, int y0, int x1, int y1,
                          int* out_x, int* out_y, int* out_scale_pct)
{
    int t_num, t_den;
    int lin_y;
    int arc;
    int scale;

    if (!out_x || !out_y || !out_scale_pct) {
        return;
    }

    /* Normalise: frames must be >= 1 (no divide-by-zero); step is clamped
     * into [0, frames] so out-of-range callers (negative step, or step
     * left running past arrival) can never drive the arc/scale math
     * outside its designed domain. */
    if (frames < 1) {
        frames = 1;
    }
    if (step < 0) {
        step = 0;
    }
    if (step > frames) {
        step = frames;
    }

    t_num = step;
    t_den = frames;

    /* X: straight linear interpolation, payer -> receiver. */
    *out_x = x0 + ((x1 - x0) * t_num) / t_den;

    /* Y: linear interpolation minus a parabolic lift, 4*H*t*(1-t), which is
     * 0 at t=0 and t=1 and H at t=0.5 - the coin arcs up and falls back
     * down rather than sliding in a straight line. */
    lin_y = y0 + ((y1 - y0) * t_num) / t_den;
    arc = (4 * SATURN_COINFX_ARC_HEIGHT_PX * t_num * (t_den - t_num))
          / (t_den * t_den);
    *out_y = lin_y - arc;

    /* Scale: same parabola shape, ramping from REST at the endpoints to
     * PEAK at the midpoint, then defensively clamped through
     * saturn_coinfx_clamp_scale_pct() so it can never be <= 0 even if this
     * formula is changed later without re-deriving its endpoint values. */
    scale = SATURN_COINFX_REST_SCALE_PCT
          + ((SATURN_COINFX_PEAK_SCALE_PCT - SATURN_COINFX_REST_SCALE_PCT)
             * 4 * t_num * (t_den - t_num)) / (t_den * t_den);

    *out_scale_pct = saturn_coinfx_clamp_scale_pct(scale);
}

void saturn_coinfx_center_to_rect(int cx, int cy, int base_w, int base_h,
                                   int scale_pct,
                                   int* out_x, int* out_y,
                                   int* out_w, int* out_h)
{
    int w, h;

    if (!out_x || !out_y || !out_w || !out_h) {
        return;
    }

    scale_pct = saturn_coinfx_clamp_scale_pct(scale_pct);

    w = (base_w * scale_pct) / 100;
    h = (base_h * scale_pct) / 100;
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    /* ZP=0xA (Center-Center) emulation: re-derive the top-left corner from
     * the FIXED centre and the CHANGING size every call - see the file
     * header for the ST-013-R3 citation and the saturn_vdp1.c gap this
     * works around. */
    *out_x = cx - w / 2;
    *out_y = cy - h / 2;
    *out_w = w;
    *out_h = h;
}

void saturn_coinfx_timer_colors(int remaining_pct, uint16_t out[4])
{
    int dr, dg, db;
    int i;

    if (!out) {
        return;
    }

    if (remaining_pct < 0) {
        remaining_pct = 0;
    }
    if (remaining_pct > 100) {
        remaining_pct = 100;
    }

    /* Never any blue in a green->amber->red sweep. */
    db = -16;

    if (remaining_pct >= 50) {
        /* Green (100%) -> Amber (50%): green plateaus at its maximum,
         * red ramps up from its minimum. */
        dg = 15;
        dr = -16 + ((15 - (-16)) * (100 - remaining_pct)) / 50;
    } else {
        /* Amber (50%) -> Red (0%): red plateaus at its maximum, green
         * ramps down to its minimum. */
        dr = 15;
        dg = -16 + ((15 - (-16)) * remaining_pct) / 50;
    }

    for (i = 0; i < 4; i++) {
        out[i] = saturn_vdp1_gouraud_word(dr, dg, db);
    }
}

/*============================================================================
 * Multi-coin pool (static allocation, no malloc)
 *============================================================================*/

static saturn_coinfx_coin_t g_coins[SATURN_COINFX_MAX_COINS];

void saturn_coinfx_reset(void)
{
    int i;
    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        g_coins[i].active = false;
    }
}

int saturn_coinfx_payout(int x0, int y0, int x1, int y1,
                          int coin_count, int frames)
{
    int spawned = 0;
    int i;

    if (frames < 1) {
        frames = 1;
    }
    if (coin_count < 0) {
        coin_count = 0;
    }

    for (i = 0; i < SATURN_COINFX_MAX_COINS && spawned < coin_count; i++) {
        if (!g_coins[i].active) {
            g_coins[i].active  = true;
            g_coins[i].x0      = x0;
            g_coins[i].y0      = y0;
            g_coins[i].x1      = x1;
            g_coins[i].y1      = y1;
            g_coins[i].frames  = frames;
            /* Stagger: this coin waits `spawned * STAGGER` frames before it
             * starts moving/drawing, so a multi-coin payout reads as
             * distinct coins in sequence rather than one blob. */
            g_coins[i].elapsed = -(spawned * SATURN_COINFX_STAGGER_FRAMES);
            spawned++;
        }
    }

    return spawned;
}

void saturn_coinfx_tick(void)
{
    int i;

    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        if (!g_coins[i].active) {
            continue;
        }

        g_coins[i].elapsed++;

        if (g_coins[i].elapsed >= g_coins[i].frames) {
            g_coins[i].active = false;
        }
    }
}

int saturn_coinfx_active_count(void)
{
    int i;
    int count = 0;

    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        if (g_coins[i].active) {
            count++;
        }
    }
    return count;
}

bool saturn_coinfx_get(int index, int* out_x, int* out_y, int* out_scale_pct)
{
    const saturn_coinfx_coin_t* coin;

    if (index < 0 || index >= SATURN_COINFX_MAX_COINS) {
        return false;
    }

    coin = &g_coins[index];

    if (!coin->active || coin->elapsed < 0) {
        /* Empty slot, or still waiting out its stagger delay - not visible
         * this frame. */
        return false;
    }

    saturn_coinfx_point(coin->elapsed, coin->frames,
                         coin->x0, coin->y0, coin->x1, coin->y1,
                         out_x, out_y, out_scale_pct);
    return true;
}

#ifdef __SATURN__
void saturn_coinfx_draw(int base_w, int base_h,
                         uint32_t tex_offset, int cram_bank)
{
    int i;

    for (i = 0; i < SATURN_COINFX_MAX_COINS; i++) {
        int cx, cy, scale_pct;

        if (!saturn_coinfx_get(i, &cx, &cy, &scale_pct)) {
            continue;
        }

        {
            int x, y, w, h;
            saturn_coinfx_center_to_rect(cx, cy, base_w, base_h, scale_pct,
                                          &x, &y, &w, &h);
            saturn_vdp1_draw_sprite_scaled(x, y, base_w, base_h, w, h,
                                            tex_offset, cram_bank);
        }
    }
}
#endif
