#!/usr/bin/env python3
"""
qa_cram_map.py - No two CRAM claims may overlap.

WHY THIS GATE EXISTS
  The loaders chain their CRAM allocation, each starting where the previous
  one stopped. verify_facelift gate E checks that the CHAINING is wired up -
  that each loader calls the previous one's end-bank accessor. It does not
  check where the chain ENDS, and the background bitmap's palette is not part
  of the chain at all: it is placed at a fixed 256-colour boundary.

  So the chain grew into it, silently.

  MEASURED 2026-08-05, before the fix: the 16-colour chain ended at byte
  0x700 while the background's 256-colour bank 3 occupies 0x600-0x7FF. The
  last EIGHT sprite palettes - SKULL, WORDMARK, CARD_BACK and all five card
  faces - were sharing CRAM with the first 128 background colours.

  On screen the background upload wins, because saturn_bg_set_scene() rewrites
  all 256 entries on every scene change, after the sprite palettes were
  uploaded once at boot. MEASURED on a captured title screen: of 1,674 solid
  interior wordmark pixels, 99.9% matched the BACKGROUND palette, mean colour
  error 18.3 against 167.2 for the wordmark's own palette.

  Nothing caught it. qa_title_wordmark.py was green throughout, because it
  correlates EDGE maps - deliberately colour-insensitive, so that VDP1 colour
  handling and emulator gamma could not affect it. That made it blind to a
  pure-colour defect, and the wordmark's gold happened to be replaced by the
  backdrop's gold, so it looked plausible.

  The lesson is in the gate list: a shape gate and a colour gate are not
  substitutes for one another.

WHAT IT MEASURES
  Rebuilds the whole CRAM claim map from the same constants the firmware
  uses, then asserts that no two claims intersect.

USAGE
  python scripts/qa/qa_cram_map.py
"""

import os
import re
import sys

CRAM_BYTES = 0x1000          # 2048 entries x 2 bytes (SGL inits 2048-colour)
BANK16 = 32                  # bytes per 16-colour bank
BANK256 = 512                # bytes per 256-colour bank


def const(path, name):
    if not os.path.isfile(path):
        return None
    s = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"#define\s+%s\s+(\d+)" % name, s)
    if m:
        return int(m.group(1))
    m = re.search(r"%s\s*=\s*(\d+)" % name, s)
    return int(m.group(1)) if m else None


def build_map():
    """Reproduce the runtime CRAM claims from the shipped constants."""
    pal = const("pal/saturn/saturn_pal.h", "SATURN_PAL_COUNT") or 16
    spr = const("examples/coup/saturn/coup_sprites.h", "COUP_SPR_COUNT")
    anim = const("examples/coup/saturn/coup_anim_sprites.h", "COUP_ANIM_CHARS")
    fx = const("examples/coup/saturn/coup_fx_data.h", "COUP_FX_COUNT")
    ui = const("examples/coup/saturn/coup_fx_data.h", "COUP_UI_COUNT")
    if None in (spr, anim, fx, ui):
        return None, None

    claims = []
    bank = 0

    def claim(label, n):
        nonlocal bank
        if n:
            claims.append((label, bank * BANK16, (bank + n) * BANK16 - 1))
        bank += n

    claim("text palettes", pal)
    claim("sprite sheet", spr)
    claim("gameover (retired)", 0)
    claim("animated portraits", anim)
    claim("effect sequences", fx)
    claim("UI sprites", ui)

    # The background bitmap palette is NOT part of the chain - it sits at a
    # fixed 256-colour boundary, which is why the chain could grow into it.
    off = None
    h = "pal/saturn/saturn_bg.h"
    if os.path.isfile(h):
        s = open(h, encoding="utf-8", errors="replace").read()
        m = re.search(r"#define\s+SATURN_BG_CRAM_OFFSET\s+0x([0-9A-Fa-f]+)", s)
        if m:
            off = int(m.group(1), 16)
    if off is not None:
        claims.append(("background bitmap (256-col)", off, off + BANK256 - 1))

    return claims, bank


def main():
    claims, top_bank = build_map()
    if claims is None:
        print("GATE CRAM MAP: RED - could not read the palette-count "
              "constants; the map cannot be verified")
        return 1

    print("=== CRAM CLAIM MAP ===")
    for label, lo, hi in claims:
        print(f"  {label:28} 0x{lo:04X}..0x{hi:04X}"
              f"  ({(hi - lo + 1) // BANK16} x 16-col banks)"
              if "256-col" not in label else
              f"  {label:28} 0x{lo:04X}..0x{hi:04X}")
    print(f"  {'':28} CRAM is 0x0000..0x{CRAM_BYTES - 1:04X}")
    print()

    fails = []
    for i in range(len(claims)):
        for j in range(i + 1, len(claims)):
            a, b = claims[i], claims[j]
            lo = max(a[1], b[1])
            hi = min(a[2], b[2])
            if lo <= hi:
                fails.append(f"{a[0]} (0x{a[1]:04X}..0x{a[2]:04X}) overlaps "
                             f"{b[0]} (0x{b[1]:04X}..0x{b[2]:04X}) at "
                             f"0x{lo:04X}..0x{hi:04X} = "
                             f"{(hi - lo + 1) // 2} colour entries")

    for label, lo, hi in claims:
        if hi >= CRAM_BYTES:
            fails.append(f"{label} ends at 0x{hi:04X}, past the end of CRAM "
                         f"(0x{CRAM_BYTES - 1:04X})")

    if fails:
        print("GATE CRAM MAP: RED")
        for f in fails:
            print("  - " + f)
        print()
        print("  A palette collision does not fail the build and does not")
        print("  fail a shape-based gate. It shows up as artwork wearing")
        print("  another asset's colours.")
        return 1

    print("GATE CRAM MAP: GREEN - every CRAM claim is disjoint")
    return 0


if __name__ == "__main__":
    sys.exit(main())
