#!/usr/bin/env python3
"""
qa_title_wordmark.py - Prove the COUP wordmark reaches the title screen.

WHY THIS GATE EXISTS
  The wordmark sprite was once deleted from the title screen on the strength
  of a colour measurement: 7,634 "gold" pixels were counted in the title band
  and read as a logo painted into the backdrop art. They were the sunset. The
  game shipped with no branding at all.

HOW IT MEASURES
  Not by counting colour, and not by generic blob analysis either - the title
  backdrop is a floodlit skyline whose own architecture produces blobs as
  large as letterforms, so a generic shape test cannot separate them.

  Instead this decodes the actual sprite out of the generated header and
  locates it in the frame by normalized cross-correlation of edge maps. That
  answers two questions with one measurement: whether the wordmark is on
  screen, and where it actually landed. Edges rather than raw luma, because
  VDP1 colour handling and the emulator's output gamma both shift absolute
  levels while leaving letter geometry intact.

NEGATIVE CONTROL
  Every run also correlates the same template against the bare backdrop art -
  the exact image the regressed screen showed. The gate must find a clearly
  weaker peak there. A gate that cannot fail proves nothing, so both numbers
  are printed and the separation between them is asserted.

USAGE
  python scripts/qa/qa_title_wordmark.py --frame build/qa/title.png
"""

import argparse
import os
import re
import sys

import numpy as np
from PIL import Image, ImageFilter

SCREEN_W, SCREEN_H = 320, 224
LOGO_W, LOGO_H = 256, 64
EXPECT_X, EXPECT_Y = 32, 2        # coup_ui.h .logo_pos

DOWN = 4                          # correlate at 1/4 scale: 64x16 template
SEARCH = 40                       # +/- pixels around the expected position

# Thresholds are set from measurement, not taste. On the 2026-08-05 capture the
# masked correlation peaked at 0.354 exactly at the placed position, while the
# same template against the logo-less backdrop reached only 0.097 (and 28 px
# away from it). MIN_PEAK sits clear above the control and comfortably below
# the real hit; the margin test is the one that actually discriminates.
MIN_PEAK = 0.25
MIN_MARGIN = 0.12
MAX_OFFSET = 6                    # px the peak may sit from where it was placed


def decode_wordmark(header):
    """Rebuild the sprite from the generated C header - the same bytes the
    console uploads to VDP1, so this tests what actually ships."""
    src = open(header, encoding="utf-8", errors="replace").read()

    m = re.search(r"coup_ui_pal_wordmark\[16\]\s*=\s*\{([^}]*)\}", src, re.S)
    if not m:
        return None, "coup_ui_pal_wordmark not found - was the wordmark built?"
    pal = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(1))]

    m = re.search(r"coup_ui_wordmark_f00\[\d+\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        return None, "coup_ui_wordmark_f00 not found"
    data = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})",
                                                m.group(1)))

    idx = []
    for byte in data:
        idx.append(byte >> 4)
        idx.append(byte & 0xF)
    idx = idx[:LOGO_W * LOGO_H]
    if len(idx) != LOGO_W * LOGO_H:
        return None, f"sprite is {len(idx)} px, expected {LOGO_W*LOGO_H}"

    def rgb(v):
        r, g, b = (v & 0x1F) << 3, ((v >> 5) & 0x1F) << 3, ((v >> 10) & 0x1F) << 3
        return (r | r >> 5, g | g >> 5, b | b >> 5)

    img = Image.new("RGB", (LOGO_W, LOGO_H))
    img.putdata([rgb(pal[i]) for i in idx])
    opaque = sum(1 for i in idx if i)
    return (img, opaque), None


def edges(img):
    g = img.convert("L").filter(ImageFilter.FIND_EDGES)
    a = np.asarray(g, dtype=np.float64)
    return a


