#!/usr/bin/env python3
"""
repair_edge_artifacts.py - Remove sheet-cut damage from an asset's edges.

Deliberately imports the DETECTOR from scripts/qa/qa_edge_artifacts.py rather
than restating it. When repair and gate carry separate copies of the rule they
drift, and the failure is silent: the repair "succeeds" while the gate still
reports damage, or worse, the gate goes green over damage the repair only
half-removed. That happened twice on the portraits.

Each anomalous run is replaced with the first pixel PAST it - which the
detector already computes, because a replacement taken from inside the run
spreads the artifact instead of removing it.

Usage:
  python scripts/repair_edge_artifacts.py <glob> [<glob> ...]
"""

import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "qa"))
from qa_edge_artifacts import edge_run, RUN, STEP, is_key   # noqa: E402


def _run_for(vals, n, side):
    """The detector's run, plus the index to source the replacement from."""
    r = edge_run(vals, n, side)
    if r is None:
        return None
    _, idxs, _ = r
    nxt = min(idxs) - 1 if side in "RB" else max(idxs) + 1
    if nxt < 0 or nxt >= n:
        return None
    return idxs, nxt


def repair(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im).astype(np.uint8).copy()
    fixed = []

    # Columns first, then re-measure before doing rows: repairing a column
    # changes every row's mean, so row detection must see the corrected image
    # or it will chase a number the columns already moved.
    for side in ("R", "L"):
        g = a.astype(float).mean(axis=2)
        got = _run_for(g.mean(axis=0), g.shape[1], side)
        if got:
            idxs, nxt = got
            for c in idxs:
                a[:, c, :] = a[:, nxt, :]
            fixed.append((side, idxs))

    for side in ("B", "T"):
        g = a.astype(float).mean(axis=2)
        got = _run_for(g.mean(axis=1), g.shape[0], side)
        if got:
            idxs, nxt = got
            for r in idxs:
                a[r, :, :] = a[nxt, :, :]
            fixed.append((side, idxs))

    if fixed:
        kw = {"quality": 82, "method": 6} \
            if os.path.splitext(path)[1].lower() == ".webp" else {}
        Image.fromarray(a).save(path, **kw)
    return fixed


def main(argv):
    import glob as _glob
    if not argv:
        print(__doc__)
        return 2
    touched = 0
    for pat in argv:
        for f in sorted(_glob.glob(pat)):
            if os.path.isdir(f):
                continue
            fx = repair(f)
            print(f"  {os.path.basename(f):26s} "
                  f"{'clean' if not fx else fx}")
            if fx:
                touched += 1
    print(f"  repaired {touched} asset(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
