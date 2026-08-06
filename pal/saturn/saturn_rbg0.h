/**
 * saturn_rbg0.h - VDP2 RBG0 rotation background, title-screen fly-in only.
 *
 * "Approach C" of docs/superpowers/specs/2026-08-04-saturn-visual-facelift-
 * design.md (section 3): a perspective title fly-in using VDP2's hardware
 * rotation/scale scroll surface (RBG0). SCOPE IS TITLE SCREEN ONLY - this
 * module does not attempt the in-game rotating table felt (that needs
 * NBG0+NBG1+RBG0 simultaneously, which has zero SGL sample precedent and is
 * explicitly out of scope; see the "why not NBG1 too" note below).
 *
 * THE CENTRAL CLAIM THIS MODULE STANDS ON, WITH ITS CITATION
 * ------------------------------------------------------------------------
 * The design doc's own §3 worried that RBG0 "forces a full re-plan of all
 * four [VRAM] banks including the text layer's home" (ST-058-R2 §6.2 cited
 * as VDP2_Manual.txt:6437-6452). Read in full, that passage says:
 *
 *   "VRAM cycle pattern register settings of the VRAM bank selected in RAM
 *   used for the rotational scroll are ignored."
 *   (VDP2_Manual.txt:6448-6450, RAMCTL / RDBSx description, ST-58-R2 p.149)
 *
 * and the four bits doing the selecting (RDBSA00/01, RDBSA10/11, RDBSB00/01,
 * RDBSB10/11) are PER-BANK - one 2-bit field per VRAM bank (A0/A1/B0/B1),
 * independently settable (VDP2_Manual.txt:6426-6441). "Ignored" therefore
 * applies ONLY to whichever bank(s) get marked 01/10/11 for RBG0's own use;
 * a bank left at 00 ("not used as RBG0 RAM") keeps completely normal NBG/
 * back-screen/line-scroll cycle-pattern arbitration. So the "full four-bank
 * re-plan" is NOT a hardware requirement - it is what happens if you
 * ignorantly claim every bank for rotation. This module claims exactly ONE
 * bank (B0, currently unused by anything, RDBSB=11 "character/bitmap
 * pattern") and leaves A0, A1 and B1 at the default 00 (A1's existing back-
 * screen-colour and line-scroll uses; B1's existing NBG0 text char/PNT data;
 * A0 unclaimed here, normally NBG1's home) completely untouched by RDBSx.
 *
 * The Rotation Parameter Table itself is NOT one of the RDBSx-classified
 * uses at all (RDBSx only classifies coefficient/pattern-name/character-
 * pattern storage - VDP2_Manual.txt:6437-6441). It is addressed by a
 * separate, always-available register (RPTA, VDP2_Manual.txt:6845-6890)
 * that takes an arbitrary VRAM address, confirmed against a real working
 * sample (S_8_9_1/MAIN.C:10,25 - see sgl_defs.h's slRparaInitSet comment).
 * So placing it in bank A1 does not require marking A1 for RBG0 use either,
 * and does not disturb A1's existing back-screen-colour / line-scroll
 * cycle-pattern arbitration.
 *
 * Net: this module's bank plan touches ONE previously-empty bank (B0) and
 * leaves the proven text stack (B1) and the design doc's other in-flight
 * VRAM claims (A0 NBG1 bitmap, A1 line-scroll/back-colour) at their
 * existing register configuration. See scripts/qa/qa_vram_rbg0_map.py for
 * the static, host-runnable gate that checks this claim never drifts onto
 * B1 or collides with the other named A1 claims.
 *
 * BANK / OFFSET PLAN (all citations above; exact addresses below)
 * ------------------------------------------------------------------------
 *   VRAM B0  (0x25E40000, entire bank, 0x20000 B) - RBG0 bitmap pattern
 *            data. COL_TYPE_256, BM_512x256 (512x256 @ 8bpp = exactly
 *            0x20000 B - the SAME proven shape saturn_bg.c already uses for
 *            NBG1 in bank A0). RDBSB0 = 11 ("character pattern (or bitmap)
 *            table"). B0 has no other claim anywhere in this tree.
 *   VRAM A1  (0x25E20000 + 0x1FE00) - rotation parameter table, 0x100 B
 *            (both param A and B slots; only A is used - Mode 0, RA-only).
 *            Chosen to satisfy the ">= 0x0380" floor (immediately above
 *            saturn_linescroll.h's 0x000-0x37F claim) while ALSO sitting
 *            far above where a concurrent back-screen-gradient/line-colour
 *            table would land per saturn_linescroll.h's own coordination
 *            note (that note asks any new A1 claim to start at >= 0x380;
 *            this module goes further and uses the SAME offset a real SGL
 *            sample uses - S_8_9_1/MAIN.C:10 `RBG0_PAR_ADR = VDP2_VRAM_A1 +
 *            0x1fe00` - keeping this claim clear of low-A1 territory
 *            entirely). Ends at 0x1FEFF, strictly below the reserved top
 *            256 B (0x1FF00-0x1FFFF, back-screen colour at +0x1FFFE,
 *            main_saturn.c:226).
 *   CRAM     0xC00-0xDFF (256-colour bank 6) - RBG0 bitmap palette.
 *
 * WHY 256-COLOUR, NOT 16-COLOUR, FOR THE BITMAP PALETTE - A HARDWARE
 * CONSTRAINT FOUND DURING THIS WORK, NOT IN THE ORIGINAL BRIEF
 * ------------------------------------------------------------------------
 * VDP2's bit-map palette-number register (BMPNA/BMPNB, 18002CH-18002EH) is
 * only 3 bits per screen (N0BMP6-4 / R0BMP6-4 etc). For a 256-colour
 * bitmap those 3 bits ARE the top 3 bits of the full 7-bit-wide 256-colour-
 * granularity palette number, reaching all 8 256-colour banks across the
 * whole 4 KB CRAM (0x000, 0x200, ..., 0xE00) - this is exactly the
 * mechanism saturn_bg.c already uses successfully for NBG1's bank 7. But
 * for a 16-COLOUR bitmap, "a '0' is attached to the lowest four bits and
 * used as the 7-bit palette number" (VDP2_Manual.txt:5029-5041) - i.e. the
 * addressable palette is (reg3bit << 4), so only 8 possible 16-colour
 * banks are reachable AT ALL, all packed into CRAM bytes 0x000-0x0FF. That
 * range is entirely inside the existing text-palette claim (0x000-0x1FF,
 * saturn_bg.h's own CRAM map). A 16-colour RBG0 bitmap therefore CANNOT
 * avoid the text palettes - it is a hardware ceiling, not a placement
 * choice. Switching to COL_TYPE_256 (same shape as the already-working
 * NBG1 bitmap path) sidesteps the ceiling entirely and is the reason this
 * module's placeholder art is 8bpp even though it only uses two colours.
 *
 * WHY NOT NBG1 TOO (staying at NBG0+RBG0, matching the one directly-
 * relevant sample precedent)
 * ------------------------------------------------------------------------
 * SGL sample S_8_9_1 (NOV96_DTS/LIBRARY/SDK_10J/SGL302/SAMPLE/S_8_9_1/
 * MAIN.C:38) arms `slScrAutoDisp(NBG0ON | RBG0ON)` - text (slPrint, via
 * NBG0) plus a rotating cell-format RBG0 - and it WORKS. That is the exact
 * two-layer combination this module reproduces (bitmap instead of cell
 * format for RBG0; everything else the same shape). NBG0+NBG1+RBG0 has no
 * sample anywhere in the local corpus and main_saturn.c's own existing
 * NBG0+NBG1 fallback-to-NBG0-alone (main_saturn.c:243-245) shows this
 * codebase already treats a 2-screen combination as the boundary of what
 * slScrAutoDisp is proven to arbitrate reliably. So: NBG1 is turned OFF
 * for the duration of the fly-in, and NBG0+RBG0 is armed in its place.
 *
 * DEMO GATING
 * ------------------------------------------------------------------------
 * The register-touching functions in this file (saturn_rbg0_init/advance/
 * teardown) are ALWAYS compiled into the Saturn build (unconditionally
 * added to examples/coup/saturn/Makefile's SRCS), but nothing calls them
 * unless the shipped disc is built with -DCOUP_RBG0_TITLE_DEMO - mirroring
 * coup_qa_screen.h's existing pattern exactly ("compiles to nothing unless
 * the flag is passed"). The default disc (no flag) is therefore
 * BYTE-IDENTICAL in behaviour to before this file existed: nothing calls
 * slBitMapRbg0/slRparaInitSet/slScrAutoDisp(RBG0ON) unless the demo is
 * explicitly requested.
 */

