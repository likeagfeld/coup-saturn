#!/usr/bin/env python3
"""SUPERSEDED - do not run this expecting to reproduce the shipping font.

!! This script does NOT generate pal/saturn/fonts/saturn_font_coup_8x8.c as
!! it currently stands. That file was extracted from ab8a59e0-*.png, a NATIVE
!! 8x8 grid, at quantum Q=11 with per-glyph bbox detection - a better source
!! than this one, because it needs no downscale at all. This script reads
!! fontfile.png, a large rendered sheet that must be reduced, and writes to
!! the SAME output path.
!!
!! MEASURED: both produce 760 bytes and 653 of them differ (85.9%). Running
!! this would silently overwrite the shipping font with the earlier, worse
!! extraction, and the only symptom would be that the text looked wrong
!! again. The generator that produced the shipping font is not in the repo.
!!
!! It is kept because its glyph-location and erosion logic is the record of
!! what was learned about reducing this typeface - see FILL_MAX below, where
!! the thinning bound is documented against the legibility failures it was
!! measured from. Read it; do not run it.

Build the COUP 8x8 font from the supplied fontfile.png sheet.

The sheet is a rendered character set in the logo's style. Glyphs are located
by projection, merged where a letter's parts are disjoint (K's stem is 5 px
from its arms; the two marks of the double quote are 3 px apart), scaled by a
SINGLE factor derived from the cap height so relative proportions survive, and
bottom-aligned on a per-row baseline so they sit on a common line.
"""
import numpy as np
from PIL import Image

import sys as _sys

_FORCE = "--i-know-this-overwrites-the-shipping-font"
if _FORCE not in _sys.argv:
    raise SystemExit(
        "make_coup_font.py: REFUSING to run.\n"
        "  This writes pal/saturn/fonts/saturn_font_coup_8x8.c, but the\n"
        "  shipping font came from a DIFFERENT and better source\n"
        "  (ab8a59e0-*.png, a native 8x8 grid needing no downscale).\n"
        "  MEASURED: 653 of its 760 bytes differ from what this produces.\n"
        "  Running it silently regresses the font, and the only symptom\n"
        "  would be that the text looked wrong again.\n"
        "  Pass " + _FORCE + " if that is genuinely what you want.")


def erode(m, n):
    """Shrink ink by n pixels. A pixel survives only if its n-neighbourhood
    is entirely ink, so strokes thin from both sides and counters open."""
    out = m.copy()
    for _ in range(n):
        p = np.pad(out, 1, constant_values=False)
        out = (p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] &
               p[1:-1, :-2] & p[1:-1, 2:])
        if not out.any():
            return m
    return out


def sample(g, tw, th):
    """Point-sample g down to tw x th - preserves thin features."""
    sy = (np.arange(th) + 0.5) * g.shape[0] / th
    sx = (np.arange(tw) + 0.5) * g.shape[1] / tw
    return g[np.clip(sy.astype(int), 0, g.shape[0] - 1)][
        :, np.clip(sx.astype(int), 0, g.shape[1] - 1)]

SHEET = "examples/coup/assets/Official Art/fontfile.png"
CELL_W, CELL_H = 8, 8
CAP_ROWS = 7            # cap height in the 8-row cell
MERGE_GAP = 8            # letter gaps measured 22+; the real splits are 3-5
FILL_MAX = 0.66          # MEASURED. Thinning is bounded by LEGIBILITY, not by
                         # ink. Sweeping down: 0.60 breaks 'a' into '=', 0.55
                         # merges 'a'/'e' and '6'/'B', 0.48 merges '0'/'O'.
                         # 0.66 is the last setting where every letter is still
                         # itself, and it still thins the five heavy glyphs
                         # (M B 6 8 9) that would otherwise be solid blocks.
FILL_MIN = 0.30          # below this it has been eaten hollow
MAX_ERODE = 8            # source stroke is 23 px; this thins it by a third

LAYOUT = [
    list("ABCDEFGHIJKLM"),
    list("NOPQRSTUVWXYZ"),
    list("abcdefghijklm"),
    list("nopqrstuvwxyz"),
    list("0123456789"),
    list(".,:;'\"!?-_+=/\\"),
    list("()[]{}&@#$%*"),
    list("<>"),
]

a = np.asarray(Image.open(SHEET).convert("L")).astype(int)
ink = a > 100

