#!/usr/bin/env python3
"""
qa_fidelity.py - Measure how much quality the Saturn conversion actually costs.

Decodes what is stored in the generated headers back into images and compares
them numerically against the source art. Nothing here is judged by eye.

Metrics per asset:
  PSNR        peak signal-to-noise ratio in dB. Above 30 dB is generally
              considered visually close for lossy image work; below 25 dB
              means visible degradation.
  MAE         mean absolute error per channel, 0-255.
  palette use how many of the available colour slots the quantizer actually
              used. Leaving slots unused is wasted fidelity.
  detail      ratio of high-frequency energy retained (Laplacian variance of
              the converted image over the source, both at target size).
              Catches blurring that PSNR alone can miss.

The comparison is against the source RESIZED TO TARGET, not the full-resolution
original. Resizing is not a defect - the Saturn genuinely has 320x224. What we
are testing is whether quantization and packing lose more than they must.

Exit 1 if any asset falls below the thresholds.
"""

import glob
import os
import re
import sys

from PIL import Image, ImageFilter

# The converter applies a bounded shadow lift to scenes that are too dark to
# read (see convert_backgrounds.lift_shadows). That is a deliberate grade, not
# a conversion loss, so the reference must carry it too - otherwise this gate
# measures the artistic decision instead of the quantization, and reports RED
# on art that converted perfectly.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "examples", "coup", "assets"))
try:
    from convert_backgrounds import (lift_shadows, trim_panel_seam,
                                     repair_edges)
    from convert_portraits import stretch_frames, repair_edge_columns
except ImportError:
    lift_shadows = None
    trim_panel_seam = None
    repair_edges = None
    stretch_frames = None
    repair_edge_columns = None

PSNR_MIN = 26.0        # below this, degradation is visible
PALETTE_USE_MIN = 0.85  # must use at least 85% of available slots
DETAIL_MIN = 0.55       # retain at least 55% of high-frequency energy

FAILURES = []


def psnr(a, b):
    pa, pb = list(a.getdata()), list(b.getdata())
    se = 0
    for (r1, g1, b1), (r2, g2, b2) in zip(pa, pb):
        se += (r1 - r2) ** 2 + (g1 - g2) ** 2 + (b1 - b2) ** 2
    mse = se / (len(pa) * 3)
    if mse == 0:
        return 99.0
    import math
    return 10 * math.log10((255.0 ** 2) / mse)


def mae(a, b):
    pa, pb = list(a.getdata()), list(b.getdata())
    tot = 0
    for (r1, g1, b1), (r2, g2, b2) in zip(pa, pb):
        tot += abs(r1 - r2) + abs(g1 - g2) + abs(b1 - b2)
    return tot / (len(pa) * 3)


def detail(img):
    """High-frequency energy: variance of a Laplacian-ish edge filter."""
    g = img.convert("L").filter(ImageFilter.FIND_EDGES)
    px = list(g.getdata())
    m = sum(px) / len(px)
    return sum((p - m) ** 2 for p in px) / len(px)


def rgb555_to_rgb(v):
    r = (v & 0x1F) << 3
    g = ((v >> 5) & 0x1F) << 3
    b = ((v >> 10) & 0x1F) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5)


def parse_palette(src, name, size):
    m = re.search(r"%s\[%d\]\s*=\s*\{([^}]*)\}" % (re.escape(name), size),
                  src, re.S)
    if not m:
        return None
    return [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(1))]


def parse_bytes(src, name):
    m = re.search(r"%s\[\d+\]\s*=\s*\{(.*?)\};" % re.escape(name), src, re.S)
    if not m:
        return None
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))


