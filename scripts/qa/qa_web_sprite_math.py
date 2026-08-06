#!/usr/bin/env python3
"""
qa_web_sprite_math.py - Prove the web sprite strips land on whole frames.

WHY THIS GATE EXISTS
  The staging client shipped with every animation "constantly streaming left
  to right over and over" instead of stepping between frames. It looked like
  a design choice until the arithmetic was checked.

  With `background-size: 800%`, a percentage background-position resolves as
  (element - image) * p = -7 * elementWidth * p. So frame N sits at

      p = N / 7        NOT N / 8

  The client's own code proves it independently: ui.js holds the
  reduced-motion still at 42.857% and calls it "frame 3 of 8", and
  3/7 = 0.42857.

  But the animation used `steps(8)`, whose default `end` behaviour emits
  0, 1/8, 2/8 ... 7/8. Multiply by 7 and the frame indices are

      0, 0.875, 1.75, 2.625, 3.5, 4.375, 5.25, 6.125

  Every frame after the first is a SLICE OF TWO FRAMES. Eight of those in
  sequence is not a flicker - it reads as one continuous slide, which is
  exactly how it was reported.

  `jump-none` emits N values INCLUDING both endpoints - 0, 1/7, 2/7 ... 7/7 -
  landing on each frame exactly.

WHY IT IS PYTHON AND NOT PART OF THE JSDOM SMOKE TEST
  scripts/smoke_web_staging.mjs needs jsdom, which is not vendored, has no
  package.json, and is not installed anywhere in this repo - it cannot
  currently run. This check is pure text analysis, so it has no dependencies
  and actually executes.

NEGATIVE CONTROL
  --selftest re-runs the frame arithmetic against the OLD `end` behaviour and
  requires it to fail. A gate that passes both ways measures nothing.
"""

import argparse
import os
import re
import sys

CSS = "web-staging/css/style.css"
UI_JS = "web-staging/js/ui.js"

TOLERANCE = 0.01        # a frame index must be this close to a whole number


def frame_indices(steps, jump_none, frames=8):
    """Frame index shown at each step, given how the timing function samples.

    Position p maps to frame p*(frames-1), because 100% aligns the image's
    RIGHT edge with the element's right edge - the LAST frame, not one past
    it. That -1 is the whole bug.
    """
    span = frames - 1
    if jump_none:
        # N values including both endpoints: i/(steps-1)
        return [(i / (steps - 1)) * span for i in range(steps)]
    # default `end`: i/steps, never reaching 1
    return [(i / steps) * span for i in range(steps)]


def check_whole(indices):
    return [i for i in indices if abs(i - round(i)) > TOLERANCE]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(CSS):
        print(f"GATE WEB SPRITE MATH: INCONCLUSIVE - {CSS} not found")
        return 2

    css = open(CSS, encoding="utf-8", errors="replace").read()
    fails = []

    # --- every sprite-strip timing function must be jump-none -------------
    # Balance parentheses by hand. `[^)]*` truncates
    # `steps(var(--fx-steps, 8), jump-none)` at the NESTED close paren and
    # reports a correct rule as broken - which this gate did on its first run,
    # against CSS that was already fixed. A parser that misreads the thing it
    # is judging produces a confident wrong verdict, which is worse than no
    # gate at all.
    rules = []
    for m in re.finditer(r"animation-timing-function:\s*steps\(", css):
        i = m.end()
        depth = 1
        while i < len(css) and depth:
            if css[i] == "(":
                depth += 1
            elif css[i] == ")":
                depth -= 1
            i += 1
        rules.append(css[m.end():i - 1])
    sprite = [r for r in rules if "--fx-steps" in r or re.match(r"\s*8\s*(,|$)", r)]
    print(f"  sprite-strip steps() rules found: {len(sprite)}")
    for r in sprite:
        state = "jump-none" if "jump-none" in r else "DEFAULT end"
        print(f"    steps({r.strip()})  -> {state}")
        if "jump-none" not in r:
            fails.append(f"steps({r.strip()}) uses the default `end`, so "
                         f"frames land between two images")
    if len(sprite) < 2:
        fails.append(f"expected at least 2 sprite-strip rules, found "
                     f"{len(sprite)} - did the selector change?")

    # --- the arithmetic itself --------------------------------------------
    good = frame_indices(8, jump_none=True)
    bad = check_whole(good)
    print(f"  jump-none frame indices: "
          f"{', '.join(f'{v:.3f}' for v in good)}")
    if bad:
        fails.append(f"jump-none still lands off-frame at {bad}")

    # --- the reduced-motion still must sit on a real frame too ------------
    if os.path.exists(UI_JS):
        ui = open(UI_JS, encoding="utf-8", errors="replace").read()
        m = re.search(r"backgroundPosition\s*=\s*'([\d.]+)%", ui)
        if m:
            p = float(m.group(1)) / 100.0
            fr = p * 7
            print(f"  reduced-motion still: {m.group(1)}% -> frame "
                  f"{fr:.3f}")
            if abs(fr - round(fr)) > TOLERANCE:
                fails.append(f"reduced-motion still sits at frame {fr:.3f}, "
                             f"between two images")
        else:
            fails.append("reduced-motion still position not found in ui.js")

    # --- negative control --------------------------------------------------
    if args.selftest:
        print()
        old = frame_indices(8, jump_none=False)
        off = check_whole(old)
        print(f"  control (default `end`): "
              f"{', '.join(f'{v:.3f}' for v in old)}")
        print(f"    -> {len(off)} of {len(old)} land between frames")
        if not off:
            print()
            print("GATE WEB SPRITE MATH: RED - the arithmetic cannot tell the "
                  "broken timing function from the correct one, so a GREEN "
                  "from it means nothing")
            return 1

    print()
    if fails:
        print("GATE WEB SPRITE MATH: RED")
        for f in fails:
            print("  - " + f)
        return 1
    print("GATE WEB SPRITE MATH: GREEN - every sprite strip steps on whole "
          "frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
