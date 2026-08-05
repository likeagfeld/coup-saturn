/**
 * saturn_linescroll.h - VDP2 NBG1 line-scroll shimmer.
 *
 * Design doc: docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md
 *   line 110: "subtle sine line-scroll shimmer on NBG1 (`slLineScrollTable`
 *              animation, sample-proven SEGA2D_1/MAIN.C:40-63)"
 *   line 152: "BG shimmer | NBG1 line scroll table | 896 B-2 KB table in A1
 *              | SEGA2D_1/MAIN.C:40-63"
 *
 * LEGALITY ON A BITMAP NBG1 - CONFIRMED, WITH CITATION.
 *
 * pal/saturn/saturn_bg.c runs NBG1 as a 256-colour BITMAP layer
 * (slBitMapNbg1(COL_TYPE_256, BM_512x256, ...), see saturn_bg.h). The VDP2
 * manual states this explicitly is fine for line scroll:
 *
 *   "Within the Normal scroll screen, there is a line scroll function and
 *   vertical cell scroll function in NBG0 and NBG1. [...] Both functions
 *   can be used without relationship to the cell format and bit map
 *   format." (ST-058-R2, section 5.3, VDP2_Manual.txt:5692-5697, p.130)
 *
 * and the scroll-screen capability table on the same page confirms only
 * NBG0/NBG1 support it at all:
 *
 *   "Line Scroll Function  Yes  Yes  No  No  No"
 *   (NBG0, NBG1, NBG2, NBG3, RBG0 - VDP2_Manual.txt:1009)
 *
 * So: legal on bitmap-mode NBG1, and it is the ONLY NBG capable of it
 * besides NBG0 (which this build uses for the 16-colour text plane and
 * cannot spare for a bitmap-format background - saturn_pal.c).
 *
 * TABLE FORMAT - matches SEGA2D_1/MAIN.C:40-63 exactly.
 *
 * MAIN.C selects `lineSZ1|lineHScroll` (1-line interval, horizontal-only -
 * SL_DEF.H:688,695) and then writes one Uint32 per line:
 *   `*tb++ = 16 * slSin(...)`
 * slSin() returns FIXED (16.16, sgl_defs.h). This lines up with the VDP2
 * manual's line-scroll-table layout for horizontal-only mode: each line
 * consumes a 16-bit integer part followed by a 16-bit fractional part
 * (ST-058-R2 Figure 5.4 / the "every 1 line" table in section 5.3,
 * VDP2_Manual.txt:5773-5786) - i.e. exactly one 32-bit FIXED value, in the
 * same byte layout FIXED already uses. This module reuses that: build a
 * pixel offset per line (this header), then upload toFIXED(offset) per
 * line (saturn_linescroll.c, Saturn-only half).
 *
 * VRAM PLACEMENT AND BUDGET.
 *
 * Design doc's memory plan (line 178-179): "A1: line-scroll table +
 * back-screen gradient table + line-color table (<=4 KB, placed below
 * SGL's reserved top 256 B at 0x1FF00)." VDP2 VRAM bank A1
 * (VDP2_VRAM_A1 = 0x25E20000, sgl_defs.h) is 128 KB and currently holds
 * only the single-colour back-screen entry that
 * examples/coup/saturn/main_saturn.c:226 already writes at
 * VDP2_VRAM_A1+0x1FFFE (the very last word of the bank, inside that
 * reserved top-256-byte region) - nothing else claims bank A1 yet in this
 * tree as of this writing.
 *
 * This module's table is SATURN_LINESCROLL_TABLE_BYTES = 896 bytes
 * (224 lines x 4 bytes/line), placed at the BOTTOM of bank A1
 * (SATURN_LINESCROLL_VRAM_OFFSET = 0x0000, i.e. 0x25E20000-0x25E2037F),
 * nowhere near the reserved top 256 B. 896 B is the low end of the design
 * doc's cited "896 B-2 KB" range for this table (line 152) - see
 * saturn_linescroll.c for why 2 KB (SEGA2D_1's 512-entry master table +
 * pointer-slide technique) was not needed here.
 *
 * COORDINATION NOTE FOR INTEGRATION: if another concurrent change also
 * places a back-screen gradient table or line-color table in bank A1 (the
 * design doc lists both as A1 residents, in the shared <=4KB budget), that
 * table's offset must be chosen to sit at or above
 * SATURN_LINESCROLL_VRAM_OFFSET + SATURN_LINESCROLL_TABLE_BYTES (0x380),
 * and must still leave the last 256 B (0x1FF00-0x1FFFF) alone. This module
 * does not know about those other tables' offsets - the integrating agent
 * must check for collisions explicitly.
 */

