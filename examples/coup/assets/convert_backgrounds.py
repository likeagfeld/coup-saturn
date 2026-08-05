#!/usr/bin/env python3
"""
convert_backgrounds.py - Convert a Coup scene PNG to a VDP2 NBG1 bitmap.

Output format (ST-058-R2 section 4.9, bitmap-mode scroll screens):
  - 512x256 bitmap, one byte per pixel (256-colour mode)
  - the visible 320x224 window occupies the top-left corner
  - palette index 0 is TRANSPARENT for a scroll screen, so it is reserved:
    image colours occupy indices 1..255
  - palette entries are Saturn RGB555, 0BBBBBGGGGGRRRRR, bit 15 clear

The bitmap is exactly 0x20000 bytes, which is one whole VDP2 VRAM bank. It is
uploaded to bank A0 at 0x25E00000 (bitmap base must be 0x20000-aligned), and
NOT to bank B1, where the text layer's character data lives - SGL's auto
arbiter non-deterministically drops a standalone NBG whose data sits in B1
(sega-saturn-developer gotcha #7, MEASURED).

Usage:
  python3 convert_backgrounds.py gamescreen.png --name game \
      --output ../saturn/coup_bg_data.h [--preview out.png]
"""

import argparse
import os
import sys

from PIL import Image

BITMAP_W = 512
BITMAP_H = 256
VISIBLE_W = 320
VISIBLE_H = 224
RESERVED_INDICES = 1                      # index 0 = transparent
MAX_COLORS = 256 - RESERVED_INDICES       # 255 usable colours


def to_rgb555(r, g, b):
    """Pack 8-bit RGB into Saturn RGB555 (bit 15 clear)."""
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


# Median luminance a scene must reach to be readable on a CRT.
#
# MEASURED on the delivered art: B4_connecting has a median of 18.0 and even
# its brightest decile only reaches 57; B2_game_table is median 23.7. Those
# are the two screens reported as "very dark" from hardware. B1_title, which
# reads fine, is median 78.3. Conversion is not the cause - it costs only
# ~3 luma to quantization.
#
# 48 sits well above the unreadable pair and well below the title, so scenes
# that are already comfortable are left completely alone.
TARGET_MEDIAN = 48.0

# Never lift harder than this. A gamma below it crushes the highlights flat
# and turns a moody interior into grey soup - the art is SUPPOSED to be dim,
# it just cannot be invisible.
MIN_GAMMA = 0.62


def lift_shadows(img, name=""):
    """Gamma-lift a scene that is too dark to read, and only that far.

    Returns (image, gamma_applied). Gamma 1.0 means untouched.
    """
    lum = img.convert("L")
    hist = lum.histogram()
    total = sum(hist)
    acc = 0
    median = 0
    for i, n in enumerate(hist):
        acc += n
        if acc >= total / 2:
            median = i
            break

    if median >= TARGET_MEDIAN or median <= 0:
        return img, 1.0

    # out = 255 * (in/255)^gamma ; solve for the gamma that puts the median
    # exactly on target, then clamp.
    import math
    gamma = math.log(TARGET_MEDIAN / 255.0) / math.log(median / 255.0)
    gamma = max(MIN_GAMMA, min(1.0, gamma))
    if gamma >= 0.999:
        return img, 1.0

    lut = [min(255, int(round(255.0 * ((i / 255.0) ** gamma))))
           for i in range(256)]
    return img.point(lut * 3), gamma


# A panel border from the source sheet shows up as a column far brighter than
# its neighbours, with a DIFFERENT image continuing past it.
SEAM_RATIO = 3.0
# Never trim more than this - a real architectural highlight in the middle of a
# scene must not be mistaken for a panel edge.
SEAM_MAX_TRIM = 0.25


