#!/usr/bin/env python3
"""
qa_portrait_registration.py - Prove the portrait frames are registered.

WHY THIS GATE EXISTS
  Reported from both the Saturn and the web builds: "the cards appear to be
  shifting in position inside their frames" during the idle animation.

  It is not a converter bug. It is in the delivered art, and it is only in
  two of the five characters. MEASURED by finding, for each frame, the (dx,
  dy) that best aligns it to frame 1:

    duke     (0,0) (-1,0) (-3,0) (-4,0) (0,-3) (-1,-2) (-3,-3) (-4,-3)
    captain  (0,0) (-1,0) ( 1,0) ( 3,0) (-1,0) (-1, 0) ( 1, 0) ( 2, 0)
    ambassador / assassin / contessa: all (0,0)

  Duke's pattern gives the cause away - horizontal drift accumulating across
  four columns, then the same horizontal pattern repeated with a ~3 px
  vertical offset. That is a 4x2 source grid whose second row sits low, cut
  on a uniform grid.

  The drift is the same PROPORTION on both platforms - 1 px of 64 on Saturn
  (1.6%), 4 px of 240 on the web (1.7%) - which is what rules the converters
  out. Scaling a mis-registered source scales the mis-registration with it.

HOW IT MEASURES
  Brute-force sum-of-squared-difference over a small shift window, against
  frame 1 of the same character. Pure geometry: it does not care what the
  character looks like, only whether successive frames sit in the same place.

NEGATIVE CONTROL
  --selftest shifts a known-good character's frames by a known amount and
  requires the measurement to recover exactly that shift. A registration
  check that cannot detect a deliberate 3 px offset cannot be trusted to
  report zero.
"""

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                "..", "..", "examples", "coup", "assets"))
try:
    from convert_portraits import repair_edge_columns
except Exception:
    repair_edge_columns = None

# One pixel of play is the quantisation floor once a 64 px frame has been
# resampled; beyond that the eye reads it as the figure bobbing.
MAX_DRIFT_PX = 1

SATURN_DIR = ("examples/coup/assets/Official Art/coup_saturn_complete_asset_pack/"
              "coup_saturn_complete_asset_pack/saturn_ready/portraits")
WEB_GLOB = "web-staging/assets/portraits/*"
FRAMES = 8


def best_shift(ref, img, radius):
    h, w = ref.shape
    best = (float("inf"), 0, 0)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            a = ref[max(0, dy):h + min(0, dy), max(0, dx):w + min(0, dx)]
            b = img[max(0, -dy):h + min(0, -dy), max(0, -dx):w + min(0, -dx)]
            if a.size == 0:
                continue
            d = float(np.mean((a - b) ** 2))
            if d < best[0]:
                best = (d, dx, dy)
    return best[1], best[2]


def drift(frames, radius):
    ref = frames[0]
    shifts = [best_shift(ref, f, radius) for f in frames]
    dxs = [s[0] for s in shifts]
    dys = [s[1] for s in shifts]
    return shifts, max(dxs) - min(dxs), max(dys) - min(dys)