#ifndef SATURN_RBG0_H
#define SATURN_RBG0_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * VRAM / CRAM placement constants (see the file banner above for citations)
 *============================================================================*/

/* Bitmap pattern data: the whole of VRAM bank B0. */
#define SATURN_RBG0_BITMAP_VRAM        0x25E40000u   /* VDP2_VRAM_B0 */
#define SATURN_RBG0_BITMAP_W           512
#define SATURN_RBG0_BITMAP_H           256
#define SATURN_RBG0_BITMAP_BYTES       (512u * 256u)   /* 0x20000 - one bank */

/* Rotation parameter table: bank A1, offset 0x1FE00 (>= the 0x0380 floor;
 * see the file banner for why this offset specifically). 0x100 B for both
 * parameter A and B slots (only A is used). */
#define SATURN_RBG0_PARAM_VRAM_OFFSET  0x1FE00u
#define SATURN_RBG0_PARAM_VRAM_BASE    0x25E20000u   /* VDP2_VRAM_A1 */
#define SATURN_RBG0_PARAM_VRAM         (SATURN_RBG0_PARAM_VRAM_BASE + \
                                         SATURN_RBG0_PARAM_VRAM_OFFSET)
#define SATURN_RBG0_PARAM_TABLE_BYTES  0x100u

