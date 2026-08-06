#!/usr/bin/env python3
"""
qa_vram_rbg0_map.py - RBG0's VRAM/CRAM claims must never land on the text
bank or collide with the other named claims, and the placement floor the
task directed must actually hold.

WHY THIS GATE EXISTS
  The whole RBG0 title-fly-in plan (pal/saturn/saturn_rbg0.h) stands or
  falls on ONE fact: RBG0's bitmap pattern data lives in VDP2 VRAM bank B0
  (empty, unclaimed anywhere else), never bank B1 (where saturn_pal.c's
  NBG0 text character data and PNT already live - ASCII_CEL_VRAM_ADDR =
  0x25e60000). Per skill gotcha #7 and the VDP2 manual's own RAMCTL
  section (VDP2_Manual.txt:6437-6452, cited in full in saturn_rbg0.h), a
  VRAM bank claimed for RBG0 use has its normal cycle-pattern arbitration
  BYPASSED - landing that on the SAME bank the text layer's character data
  and PNT already occupy would silently corrupt the text stack every frame
  RBG0 is armed. This is exactly the regression class the design doc's
  Approach C worry was about, and this gate is the static, host-runnable
  proof that the shipped constant does not have it.

  It also pins the task's own placement directive (rotation parameter
  table offset within bank A1 must be >= 0x0380) and checks the table does
  not run into the reserved top-256-byte region where the back-screen
  colour lives (main_saturn.c:226).

WHAT IT MEASURES
  Parses the shipped constants straight out of pal/saturn/saturn_rbg0.h and
  pal/saturn/saturn_bg.h (never duplicates a numeric literal that could
  drift out of sync with the source) and asserts:
    1. the RBG0 bitmap bank is B0, never B1
    2. the bitmap claim is exactly one whole VRAM bank (0x20000 B)
    3. the rotation parameter table offset is >= the directed 0x0380 floor
    4. the table stays clear of the reserved top-256-B region of bank A1
    5. the CRAM palette claim does not collide with the background
       bitmap's own 256-colour bank (saturn_bg.h's SATURN_BG_CRAM_OFFSET)

  Before trusting any of that, --selftest runs the SAME comparison logic
  against a deliberately-bad B1 fixture and confirms it fires RED - the
  task's explicit requirement: "deliberately set RDBSx(B1)=11 instead of
  B0 and show the gate fires RED, proving it can detect the text stack
  being broken." That is run automatically as part of `main()` before the
  real check, every time this script runs.

USAGE
  python scripts/qa/qa_vram_rbg0_map.py
"""

import os
import re
import sys

# Hardware constants this gate reasons about (never invented - see
# saturn_rbg0.h's citation trail for each).
VDP2_VRAM_B0 = 0x25E40000
VDP2_VRAM_B1 = 0x25E60000   # NBG0 text char data + PNT (saturn_pal.c:93)
VRAM_BANK_BYTES = 0x20000
A1_RESERVED_TOP = 0x1FF00   # back-screen colour at A1+0x1FFFE (main_saturn.c:226)
PARAM_MIN_OFFSET = 0x0380   # the task's directive floor


def hex_const(text, name):
    m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+)u?" % re.escape(name), text)
    return int(m.group(1), 16) if m else None


def dec_const(text, name):
    m = re.search(r"#define\s+%s\s+(\d+)" % re.escape(name), text)
    return int(m.group(1)) if m else None


def expr_const(text, name):
    """Evaluate a #define whose value is a small C arithmetic expression of
    integer literals, e.g. `(512u * 256u)` - handles SATURN_RBG0_BITMAP_BYTES,
    which is deliberately written as W*H in the header rather than a literal
    so it can never drift out of sync with SATURN_RBG0_BITMAP_W/H. Strips the
    C `u`/`U` unsigned suffix (not valid Python int syntax) before eval'ing
    only digits/hex/parens/whitespace/operators - never arbitrary code."""
    m = re.search(r"#define\s+%s\s+(.+?)(?://.*)?$" % re.escape(name),
                   text, re.M)
    if not m:
        return None
    expr = re.sub(r"/\*.*?\*/", "", m.group(1))
    expr = re.sub(r"(?<=[0-9A-Fa-fxX])[uU]\b", "", expr).strip()
    if not re.fullmatch(r"[0-9A-Fa-fxX+\-*/() \t]+", expr):
        return None
    try:
        return int(eval(expr, {"__builtins__": {}}, {}))
    except Exception:
        return None