def saturn_sets():
    out = []
    if not os.path.isdir(SATURN_DIR):
        return out
    for ch in sorted(os.listdir(SATURN_DIR)):
        files = sorted(glob.glob(os.path.join(SATURN_DIR, ch, "*.png")))
        if len(files) < 2:
            continue
        # Through the converter's edge repair, because that is what reaches
        # the disc. Scanning the untouched masters measures art nobody
        # ships, and reported RED against a build that was already correct -
        # the same scoping mistake qa_fidelity made by comparing against raw
        # sources. A gate must measure the artefact, not the input.
        ims = [Image.open(f).convert("RGB") for f in files]
        if repair_edge_columns is not None:
            ims = [repair_edge_columns(i) for i in ims]
        frames = [np.asarray(i.convert("L"), dtype=float) for i in ims]
        out.append((f"saturn/{ch}", frames, max(3, frames[0].shape[1] // 12)))
    return out


def web_sets():
    out = []
    for path in sorted(glob.glob(WEB_GLOB)):
        im = Image.open(path).convert("L")
        w, h = im.size
        fw = w // FRAMES
        a = np.asarray(im, dtype=float)
        frames = [a[:, i * fw:(i + 1) * fw] for i in range(FRAMES)]
        out.append((f"web/{os.path.basename(path)}", frames, max(3, fw // 12)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    sets = saturn_sets() + web_sets()
    if not sets:
        print("GATE PORTRAIT REGISTRATION: INCONCLUSIVE - no portrait sets "
              "found; nothing measured")
        return 2

    fails = []

    # --- bright edge columns ---------------------------------------------
    # Reported as "faint white vertical slice clips on the right side of a
    # couple of the duke animation frames" plus "a sliver in one of the
    # contessa frames". MEASURED on the web strips before repair:
    #   duke     frames 1,2,5,6  cols 238-239 at 244-250 vs interior 61
    #   contessa frames 1,2,3,5,6,7  col 239 at 191-211 vs interior 47
    # The first threshold was +120, and it MISSED what the user still saw:
    # duke frames 2 and 6 carry three columns at 155-156 against 61, only
    # +94. Worse, the repair then sourced its replacement from column 237 -
    # itself part of the sliver but under threshold - and propagated the
    # artifact instead of removing it.
    #
    # +60 catches those (+94) and still leaves genuine rim lighting alone
    # (duke's remaining edge measures +25 after repair). Ambassador's edge
    # columns alternate sides frame to frame - R, L, R, L - which is a
    # grid-cut signature, not lighting, so those are repaired too.
    EDGE_DELTA = 60
    for name, frames, _ in sets:
        bad = []
        for i, fr in enumerate(frames):
            col = fr.mean(axis=1) if fr.ndim == 1 else fr.mean(axis=0)
            col = fr.mean(axis=0)
            interior = float(np.median(col[2:-2]))
            n = fr.shape[1]
            m = max(1, n // 20)
            for idx in list(range(m)) + list(range(n - m, n)):
                if col[idx] > interior + EDGE_DELTA:
                    bad.append((i + 1, idx, round(float(col[idx])),
                                round(interior)))
        if bad:
            fails.append(f"{name}: bright edge column(s) {bad} - a slice of "
                         f"the neighbouring grid cell, visible as a white "
                         f"sliver during playback")

    for name, frames, radius in sets:
        shifts, rx, ry = drift(frames, radius)
        worst = max(rx, ry)
        fw = frames[0].shape[1]
        mark = "" if worst <= MAX_DRIFT_PX else "   <-- DRIFTS"
        print(f"  {name:26s} frame {fw:4d}px  dx {rx}  dy {ry}  "
              f"({100.0 * worst / fw:.1f}% of width){mark}")
        if worst > MAX_DRIFT_PX:
            fails.append(f"{name}: frames move up to {worst} px "
                         f"({100.0 * worst / fw:.1f}% of the frame) - "
                         f"shifts {shifts}")

    if args.selftest:
        print()
        # Take a set that measured clean and displace one frame deliberately.
        clean = None
        for name, frames, radius in sets:
            _, rx, ry = drift(frames, radius)
            if max(rx, ry) == 0:
                clean = (name, frames, radius)
                break
        if clean is None:
            print("  control: no clean set available to displace; skipping")
        else:
            name, frames, radius = clean
            moved = list(frames)
            moved[3] = np.roll(np.roll(frames[3], 3, axis=1), 2, axis=0)
            _, rx, ry = drift(moved, radius)
            print(f"  control: displaced one frame of {name} by (3, 2) -> "
                  f"measured dx {rx}, dy {ry}")
            if rx < 3 or ry < 2:
                print()
                print("GATE PORTRAIT REGISTRATION: RED - the measurement "
                      "cannot recover a deliberate 3x2 px offset, so a zero "
                      "from it means nothing")
                return 1

    print()
    if fails:
        print("GATE PORTRAIT REGISTRATION: RED")
        for f in fails:
            print("  - " + f)
        return 1
    print(f"GATE PORTRAIT REGISTRATION: GREEN - all {len(sets)} portrait sets "
          f"hold position within {MAX_DRIFT_PX} px")
    return 0


if __name__ == "__main__":
    sys.exit(main())
