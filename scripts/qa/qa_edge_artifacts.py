#!/usr/bin/env python3
"""
qa_edge_artifacts.py - Global sweep for sheet-cut damage on EVERY edge.

WHY THIS GATE EXISTS
  The background seam was chased three times and each fix only looked at the
  RIGHT edge, because that is where it was first reported. A global sweep of
  all four edges across every asset class found the same damage on axes
  nobody had checked:

    every Saturn background has an anomalous edge ROW - B1/B2/B3 at the top,
    B4/B5/B6/B7 at the bottom
    all five card faces carry 1 px columns on both sides
    web backgrounds and cards inherit the same

  Same cause as the right-edge seam: panels cut from a sheet slightly too
  large in every direction, not just horizontally.

HOW IT DISCRIMINATES - this is the whole difficulty
  Bright edges are not automatically artifacts. A first pass that flagged
  "edge much brighter than the interior" produced 29 hits, most of them real
  artwork:
    - the UI sprites' edges measure EXACTLY 170, which is the luma of
      magenta #FF00FF, the chroma key the Saturn pipeline requires
    - boxart.webp's left columns are the cream SEGA spine
    - a skyline background legitimately has a bright sky at the top

  What separates damage from art is a SHARP STEP against the immediately
  adjacent pixels, not brightness against the global interior. A slice of a
  neighbouring panel meets its host at a discontinuity; sky, borders and
  spines transition into what surrounds them. Measuring the step dropped 29
  candidates to 18 genuine ones, and the excluded 11 are all verifiably
  artwork.

NEGATIVE CONTROL
  --selftest paints a synthetic bright edge onto a clean asset and requires
  detection, then verifies the magenta key and a smooth gradient are NOT
  flagged. A sweep that flags everything is as useless as one that flags
  nothing.
"""

import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "examples", "coup", "assets"))
try:
    from convert_backgrounds import repair_edges
except Exception:
    repair_edges = None

STEP = 60          # step against the adjacent pixel that means "different image"
RUN = 6            # deepest edge run considered
KEY_LUMA = 170.0   # mean of #FF00FF - the Saturn chroma key, never an artifact

SATURN = ("examples/coup/assets/Official Art/coup_saturn_complete_asset_pack/"
          "coup_saturn_complete_asset_pack/saturn_ready")

GROUPS = {
    "web/bg":     "web-staging/assets/bg/*",
    "web/cards":  "web-staging/assets/cards/*",
    "web/fx":     "web-staging/assets/fx/*",
    "web/ui":     "web-staging/assets/ui/*",
    "web/logo":   "web-staging/assets/logo/*",
    "web/portraits": "web-staging/assets/portraits/*",
    "sat/bg":     SATURN + "/backgrounds/*",
    "sat/cards":  SATURN + "/cards/*",
    "sat/ui":     SATURN + "/ui/*",
}


def is_key(v):
    return abs(v - KEY_LUMA) < 3.0


def edge_run(vals, n, side):
    """Contiguous edge run and the step to the first pixel past it.

    The run continues while each pixel is ANOMALOUS AGAINST THE INTERIOR,
    not while it resembles its predecessor. The predecessor test looks
    reasonable and is wrong: a card's edge reads 252 then 146 then 67, so it
    stops after ONE pixel, and a repair sourced from the next pixel copies
    146 - still damage. That mistake was made twice, on the portraits and
    again here, and both times the artifact was spread rather than removed.
    """
    interior = float(np.median(vals[RUN:-RUN])) if n > 2 * RUN         else float(np.median(vals))
    idxs = []
    rng = range(n - 1, n - 1 - RUN, -1) if side in "RB" else range(RUN)
    for i in rng:
        if abs(vals[i] - interior) <= STEP:
            break
        idxs.append(i)
    if not idxs:
        return None
    nxt = min(idxs) - 1 if side in "RB" else max(idxs) + 1
    if nxt < 0 or nxt >= n:
        return None
    mean = float(np.mean([vals[i] for i in idxs]))
    step = abs(mean - float(vals[nxt]))
    if step > STEP and not is_key(mean):
        return (side, sorted(idxs), round(step))
    return None


def scan_image(a):
    g = a.mean(axis=2)
    h, w = g.shape
    col, row = g.mean(axis=0), g.mean(axis=1)
    out = []
    for side, vals, n in (("R", col, w), ("L", col, w),
                          ("B", row, h), ("T", row, h)):
        r = edge_run(vals, n, side)
        if r:
            out.append(r)
    return out


def sweep():
    findings = {}
    for name, pat in GROUPS.items():
        hits = []
        for f in sorted(glob.glob(pat)):
            if os.path.isdir(f):
                continue
            try:
                im = Image.open(f).convert("RGB")
            except Exception:
                continue
            # Saturn assets are measured THROUGH the converter's repair,
            # because that is what reaches the disc. Scanning the untouched
            # masters judges art nobody ships and reports RED against a
            # correct build - the same scoping mistake qa_fidelity and
            # qa_portrait_registration each made once.
            if name.startswith("sat/") and repair_edges is not None:
                im = repair_edges(im)
            e = scan_image(np.asarray(im).astype(float))
            if e:
                hits.append((os.path.basename(f), e))
        findings[name] = hits
    return findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    findings = sweep()
    total = 0
    for name, hits in findings.items():
        print(f"  {name:16s} {'clean' if not hits else str(len(hits)) + ' file(s)'}")
        for fn, e in hits:
            print(f"      {fn:26s} {e}")
            total += 1

    if args.selftest:
        print()
        # Positive: a painted bright edge must be caught.
        synth = np.full((64, 64, 3), 40.0)
        synth[:, -1, :] = 240.0
        got = scan_image(synth)
        print(f"  control +: painted bright edge -> "
              f"{'DETECTED' if got else 'MISSED'}")
        # Negative 1: the magenta key must NOT be flagged.
        keyed = np.full((64, 64, 3), 40.0)
        keyed[:, -1, :] = [255, 0, 255]
        k = scan_image(keyed)
        print(f"  control -: magenta chroma key -> "
              f"{'flagged (BAD)' if k else 'ignored (correct)'}")
        # Negative 2: a smooth gradient must NOT be flagged.
        grad = np.zeros((64, 64, 3))
        for x in range(64):
            grad[:, x, :] = 20 + x * 3
        gr = scan_image(grad)
        print(f"  control -: smooth gradient -> "
              f"{'flagged (BAD)' if gr else 'ignored (correct)'}")
        if not got or k or gr:
            print()
            print("GATE EDGE ARTIFACTS: RED - the discriminator does not "
                  "separate damage from artwork, so its verdict means nothing")
            return 1

    print()
    if total:
        print(f"GATE EDGE ARTIFACTS: RED - {total} asset(s) carry a slice of "
              f"a neighbouring panel")
        return 1
    print("GATE EDGE ARTIFACTS: GREEN - no asset carries sheet-cut damage on "
          "any edge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