def trim_panel_seam(img):
    """Cut off a neighbouring panel that bled in from the source sheet.

    MEASURED on the delivered art: B5_victory carries a bright border column
    at x=285 with a different, darker scene continuing to its right; the same
    fault appears in B4_connecting (x=272) and B6_defeat (x=294) at 4.4-5.5x
    the surrounding brightness. The panels were cut from a sheet slightly too
    wide, so each kept a slice of its neighbour.

    Returns (image, trimmed_px). The kept region is rescaled back to full
    width, which stretches it very slightly - far less objectionable than a
    stripe of the wrong scene down the edge.
    """
    import numpy as np

    a = np.asarray(img.convert("RGB")).astype(float)
    h, w, _ = a.shape
    col = a.mean(axis=(0, 2))
    limit = int(w * (1.0 - SEAM_MAX_TRIM))

    best = None
    for x in range(limit, w - 2):
        nb = (col[max(0, x - 4):x].mean() + col[x + 1:x + 5].mean()) / 2.0
        if col[x] > nb * SEAM_RATIO and col[x] > 8:
            ratio = col[x] / max(nb, 1.0)
            if best is None or ratio > best[1]:
                best = (x, ratio)
    if best is None:
        return img, 0
    x = best[0]
    return img.crop((0, 0, x, h)), w - x


def convert(src_path):
    """Return (bitmap_bytes, palette_list, preview_image)."""
    img = Image.open(src_path).convert("RGB")
    img, _trim = trim_panel_seam(img)
    img = img.resize((VISIBLE_W, VISIBLE_H), Image.LANCZOS)
    img, _gamma = lift_shadows(img, src_path)

    quant = img.quantize(colors=MAX_COLORS, method=Image.MEDIANCUT)
    src_palette = quant.getpalette()[: MAX_COLORS * 3]
    indices = quant.tobytes()   # P-mode: raw palette indices, one byte each

    # Shift every index up by RESERVED_INDICES so index 0 stays transparent.
    palette = [0x0000] * 256
    for i in range(MAX_COLORS):
        r, g, b = src_palette[i * 3: i * 3 + 3]
        palette[i + RESERVED_INDICES] = to_rgb555(r, g, b)

    # Store ONLY the visible window, not the full 512x256 bitmap.
    #
    # VDP2 requires a 512-pixel-wide bitmap because that is the hardware size
    # selector, but 320x224 of it is ever displayed. Storing the full plane
    # would carry 59,392 bytes of off-screen padding per scene - 45% waste.
    # saturn_bg_upload expands these rows into the 512-wide VRAM plane, so the
    # saving is in WRAM only and costs nothing in quality or upload time.
    bitmap = bytearray(VISIBLE_W * VISIBLE_H)
    for y in range(VISIBLE_H):
        row = y * VISIBLE_W
        for x in range(VISIBLE_W):
            bitmap[row + x] = indices[row + x] + RESERVED_INDICES

    return bytes(bitmap), palette, quant.convert("RGB")


def verify(bitmap, palette):
    """Assert every hardware invariant before emitting anything."""
    assert len(bitmap) == VISIBLE_W * VISIBLE_H, (
        f"stored bitmap must be exactly {VISIBLE_W * VISIBLE_H} bytes (the "
        f"visible window only), got {len(bitmap)}")

    assert palette[0] == 0x0000, (
        "palette index 0 must stay reserved: VDP2 treats colour 0 in a scroll "
        "screen as transparent")

    bad = [c for c in palette if c > 0x7FFF]
    assert not bad, f"RGB555 bit 15 must be clear, found {len(bad)} entries above 0x7FFF"

    for y in range(VISIBLE_H):
        row = bitmap[y * VISIBLE_W: (y + 1) * VISIBLE_W]
        assert 0 not in row, (
            f"visible row {y} uses the transparent index - the background "
            "would show holes")

    used = set(bitmap)
    assert max(used) <= 255, "index out of range"
    return {"colours_used": len(used)}


def emit_binaries(scenes, out_dir):
    """Write one .BIN per scene for streaming off the disc.

    Layout, chosen so the Saturn side needs no parsing at all:

        offset      0   512 B   palette, 256 x uint16 BIG-ENDIAN RGB555
        offset    512 71680 B   pixels, 224 rows of 320 bytes, 8bpp
        total          72192 B

    Big-endian because the SH-2 is big-endian, so the palette block can be
    read straight into a uint16_t array and written to CRAM without byte
    swapping. Pixels are the visible window only, exactly as the resident
    tables were - saturn_bg_upload() already expands each row into the
    512-wide VDP2 plane, so the streaming path reuses that code unchanged.

    Names are 8.3 uppercase. ISO9660 mangles anything longer, and a scene
    that cannot be opened by name is a black screen with no diagnostic.
    """
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for name, bitmap, palette, _src in scenes:
        fname = cd_filename(name)
        blob = bytearray()
        for v in palette:
            blob.append((v >> 8) & 0xFF)
            blob.append(v & 0xFF)
        assert len(blob) == 512, f"{name}: palette is {len(blob)} B, expected 512"
        blob.extend(bitmap)
        expect = 512 + VISIBLE_W * VISIBLE_H
        assert len(blob) == expect, \
            f"{name}: scene file is {len(blob)} B, expected {expect}"
        path = os.path.join(out_dir, fname)
        with open(path, "wb") as f:
            f.write(blob)
        written.append((name, fname, len(blob)))
    return written


