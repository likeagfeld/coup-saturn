#!/usr/bin/env python3
"""
qa_rbg0_legibility.py - The gate that actually counts: with RBG0 armed, the
title screen's text must remain legible.

WHY THIS GATE EXISTS
  A WRAM witness (qa_rbg0_witness.py) proves the RBG0 control code runs and
  its rotation state advances. It does NOT prove anything reached the
  screen - it never reads a pixel. This gate reuses qa_legibility.py's
  proven ink-to-background contrast measure (same threshold, same region
  definitions - MIN_CONTRAST=60.0, "title" region = (100,195,230,215), the
  title's densest/worst-case text) against a capture taken WITH RBG0
  armed, so a regression that corrupts the text stack (the exact failure
  mode qa_vram_rbg0_map.py's bank check exists to prevent) would show up
  here even if that static gate were somehow wrong.

TWO THINGS THIS SCRIPT DOES, DELIBERATELY KEPT SEPARATE
  1. --selftest (default, always runs first): a host-only, no-emulator
     proof that the underlying measure (qa_legibility.contrast()) CAN
     detect the regression this whole plan exists to avoid. It builds two
     SYNTHETIC images - a legible one (bright ink on a darker background,
     scoring well above threshold) and a corrupted one modelling what
     RDBSx(B1)=11 instead of B0 would do to the text region (uniform noise
     with no ink/background separation, scoring at/near zero) - and
     asserts contrast() correctly classifies both. This is the RED-before-
     GREEN proof required before trusting a real capture, run entirely on
     the host with no RetroArch involved.

  2. The real check: reads build/qa/screens/title_rbg0.png (a captured
     frame of the title screen with the RBG0 demo armed) and applies the
     exact same measure/threshold qa_legibility.py already uses for the
     "title" region. If that file does not exist yet, this reports
     INCONCLUSIVE with the exact command needed to produce it - it does
     NOT invent a result, and it does NOT launch an emulator itself.

USAGE
  python scripts/qa/qa_rbg0_legibility.py                # selftest only if
                                                           # the capture is
                                                           # missing
  python scripts/qa/qa_rbg0_legibility.py --dir build/qa/screens
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_legibility as leg   # noqa: E402  - reuse the proven contrast() measure

MIN_CONTRAST = leg.MIN_CONTRAST          # same threshold as the shipped gate
TITLE_REGION = leg.REGIONS["title"]      # (100, 195, 230, 215) - the [R] hint
CAPTURE_NAME = "title_rbg0.png"


def _synthetic_capture(path, legible):
    """Write a 320x224 synthetic frame for the self-test. `legible=True`
    draws bright ink on a darker background inside TITLE_REGION (models a
    correctly-rendering text stack); `legible=False` fills the region with
    flat mid-grey noise with no ink/background separation (models what a
    VRAM-bank collision - RDBSx(B1) instead of B0 - would do: garbage
    character-cell data, no coherent glyph strokes)."""
    rng = np.random.default_rng(1234 if legible else 5678)
    frame = np.full((224, 320), 40, dtype=np.uint8)   # dark background overall

    x0, y0, x1, y1 = TITLE_REGION
    if legible:
        # A dark region with a handful of bright "glyph stroke" pixels -
        # the 90th-percentile-vs-median gap qa_legibility.contrast()
        # measures is large for exactly this shape.
        frame[y0:y1, x0:x1] = 30
        ink_rows = rng.integers(y0, y1, size=200)
        ink_cols = rng.integers(x0, x1, size=200)
        frame[ink_rows, ink_cols] = 220
    else:
        # Uniform mid-grey +/- tiny noise: no bright decile stands out from
        # the median, because there is no coherent ink at all - this is
        # the measurable shape of "the character cell data is garbage".
        noise = rng.integers(118, 138, size=(y1 - y0, x1 - x0), dtype=np.uint8)
        frame[y0:y1, x0:x1] = noise

    Image.fromarray(frame, mode="L").convert("RGB").save(path)


def selftest(tmp_dir):
    """Proves qa_legibility.contrast() actually discriminates a corrupted
    text region from a legible one, entirely on the host. Returns True if
    the measure is trustworthy (RED on the bad fixture, GREEN on the good
    one)."""
    print("=== SELF-TEST: can the legibility MEASURE detect a corrupted "
          "text region? (host-only, no emulator) ===")
    os.makedirs(tmp_dir, exist_ok=True)
    good_path = os.path.join(tmp_dir, "_selftest_legible.png")
    bad_path = os.path.join(tmp_dir, "_selftest_corrupted.png")

    _synthetic_capture(good_path, legible=True)
    _synthetic_capture(bad_path, legible=False)

    good_score = leg.contrast(good_path, TITLE_REGION)
    bad_score = leg.contrast(bad_path, TITLE_REGION)

    good_pass = good_score >= MIN_CONTRAST
    bad_fails = bad_score < MIN_CONTRAST

    print(f"  synthetic LEGIBLE fixture   contrast={good_score:6.1f}  "
          f"{'GREEN (correctly clean)' if good_pass else 'RED - BUG IN THE MEASURE'}")
    print(f"  synthetic CORRUPTED fixture contrast={bad_score:6.1f}  "
          f"{'RED (correctly flagged)' if bad_fails else 'GREEN - BUG IN THE MEASURE'}")

    ok = good_pass and bad_fails
    print(f"  SELF-TEST: {'PASS' if ok else 'FAIL'} - the measure "
          f"{'can' if ok else 'CANNOT'} distinguish a corrupted text region "
          "from a legible one at the shipped threshold "
          f"({MIN_CONTRAST:.0f})")

    for p in (good_path, bad_path):
        try:
            os.remove(p)
        except OSError:
            pass
    print()
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="build/qa/screens")
    ap.add_argument("--tmp-dir", default="build/qa/_rbg0_selftest")
    ap.add_argument("--min", type=float, default=MIN_CONTRAST)
    ap.add_argument("--skip-selftest", action="store_true")
    args = ap.parse_args()

    if not args.skip_selftest:
        if not selftest(args.tmp_dir):
            print("GATE RBG0 LEGIBILITY: RED - the underlying contrast "
                  "measure failed its own self-test; a GREEN on a real "
                  "capture would not be trustworthy")
            return 1

    path = os.path.join(args.dir, CAPTURE_NAME)
    if not os.path.isfile(path):
        print("=== REAL CAPTURE CHECK ===")
        print(f"  {path} not found.")
        print()
        print("GATE RBG0 LEGIBILITY: INCONCLUSIVE - the self-test above "
              "proves the MEASURE works; it does not prove the actual "
              "firmware is legible with RBG0 armed. That needs a real "
              "capture, which this script does not take itself:")
        print()
        print("    CCFLAGS_EXTRA=\"-DCOUP_RBG0_TITLE_DEMO\" "
              "bash scripts/docker-saturn-build.sh examples/coup/saturn")
        print("    cp examples/coup/saturn/_build/track01.bin "
              "build/coup_game/track01.bin")
        print("    cp examples/coup/saturn/_build/game.cue "
              "build/coup_game/game.cue")
        print(f"    python scripts/qa/qa_retroarch.py --shot "
              f"build/coup_game/game.cue --seconds 15 --out {path}")
        print()
        print("  (--seconds 15 covers the demo's boot + fly-in window; "
              "SATURN_RBG0_FLYIN_FRAMES=90 is 1.5 s, so the capture should "
              "land mid-fly-in or just after, when RBG0 is still/most "
              "recently armed.)")
        return 2

    print("=== REAL CAPTURE CHECK ===")
    c = leg.contrast(path, TITLE_REGION)
    bar = "#" * int(min(c, 180) / 6)
    print(f"  title_rbg0   {c:6.1f}  {bar}")
    print(f"  threshold    {args.min:.0f}")
    print()

    if c < args.min:
        print(f"GATE RBG0 LEGIBILITY: RED - {c:.1f} is below {args.min:.0f}; "
              "RBG0 being armed has degraded (or destroyed) the title "
              "text's legibility")
        return 1

    print("GATE RBG0 LEGIBILITY: GREEN - the title text separates cleanly "
          "from its background with RBG0 armed, at the same threshold the "
          "shipped legibility gate already uses.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