def check_background(scene, src_png, bin_path):
    """Decode a streamed scene binary back to an image and score it.

    Scenes are no longer linked into the binary, so this reads the .BIN that
    actually ships on the disc: 512 bytes of big-endian RGB555 palette
    followed by 224 rows of 320 8bpp pixels. Testing the shipped file rather
    than a C array means the converter's packing is under test too.
    """
    w, h = 320, 224
    pal_bytes, pix_bytes = 512, w * h

    if not os.path.isfile(bin_path):
        FAILURES.append(f"background {scene}: scene file missing at {bin_path}")
        return
    blob = open(bin_path, "rb").read()
    if len(blob) != pal_bytes + pix_bytes:
        FAILURES.append(f"background {scene}: {bin_path} is {len(blob)} B, "
                        f"expected {pal_bytes + pix_bytes}")
        return

    pal = [(blob[i * 2] << 8) | blob[i * 2 + 1] for i in range(256)]
    data = blob[pal_bytes:]

    out = Image.new("RGB", (w, h))
    out.putdata([rgb555_to_rgb(pal[b]) for b in data[: w * h]])

    # The reference must go through EVERY deliberate transform the converter
    # applies - seam trim then shadow lift, in that order - or this gate
    # measures the artistic decisions instead of the quantization. That has
    # now bitten twice: the shadow lift dropped game to 21.3 dB (really 37.8)
    # and the seam trim dropped defeat to 20.0 dB (really ~37).
    ref = Image.open(src_png).convert("RGB")
    if trim_panel_seam is not None:
        ref, _t = trim_panel_seam(ref)
    # FIFTH transform this reference has had to learn. Every time the
    # converter gains a deliberate step and the reference does not, a correct
    # conversion scores as a regression - shadow lift, seam trim, portrait
    # stretch, portrait edge repair, and now background edge repair. The
    # splash dropped to 24.9 dB on art that had converted perfectly.
    if repair_edges is not None:
        ref = repair_edges(ref)
    ref = ref.resize((w, h), Image.LANCZOS)
    if lift_shadows is not None:
        ref, _g = lift_shadows(ref)

    p, e = psnr(out, ref), mae(out, ref)
    used = len(set(data))
    use_ratio = used / 255.0
    d = detail(out) / max(1e-6, detail(ref))

    status = []
    if p < PSNR_MIN:
        status.append("PSNR")
    if use_ratio < PALETTE_USE_MIN:
        status.append("PALETTE")
    if d < DETAIL_MIN:
        status.append("DETAIL")

    print(f"  bg {scene:<10} PSNR {p:5.1f} dB  MAE {e:4.1f}  "
          f"palette {used:3}/255 ({use_ratio*100:4.1f}%)  detail {d*100:5.1f}%"
          + ("  <-- " + ",".join(status) if status else ""))

    for s in status:
        FAILURES.append(f"background {scene}: {s} below threshold "
                        f"(PSNR {p:.1f}, palette {use_ratio*100:.0f}%, "
                        f"detail {d*100:.0f}%)")


def check_portrait(name, src_dir, hdr_data, hdr_spr):
    pal = parse_palette(hdr_spr, f"coup_anim_pal_{name}", 16)
    blob = parse_bytes(hdr_data, f"coup_animdata_{name}_f00")
    if pal is None or blob is None:
        FAILURES.append(f"portrait {name}: could not parse from header")
        return

    w, h = 64, 96
    px = []
    for byte in blob:
        px.append(rgb555_to_rgb(pal[byte >> 4]))
        px.append(rgb555_to_rgb(pal[byte & 0xF]))
    out = Image.new("RGB", (w, h))
    out.putdata(px[: w * h])

    frames = sorted(glob.glob(os.path.join(src_dir, name, "*.png")))
    if not frames:
        FAILURES.append(f"portrait {name}: no source frames to compare")
        return
    # Same rule as the backgrounds: the reference must carry every deliberate
    # transform, or this gate scores the grading instead of the quantization.
    # The portraits are auto-levelled per channel and saturation-boosted
    # before quantizing (convert_portraits.stretch_frames), which is what
    # fixed them being washed out - without it here they read as 15-19 dB.
    ref = Image.open(frames[0]).convert("RGB")
    # The reference must carry EVERY deliberate transform the converter
    # applies, or a correct conversion scores as a regression. This gate has
    # been caught by that three times already (shadow lift, seam trim,
    # portrait stretch). Edge repair is the fourth: duke's reference still
    # held the white neighbour columns that the converter now removes, and he
    # scored 20.5 dB against 32-36 for everyone else - a depressed baseline
    # that would have hidden a real future regression under it.
    if repair_edge_columns is not None:
        ref = repair_edge_columns(ref)
    if stretch_frames is not None:
        ref = stretch_frames([ref])[0][0]
    ref = ref.resize((w, h), Image.LANCZOS)

    p, e = psnr(out, ref), mae(out, ref)
    # Across the WHOLE sequence, not frame 0. The palette is shared by all
    # frames by design (so a frame change needs no CRAM upload), so any single
    # frame legitimately uses only part of it - assassin's first frame uses 12
    # of 15 while the sequence uses all 15. Scoring one frame punished a
    # correct shared palette.
    all_idx = set()
    for fi in range(16):
        fb = parse_bytes(hdr_data, f"coup_animdata_{name}_f{fi:02d}")
        if fb is None:
            break
        for byte in fb:
            all_idx.add(byte >> 4)
            all_idx.add(byte & 0xF)
    used = len(all_idx) if all_idx else len(
        set(n for byte in blob for n in (byte >> 4, byte & 0xF)))
    use_ratio = used / 15.0
    d = detail(out) / max(1e-6, detail(ref))

    status = []
    if p < 20.0:            # 15 colours is a hard limit; expect lower PSNR
        status.append("PSNR")
    if use_ratio < PALETTE_USE_MIN:
        status.append("PALETTE")
    if d < 0.45:
        status.append("DETAIL")

    print(f"  portrait {name:<11} PSNR {p:5.1f} dB  MAE {e:4.1f}  "
          f"palette {used:2}/15 ({use_ratio*100:5.1f}%)  detail {d*100:5.1f}%"
          + ("  <-- " + ",".join(status) if status else ""))

    for s in status:
        FAILURES.append(f"portrait {name}: {s} below threshold")


