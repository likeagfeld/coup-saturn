#!/usr/bin/env python3
"""
qa_cram_msb.py - No uploaded CRAM entry may have bit 15 set.

WHY THIS GATE EXISTS
  The UI plates blend with the painted backdrop and the artwork does not. That
  separation rests on VDP2's sprite colour-calculation condition SPCCCS=3
  ("MSB"), and the two halves of it work by DIFFERENT mechanisms:

    RGB-code plates   ST-058-R2 p.207: "When the sprite color format is RGB,
                      color calculation is always performed if SPCCCS is set
                      to '3'." Unconditional. Nothing can switch it off.

    Palette artwork   The MSB tested is bit 15 of the CRAM ENTRY, not of the
                      framebuffer word. Sega Technical Bulletin SOA-7,
                      16 Aug 1995, "Sprite Transparency": "only pixels whose
                      PALETTE ENTRIES have their most-significant bits set
                      will be subjected to color calculations... simply by
                      rewriting a palette entry or two."

  The second half is the trap. Bit 15 is a don't-care when authoring RGB555,
  and every converter in this project happens to emit it clear. Nothing
  enforces that. One palette entry written as 0x8000|colour would make those
  glyphs, portraits or cards silently translucent - a defect with no build
  error, no test failure, and no symptom until someone looks at the right
  screen on the right background.

  MEASURED 2026-08-05: 673 uploaded values, 0 with bit 15 set. Correct today.
  This gate keeps it that way.

  (This also corrects the reasoning previously written into sgl_defs.h and
  main_saturn.c, which claimed the discrimination came from bit 15 of the
  framebuffer word. It reaches the right answer for the wrong reason: with
  SPCLMD=1 a palette pixel's framebuffer MSB is 0 BY DEFINITION - it is the
  format discriminator, ST-058-R2 p.203 - so a framebuffer reading would make
  the condition dead for palette sprites, contradicting SOA-7.)

USAGE
  python scripts/qa/qa_cram_msb.py
"""

import os
import re
import sys

# Every file that uploads palette words to CRAM.
SOURCES = [
    "examples/coup/saturn/coup_anim_sprites.h",
    "examples/coup/saturn/coup_fx_data.h",
    "examples/coup/saturn/coup_sprites.h",
    "pal/saturn/saturn_vdp2.c",
]

PALETTE_DECL = re.compile(
    r"(\w*palette\w*|\w*_pal\w*)\s*\[[^\]]*\]\s*(?:\[[^\]]*\]\s*)?=\s*\{",
    re.I)


def palette_blocks(src):
    """Yield (name, brace-balanced initialiser text) for each palette array."""
    for m in PALETTE_DECL.finditer(src):
        start = m.end() - 1
        depth = 0
        end = None
        for i in range(start, len(src)):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        if end is not None:
            yield m.group(1), src[start:end]


def scan(paths):
    total, offenders = 0, []
    missing = []
    for p in paths:
        if not os.path.isfile(p):
            missing.append(p)
            continue
        src = open(p, encoding="utf-8", errors="replace").read()
        for name, blk in palette_blocks(src):
            vals = [int(v, 16)
                    for v in re.findall(r"0[xX]([0-9a-fA-F]{1,4})\b", blk)]
            total += len(vals)
            hits = [v for v in vals if v & 0x8000]
            if hits:
                offenders.append((p, name, hits))
    return total, offenders, missing


def main():
    paths = sys.argv[1:] or SOURCES
    total, offenders, missing = scan(paths)

    print("=== CRAM ENTRIES WITH BIT 15 SET ===")
    print("  ST-058-R2 p.207 + Tech Bulletin SOA-7: for a PALETTE sprite the")
    print("  colour-calculation MSB condition tests bit 15 of the CRAM entry,")
    print("  so a set bit there makes that artwork translucent.")
    print()

    for p in missing:
        print(f"  MISSING SOURCE  {p}")
    for p, name, hits in offenders:
        shown = ", ".join(hex(h) for h in hits[:8])
        more = f" (+{len(hits) - 8} more)" if len(hits) > 8 else ""
        print(f"  {os.path.basename(p)} :: {name} -> {shown}{more}")

    print(f"  scanned {total} palette word(s), "
          f"{sum(len(h) for _, _, h in offenders)} with bit 15 set")
    print()

    if missing:
        print("GATE CRAM MSB: RED - a palette source is missing; the scan is "
              "incomplete and cannot be trusted")
        return 1
    if offenders:
        print("GATE CRAM MSB: RED - artwork would blend that must stay opaque")
        return 1
    print("GATE CRAM MSB: GREEN - no uploaded palette entry sets bit 15, so "
          "only the RGB-code UI plates blend")
    return 0


if __name__ == "__main__":
    sys.exit(main())
