#!/usr/bin/env python3
"""
qa_portraits.py - Adversarial verification of the generated portrait sprites.

Deliberately does NOT trust convert_portraits.py. It re-parses the emitted
headers and tries to prove the output is unusable. A generator that checks its
own work proves nothing; this reads the artifact that will actually be compiled.

Failure modes it is built to catch:
  - any pixel using colour index 0, which VDP1 renders TRANSPARENT and which
    would let the painted backdrop show through the characters again
  - "animation" whose frames are identical, i.e. motion that does not exist
  - a loop that jumps, so frame N-1 does not lead back into frame 0
  - a degenerate palette wasting the 15 colours available
  - RGB555 words with bit 15 set, which is not a valid palette entry
  - frame sizes disagreeing with what the loader will index

Exit code 1 on any failure.
"""

import re
import sys

DATA = "examples/coup/saturn/coup_anim_sprite_data.h"
SPRITES = "examples/coup/saturn/coup_anim_sprites.h"

SPRITE_W, SPRITE_H = 32, 48
EXPECT_FRAME_BYTES = SPRITE_W * SPRITE_H // 2


def parse_frames(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for name, fno, body in re.findall(
            r"coup_animdata_(\w+?)_f(\d+)\[\d+\]\s*=\s*\{(.*?)\};", src, re.S):
        blob = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body))
        out.setdefault(name, {})[int(fno)] = blob
    return out


def parse_palettes(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    return {name: [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", body)]
            for name, body in re.findall(
                r"coup_anim_pal_(\w+)\[16\]\s*=\s*\{([^}]*)\}", src)}


def nybbles(blob):
    out = []
    for b in blob:
        out.append(b >> 4)
        out.append(b & 0xF)
    return out


def main():
    frames = parse_frames(DATA)
    palettes = parse_palettes(SPRITES)
    failures = []

    if not frames:
        print("FAIL: no frame arrays parsed"); return 1

    for name in sorted(frames):
        fr = frames[name]
        idx = sorted(fr)

        # --- size contract -------------------------------------------------
        for n in idx:
            if len(fr[n]) != EXPECT_FRAME_BYTES:
                failures.append(f"{name} f{n}: {len(fr[n])} bytes, "
                                f"expected {EXPECT_FRAME_BYTES}")

        # --- transparency: the bug that started all of this -----------------
        transparent = 0
        for n in idx:
            transparent += nybbles(fr[n]).count(0)
        if transparent:
            failures.append(
                f"{name}: {transparent} pixels use index 0 (TRANSPARENT) - "
                "the backdrop would show through the character")

        # --- the animation must actually animate ----------------------------
        identical = 0
        deltas = []
        for a, b in zip(idx, idx[1:]):
            d = sum(1 for x, y in zip(fr[a], fr[b]) if x != y)
            deltas.append(d)
            if d == 0:
                identical += 1
        if identical:
            failures.append(f"{name}: {identical} consecutive frame pairs are "
                            "byte-identical - that is not animation")

        # --- the loop must close --------------------------------------------
        wrap = sum(1 for x, y in zip(fr[idx[-1]], fr[idx[0]]) if x != y)
        typical = sorted(deltas)[len(deltas) // 2] if deltas else 0
        if typical and wrap > typical * 6:
            failures.append(
                f"{name}: wrap frame {idx[-1]}->0 differs by {wrap} bytes vs a "
                f"typical step of {typical} - the loop visibly jumps")

        # --- palette sanity ---------------------------------------------------
        pal = palettes.get(name)
        if not pal:
            failures.append(f"{name}: no palette found")
        else:
            if pal[0] != 0:
                failures.append(f"{name}: palette index 0 = 0x{pal[0]:04X}, "
                                "must stay reserved at 0x0000")
            bad = [p for p in pal if p > 0x7FFF]
            if bad:
                failures.append(f"{name}: {len(bad)} palette words have bit 15 "
                                "set; not valid RGB555 entries")
            distinct = len(set(pal[1:]))
            if distinct < 10:
                failures.append(f"{name}: only {distinct} distinct colours in "
                                "15 slots - degenerate palette")

        used = set()
        for n in idx:
            used.update(nybbles(fr[n]))
        print(f"  {name:11} {len(idx):2} frames  indices {min(used)}-{max(used)}  "
              f"median delta {sorted(deltas)[len(deltas)//2] if deltas else 0:4} B  "
              f"wrap {wrap:4} B")

    print()
    if failures:
        print(f"GATE PORTRAITS: RED - {len(failures)} failure(s)")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("GATE PORTRAITS: GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