def main():
    pack = ("examples/coup/assets/Official Art/coup_saturn_complete_asset_pack/"
            "coup_saturn_complete_asset_pack/saturn_ready")

    hdr = open("examples/coup/saturn/coup_bg_index.h", encoding="utf-8",
               errors="replace").read()
    scenes = re.findall(r"COUP_BG_SCENE_(\w+) = \d+", hdr)
    # Sources come from the header's own provenance block, written by
    # convert_backgrounds.py. Guessing them from a hardcoded name map is how
    # the rules scene came to report 3.7 dB PSNR: it was being compared
    # against B7_rules.png when it had been built from rulesoverlay.png.
    src_map = dict(re.findall(r"COUP_BG_SOURCE\s+(\w+)\s*=\s*(.+)", hdr))
    # Scene -> shipped .BIN, taken from the index in declaration order so the
    # gate always tests the file the console actually opens.
    bin_names = re.findall(r'"(BG[A-Z0-9]*\.BIN)"', hdr)
    bin_for = {s.lower(): n for s, n in zip(scenes, bin_names)}
    assets_root = "examples/coup/assets"

    print("=== BACKGROUND FIDELITY (255 colours available) ===")
    if not src_map:
        print("  no COUP_BG_SOURCE provenance in the header - regenerate it "
              "with convert_backgrounds.py")
        FAILURES.append("background provenance missing; fidelity unverifiable")
    for s in scenes:
        rel = src_map.get(s.lower())
        if not rel:
            FAILURES.append(f"background {s.lower()}: no source recorded; "
                            "fidelity was never measured for this scene")
            print(f"  bg {s.lower():<10} NOT MEASURED - no source recorded")
            continue
        f = os.path.join(assets_root, rel.strip())
        if not os.path.isfile(f):
            FAILURES.append(f"background {s.lower()}: recorded source missing "
                            f"at {f}")
            print(f"  bg {s.lower():<10} NOT MEASURED - recorded source missing")
            continue
        binp = os.path.join("examples/coup/saturn/cd",
                            bin_for.get(s.lower(), ""))
        check_background(s.lower(), f, binp)

    print()
    print("=== PORTRAIT FIDELITY (15 colours available) ===")
    hd = open("examples/coup/saturn/coup_anim_sprite_data.h", encoding="utf-8",
              errors="replace").read()
    hs = open("examples/coup/saturn/coup_anim_sprites.h", encoding="utf-8",
              errors="replace").read()
    for name in ("duke", "assassin", "captain", "ambassador", "contessa"):
        check_portrait(name, os.path.join(pack, "portraits"), hd, hs)

    print()
    if FAILURES:
        print(f"GATE FIDELITY: RED - {len(FAILURES)} asset(s) below threshold")
        for f in FAILURES:
            print("  - " + f)
        return 1
    print("GATE FIDELITY: GREEN - conversion loses no more than it must")
    return 0


if __name__ == "__main__":
    sys.exit(main())
