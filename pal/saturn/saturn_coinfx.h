/**
 * saturn_coinfx.h - Coin payout fly animation + timer-bar gouraud sweep.
 *
 * Design doc: docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md
 * line 126-127:
 *   "timer bar = gouraud gradient that shifts green->amber->red;
 *    coin payouts = zoom-point [sprite]"
 * (full line 127: "coin payouts = zoom-point scaled sprite pop
 *  (ZP=0xA centre anchor)")
 *
 * All trajectory / scale / colour math below is PURE integer arithmetic so
 * it is host-testable without Saturn hardware (see tests/coup/
 * test_saturn_coinfx.c). Only the VRAM-touching draw calls are Saturn-only
 * and are guarded by #ifdef __SATURN__.
 *
 * ---------------------------------------------------------------------------
 * ZP=0xA (centre-centre) anchor note - read before wiring saturn_coinfx_draw()
 * ---------------------------------------------------------------------------
 * ST-013-R3 (VDP1 User's Manual) p.73 "Zoom Point" table, CMDCTRL bits 11-8
 * (sega_saturn_docs/VDP1_Manual.txt:3084-3104): code "AH (10)" = Center-
 * Center - confirmed against SL_DEF.H:167, `#define _ZmCC (0x0a << 8)`
 * ("Zoom base Center Center", NOV96_DTS/LIBRARY/SDK_10J/SGL302/INC/
 * SL_DEF.H). With ZP=0xA, the hardware keeps the sprite's CENTRE fixed at
 * the zoom-point coordinate as CMDXB/CMDYB (display width/height) change -
 * the sprite grows/shrinks symmetrically outward from its own middle.
 *
 * pal/saturn/saturn_vdp1.c's saturn_vdp1_draw_sprite_scaled() (read, not
 * modified, by this module) does NOT use the CMDCTRL ZP field: it issues a
 * Distorted Sprite (Comm=0x2) with four explicit corners computed from
 * (x,y) as the TOP-LEFT corner (saturn_vdp1.c:227-235, `cmd.xa = x; ...
 * cmd.xb = x + dst_w - 1; ...`). Handing it a growing dst_w/dst_h with a
 * fixed (x,y) therefore grows the sprite DOWN-AND-RIGHT, not outward from
 * centre - the opposite of the ZP=0xA "pop" the design doc asks for.
 *
 * saturn_coinfx works around this without touching saturn_vdp1.c: the
 * trajectory function's (out_x, out_y) is the coin's CENTRE point, and
 * saturn_coinfx_center_to_rect() re-derives the top-left corner from the
 * (fixed) centre and the (changing) size every frame - which reproduces a
 * true ZP=0xA anchor on top of a top-left-anchored primitive. Call it
 * immediately before saturn_vdp1_draw_sprite_scaled(); do not feed
 * (out_x, out_y) to that function directly.
 *
 * ---------------------------------------------------------------------------
 * Zero/negative zoom precaution
 * ---------------------------------------------------------------------------
 * ST-013-R3 p.74 (sega_saturn_docs/VDP1_Manual.txt:3143-3144): "A negative
 * value cannot be specified for the display width. Drawing cannot be
 * guaranteed when a negative value is specified for the display width."
 * Chapter 9 "Precautions for Use", Commands section (VDP1_Manual.txt:
 * 6847-6848): "Only 0H, 5H, 6H, 7H, 9H, AH (10), BH (11), DH (13), EH (14,)
 * and FH (15) are allowed as zoom-point values."
 *
 * saturn_coinfx_point() and saturn_coinfx_clamp_scale_pct() therefore never
 * return a scale <= 0, for any input including out-of-range step/frames -
 * see the RED-firing tests in tests/coup/test_saturn_coinfx.c.
 */

#ifndef SATURN_COINFX_H
#define SATURN_COINFX_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Tunables
 *============================================================================*/

/** Static pool size - no malloc anywhere in this module. */
#define SATURN_COINFX_MAX_COINS      8

/**
 * Hardware floor. Must be > 0 for every input (ST-013-R3 p.74, above).
 * Deliberately far below the artistic REST scale so the floor's own clamp
 * is exercised by adversarial inputs (see test file), not merely inherited
 * from the parabola's natural minimum.
 */
#define SATURN_COINFX_MIN_SCALE_PCT  1

/** Scale at lift-off / landing (t=0, t=1). Comfortably above the hardware
 *  floor; this is an artistic choice, not a hardware limit. */
#define SATURN_COINFX_REST_SCALE_PCT 25

/** Scale at the apex of the flight (t=0.5). */
#define SATURN_COINFX_PEAK_SCALE_PCT 100