# --- rows -------------------------------------------------------------
rowhas = ink.sum(axis=1) > 0
bands, s = [], None
for y in range(len(rowhas)):
    if rowhas[y] and s is None:
        s = y
    elif not rowhas[y] and s is not None:
        if y - s > 8:
            bands.append((s, y - 1))
        s = None
if s is not None:
    bands.append((s, len(rowhas) - 1))
assert len(bands) == len(LAYOUT), (len(bands), len(LAYOUT))

# --- cap height sets the single global scale --------------------------
cap_px = bands[0][1] - bands[0][0] + 1
scale = CAP_ROWS / float(cap_px)
print(f"  cap height {cap_px}px -> {CAP_ROWS} rows, scale {scale:.4f}")

glyphs = {}
for bi, (y0, y1) in enumerate(bands):
    sub = ink[y0:y1 + 1]
    colhas = sub.sum(axis=0) > 0
    runs, c = [], None
    for x in range(len(colhas)):
        if colhas[x] and c is None:
            c = x
        elif not colhas[x] and c is not None:
            runs.append((c, x - 1))
            c = None
    if c is not None:
        runs.append((c, len(colhas) - 1))
    runs = [r for r in runs if r[1] - r[0] >= 2]

    merged = [list(runs[0])]
    for x0, x1 in runs[1:]:
        if x0 - merged[-1][1] <= MERGE_GAP:
            merged[-1][1] = x1
        else:
            merged.append([x0, x1])

    want = LAYOUT[bi]
    assert len(merged) == len(want), (
        f"row {bi}: {len(merged)} glyphs, expected {len(want)} ({''.join(want)})")

    # per-row baseline: the median bottom, so descenders do not drag the line
    bottoms = []
    for x0, x1 in merged:
        g = sub[:, x0:x1 + 1]
        rs = np.where(g.sum(axis=1) > 0)[0]
        bottoms.append(rs.max())
    baseline = int(np.median(bottoms))

    for (x0, x1), ch in zip(merged, want):
        g = sub[:, x0:x1 + 1]
        rs = np.where(g.sum(axis=1) > 0)[0]
        cs = np.where(g.sum(axis=0) > 0)[0]
        g = g[rs.min():rs.max() + 1, cs.min():cs.max() + 1]

        tw = max(1, int(round(g.shape[1] * scale)))
        th = max(1, int(round(g.shape[0] * scale)))
        tw, th = min(tw, CELL_W), min(th, CELL_H)
        # POINT sampling, not LANCZOS. These letterforms are heavy - the
        # sheet measures cap/stroke 3.74 - so at 6x7 the counters are only
        # ~2.6 px wide. A smooth filter blurs them shut and the glyph
        # collapses to a solid block: MEASURED, M and W came out 100% ink at
        # every threshold from 96 to 160.
        small = sample(g, tw, th)

        # A few glyphs are still too heavy to survive the reduction - M, B and
        # the round digits, whose counters are the smallest relative to their
        # stroke. Erode the SOURCE progressively until the counter opens,
        # rather than accepting a solid block. The erosion is measured per
        # glyph, not applied blanket, so light glyphs are untouched.
        if small.mean() > FILL_MAX:
            # Take the LEAST erosion that opens the counter without eating the
            # glyph hollow. Unbounded erosion is worse than none: at
            # FILL_MAX alone it thinned 'a' into '=' and '1' into a dot.
            for n in range(1, MAX_ERODE + 1):
                trial = sample(erode(g, n), tw, th)
                f = trial.mean()
                if f < FILL_MIN:
                    break            # gone too far - keep the previous
                small = trial
                if f <= FILL_MAX:
                    break

        # bottom-align on the shared baseline, then clamp into the cell
        below = rs.max() - baseline           # how far this glyph descends
        bottom_row = int(round((CAP_ROWS - 1) + below * scale))
        top = max(0, min(CELL_H - th, bottom_row - th + 1))
        left = max(0, (CELL_W - 1 - tw) // 2)

        cell = np.zeros((CELL_H, CELL_W), bool)
        cell[top:top + th, left:left + tw] = small
        glyphs[ch] = cell

glyphs[' '] = np.zeros((CELL_H, CELL_W), bool)

FIRST, COUNT = 32, 95
missing = [chr(FIRST + i) for i in range(COUNT) if chr(FIRST + i) not in glyphs]
print(f"  extracted {len(glyphs)} glyphs; {len(missing)} not on the sheet: "
      f"{''.join(missing)}")

# Anything the sheet does not supply falls back to the condensed Alagard face,
# so no character renders blank.
import re
alag = open("pal/saturn/fonts/saturn_font_alagard_8x8.c",
            encoding="utf-8", errors="replace").read()
ad = [int(v, 16) for v in re.findall(
    r"0x([0-9A-Fa-f]{2})",
    re.search(r"_1bpp\[\d+\]\s*=\s*\{(.*?)\};", alag, re.S).group(1))]

data = []
for i in range(COUNT):
    ch = chr(FIRST + i)
    if ch in glyphs:
        cell = glyphs[ch]
        for r in range(CELL_H):
            byte = 0
            for c in range(CELL_W):
                if cell[r, c]:
                    byte |= 0x80 >> c
            data.append(byte)
    else:
        data.extend(ad[i * 8:(i + 1) * 8])

body = "\n".join("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ","
                 for i in range(0, len(data), 16))
src = f'''/**
 * saturn_font_coup_8x8.c - Game font in the COUP logo's style.
 *
 * EXTRACTED from examples/coup/assets/Official Art/fontfile.png, a rendered
 * character set supplied in the logo's own style. Not redrawn and not
 * generated from rules - these are the delivered letterforms, reduced.
 *
 * Method: glyphs located by projection; runs closer than {MERGE_GAP} px merged,
 * because K's stem sits 5 px from its arms and the two marks of the double
 * quote are 3 px apart and would otherwise be read as separate glyphs. A
 * SINGLE scale factor is taken from the cap height ({cap_px} px -> {CAP_ROWS} rows) so
 * relative proportions survive, and each glyph is bottom-aligned on its row's
 * MEDIAN baseline, so descenders hang below the line instead of dragging it.
 *
 * {len(missing)} characters are absent from the sheet and fall back to the condensed
 * Alagard face, so nothing renders blank.
 *
 * 1bpp, 8 bytes per glyph, 95 glyphs from ASCII 32.
 */

#include "saturn_font_coup_8x8.h"
#include <stddef.h>

static const uint8_t s_font_coup_8x8_1bpp[{len(data)}] = {{
{body}
}};

void saturn_font_coup_8x8_desc(saturn_font_desc_t* desc)
{{
    if (!desc) return;

    desc->name = "coup_8x8";
    desc->data_1bpp = s_font_coup_8x8_1bpp;
    desc->cell_width = 8;
    desc->cell_height = 8;
    desc->advance_x = 8;
    desc->first_char = 32;
    desc->char_count = 95;
    desc->bytes_per_row_1bpp = 1;
}}
'''
open("pal/saturn/fonts/saturn_font_coup_8x8.c", "w", newline="").write(src)
print(f"  wrote {len(data)} bytes")

# --- sample render ----------------------------------------------------
lines = ["VESPER CLAIMS DUKE", "MARLOWE blocks with", "RAVEN CHALLENGES!",
         "GARY WINS!  $12", "Allow / Challenge", "Block as Ambassador",
         "[A] Return to Lobby", "Page 1 of 6  0123456789"]
SC = 3
W = max(len(l) for l in lines) * 8 * SC
img = Image.new("RGB", (W, len(lines) * 10 * SC), (20, 20, 28))
px = img.load()
for li, l in enumerate(lines):
    for ci, ch in enumerate(l):
        g = ord(ch) - 32
        if not 0 <= g < COUNT:
            continue
        for r in range(8):
            b = data[g * 8 + r]
            for c in range(8):
                if b & (0x80 >> c):
                    for u in range(SC):
                        for v in range(SC):
                            px[ci * 8 * SC + c * SC + v,
                               li * 10 * SC + r * SC + u] = (232, 232, 238)
img.save("build/qa/coup_font_sample.png")

sheet = Image.new("RGB", (16 * 8 * 6, ((COUNT + 15) // 16) * 10 * 6), (24, 24, 32))
sp = sheet.load()
for i in range(COUNT):
    gx, gy = (i % 16) * 8 * 6, (i // 16) * 10 * 6
    for r in range(8):
        b = data[i * 8 + r]
        for c in range(8):
            if b & (0x80 >> c):
                for u in range(6):
                    for v in range(6):
                        sp[gx + c * 6 + v, gy + r * 6 + u] = (235, 235, 240)
sheet.save("build/qa/coup_font_sheet.png")
print("  sample + sheet written")
