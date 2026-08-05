#!/usr/bin/env python3
"""
qa_magenta.py - No sprite palette may contain the transparency key colour.

WHY THIS GATE EXISTS
  The effect and UI art arrives keyed on flat magenta (#FF00FF), which the
  converter turns into palette index 0. The strict key test catches flat
  magenta exactly - but the source art was resampled before delivery, so every
  key edge carries PARTIAL magenta: measured examples (231,33,165),
  (247,99,156), (198,41,181).

  Those fail the key test. They survive as opaque "artwork", and the quantizer
  then spends real palette slots on them. MEASURED before the fix: 45
  magenta-ish entries across 16 sprites - coup, assassinate, steal, tax,
  exchange, block, challenge, question, crown, all five coin tiers, treasury
  and skull. Every one of them paints a visible magenta fringe around the
  sprite wherever the key edge fell.

  Widening the key would be the wrong fix: it eats genuine purple and red
  artwork. convert_effects.py instead UNMIXES each contaminated pixel from its
  clean neighbours, so the edge keeps its real colour, and keys only pixels
  that are more key than artwork.

WHAT IT MEASURES
  Decodes every generated palette back to RGB and fails on any entry with the
  magenta signature - high red AND high blue with low green. Index 0 is
  skipped, because that IS the key.

USAGE
  python scripts/qa/qa_magenta.py
  python scripts/qa/qa_magenta.py --header <path>    # e.g. a git-extracted copy
"""

import argparse
import re
import sys

# Magenta signature. Deliberately generous: a fringe pixel does not have to be
# saturated to be visible against dark artwork.
R_MIN, G_MAX, B_MIN = 140, 100, 140


def rgb555(v):
    r, g, b = (v & 0x1F) << 3, ((v >> 5) & 0x1F) << 3, ((v >> 10) & 0x1F) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5)


def scan(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    hits = []
    total = 0
    for m in re.finditer(r"coup_(?:fx|ui)_pal_(\w+)\[16\]\s*=\s*\{([^}]*)\}",
                         src, re.S):
        name = m.group(1)
        vals = [int(v, 16)
                for v in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(2))]
        for i, v in enumerate(vals):
            if i == 0:
                continue            # index 0 IS the key
            total += 1
            r, g, b = rgb555(v)
            if r > R_MIN and b > B_MIN and g < G_MAX:
                hits.append((name, i, (r, g, b)))
    return total, hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", default="examples/coup/saturn/coup_fx_data.h")
    args = ap.parse_args()

    total, hits = scan(args.header)

    print("=== MAGENTA KEY BLEED IN SPRITE PALETTES ===")
    print(f"  scanned {total} palette entries in {args.header}")
    print()
    if hits:
        by_sprite = {}
        for name, i, c in hits:
            by_sprite.setdefault(name, []).append((i, c))
        for name, lst in sorted(by_sprite.items()):
            shown = ", ".join(f"idx{i}={c}" for i, c in lst[:3])
            more = f" (+{len(lst) - 3} more)" if len(lst) > 3 else ""
            print(f"  {name:16} {len(lst):>3} entries   {shown}{more}")
        print()
        print(f"GATE MAGENTA: RED - {len(hits)} palette entr(ies) carry the "
              "key colour")
        print("  Each one paints a visible fringe wherever the key edge fell.")
        print("  Fix in convert_effects.py defringe(), not by widening the key")
        print("  - widening eats genuine purple and red artwork.")
        return 1

    print("GATE MAGENTA: GREEN - no palette entry carries the key colour")
    return 0


if __name__ == "__main__":
    sys.exit(main())