def downsample(a, f=DOWN):
    h, w = a.shape
    h, w = (h // f) * f, (w // f) * f
    return a[:h, :w].reshape(h // f, f, w // f, f).mean(axis=(1, 3))


def ncc_peak(scene, template, cx, cy, search=SEARCH, weight=None):
    """Best normalized cross-correlation near (cx, cy) -> (score, dx, dy).

    `weight` restricts the comparison to pixels that carry information. This
    matters more than it looks: the sprite is 63% transparent, and in those
    pixels the console draws the BACKDROP, not the template's black. Including
    them compares the template against something it can never match and buries
    a real hit in noise (MEASURED: unmasked peak 0.114 for a wordmark that is
    plainly on screen). Weighting by the opaque mask compares only the pixels
    the sprite actually paints.
    """
    if weight is None:
        weight = np.ones_like(template)
    wsum = weight.sum()
    if wsum == 0:
        return 0.0, 0, 0

    tm = (template * weight).sum() / wsum
    t = (template - tm) * weight
    tn = np.sqrt((t * t * weight).sum())
    if tn == 0:
        return 0.0, 0, 0

    th, tw = template.shape
    best = (-1.0, 0, 0)
    s = max(1, search // DOWN)
    for dy in range(-s, s + 1):
        for dx in range(-s, s + 1):
            y, x = cy + dy, cx + dx
            if y < 0 or x < 0 or y + th > scene.shape[0] or x + tw > scene.shape[1]:
                continue
            win = scene[y:y + th, x:x + tw]
            wm = (win * weight).sum() / wsum
            w = (win - wm) * weight
            wn = np.sqrt((w * w * weight).sum())
            if wn == 0:
                continue
            score = float((t * w * weight).sum() / (tn * wn))
            if score > best[0]:
                best = (score, dx * DOWN, dy * DOWN)
    return best


def to_screen(img):
    """Normalise a capture to 320x224. RetroArch emits 330 px wide for this
    core (aspect-corrected), which is horizontal scaling only."""
    if img.size != (SCREEN_W, SCREEN_H):
        img = img.resize((SCREEN_W, SCREEN_H), Image.LANCZOS)
    return img.convert("RGB")


def frame_is_usable(img):
    """Reject a contended or half-drawn capture before judging its content.

    Saturn skill gotcha #3: a second emulator sharing the host slows the
    compute-bound boot, so a fixed wall-clock capture can land on the BIOS,
    a load screen, or a black frame. That is a bad capture, not a bad build.
    """
    a = np.asarray(img.convert("L"), dtype=np.float64)
    mean, spread = a.mean(), a.max() - a.min()
    if mean < 8:
        return False, f"essentially black (mean luma {mean:.1f})"
    if spread < 40:
        return False, f"almost no contrast (range {spread:.0f})"
    return True, f"mean luma {mean:.1f}, range {spread:.0f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", default="build/qa/title.png")
    ap.add_argument("--header", default="examples/coup/saturn/coup_fx_data.h")
    ap.add_argument("--control", default=(
        "examples/coup/assets/Official Art/coup_saturn_complete_asset_pack/"
        "coup_saturn_complete_asset_pack/saturn_ready/backgrounds/B1_title.png"))
    args = ap.parse_args()

    got, err = decode_wordmark(args.header)
    if err:
        print(f"GATE WORDMARK: RED - {err}")
        return 1
    sprite, opaque = got
    print(f"  template      {LOGO_W}x{LOGO_H} from {args.header}, "
          f"{opaque} opaque px ({opaque/(LOGO_W*LOGO_H)*100:.1f}%)")

    tpl = downsample(edges(sprite))
    # Weight by the sprite's own opaque mask - see ncc_peak().
    opaque_mask = (np.asarray(sprite.convert("RGB")).sum(axis=2) > 0)
    wt = downsample(opaque_mask.astype(np.float64))
    cx, cy = EXPECT_X // DOWN, EXPECT_Y // DOWN

    # --- negative control: the logo-less backdrop -------------------------
    if not os.path.exists(args.control):
        print("GATE WORDMARK: RED - control art missing, cannot prove the "
              "gate discriminates")
        return 1
    ctrl = to_screen(Image.open(args.control))
    c_score, c_dx, c_dy = ncc_peak(downsample(edges(ctrl)), tpl, cx, cy,
                                   weight=wt)
    print(f"  control peak  {c_score:.3f} at ({c_dx:+d}, {c_dy:+d})  (bare backdrop, no wordmark)")

    # --- the captured frame ----------------------------------------------
    if not os.path.exists(args.frame):
        print(f"GATE WORDMARK: RED - {args.frame} not found; run "
              "qa_retroarch.py --shot first")
        return 1

    # A capture older than the disc it is supposed to show is not evidence.
    # MEASURED 2026-08-05: a sweep silently failed to write title.png, and
    # this gate scored a three-hour-old frame showing the PREVIOUS wordmark -
    # reporting RED on a build that was correct. A stale GREEN would have been
    # worse. Freshness is part of validity, not a detail.
    disc = "build/coup_game/track01.bin"
    if os.path.exists(disc):
        age = os.path.getmtime(disc) - os.path.getmtime(args.frame)
        if age > 0:
            print(f"GATE WORDMARK: INCONCLUSIVE - {args.frame} is "
                  f"{age/60:.0f} min OLDER than the disc it should show. "
                  "Re-capture; do not read this as a build verdict.")
            return 2
    raw = Image.open(args.frame)
    frame = to_screen(raw)
    usable, why = frame_is_usable(frame)
    print(f"  capture       {raw.size} -> {frame.size}, {why}")
    if not usable:
        print()
        print("GATE WORDMARK: INCONCLUSIVE - unusable capture. Re-run with "
              "the host quiet; this is not a build verdict.")
        return 2

    score, dx, dy = ncc_peak(downsample(edges(frame)), tpl, cx, cy,
                             weight=wt)
    off = max(abs(dx), abs(dy))
    print(f"  frame peak    {score:.3f} at offset ({dx:+d}, {dy:+d}) px "
          f"from the placed position ({EXPECT_X}, {EXPECT_Y})")
    print(f"  margin        {score - c_score:+.3f} over control")

    fails = []
    if score < MIN_PEAK:
        fails.append(f"peak {score:.3f} below {MIN_PEAK} - wordmark not found")
    if score - c_score < MIN_MARGIN:
        fails.append(f"margin {score-c_score:+.3f} below {MIN_MARGIN} - the "
                     "match is no better than on art with no wordmark")
    if off > MAX_OFFSET:
        fails.append(f"drawn {off} px from its placed position (max "
                     f"{MAX_OFFSET})")

    print()
    if fails:
        print("GATE WORDMARK: RED")
        for f in fails:
            print("  - " + f)
        return 1
    print(f"GATE WORDMARK: GREEN - wordmark located on screen at the placed "
          f"position (peak {score:.3f} vs {c_score:.3f} on logo-less art)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