def load(path):
    if not os.path.isfile(path):
        return None
    return open(path, encoding="utf-8", errors="replace").read()


def check_bitmap_bank(bitmap_vram_addr, label):
    """Returns a list of failure strings; empty means GREEN for this check."""
    fails = []
    if bitmap_vram_addr == VDP2_VRAM_B1:
        fails.append(f"{label}: bitmap data is in VRAM bank B1 "
                     f"(0x{VDP2_VRAM_B1:08X}) - THIS IS THE TEXT BANK "
                     "(NBG0 character data + PNT, saturn_pal.c:93). Arming "
                     "RBG0 with a B1 claim bypasses B1's normal cycle-"
                     "pattern arbitration and corrupts the text layer every "
                     "frame RBG0 is armed.")
    elif bitmap_vram_addr != VDP2_VRAM_B0:
        fails.append(f"{label}: bitmap data at 0x{bitmap_vram_addr:08X} is "
                     "neither bank B0 nor B1 - unexpected placement, not "
                     "verified against any citation in saturn_rbg0.h.")
    return fails


def selftest():
    """RED-then-GREEN proof that check_bitmap_bank() actually discriminates,
    per the task's explicit requirement. Returns True if the self-test
    itself passes (i.e. the gate mechanism is trustworthy)."""
    print("=== SELF-TEST: can this gate detect the B1 regression? ===")

    red = check_bitmap_bank(VDP2_VRAM_B1, "fixture (deliberately B1)")
    green = check_bitmap_bank(VDP2_VRAM_B0, "fixture (correct B0)")

    red_fires = len(red) > 0
    green_clean = len(green) == 0

    print(f"  RDBSx(B1) fixture  -> {'RED (correctly flagged)' if red_fires else 'GREEN - BUG IN THE GATE'}")
    for f in red:
        print(f"    - {f}")
    print(f"  RDBSx(B0) fixture  -> {'GREEN (correctly clean)' if green_clean else 'RED - BUG IN THE GATE'}")
    for f in green:
        print(f"    - {f}")

    ok = red_fires and green_clean
    print(f"  SELF-TEST: {'PASS' if ok else 'FAIL'} - the gate "
          f"{'can' if ok else 'CANNOT'} distinguish the broken fixture from "
          "the working one")
    print()
    return ok