def cd_filename(name):
    """8.3 uppercase name for a scene. Must stay <= 8 chars before the dot."""
    stem = ("BG" + name.upper().replace("_", ""))[:8]
    return stem + ".BIN"


def emit_index(scenes, out_path):
    """Emit the scene enum and filenames - and NO pixel data.

    This is the whole point of streaming: the header that used to carry
    215,040 bytes of resident bitmap now carries only names.
    """
    guard = "COUP_BG_INDEX_H"
    with open(out_path, "w") as f:
        f.write("/* Generated by convert_backgrounds.py. Do not edit. */\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("/* Scenes are streamed from the disc, not linked in. Each\n"
                "   file is a 512-byte big-endian RGB555 palette followed by\n"
                "   224 rows of 320 8bpp pixels. */\n\n")
        f.write(f"#define COUP_BG_VISIBLE_W {VISIBLE_W}\n")
        f.write(f"#define COUP_BG_VISIBLE_H {VISIBLE_H}\n")
        f.write(f"#define COUP_BG_W {BITMAP_W}\n")
        f.write(f"#define COUP_BG_H {BITMAP_H}\n")
        f.write(f"#define COUP_BG_PAL_BYTES 512\n")
        f.write(f"#define COUP_BG_PIX_BYTES {VISIBLE_W * VISIBLE_H}\n")
        f.write(f"#define COUP_BG_FILE_BYTES {512 + VISIBLE_W * VISIBLE_H}\n")
        f.write(f"#define COUP_BG_SCENE_COUNT {len(scenes)}\n\n")

        f.write("enum {\n")
        for i, (name, _, _, _) in enumerate(scenes):
            f.write(f"    COUP_BG_SCENE_{name.upper()} = {i},\n")
        f.write("};\n\n")

        f.write("/* Source artwork, for the fidelity gate. Do not edit.\n")
        for name, _, _, src in scenes:
            f.write(f"   COUP_BG_SOURCE {name} = {src.replace(chr(92), '/')}\n")
        f.write("*/\n\n")

        f.write("static const char* const coup_bg_files[COUP_BG_SCENE_COUNT] "
                "= {\n")
        for name, _, _, _ in scenes:
            f.write(f'    "{cd_filename(name)}",\n')
        f.write("};\n\n")
        f.write(f"#endif /* {guard} */\n")


def emit_header(scenes, out_path):
    """Emit one header holding every scene, plus a lookup table.

    `scenes` is a list of (name, bitmap, palette, source_path) in enum order.

    The source path of every scene is recorded in the header. Without it a
    fidelity gate has to GUESS which artwork a scene came from, and a wrong
    guess reads as a catastrophic conversion failure: comparing the rules
    scene against B7_rules.png (it was actually built from rulesoverlay.png)
    reported 3.7 dB PSNR for a conversion that was fine.
    """
    guard = "COUP_BG_DATA_H"
    with open(out_path, "w") as f:
        f.write("/* Generated by convert_backgrounds.py. Do not edit. */\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define COUP_BG_W {BITMAP_W}\n")
        f.write(f"#define COUP_BG_H {BITMAP_H}\n")
        f.write(f"#define COUP_BG_VISIBLE_W {VISIBLE_W}\n")
        f.write(f"#define COUP_BG_VISIBLE_H {VISIBLE_H}\n")
        f.write(f"#define COUP_BG_SIZE {VISIBLE_W * VISIBLE_H}\n")
        f.write("/* Stored as the visible window only. saturn_bg_upload expands\n"
                "   each row into the 512-wide VDP2 plane, so this is 45% smaller\n"
                "   in WRAM than the full bitmap at no cost in quality. */\n")
        f.write(f"#define COUP_BG_SCENE_COUNT {len(scenes)}\n\n")

        f.write("enum {\n")
        for i, (name, _, _, _) in enumerate(scenes):
            f.write(f"    COUP_BG_SCENE_{name.upper()} = {i},\n")
        f.write("};\n\n")

        # Machine-readable provenance. scripts/qa/qa_fidelity.py reads these so
        # it never has to guess which artwork a scene came from. A wrong guess
        # reads as a catastrophic conversion failure: the rules scene measured
        # 3.7 dB PSNR against B7_rules.png when it was built from
        # rulesoverlay.png, and the conversion was fine all along.
        f.write("/* Source artwork, for the fidelity gate. Do not edit.\n")
        for name, _, _, src in scenes:
            f.write(f"   COUP_BG_SOURCE {name} = {src.replace(chr(92), '/')}\n")
        f.write("*/\n\n")

        for name, bitmap, palette, _src in scenes:
            f.write(f"/* --- {name} --- */\n")
            f.write("/* Saturn RGB555. Index 0 is transparent and unused. */\n")
            f.write(f"static const uint16_t coup_bg_pal_{name}[256] = {{\n")
            for i in range(0, 256, 8):
                row = ", ".join(f"0x{c:04X}" for c in palette[i:i + 8])
                f.write(f"    {row},\n")
            f.write("};\n\n")

            f.write(f"static const uint8_t coup_bg_tbl_{name}[{len(bitmap)}] = {{\n")
            for i in range(0, len(bitmap), 16):
                row = ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16])
                f.write(f"    {row},\n")
            f.write("};\n\n")

        f.write("static const uint8_t* const coup_bg_tables[COUP_BG_SCENE_COUNT] = {\n")
        for name, _, _, _ in scenes:
            f.write(f"    coup_bg_tbl_{name},\n")
        f.write("};\n\n")

        f.write("static const uint16_t* const coup_bg_palettes[COUP_BG_SCENE_COUNT] = {\n")
        for name, _, _, _ in scenes:
            f.write(f"    coup_bg_pal_{name},\n")
        f.write("};\n\n")

        f.write(f"#endif /* {guard} */\n")