#ifndef SATURN_LINESCROLL_H
#define SATURN_LINESCROLL_H

#include <stdint.h>
#include <stdbool.h>

/* Screen height driving the table size. TV_320x224 is the only mode this
 * build selects (examples/coup/saturn/main_saturn.c calls
 * slInitSystem(TV_320x224, ...)). */
#define SATURN_LINESCROLL_LINES              224

/* One line-scroll table entry, for lineSZ1|lineHScroll mode (1-line
 * interval, horizontal-only, no vertical scroll, no zoom): a single FIXED
 * (16.16) horizontal screen scroll value - 4 bytes. ST-058-R2 5.3: "Stored
 * line scroll data is only composed of data required by the line scroll
 * register setting" (VDP2_Manual.txt:5737-5738) - selecting only
 * lineHScroll (no lineVScroll, no lineZoom) means each line is exactly
 * this one 4-byte field, matching SEGA2D_1/MAIN.C:42-43's `Uint32 *tb`
 * loop. */
#define SATURN_LINESCROLL_BYTES_PER_LINE     4

/* 224 * 4 = 896 bytes - the low end of the design doc's cited "896 B-2 KB
 * table in A1" (design doc line 152). */
#define SATURN_LINESCROLL_TABLE_BYTES \
    (SATURN_LINESCROLL_LINES * SATURN_LINESCROLL_BYTES_PER_LINE)

/* VDP2 VRAM bank A1 byte offset for this table. See the placement note
 * above the top of this file for the coordination requirement. */
#define SATURN_LINESCROLL_VRAM_OFFSET        0x0000u

/* Hard ceiling on wobble amplitude, in pixels. saturn_linescroll_build()
 * clamps to this regardless of what a caller asks for - this is the
 * RED-firing gate against a future change making the shimmer "seasick".
 * See test_saturn_linescroll.c's amplitude-clamp tests. */
#define SATURN_LINESCROLL_MAX_AMPLITUDE_PX   3

/* Default amplitude actually armed by saturn_linescroll_arm().
 *
 * Justification (subtle, not a vibe): NBG1 is the 320px-wide painted
 * background bitmap (saturn_bg.c). A +/-2px horizontal wobble is 0.625%
 * of screen width. For comparison, the ONE proven local sample of this
 * effect - SEGA2D_1/MAIN.C:43 - uses `16 * slSin(...)`, i.e. +/-16px
 * (5% of screen width): that demo is titled "SEGA2D LINESCROLL SAMPLE"
 * and is a deliberately showy flag-waving effect, not a subtle ambient
 * shimmer. This module's default is 1/8 of that demo's amplitude, and is
 * still one pixel inside SATURN_LINESCROLL_MAX_AMPLITUDE_PX so a wrong
 * guess here cannot exceed the hard ceiling either. */
#define SATURN_LINESCROLL_DEFAULT_AMPLITUDE_PX     2

/* Default wavelength, in lines per full sine cycle. 224/64 = 3.5 cycles
 * down the screen. MEASURED (pal-saturn-agent, host build): at
 * amplitude=2px, wavelength=64 lines, the built table's actual range is
 * [-2,+2] and the actual maximum adjacent-line delta is 1px - see
 * test_saturn_linescroll.c's continuity gate, which pins that number. */
#define SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES 64