/** Arc lift, in pixels, at the midpoint of the flight. */
#define SATURN_COINFX_ARC_HEIGHT_PX  24

/** Frame stagger between consecutive coins in a multi-coin payout, so a
 *  3-coin payout reads as three coins in sequence, not one blob. */
#define SATURN_COINFX_STAGGER_FRAMES 6

/** Default flight duration in frames, used by saturn_coinfx_payout() when
 *  the caller does not need a different pace. */
#define SATURN_COINFX_DEFAULT_FRAMES 30

/*============================================================================
 * Pure trajectory / scale math (host-testable)
 *============================================================================*/

/**
 * Compute one coin's centre position and scale at a given step of its
 * flight, arcing from (x0,y0) to (x1,y1).
 *
 * - X is a straight linear interpolation.
 * - Y is a linear interpolation minus a parabolic lift
 *   (4*H*t*(1-t), H = SATURN_COINFX_ARC_HEIGHT_PX), so the coin arcs up and
 *   falls back down rather than sliding in a straight line.
 * - Scale ramps from SATURN_COINFX_REST_SCALE_PCT at t=0/t=1 up to
 *   SATURN_COINFX_PEAK_SCALE_PCT at t=0.5 (same parabola shape), then is
 *   clamped through saturn_coinfx_clamp_scale_pct() so it can never reach
 *   zero or go negative regardless of the inputs (see file header).
 *
 * step and frames are normalised internally: frames < 1 is treated as 1,
 * and step is clamped to [0, frames] before any arithmetic, so out-of-range
 * inputs (negative step, step > frames, frames <= 0) are safe and never
 * divide by zero.
 *
 * (out_x, out_y) is the coin's CENTRE, not a draw-ready top-left corner -
 * see saturn_coinfx_center_to_rect() and the ZP=0xA note in the file header.
 *
 * @param step             current frame within the flight (any int; clamped)
 * @param frames           total frames for the flight (any int; clamped >=1)
 * @param x0,y0            payer position (flight start)
 * @param x1,y1            receiver position (flight end)
 * @param out_x,out_y      [out] coin centre this step
 * @param out_scale_pct    [out] scale percent, always in
 *                         [SATURN_COINFX_MIN_SCALE_PCT, SATURN_COINFX_PEAK_SCALE_PCT]
 */
void saturn_coinfx_point(int step, int frames,
                          int x0, int y0, int x1, int y1,
                          int* out_x, int* out_y, int* out_scale_pct);

/**
 * Clamp a scale percentage into the hardware-safe range
 * [SATURN_COINFX_MIN_SCALE_PCT, SATURN_COINFX_PEAK_SCALE_PCT].
 *
 * Exposed standalone (rather than only inlined in saturn_coinfx_point) so
 * the zero/negative-zoom precaution (ST-013-R3 p.74) has its own direct,
 * adversarial-input test independent of the trajectory formula.
 *
 * @param scale_pct  any integer, including <= 0 or absurdly large
 * @return clamped value, always > 0
 */
int saturn_coinfx_clamp_scale_pct(int scale_pct);

/**
 * Convert a coin's CENTRE point + a base sprite size at a given scale into
 * the top-left rectangle saturn_vdp1_draw_sprite_scaled() expects, so that
 * growing/shrinking the sprite appears anchored at its centre (mimicking a
 * true VDP1 ZP=0xA zoom point on top of that function's top-left-anchored
 * Distorted Sprite implementation - see the file header note).
 *
 * @param cx,cy         coin centre (e.g. from saturn_coinfx_point)
 * @param base_w,base_h sprite size at 100% scale (source texture size)
 * @param scale_pct     scale percent (e.g. from saturn_coinfx_point)
 * @param out_x,out_y   [out] top-left corner for saturn_vdp1_draw_sprite_scaled
 * @param out_w,out_h   [out] dst_w/dst_h for saturn_vdp1_draw_sprite_scaled
 */
void saturn_coinfx_center_to_rect(int cx, int cy, int base_w, int base_h,
                                   int scale_pct,
                                   int* out_x, int* out_y,
                                   int* out_w, int* out_h);

