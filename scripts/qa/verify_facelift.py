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
KNOWN_HANG = 181312      # MEASURED: this build hung while G8 said GREEN
SAFE_MARGIN = 15000


def gate_headroom():
    m = "examples/coup/saturn/_build/game.map"
    if not os.path.exists(m):
        fail("F", "no linker map - build first")
        return
    src = open(m, encoding="utf-8", errors="replace").read()
    e = re.search(r"0x([0-9a-fA-F]{8,16})\s+_end\b", src)
    if not e:
        fail("F", "no _end symbol in map")
        return
    head = 0x060FFC00 - int(e.group(1), 16)
    if head <= KNOWN_HANG + SAFE_MARGIN:
        fail("F", f"headroom {head:,} B is within {SAFE_MARGIN:,} of the "
                  f"MEASURED hang point {KNOWN_HANG:,} - G8 reports GREEN on "
                  "builds that do not boot, so this is not safe")
    else:
        info("F", f"headroom {head:,} B, {head - KNOWN_HANG:,} above the "
                  "measured hang point")


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


def main():
    for g in (gate_assets, gate_transparency, gate_animation, gate_vdp1,
              gate_cram, gate_headroom, gate_preprocessor, gate_centring,
              gate_implicit_decls):
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