/* The floor this module's placement must satisfy, per the task's directive
 * (immediately above saturn_linescroll.h's 0x000-0x37F claim). Gated by
 * qa_vram_rbg0_map.py and by a host unit test. */
#define SATURN_RBG0_PARAM_MIN_OFFSET   0x0380u

/* The reserved top-of-bank region this module's table must stay clear of
 * (back-screen colour lives at A1+0x1FFFE - main_saturn.c:226). */
#define SATURN_RBG0_A1_RESERVED_TOP    0x1FF00u

/* Bitmap palette: CRAM 256-colour bank 6 (0xC00-0xDFF), the last free
 * 256-colour bank, immediately below the background bitmap's bank 7
 * (0xE00-0xFFF, saturn_bg.h). Not a 16-colour bank choice - see the file
 * banner for the hardware reason 16-colour bitmap mode could not avoid the
 * text palettes. */
#define SATURN_RBG0_CRAM_BASE          0x25F00000u   /* VDP2_COLRAM */
#define SATURN_RBG0_CRAM_OFFSET        0xC00u
#define SATURN_RBG0_PALETTE_BANK       6

/*============================================================================
 * Pure host-testable math (no floating point, no libm, no SGL types).
 *
 * Two curves drive the fly-in: a shrink-to-full-size zoom (slZoomR takes
 * the RECIPROCAL of scale - see sgl_defs.h) and a settling rotation. Both
 * are plain integer functions so they can be pinned by tests/coup/
 * test_saturn_rbg0.c on the host, exactly like saturn_linescroll_build().
 *============================================================================*/

/* Total fly-in length and curve endpoints. */
#define SATURN_RBG0_FLYIN_FRAMES         90     /* 1.5 s at 60 fps */
#define SATURN_RBG0_FLYIN_START_RECIP_Q16 (8 << 16)  /* 1/8 size at frame 0 */
#define SATURN_RBG0_FLYIN_END_RECIP_Q16   (1 << 16)  /* full size, reciprocal 1 */
#define SATURN_RBG0_FLYIN_START_ANGLE     8192  /* 45 deg (65536/8), BAMS units */

/**
 * FIXED-point (16.16) reciprocal-of-scale value for slZoomR, at `frame` of
 * `total_frames`. Linearly interpolates from
 * SATURN_RBG0_FLYIN_START_RECIP_Q16 down to SATURN_RBG0_FLYIN_END_RECIP_Q16,
 * clamped at both ends (frame <= 0 returns the start value, frame >=
 * total_frames returns the end value). total_frames <= 0 returns the end
 * value (degenerate input collapses to "already arrived" rather than
 * dividing by zero).
 */
int32_t saturn_rbg0_flyin_recip_q16(int frame, int total_frames);

/**
 * BAMS ANGLE-range (int16, 65536 units/circle) rotation at `frame` of
 * `total_frames`. Linearly decays from SATURN_RBG0_FLYIN_START_ANGLE to 0,
 * clamped at both ends the same way as saturn_rbg0_flyin_recip_q16().
 */
int16_t saturn_rbg0_flyin_angle(int frame, int total_frames);

/**
 * True once `frame` has reached the end of the fly-in (frame >=
 * total_frames, or total_frames <= 0).
 */
bool saturn_rbg0_flyin_is_done(int frame, int total_frames);

/**
 * Placeholder bitmap pixel generator: a simple two-colour ring pattern used
 * to prove the RBG0 mechanism without depending on the Approach-B asset
 * pipeline's art (owned by a concurrent change, out of this module's
 * scope). Returns a palette index (0 or 1) for bitmap pixel (x, y). Pure
 * function; out-of-range x/y (outside SATURN_RBG0_BITMAP_W/H) returns 0.
 */