/**
 * Map a remaining-time fraction to a 4-entry VDP1 gouraud correction table
 * that sweeps green (100%) -> amber (50%) -> red (0%), for the in-game
 * response timer bar (design doc line 126).
 *
 * Output format matches saturn_vdp1_gouraud_word()/saturn_vdp1_set_gouraud_
 * table(): each entry is a packed 0BBBBBGGGGGRRRRR correction word (ST-013-
 * R3 section 5.3, restated at pal/saturn/saturn_vdp1.h:124-130). All four
 * corners get the same value (a flat colour sweep, not a top/bottom shade -
 * pair with saturn_vdp1_gouraud_vshade() separately if a lighting gradient
 * is also wanted on the same panel).
 *
 * Blue is always at its minimum correction (-16) - this is a two-channel
 * (red/green) hue sweep, never blue.
 *
 * Endpoints (verified in tests/coup/test_saturn_coinfx.c):
 *   remaining_pct = 100 -> pure green  (R=0x00,  G=0x1F, B=0x00 per channel)
 *   remaining_pct = 50  -> amber       (R=0x1F,  G=0x1F, B=0x00)
 *   remaining_pct = 0   -> pure red    (R=0x1F,  G=0x00, B=0x00)
 * The decoded red channel is monotonically non-decreasing as remaining_pct
 * falls from 100 to 0 (the bar only ever gets redder as time runs out).
 *
 * @param remaining_pct  0-100 (clamped); 100 = full time left, 0 = expired
 * @param out            4 gouraud words, corners A,B,C,D (all identical)
 */
void saturn_coinfx_timer_colors(int remaining_pct, uint16_t out[4]);

/*============================================================================
 * Multi-coin pool (static allocation, no malloc)
 *============================================================================*/

/** One in-flight coin. Exposed for tests; treat as read-only elsewhere. */
typedef struct saturn_coinfx_coin {
    bool active;
    int  x0, y0, x1, y1;   /* flight endpoints (centre-to-centre) */
    int  frames;           /* total flight duration for this coin */
    int  elapsed;          /* frames since the payout was triggered; the
                             * coin does not move/draw until elapsed >= 0 -
                             * this is what staggers multi-coin payouts */
} saturn_coinfx_coin_t;

/** Clear the pool. All coins become inactive. */
void saturn_coinfx_reset(void);

/**
 * Spawn a multi-coin payout: up to `coin_count` coins fly from (x0,y0) to
 * (x1,y1), each starting SATURN_COINFX_STAGGER_FRAMES later than the last,
 * so a 3-coin payout reads as three distinct coins rather than one sprite.
 *
 * Coins beyond the pool's free capacity are silently dropped (static
 * allocation - no malloc, no growth).
 *
 * @param x0,y0       payer centre position
 * @param x1,y1       receiver centre position
 * @param coin_count  how many coins to spawn (clamped to pool capacity)
 * @param frames      flight duration per coin, in frames (<=0 -> 1)
 * @return number of coins actually spawned (<= coin_count)
 */
int saturn_coinfx_payout(int x0, int y0, int x1, int y1,
                          int coin_count, int frames);

/**
 * Advance every active coin by one frame. Coins whose flight has completed
 * (elapsed >= frames) are deactivated and freed back to the pool.
 * Call once per rendered frame.
 */
void saturn_coinfx_tick(void);

/** Number of coins currently occupying the pool (active, including those
 *  still waiting out their stagger delay). */
int saturn_coinfx_active_count(void);

/**
 * Query pool slot `index`.
 *
 * @param index          0..SATURN_COINFX_MAX_COINS-1
 * @param out_x,out_y    [out] coin centre this frame (valid only if return true)
 * @param out_scale_pct  [out] scale percent this frame (valid only if return true)
 * @return true if the coin is active AND past its stagger delay (i.e.
 *         should be drawn this frame); false if the slot is empty or the
 *         coin's stagger delay hasn't elapsed yet.
 */
bool saturn_coinfx_get(int index, int* out_x, int* out_y, int* out_scale_pct);

#ifdef __SATURN__
/**
 * Draw every visible pool coin as a VDP1 scaled sprite (Saturn-only).
 *
 * Pairs saturn_coinfx_get() + saturn_coinfx_center_to_rect() with
 * pal/saturn/saturn_vdp1.h's saturn_vdp1_draw_sprite_scaled() for each
 * active, stagger-elapsed coin in the pool. Call after saturn_coinfx_tick()
 * and before the VDP1 command buffer is flushed for the frame.
 *
 * @param base_w,base_h  coin sprite size at 100% scale (source texture size,
 *                       base_w must be a multiple of 8 per
 *                       saturn_vdp1_draw_sprite_scaled's src_w contract)
 * @param tex_offset     coin texture's VDP1 VRAM byte offset
 * @param cram_bank      CRAM palette bank for the coin texture
 */
void saturn_coinfx_draw(int base_w, int base_h,
                         uint32_t tex_offset, int cram_bank);
#endif

#endif /* SATURN_COINFX_H */
