#!/usr/bin/env python3
"""
qa_fx_geometry.py - Prove the effect sprites are fetched and drawn correctly.

WHY THIS GATE EXISTS
  Effects rendered "duplicated or cut off or off center or partially
  rendered" - four symptoms that read like four bugs and are in fact one:
  the source-size and destination-size argument pairs were passed to
  saturn_vdp1_draw_sprite_scaled() in the wrong order.

  saturn_vdp1.c:225 builds CMDSIZE from src_w/src_h - that is the TEXTURE
  extent VDP1 fetches. saturn_vdp1.c:230-235 build the quad vertices from
  dst_w/dst_h - that is the size drawn. Swap them and VDP1 fetches a
  128x128 region out of a 64x64 texture (running into the next frame, then
  off the end of the allocation) and paints it at native size at a position
  computed for double size.

HOW IT MEASURES
  Statically, from the two files that define the contract - no capture,
  no emulator. It reads the parameter order out of the header, reads the
  argument order out of the call site, and reads the real frame dimensions
  out of the generated data header. Then for every effect and every frame
  it computes what VDP1 would actually fetch and where the quad would land,
  and asserts three things:

    1. FETCH EXTENT  - the bytes CMDSIZE causes VDP1 to read must equal the
                       bytes that frame occupies. Over-reading is the
                       "duplicated" and "partial" symptom.
    2. IN BOUNDS     - the fetch must not run past the last frame of the
                       effect into whatever is uploaded after it.
    3. CENTRED       - the quad's centre must land on the requested anchor
                       within 1 px (integer halving). Off by more is the
                       "off center" symptom.

NEGATIVE CONTROL
  --selftest re-runs every assertion against the swapped argument order.
  All three must fail there. A gate that cannot fail proves nothing.
"""

import argparse
import re
import sys

VDP1_H = "pal/saturn/saturn_vdp1.h"
FX_C = "examples/coup/saturn/coup_fx_loader.c"
FX_DATA = "examples/coup/saturn/coup_fx_data.h"

SCALE_NUM, SCALE_DEN = 2, 1       # coup_fx.c draws effects at 2x
BPP_DIV = 2                       # 4bpp: two pixels per byte


def read(p):
    return open(p, encoding="utf-8", errors="replace").read()


def param_order():
    """The declared parameter order of saturn_vdp1_draw_sprite_scaled."""
    m = re.search(r"bool\s+saturn_vdp1_draw_sprite_scaled\s*\((.*?)\)\s*;",
                  read(VDP1_H), re.S)
    if not m:
        return None, "declaration not found in " + VDP1_H
    names = re.findall(r"\b(?:int|uint32_t)\s+(\w+)", m.group(1))
    return names, None


def call_args():
    """The argument expressions at the coup_fx_draw_scaled call site."""
    src = read(FX_C)
    m = re.search(r"saturn_vdp1_draw_sprite_scaled\s*\((.*?)\)\s*;", src, re.S)
    if not m:
        return None, "call not found in " + FX_C
    depth, cur, out = 0, "", []
    for ch in m.group(1):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return [" ".join(a.split()) for a in out], None


def fx_table():
    """(w, h, frames, frame_bytes) for every effect, from the generated data."""
    src = read(FX_DATA)
    m = re.search(r"coup_fx_info\[COUP_FX_COUNT\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        return None, "coup_fx_info table not found in " + FX_DATA
    rows = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
                      m.group(1))
    return [tuple(int(v) for v in r) for r in rows], None