def main():
    if not selftest():
        print("GATE VRAM RBG0 MAP: RED - the gate's own detection logic is "
              "broken; its GREEN on the real build would not mean anything")
        return 1

    print("=== REAL BUILD CHECK ===")
    rbg0_h = load("pal/saturn/saturn_rbg0.h")
    bg_h = load("pal/saturn/saturn_bg.h")

    if rbg0_h is None:
        print("GATE VRAM RBG0 MAP: RED - pal/saturn/saturn_rbg0.h not found")
        return 1

    bitmap_vram = hex_const(rbg0_h, "SATURN_RBG0_BITMAP_VRAM")
    bitmap_bytes = (hex_const(rbg0_h, "SATURN_RBG0_BITMAP_BYTES") or
                     dec_const(rbg0_h, "SATURN_RBG0_BITMAP_BYTES") or
                     expr_const(rbg0_h, "SATURN_RBG0_BITMAP_BYTES"))
    param_offset = hex_const(rbg0_h, "SATURN_RBG0_PARAM_VRAM_OFFSET")
    param_min = hex_const(rbg0_h, "SATURN_RBG0_PARAM_MIN_OFFSET")
    table_bytes = hex_const(rbg0_h, "SATURN_RBG0_PARAM_TABLE_BYTES")
    reserved_top = hex_const(rbg0_h, "SATURN_RBG0_A1_RESERVED_TOP")
    cram_offset = hex_const(rbg0_h, "SATURN_RBG0_CRAM_OFFSET")

    missing = [n for n, v in [
        ("SATURN_RBG0_BITMAP_VRAM", bitmap_vram),
        ("SATURN_RBG0_BITMAP_BYTES", bitmap_bytes),
        ("SATURN_RBG0_PARAM_VRAM_OFFSET", param_offset),
        ("SATURN_RBG0_PARAM_MIN_OFFSET", param_min),
        ("SATURN_RBG0_PARAM_TABLE_BYTES", table_bytes),
        ("SATURN_RBG0_A1_RESERVED_TOP", reserved_top),
        ("SATURN_RBG0_CRAM_OFFSET", cram_offset),
    ] if v is None]
    if missing:
        print(f"GATE VRAM RBG0 MAP: RED - could not parse: {', '.join(missing)}")
        return 1

    print(f"  bitmap VRAM         0x{bitmap_vram:08X} "
          f"({'B0' if bitmap_vram == VDP2_VRAM_B0 else 'B1' if bitmap_vram == VDP2_VRAM_B1 else '?'})")
    print(f"  bitmap size         0x{bitmap_bytes:X} B")
    print(f"  param table offset  0x{param_offset:X} (A1), "
          f"{param_offset + table_bytes - 1:#X} end")
    print(f"  directed floor      0x{param_min:X}")
    print(f"  A1 reserved top     0x{reserved_top:X}")
    print(f"  CRAM claim          0x{cram_offset:X}..0x{cram_offset + 0x1FF:X}")

    fails = []
    fails += check_bitmap_bank(bitmap_vram, "shipped constant")

    if bitmap_bytes != VRAM_BANK_BYTES:
        fails.append(f"bitmap claims 0x{bitmap_bytes:X} B, not exactly one "
                     f"VRAM bank (0x{VRAM_BANK_BYTES:X})")

    if param_min != PARAM_MIN_OFFSET:
        fails.append(f"SATURN_RBG0_PARAM_MIN_OFFSET is 0x{param_min:X}, "
                     f"expected the task's directed floor 0x{PARAM_MIN_OFFSET:X}")
    if param_offset < param_min:
        fails.append(f"param table offset 0x{param_offset:X} is BELOW the "
                     f"directed floor 0x{param_min:X}")

    table_end = param_offset + table_bytes
    if reserved_top != A1_RESERVED_TOP:
        fails.append(f"SATURN_RBG0_A1_RESERVED_TOP is 0x{reserved_top:X}, "
                     f"expected 0x{A1_RESERVED_TOP:X} (back-screen colour "
                     "region, main_saturn.c:226)")
    if table_end > reserved_top:
        fails.append(f"param table ends at 0x{table_end:X}, which runs into "
                     f"the reserved top-of-bank region starting 0x{reserved_top:X}")

    # CRAM: must not collide with the background bitmap's own claim.
    if bg_h is not None:
        bg_offset = hex_const(bg_h, "SATURN_BG_CRAM_OFFSET")
        if bg_offset is not None:
            claim_lo, claim_hi = cram_offset, cram_offset + 0x1FF
            bg_lo, bg_hi = bg_offset, bg_offset + 0x1FF
            if max(claim_lo, bg_lo) <= min(claim_hi, bg_hi):
                fails.append(f"CRAM claim 0x{claim_lo:X}..0x{claim_hi:X} "
                             f"overlaps the background bitmap palette "
                             f"0x{bg_lo:X}..0x{bg_hi:X} (saturn_bg.h)")
        else:
            print("  (saturn_bg.h present but SATURN_BG_CRAM_OFFSET not "
                  "found - skipping the CRAM cross-check)")
    else:
        print("  (pal/saturn/saturn_bg.h not found - skipping the CRAM "
              "cross-check)")

    print()
    if fails:
        print("GATE VRAM RBG0 MAP: RED")
        for f in fails:
            print("  - " + f)
        return 1

    print("GATE VRAM RBG0 MAP: GREEN - RBG0's bitmap is in bank B0 (not the "
          "text bank), fills exactly one bank, the rotation parameter table "
          "sits above the directed floor and clear of the reserved region, "
          "and the CRAM palette claim does not collide with the background")
    return 0


if __name__ == "__main__":
    sys.exit(main())