/*============================================================================
 * Pure table math (host-testable; no floating point, no libm).
 *
 * Uses Bhaskara I's 7th-century integer rational sine approximation (see
 * saturn_linescroll.c) instead of SGL's slSin(), because the goal here is
 * a table generator that runs identically on the host test build and on
 * SH-2 - slSin() is a Saturn-only SGL library function with no host
 * equivalent, and the task requires this math to be host-testable.
 *============================================================================*/

/**
 * Fill `table[0..lines-1]` with per-line horizontal pixel offsets for a
 * sine shimmer.
 *
 * table[i] = clamp(amplitude) * sin(2*pi*(i+phase)/wavelength), in whole
 * pixels.
 *
 * Periodic in phase: saturn_linescroll_build(t, n, 0, a, w) and
 * saturn_linescroll_build(t, n, w, a, w) produce IDENTICAL output for any
 * w > 0, because (i+phase) is always reduced modulo wavelength before use.
 * Negative phase wraps the same way modulo arithmetic does for any other
 * signed input (phase=-1 behaves like phase=wavelength-1).
 *
 * amplitude is clamped to +/-SATURN_LINESCROLL_MAX_AMPLITUDE_PX regardless
 * of the caller's value - the anti-seasickness gate.
 *
 * @param table      destination array, at least `lines` entries. NULL is
 *                    a no-op.
 * @param lines      number of lines to fill. <= 0 is a no-op.
 * @param phase      starting sample offset, any integer (negative allowed)
 * @param amplitude  peak deflection in pixels before clamping
 * @param wavelength lines per full sine cycle (<= 0 is treated as 1)
 */
void saturn_linescroll_build(int16_t *table, int lines, int phase,
                              int amplitude, int wavelength);

#ifdef __SATURN__
/*============================================================================
 * Saturn-only: NBG1 line-scroll register control.
 *============================================================================*/

/**
 * Arm the shimmer.
 *
 * - Sets NBG1's line-scroll mode to 1-line-interval, horizontal-only
 *   (SL_DEF.H:688 lineSZ1 | SL_DEF.H:695 lineHScroll, via slLineScrollMode
 *   - matching SEGA2D_1/MAIN.C:40's slLineScrollModeNbg1(lineSZ1|
 *   lineHScroll)).
 * - Builds the table at phase 0 and writes it to VDP2 VRAM bank A1 at
 *   SATURN_LINESCROLL_VRAM_OFFSET.
 * - Points LSTA1 at that address (slLineScrollTable1 - SL_DEF.H:1069,
 *   matching SEGA2D_1/MAIN.C:44).
 *
 * Call once, after saturn_bg_init() has armed NBG1's bitmap (saturn_bg.h).
 * Safe to call again to re-arm after saturn_linescroll_disarm().
 */
void saturn_linescroll_arm(void);

/**
 * Disable the shimmer: clears NBG1's line-scroll mode bits (mode 0 - no
 * line scroll, no vertical cell scroll). The table itself is left in VRAM
 * un-erased; saturn_linescroll_arm() rebuilds it before re-enabling.
 */
void saturn_linescroll_disarm(void);

/**
 * Advance the shimmer by one frame.
 *
 * Increments the internal phase counter (mod
 * SATURN_LINESCROLL_DEFAULT_WAVELENGTH_LINES) and rewrites the table in
 * place at the fixed VRAM address armed by saturn_linescroll_arm(). The
 * LSTA1 register itself is NOT re-written - VDP2 re-reads the table live
 * from VRAM every field, there is no separate "commit" step (ST-058-R2
 * 5.3: line scroll table values are simply "stored in VRAM" and read from
 * there each line, VDP2_Manual.txt:5701-5703).
 *
 * A no-op if the shimmer is not currently armed.
 * Call once per rendered frame, after saturn_linescroll_arm().
 */
void saturn_linescroll_advance(void);

/** True after saturn_linescroll_arm() and before saturn_linescroll_disarm(). */
bool saturn_linescroll_is_armed(void);

#endif /* __SATURN__ */

#endif /* SATURN_LINESCROLL_H */
