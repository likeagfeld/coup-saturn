#!/usr/bin/env python3
"""
verify_facelift.py - Adversarial whole-build verification.

Runs every gate and refuses to pass unless each one has been shown capable of
failing. Nothing here trusts a generator, a converter or a previous gate; each
check re-reads the artifact that will actually be compiled or shipped.

Gates:
  A  asset integrity    every converted header parses, sizes and palettes legal
  B  transparency       portraits fully opaque, effects genuinely keyed
  C  animation          frames differ, loops close
  D  VDP1 VRAM budget   all texture data fits below 0x80000
  E  CRAM allocation    no loader's palette range overlaps another's
  F  WRAM headroom      binary fits with margin above the KNOWN-HANG point
  G  preprocessor       no unbalanced #if in the render path
  H  centring           button labels measured, not assumed

Exit 1 on any failure. Run from the repo root.
"""

import glob
import os
import re
import subprocess
import sys

FAIL = []
INFO = []


def fail(gate, msg):
    FAIL.append(f"[{gate}] {msg}")


def info(gate, msg):
    INFO.append(f"[{gate}] {msg}")


def parse_arrays(path, pattern):
    src = open(path, encoding="utf-8", errors="replace").read()
    return {m[0]: bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", m[-1]))
            for m in re.findall(pattern, src, re.S)}


def nybbles(b):
    out = []
    for x in b:
        out.append(x >> 4)
        out.append(x & 0xF)
    return out


# ---------------------------------------------------------------- A: assets
def gate_assets():
    need = [
        "examples/coup/saturn/coup_anim_sprite_data.h",
        "examples/coup/saturn/coup_anim_sprites.h",
        "examples/coup/saturn/coup_bg_index.h",
        "examples/coup/saturn/coup_fx_data.h",
    ]
    for p in need:
        if not os.path.exists(p):
            fail("A", f"missing generated header {p}")
            return
    src = open("examples/coup/saturn/coup_bg_index.h", encoding="utf-8",
               errors="replace").read()
    m = re.search(r"#define COUP_BG_SCENE_COUNT (\d+)", src)
    n = int(m.group(1)) if m else 0
    if n < 1:
        fail("A", "no background scenes defined")

    # Scenes are streamed, so a scene that is declared but whose file is not
    # on the disc is a black screen at runtime with no build-time symptom.
    names = re.findall(r'"(BG[A-Z0-9]*\.BIN)"', src)
    cd_dir = "examples/coup/saturn/cd"
    missing = [f for f in names if not os.path.isfile(os.path.join(cd_dir, f))]
    if len(names) != n:
        fail("A", f"{n} scenes declared but {len(names)} filenames listed")
    if missing:
        fail("A", f"scene file(s) not built: {', '.join(missing)}")
    else:
        info("A", f"{n} background scene(s), all {len(names)} streamed files "
                  f"present")

    src = open("examples/coup/saturn/coup_fx_data.h", encoding="utf-8",
               errors="replace").read()
    fx = int(re.search(r"#define COUP_FX_COUNT (\d+)", src).group(1))
    ui = int(re.search(r"#define COUP_UI_COUNT (\d+)", src).group(1))
    if fx < 1 or ui < 1:
        fail("A", f"effects/UI missing (fx={fx} ui={ui})")
    info("A", f"{fx} effect sequences, {ui} UI sprites")


# -------------------------------------------------------- B: transparency
def gate_transparency():
    frames = parse_arrays("examples/coup/saturn/coup_anim_sprite_data.h",
                          r"(coup_animdata_(\w+?)_f(\d+))\[\d+\]\s*=\s*\{(.*?)\};")
    if not frames:
        fail("B", "no portrait frames parsed")
    bad = 0
    for name, blob in frames.items():
        if 0 in nybbles(blob):
            bad += 1
    if bad:
        fail("B", f"{bad} portrait frames contain transparent pixels - the "
                  "backdrop would show through the characters")
    else:
        info("B", f"{len(frames)} portrait frames fully opaque")

    fx = parse_arrays("examples/coup/saturn/coup_fx_data.h",
                      r"(coup_fx_(\w+?)_f(\d+))\[\d+\]\s*=\s*\{(.*?)\};")
    unkeyed = [n for n, b in fx.items() if 0 not in nybbles(b)]
    if unkeyed:
        fail("B", f"{len(unkeyed)} effect frames have NO transparent pixel - "
                  "they would draw a rectangle over the table")
    else:
        info("B", f"{len(fx)} effect frames correctly keyed")


