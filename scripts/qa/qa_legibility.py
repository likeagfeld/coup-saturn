#!/usr/bin/env python3
"""
qa_legibility.py - Measure that text is actually readable on every screen.

WHY THIS GATE EXISTS
  The UI plates are blended with the painted backdrop so the art shows through
  instead of being buried. That is a direct trade against readability, and a
  trade needs a number on both sides. Without one, "it still looks fine" is a
  judgement made at thumbnail size on a monitor, about a console output that
  will be viewed on a CRT.

  It also catches text that was never legible in the first place. The title's
  [R] Rules hint measured 40.4 against 95-135 everywhere else - the least
  readable text in the game, drawn over the brightest part of the skyline with
  nothing behind it, and that was true long before any blending existed.

WHAT IT MEASURES
  Per screen, in that screen's densest text region: the gap between the
  brightest decile of pixels (glyph strokes) and the region median (whatever
  the glyphs sit on). A large gap means the letters separate from their
  background. It is deliberately NOT a check that the region is dark - a
  bright plate with dark text scores just as well.

  Thresholds come from measurement, not taste. Opaque panels scored 176. The
  blended screens land at 95-135. 60 is set well below every passing screen
  and well above the 40.4 that was genuinely hard to read.

USAGE
  python scripts/qa/qa_legibility.py
  python scripts/qa/qa_legibility.py --dir build/qa/screens
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image

MIN_CONTRAST = 60.0

# Each screen's densest text region, in 320x224 screen coordinates.
REGIONS = {
    "title":      (100, 195, 230, 215),   # the [R] Rules hint
    "settings":   (20, 95, 300, 125),     # difficulty row
    "rules":      (10, 30, 300, 190),     # body text
    "connecting": (40, 60, 290, 180),     # stage text and log
    "name_entry": (50, 115, 280, 165),    # controls
    "lobby":      (44, 110, 280, 170),    # overlay controls
    "game":       (0, 0, 320, 45),        # game log
    "game_over":  (80, 180, 240, 205),    # winner line
}


def contrast(path, box):
    a = np.asarray(Image.open(path).convert("L")).astype(float)
    if a.shape[1] != 320:
        # RetroArch emits 330 px wide for this core (5 px overscan each side).
        a = np.asarray(Image.open(path).convert("L").resize((320, 224),
                                                            Image.LANCZOS)
                       ).astype(float)
    x0, y0, x1, y1 = box
    r = a[y0:y1, x0:x1]
    if r.size == 0:
        return None
    f = np.sort(r.flatten())
    ink = f[int(len(f) * 0.90):].mean()
    bg = np.median(f)
    return ink - bg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="build/qa/screens")
    ap.add_argument("--min", type=float, default=MIN_CONTRAST)
    args = ap.parse_args()

    print("=== TEXT LEGIBILITY (ink-to-background contrast) ===")
    print(f"  threshold {args.min:.0f} - opaque panels score 176, the blended "
          "screens 95-135,")
    print("  and the unreadable title hint scored 40.4 before it was fixed")
    print()

    missing, fails = [], []
    for name, box in sorted(REGIONS.items()):
        p = os.path.join(args.dir, f"{name}.png")
        if not os.path.isfile(p):
            missing.append(name)
            print(f"  {name:12}   NOT CAPTURED")
            continue
        c = contrast(p, box)
        bar = "#" * int(min(c, 180) / 6)
        flag = "" if c >= args.min else "   <-- BELOW THRESHOLD"
        if c < args.min:
            fails.append((name, c))
        print(f"  {name:12} {c:6.1f}  {bar}{flag}")

    print()
    if missing:
        print("GATE LEGIBILITY: RED - screen(s) never captured: "
              f"{', '.join(missing)}")
        print("  run: bash scripts/qa/capture_all_screens.sh")
        return 1
    if fails:
        print(f"GATE LEGIBILITY: RED - {len(fails)} screen(s) below "
              f"{args.min:.0f}")
        for n, c in fails:
            print(f"  - {n}: {c:.1f}")
        return 1
    print("GATE LEGIBILITY: GREEN - every screen's text separates cleanly "
          "from what it sits on")
    return 0


if __name__ == "__main__":
    sys.exit(main())