def main():
    ap = argparse.ArgumentParser(
        description="Convert one or more scene PNGs into a VDP2 bitmap header.")
    ap.add_argument("--scene", action="append", required=True, metavar="NAME=PATH",
                    help="scene to include, in enum order; repeatable")
    ap.add_argument("--output", required=True)
    ap.add_argument("--preview-dir", help="write quantized previews here")
    ap.add_argument("--binary-dir",
                    help="write one streamable .BIN per scene here; when set, "
                         "--output receives the index header instead of the "
                         "resident bitmap tables")
    args = ap.parse_args()

    scenes = []
    for spec in args.scene:
        if "=" not in spec:
            raise SystemExit(f"--scene expects NAME=PATH, got {spec!r}")
        name, path = spec.split("=", 1)

        bitmap, palette, preview = convert(path)
        stats = verify(bitmap, palette)
        _probe = Image.open(path).convert("RGB").resize((VISIBLE_W, VISIBLE_H),
                                                        Image.LANCZOS)
        _, gamma = lift_shadows(_probe)
        _, trim = trim_panel_seam(Image.open(path).convert("RGB"))
        scenes.append((name, bitmap, palette, path))

        if args.preview_dir:
            os.makedirs(args.preview_dir, exist_ok=True)
            preview.save(os.path.join(args.preview_dir, f"bg-{name}.png"))

        print(f"  {name:<10} {os.path.basename(path):<24} "
              f"{stats['colours_used']:>3} colours"
              + (f"  seam-trim {trim}px" if trim else "")
              + (f"  shadow-lift gamma {gamma:.2f}" if gamma < 1.0 else ""))

    total = len(scenes) * VISIBLE_W * VISIBLE_H

    if args.binary_dir:
        written = emit_binaries(scenes, args.binary_dir)
        emit_index(scenes, args.output)
        print(f"-> {args.binary_dir}")
        for name, fname, size in written:
            print(f"   {fname:<14} {size:>8,} B   ({name})")
        print(f"-> {args.output}  (index only, no pixel data)")
        print(f"   {len(scenes)} scene(s) streamed; {total:,} bytes moved off "
              f"WRAM onto the disc")
        return 0

    emit_header(scenes, args.output)
    print(f"-> {args.output}")
    print(f"   {len(scenes)} scene(s), {BITMAP_W}x{BITMAP_H} 8bpp, "
          f"{total:,} bytes of bitmap data")
    print(f"   header {os.path.getsize(args.output):,} bytes of source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