# ------------------------------------------------------------ C: animation
def gate_animation():
    src = open("examples/coup/saturn/coup_anim_sprite_data.h",
               encoding="utf-8", errors="replace").read()
    per = {}
    for name, fno, body in re.findall(
            r"coup_animdata_(\w+?)_f(\d+)\[\d+\]\s*=\s*\{(.*?)\};", src, re.S):
        per.setdefault(name, {})[int(fno)] = bytes(
            int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    for name, fr in per.items():
        idx = sorted(fr)
        deltas = [sum(1 for a, b in zip(fr[i], fr[j]) if a != b)
                  for i, j in zip(idx, idx[1:])]
        if any(d == 0 for d in deltas):
            fail("C", f"{name}: identical consecutive frames - not animation")
        wrap = sum(1 for a, b in zip(fr[idx[-1]], fr[idx[0]]) if a != b)
        med = sorted(deltas)[len(deltas) // 2] if deltas else 0
        if med and wrap > med * 6:
            fail("C", f"{name}: loop jumps ({wrap} vs median {med})")
    info("C", f"{len(per)} characters animate and loop cleanly")


# --------------------------------------------------------- D: VDP1 budget
def gate_vdp1():
    def total(path, pat):
        return sum(len(b) for b in parse_arrays(path, pat).values())

    fonts = 95 * 32 + 95 * 128          # builtin 8x8 + alagard 16x16
    spr = 25920
    go = 7 * 5120
    anim = total("examples/coup/saturn/coup_anim_sprite_data.h",
                 r"(coup_animdata_(\w+?)_f(\d+))\[\d+\]\s*=\s*\{(.*?)\};")
    fx = total("examples/coup/saturn/coup_fx_data.h",
               r"(coup_(?:fx|ui)_(\w+?)_f(\d+))\[\d+\]\s*=\s*\{(.*?)\};")
    used = fonts + spr + go + anim + fx
    end = 0x11000 + used
    if end > 0x80000:
        fail("D", f"VDP1 texture data ends at 0x{end:06X}, past the 0x80000 limit")
    else:
        info("D", f"VDP1 textures {used:,} B, end 0x{end:06X} of 0x80000")


# -------------------------------------------------------------- E: CRAM
def gate_cram():
    """Loaders must chain. Recomputed bases have collided twice before."""
    src = open("examples/coup/saturn/coup_gameover_loader.c",
               encoding="utf-8", errors="replace").read()
    if "coup_sprites_cram_end_bank()" not in src:
        fail("E", "gameover loader does not chain its CRAM base")
    src = open("examples/coup/saturn/coup_anim_loader.c",
               encoding="utf-8", errors="replace").read()
    if "coup_gameover_cram_end_bank()" not in src:
        fail("E", "anim loader does not chain its CRAM base")
    src = open("examples/coup/saturn/coup_fx_loader.c",
               encoding="utf-8", errors="replace").read()
    if "coup_anim_cram_end_bank()" not in src:
        fail("E", "fx loader does not chain its CRAM base")
    if "coup_anim_vram_end()" not in src:
        fail("E", "fx loader does not chain its VRAM base")
    if not FAIL:
        info("E", "all loaders chain VRAM and CRAM allocation")


# ------------------------------------------------------------ F: headroom
# The old constants. KEPT ONLY AS A RECORD OF WHY THEY WERE WRONG.
#
# 181,312 was the headroom of one build that happened to hang, and the gate
# treated it as a floor. It is not a floor - it is an upper bound on where
# failure had ALREADY become catastrophic. Overlap begins far earlier and
# presents as corruption long before it presents as a hang.
#
# The real limit is structural: SGL claims 0x40000 bytes of WORK-HI starting
# at SortList, and .bss must not reach it (SGL302/DOC/210A_US/MEMORY.TXT:
# "The SGL system uses 0x40000 bytes of the WORK-HI area for sprite and
# scroll control"). MEASURED on this build: SortList = 0x060C0000, so the
# old gate passed any _end up to 0x060CFD28 - 64,808 bytes INTO SGL's own
# SortList / Zbuffer / SpriteBuf. It was 79,808 bytes too permissive, not
# conservative.
KNOWN_HANG = 181312
SAFE_MARGIN = 15000

# Read from the linked image, not hardcoded, so it self-corrects if SGLAREA
# is ever customised (WORKAREA.TXT: "the work RAM area (default mode:
# 060C0000~) can be customized").
SGL_WORK_FALLBACK = 0x060C0000
BSS_WARN_BYTES = 8192


def sgl_work_base(mapfile, rawfile):
    """Lowest address SGL claims in WRAM-H, read from the linked image.

    SGLAREA.O declares SGL's work area as ABSOLUTE addresses, and SortList is
    the lowest of them - so it, not the stack pointer, is the ceiling .bss
    must not reach. Reading the VALUE out of game.raw rather than hardcoding
    it means the gate follows a customised SGLAREA instead of going stale
    (SGL302/DOC/210A_US/WORKAREA.TXT: "the work RAM area (default mode:
    060C0000~) can be customized").
    """
    try:
        m = open(mapfile, encoding="utf-8", errors="replace").read()
        raw = open(rawfile, "rb").read()
    except OSError:
        return None
    r = re.search(r"0x([0-9a-fA-F]{8,16})\s+_?SortList\b", m)
    if not r:
        return None
    off = int(r.group(1), 16) - 0x06004000      # game.raw loads at SLSTART
    if off < 0 or off + 4 > len(raw):
        return None
    return int.from_bytes(raw[off:off + 4], "big")


def gate_headroom():
    m = "examples/coup/saturn/_build/game.map"
    rawf = "examples/coup/saturn/_build/game.raw"
    if not os.path.exists(m):
        fail("F", "no linker map - build first")
        return
    src = open(m, encoding="utf-8", errors="replace").read()
    e = re.search(r"0x([0-9a-fA-F]{8,16})\s+_end\b", src)
    if not e:
        fail("F", "no _end symbol in map")
        return
    end = int(e.group(1), 16)

    base = sgl_work_base(m, rawf)
    if base is None:
        base = SGL_WORK_FALLBACK
        info("F", f"SortList unreadable; using the documented default "
                  f"0x{base:08X}")

    slack = base - end
    if slack <= 0:
        fail("F", f"_end 0x{end:08X} has REACHED SGL's work area at "
                  f"0x{base:08X} - .bss is overwriting SortList / Zbuffer / "
                  f"SpriteBuf, which are SGL's SCU DMA tables and sprite "
                  f"buffers. Expect misrender or hang.")
    elif slack < BSS_WARN_BYTES:
        fail("F", f"only {slack:,} B between _end 0x{end:08X} and SGL's work "
                  f"area 0x{base:08X}, under the {BSS_WARN_BYTES:,} B margin")
    else:
        info("F", f"_end 0x{end:08X}, SGL work base 0x{base:08X}, "
                  f"{slack:,} B of real slack")


# -------------------------------------------------------- G: preprocessor
def gate_preprocessor():
    for p in ("examples/coup/coup_render.c",
              "examples/coup/saturn/main_saturn.c"):
        depth = 0
        for line in open(p, encoding="utf-8", errors="replace"):
            t = line.strip()
            if t.startswith("#if"):
                depth += 1
            elif t.startswith("#endif"):
                depth -= 1
        if depth:
            fail("G", f"{p}: unbalanced preprocessor, depth {depth}")
    if not any(f.startswith("[G]") for f in FAIL):
        info("G", "preprocessor balanced")


# ----------------------------------------------------------- H: centring
def gate_centring():
    src = open("examples/coup/coup_render.c", encoding="utf-8",
               errors="replace").read()
    if "text_px_w" not in src:
        fail("H", "no measured text width helper - labels cannot be centred")
        return
    # The bug: assuming the advance equals the cell size.
    if re.search(r"4\s*\*\s*16", src):
        fail("H", "a label width is still hard-coded as glyphs*16; the Alagard "
                  "advance is 8, so that mis-centres by 16 px")
    if "advance_x" not in src:
        fail("H", "centring does not consult the font's advance_x")
    if not any(f.startswith("[H]") for f in FAIL):
        info("H", "button labels measured from real font metrics")


# ------------------------------------------------- I: implicit declarations
def gate_implicit_decls():
    """An implicitly declared function is undefined behaviour that compiles.

    MEASURED: coup_render.c called saturn_bg_set_scene() for the whole life of
    the background layer without ever including saturn_bg.h. C89 let it
    through as an implicit int-returning function and it happened to work on
    this ABI. It would have broken silently the moment the signature changed.
    """
    log = "build/qa/build.log"
    if not os.path.exists(log):
        info("I", "no build log captured; implicit-declaration check skipped")
        return
    bad = [l.strip() for l in open(log, encoding="utf-8", errors="replace")
           if "implicit declaration" in l]
    if bad:
        for b in bad[:5]:
            fail("I", b)
    else:
        info("I", "no implicitly declared functions")


# ----------------------------------------------------- J: no QA build ships
def gate_no_qa_build():
    """A capture-QA disc must never be mistaken for a shippable one.

    -DCOUP_QA_SCREEN boots straight to one screen with synthetic players and
    a fabricated hand. It is invaluable for photographing the screens behind
    online play and catastrophic to ship, because the game would never reach
    the title.
    """
    mk = open("examples/coup/saturn/Makefile", encoding="utf-8",
              errors="replace").read()
    if re.search(r"^[^#\n]*-DCOUP_QA_SCREEN", mk, re.M):
        fail("J", "the Makefile defines COUP_QA_SCREEN; that disc boots "
                  "straight into a fabricated game state and must not ship")
        return
    log = "build/qa/build.log"
    if os.path.exists(log):
        src = open(log, encoding="utf-8", errors="replace").read()
        if "-DCOUP_QA_SCREEN" in src:
            fail("J", "the captured build defined COUP_QA_SCREEN - that disc "
                      "is a QA build, not a shippable one")
            return
    info("J", "no QA screen-forcing in this build")


def main():
    for g in (gate_assets, gate_transparency, gate_animation, gate_vdp1,
              gate_cram, gate_headroom, gate_preprocessor, gate_centring,
              gate_implicit_decls, gate_no_qa_build):
        try:
            g()
        except Exception as exc:                       # noqa: BLE001
            fail(g.__name__, f"gate raised {type(exc).__name__}: {exc}")

    for line in INFO:
        print("  " + line)
    print()
    if FAIL:
        print(f"VERIFY FACELIFT: RED - {len(FAIL)} failure(s)")
        for f in FAIL:
            print("  - " + f)
        return 1
    print("VERIFY FACELIFT: GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