def evaluate(args, names, table, label):
    """Compute the fetch extent and quad geometry these arguments produce."""
    # Map each declared parameter to the expression actually passed.
    bind = dict(zip(names, args))

    def kind(expr):
        """Which quantity an argument expression carries."""
        if expr in ("w", "h"):
            return "scaled"
        if expr in ("info->w", "info->h"):
            return "native"
        return "other"

    fails = []
    print(f"  [{label}]")
    for name in ("src_w", "src_h", "dst_w", "dst_h"):
        print(f"    {name:6s} <- {bind.get(name, '?'):12s} ({kind(bind.get(name,''))})")

    for fx, (w, h, frames, frame_bytes) in enumerate(table):
        sw = w * SCALE_NUM // SCALE_DEN
        sh = h * SCALE_NUM // SCALE_DEN
        val = {"w": sw, "h": sh, "info->w": w, "info->h": h}

        src_w = val.get(bind.get("src_w"), 0)
        src_h = val.get(bind.get("src_h"), 0)
        dst_w = val.get(bind.get("dst_w"), 0)
        dst_h = val.get(bind.get("dst_h"), 0)

        # 1. FETCH EXTENT: CMDSIZE = ((src_w/8) << 8) | src_h  (saturn_vdp1.c:225)
        fetched = (src_w // BPP_DIV) * src_h
        if fetched != frame_bytes:
            fails.append(
                f"fx{fx} ({w}x{h}): CMDSIZE fetches {fetched} B but the frame "
                f"is {frame_bytes} B "
                f"({'over' if fetched > frame_bytes else 'under'}-read "
                f"{abs(fetched - frame_bytes)} B)")

        # 2. IN BOUNDS: the last frame's fetch must not leave the allocation.
        last = (frames - 1) * ((frame_bytes + 7) & ~7)
        if last + fetched > frames * ((frame_bytes + 7) & ~7):
            over = last + fetched - frames * ((frame_bytes + 7) & ~7)
            fails.append(
                f"fx{fx}: frame {frames-1} reads {over} B past the effect's "
                f"last byte")

        # 3. CENTRED: top-left is cx - w/2, so the quad centre is
        #    (cx - w/2) + dst_w/2 and must land back on cx.
        drift = abs((-(sw // 2)) + dst_w // 2)
        if drift > 1:
            fails.append(
                f"fx{fx}: quad centre lands {drift} px from the anchor "
                f"(positioned for {sw} px, drawn {dst_w} px)")

    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true",
                    help="also prove the gate fails on the swapped order")
    args = ap.parse_args()

    names, err = param_order()
    if err:
        print(f"GATE FX-GEOMETRY: RED - {err}")
        return 1
    call, err = call_args()
    if err:
        print(f"GATE FX-GEOMETRY: RED - {err}")
        return 1
    table, err = fx_table()
    if err:
        print(f"GATE FX-GEOMETRY: RED - {err}")
        return 1

    print(f"  declaration   {VDP1_H}: ({', '.join(names)})")
    print(f"  call site     {FX_C}: {len(call)} args, {len(table)} effects")
    print()

    fails = evaluate(call, names, table, "as shipped")

    if args.selftest:
        print()
        # The control is pinned to the KNOWN-BAD order - the scaled size in
        # src_w/src_h - not to "whatever the call site does, swapped". Swapping
        # relative to the shipped order makes the control follow the code: when
        # the code is wrong the control is right and reports zero failures,
        # which is exactly backwards. MEASURED: that is what this gate did on
        # its first run, and it is the same class of self-agreeing gate that
        # let three earlier gates pass on broken art.
        i = names.index("src_w")
        bad = list(call)
        bad[i:i + 4] = ["w", "h", "info->w", "info->h"]
        ctrl = evaluate(bad, names, table,
                        "negative control: scaled size in src_w/src_h")
        print(f"    -> {len(ctrl)} failures on the swapped order")
        if not ctrl:
            print()
            print("GATE FX-GEOMETRY: RED - the gate does not fail on a known-"
                  "bad argument order, so a GREEN from it means nothing")
            return 1

    print()
    if fails:
        print("GATE FX-GEOMETRY: RED")
        for f in fails[:12]:
            print("  - " + f)
        if len(fails) > 12:
            print(f"  ... and {len(fails) - 12} more")
        return 1
    print(f"GATE FX-GEOMETRY: GREEN - all {len(table)} effects fetch exactly "
          f"their own frame and draw centred on the anchor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