uint8_t saturn_rbg0_pattern_pixel(int x, int y);

#ifdef __SATURN__
/*============================================================================
 * Saturn-only: VDP2 register / VRAM / CRAM control.
 *============================================================================*/

/**
 * WRAM witness struct, polled live by scripts/qa/qa_rbg0_witness.py over
 * RetroArch's READ_CORE_RAM (see qa_retroarch.py's locate_witness(), same
 * pattern as saturn_cd.h's g_saturn_cd_stats). Proves the control code RUNS
 * and the rotation state ADVANCES - it does NOT prove anything reached the
 * screen (skill gotcha #12: SGL rewrites VDP2 registers every vblank, and a
 * WRAM struct is not a register/framebuffer read either way). The gate that
 * proves on-screen legibility is scripts/qa/qa_rbg0_legibility.py, which
 * needs a real capture.
 */
typedef struct {
    uint32_t magic;      /* SATURN_RBG0_MAGIC once saturn_rbg0_init() has run */
    int32_t  armed;      /* 0/1 - RBG0 currently armed */
    int32_t  frame;      /* frames advanced since saturn_rbg0_init() */
    int32_t  angle;      /* current ANGLE, sign-extended to int32_t */
    int32_t  recip_q16;  /* current FIXED (16.16) reciprocal zoom value */
    int32_t  finished;   /* 0/1 - fly-in has reached its final frame */
} saturn_rbg0_witness_t;

#define SATURN_RBG0_MAGIC 0x52424730u   /* 'RBG0' */

/**
 * Arm RBG0: uploads the placeholder bitmap + palette, sets up the rotation
 * parameter table at SATURN_RBG0_PARAM_VRAM, and arms
 * slScrAutoDisp(NBG0ON | RBG0ON) (falling back to NBG0ON alone on NG,
 * mirroring main_saturn.c's existing NBG0+NBG1 fallback idiom exactly).
 * Caller must have already armed NBG0 (cui_saturn_init()) and turned NBG1
 * OFF before calling this - RBG0 replaces NBG1's slot for the duration of
 * the fly-in (see the file banner's "why not NBG1 too").
 *
 * Returns true if RBG0ON was accepted by the cycle-pattern arbiter, false
 * if the fallback to NBG0-only fired (in which case there is nothing to
 * animate - caller should skip the fly-in loop entirely).
 */
bool saturn_rbg0_init(void);

/**
 * Advance the fly-in by one frame: recomputes zoom/angle from the pure
 * curve functions above and pushes them to SGL (slZoomR/slZrotR), updates
 * the witness. A no-op if not armed. Does NOT call slSynch() - the caller
 * owns frame pacing (main_saturn.c's demo entry point calls slSynch()
 * itself, matching S_8_9_1/MAIN.C:44's raw loop, since this runs before
 * cui/game state exists and cannot use coup_render_now()).
 */
void saturn_rbg0_advance(void);

/**
 * Tear down: disarms slScrAutoDisp back to NBG0ON alone (the same call
 * already used by main_saturn.c's own NBG0+NBG1 fallback path at
 * main_saturn.c:244, so the post-teardown scroll-screen state matches what
 * the surrounding boot code already expects at that point). Caller is
 * responsible for re-arming NBG1 afterward through the normal path
 * (cui_saturn_init() / saturn_bg_init()), which this module does not touch.
 */
void saturn_rbg0_teardown(void);

/** True between saturn_rbg0_init() (if it returned true) and saturn_rbg0_teardown(). */
bool saturn_rbg0_is_armed(void);

/** True once the fly-in has reached SATURN_RBG0_FLYIN_FRAMES. */
bool saturn_rbg0_is_finished(void);

/** The witness struct, for host-side inspection paths (does not exist on host). */
const saturn_rbg0_witness_t* saturn_rbg0_witness(void);

/**
 * Run the whole demo synchronously: init, loop advance+slSynch() until
 * finished, teardown. This is the ENTIRE surface main_saturn.c calls, and
 * only under -DCOUP_RBG0_TITLE_DEMO (see coup_qa_screen.h for the identical
 * gating pattern this mirrors). A no-op (returns immediately) if
 * saturn_rbg0_init() falls back to NBG0-only.
 */
void saturn_rbg0_run_title_demo(void);

#endif /* __SATURN__ */

#endif /* SATURN_RBG0_H */
