#!/usr/bin/env python3
"""
convert_assets.py - Convert Coup PNGs to Saturn VDP1 sprite format

Converts high-resolution PNG artwork to:
  - 4bpp packed pixel data (.bin) for VDP1 texture sprites
  - 16-color palettes (.pal) in Saturn RGB555 format
  - C header with sprite metadata (offsets, dimensions, palette data)

Saturn VDP1 4bpp format:
  - Each byte = 2 pixels (high nibble = left pixel, low nibble = right pixel)
  - Color index 0 = transparent (mapped from pure black #000000)
  - Palette: 16 entries x 16-bit RGB555 (0bMBBBBBGGGGGRRRRR, M=0)
  - Width must be multiple of 8 for VDP1

Saturn RGB555: bits [15]=0, [14:10]=B, [9:5]=G, [4:0]=R

Usage:
  python3 convert_assets.py                    # Convert all, output to ../saturn/cd/
  python3 convert_assets.py --output ./out     # Custom output dir
  python3 convert_assets.py --preview          # Save preview PNGs at target size
"""

import argparse
import os
import struct
import sys

from PIL import Image

# ---------------------------------------------------------------------------
# Asset definitions: source filename -> (target_w, target_h, name, crop_box)
# crop_box = None for full image, or (left, top, right, bottom) in source coords
# Width MUST be multiple of 8 for VDP1
# ---------------------------------------------------------------------------

ASSETS = [
    # Portraits: 64x96 (nice 2:3 ratio, fits VDP1 width constraint)
    {
        "src": "DukePortrait.png",
        "name": "duke",
        "w": 64, "h": 96,
        "colors": 15,
    },
    {
        "src": "AssassinPortrait.png",
        "name": "assassin",
        "w": 64, "h": 96,
        "colors": 15,
    },
    {
        "src": "CaptainPortrait.png",
        "name": "captain",
        "w": 64, "h": 96,
        "colors": 15,
    },
    {
        "src": "AmbassadorPortrait.png",
        "name": "ambassador",
        "w": 64, "h": 96,
        "colors": 15,
    },
    {
        "src": "ContessaPortrait.png",
        "name": "contessa",
        "w": 64, "h": 96,
        "colors": 15,
    },
    # Card back: 48x72 (3:4.5 ratio, close to playing card proportions)
    {
        "src": "CardBackDesign.png",
        "name": "card_back",
        "w": 48, "h": 72,
        "colors": 15,
    },
    # Title logo: 256x64 (wide banner, crops center band of source)
    {
        "src": "CoupTitleScreen.png",
        "name": "title",
        "w": 256, "h": 64,
        "colors": 15,
        "crop_ratio": (0.0, 0.15, 1.0, 0.85),  # center 70% vertically
    },
    # Gold coin: 16x16 (crop left half of combined image)
    {
        "src": "GoldCoinIconAndBackgroundTile.png",
        "name": "coin",
        "w": 16, "h": 16,
        "colors": 15,
        "crop_ratio": (0.15, 0.2, 0.48, 0.8),  # left coin area
    },
    # Background tile: 32x32 (crop right half of combined image)
    {
        "src": "GoldCoinIconAndBackgroundTile.png",
        "name": "bg_tile",
        "w": 32, "h": 32,
        "colors": 15,
        "crop_ratio": (0.52, 0.2, 0.85, 0.8),  # right tile area
    },
]

# Transparent color threshold: pixels darker than this become index 0
BLACK_THRESHOLD = 12


def rgb_to_saturn555(r, g, b):
    """Convert 8-bit RGB to Saturn 16-bit RGB555: 0bMBBBBBGGGGGRRRRR."""
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return (b5 << 10) | (g5 << 5) | r5


def is_near_black(r, g, b):
    """Check if a color is close enough to black to be transparent."""
    return r <= BLACK_THRESHOLD and g <= BLACK_THRESHOLD and b <= BLACK_THRESHOLD


def quantize_image(img, max_colors):
    """
    Quantize an RGB image to max_colors + 1 (index 0 = transparent black).

    Returns (indexed_pixels, palette) where:
      - indexed_pixels: list of palette indices (0 = transparent)
      - palette: list of (r,g,b) tuples, palette[0] = (0,0,0) = transparent
    """
    w, h = img.size
    pixels = list(img.getdata())

    # Separate transparent (near-black) and opaque pixels
    opaque_pixels = []
    opaque_mask = []  # True if pixel is opaque
    for r, g, b in pixels:
        if is_near_black(r, g, b):
            opaque_mask.append(False)
        else:
            opaque_mask.append(True)
            opaque_pixels.append((r, g, b))

    if not opaque_pixels:
        # All transparent
        palette = [(0, 0, 0)] * 16
        return [0] * (w * h), palette

    # Create a temporary image of just the opaque pixels for quantization
    temp = Image.new("RGB", (len(opaque_pixels), 1))
    temp.putdata(opaque_pixels)

    # Quantize to max_colors
    quantized = temp.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT,
                               dither=Image.Dither.FLOYDSTEINBERG)
    q_palette_flat = quantized.getpalette()[:max_colors * 3]
    q_pixels = list(quantized.getdata())

    # Build final palette: index 0 = transparent black
    palette = [(0, 0, 0)]  # index 0 = transparent
    for i in range(max_colors):
        idx = i * 3
        if idx + 2 < len(q_palette_flat):
            palette.append((q_palette_flat[idx], q_palette_flat[idx + 1], q_palette_flat[idx + 2]))
        else:
            palette.append((0, 0, 0))

    # Pad to exactly 16 entries
    while len(palette) < 16:
        palette.append((0, 0, 0))

    # Map all pixels to palette indices
    indexed = []
    q_idx = 0
    for is_opaque in opaque_mask:
        if not is_opaque:
            indexed.append(0)  # transparent
        else:
            # Quantized index + 1 (because we shifted palette by 1)
            indexed.append(q_pixels[q_idx] + 1)
            q_idx += 1

    return indexed, palette[:16]


def pack_4bpp(indexed_pixels, width, height):
    """
    Pack indexed pixels into VDP1 4bpp format.
    Each byte = 2 pixels: high nibble = left, low nibble = right.
    """
    data = bytearray()
    for y in range(height):
        for x in range(0, width, 2):
            idx = y * width + x
            left = indexed_pixels[idx] & 0x0F
            right = indexed_pixels[idx + 1] & 0x0F if (idx + 1) < len(indexed_pixels) else 0
            data.append((left << 4) | right)
    return bytes(data)


def convert_asset(asset_def, src_dir, out_dir, preview=False):
    """Convert a single asset. Returns (name, w, h, data_size, palette_rgb555)."""
    src_path = os.path.join(src_dir, asset_def["src"])
    if not os.path.exists(src_path):
        print(f"  SKIP: {asset_def['src']} not found")
        return None

    name = asset_def["name"]
    tw = asset_def["w"]
    th = asset_def["h"]
    max_colors = asset_def["colors"]

    print(f"  {asset_def['src']} -> {name} ({tw}x{th}, {max_colors} colors)")

    img = Image.open(src_path).convert("RGB")

    # Optional crop (ratio-based)
    crop = asset_def.get("crop_ratio")
    if crop:
        sw, sh = img.size
        box = (int(crop[0] * sw), int(crop[1] * sh),
               int(crop[2] * sw), int(crop[3] * sh))
        img = img.crop(box)

    # High-quality downscale
    img = img.resize((tw, th), Image.Resampling.LANCZOS)

    # Save preview if requested
    if preview:
        preview_path = os.path.join(out_dir, f"{name}_preview.png")
        img.save(preview_path)
        print(f"    preview: {preview_path}")

    # Quantize
    indexed, palette = quantize_image(img, max_colors)

    # Pack to 4bpp
    sprite_data = pack_4bpp(indexed, tw, th)

    # Build palette in Saturn RGB555
    palette_555 = []
    for r, g, b in palette:
        palette_555.append(rgb_to_saturn555(r, g, b))

    # Write raw sprite data
    bin_path = os.path.join(out_dir, f"{name}.bin")
    with open(bin_path, "wb") as f:
        f.write(sprite_data)

    # Write palette (16 x uint16 LE)
    pal_path = os.path.join(out_dir, f"{name}.pal")
    with open(pal_path, "wb") as f:
        for c in palette_555:
            f.write(struct.pack("<H", c))

    print(f"    -> {bin_path} ({len(sprite_data)} bytes)")
    print(f"    -> {pal_path} (32 bytes)")

    return {
        "name": name,
        "w": tw,
        "h": th,
        "data_size": len(sprite_data),
        "palette_555": palette_555,
    }


def write_sprite_data_header(assets_info, out_dir, bin_dir):
    """Generate coup_sprite_data.h with embedded pixel data as C arrays."""
    path = os.path.join(out_dir, "coup_sprite_data.h")

    lines = [
        "/**",
        " * coup_sprite_data.h - Embedded sprite pixel data for Saturn VDP1",
        " *",
        " * Generated by convert_assets.py. Do not edit manually.",
        " * This data gets copied to VDP1 VRAM at init time.",
        " */",
        "",
        "#ifndef COUP_SPRITE_DATA_H",
        "#define COUP_SPRITE_DATA_H",
        "",
        "#include <stdint.h>",
        "",
    ]

    for a in assets_info:
        name = a["name"]
        bin_path = os.path.join(bin_dir, f"{name}.bin")
        with open(bin_path, "rb") as f:
            data = f.read()

        lines.append(f"/* {name}: {a['w']}x{a['h']} 4bpp ({len(data)} bytes) */")
        lines.append(f"static const uint8_t coup_sprdata_{name}[{len(data)}] = {{")

        # 16 bytes per line
        for row in range(0, len(data), 16):
            chunk = data[row:row + 16]
            vals = ", ".join(f"0x{b:02X}" for b in chunk)
            lines.append(f"    {vals},")

        lines.append("};")
        lines.append("")

    # Pointer array
    lines.append("static const uint8_t* const coup_sprdata_all[COUP_SPR_COUNT] = {")
    for a in assets_info:
        lines.append(f"    coup_sprdata_{a['name']},")
    lines.append("};")
    lines.append("")

    lines.append("#endif /* COUP_SPRITE_DATA_H */")
    lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))
    print(f"  Data header: {path}")


def write_c_header(assets_info, out_dir):
    """Generate coup_sprites.h with all sprite metadata and palette data."""
    path = os.path.join(out_dir, "coup_sprites.h")

    lines = [
        "/**",
        " * coup_sprites.h - Auto-generated sprite metadata for Coup",
        " *",
        " * Generated by convert_assets.py. Do not edit manually.",
        " *",
        " * Each sprite is stored as a .bin file on the CD filesystem.",
        " * Palettes are embedded here for direct CRAM upload.",
        " */",
        "",
        "#ifndef COUP_SPRITES_H",
        "#define COUP_SPRITES_H",
        "",
        "#include <stdint.h>",
        "",
        "/*============================================================================",
        " * Sprite Dimensions",
        " *============================================================================*/",
        "",
    ]

    # Dimension defines
    for a in assets_info:
        NAME = a["name"].upper()
        lines.append(f"#define COUP_SPR_{NAME}_W   {a['w']}")
        lines.append(f"#define COUP_SPR_{NAME}_H   {a['h']}")
        lines.append(f"#define COUP_SPR_{NAME}_SIZE {a['data_size']}  /* bytes (4bpp) */")
        lines.append("")

    # Total VRAM budget
    total = sum(a["data_size"] for a in assets_info)
    lines.append(f"#define COUP_SPR_TOTAL_SIZE {total}  /* total bytes for all sprites */")
    lines.append("")

    # Sprite index enum
    lines.append("/*============================================================================")
    lines.append(" * Sprite Indices")
    lines.append(" *============================================================================*/")
    lines.append("")
    lines.append("enum {")
    for i, a in enumerate(assets_info):
        lines.append(f"    COUP_SPR_{a['name'].upper()} = {i},")
    lines.append(f"    COUP_SPR_COUNT = {len(assets_info)}")
    lines.append("};")
    lines.append("")

    # CD filenames
    lines.append("/*============================================================================")
    lines.append(" * CD Filenames (for loading)")
    lines.append(" *============================================================================*/")
    lines.append("")
    lines.append("static const char* const coup_spr_filenames[COUP_SPR_COUNT] = {")
    for a in assets_info:
        # ISO9660 Level 1: 8.3 uppercase
        fname = a["name"].upper()[:8] + ".BIN"
        lines.append(f'    "{fname}",')
    lines.append("};")
    lines.append("")

    # Palette data (embedded in code, no file I/O needed)
    lines.append("/*============================================================================")
    lines.append(" * Palette Data (Saturn RGB555, 16 colors per sprite)")
    lines.append(" *============================================================================*/")
    lines.append("")

    for a in assets_info:
        NAME = a["name"].upper()
        pal = a["palette_555"]
        lines.append(f"static const uint16_t coup_pal_{a['name']}[16] = {{")
        # 4 per line
        for row in range(0, 16, 4):
            vals = ", ".join(f"0x{pal[row+i]:04X}" for i in range(4) if row + i < 16)
            lines.append(f"    {vals},")
        lines.append("};")
        lines.append("")

    # Palette pointer array
    lines.append("static const uint16_t* const coup_palettes[COUP_SPR_COUNT] = {")
    for a in assets_info:
        lines.append(f"    coup_pal_{a['name']},")
    lines.append("};")
    lines.append("")

    # Sprite info struct
    lines.append("/*============================================================================")
    lines.append(" * Sprite Info Table")
    lines.append(" *============================================================================*/")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    uint16_t w;")
    lines.append("    uint16_t h;")
    lines.append("    uint16_t data_size;")
    lines.append("} coup_spr_info_t;")
    lines.append("")
    lines.append("static const coup_spr_info_t coup_spr_info[COUP_SPR_COUNT] = {")
    for a in assets_info:
        lines.append(f"    {{ {a['w']}, {a['h']}, {a['data_size']} }},  /* {a['name']} */")
    lines.append("};")
    lines.append("")

    lines.append("#endif /* COUP_SPRITES_H */")
    lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))
    print(f"\n  Header: {path}")


def convert_music_cdda(src_path, out_dir):
    """
    Convert an audio file (WAV/MP3) to 44100 Hz stereo 16-bit WAV
    for CD-DA (Red Book Audio) playback on Saturn.

    CD-DA is played directly from disc by the CD Block hardware —
    no RAM usage, full quality, hardware-looped.  The WAV file is
    referenced by the CUE sheet as Track 02.

    Output:
      - rebellion.wav : 44100 Hz stereo 16-bit WAV (CD-DA ready)

    Returns dict with duration info, or None on failure.
    """
    try:
        from pydub import AudioSegment
    except ImportError:
        print("  SKIP: pydub not installed (pip install pydub)")
        return None

    if not os.path.exists(src_path):
        print(f"  SKIP: {src_path} not found")
        return None

    print(f"  {os.path.basename(src_path)} -> rebellion.wav (44100 Hz stereo 16-bit, CD-DA)")

    audio = AudioSegment.from_file(src_path)

    # Convert to CD-DA format: 44100 Hz, stereo, 16-bit
    audio = audio.set_channels(2).set_frame_rate(44100).set_sample_width(2)

    duration_s = len(audio) / 1000.0
    data_size = len(audio.raw_data)

    wav_path = os.path.join(out_dir, "rebellion.wav")
    audio.export(wav_path, format="wav")

    print(f"    Duration: {duration_s:.1f}s")
    print(f"    -> {wav_path} ({data_size:,} bytes)")

    return {
        "duration_s": duration_s,
        "data_size": data_size,
    }


def main():
    parser = argparse.ArgumentParser(description="Convert Coup PNGs to Saturn VDP1 sprites")
    parser.add_argument("--output", default=None,
                        help="Output directory (default: ../saturn/cd)")
    parser.add_argument("--preview", action="store_true",
                        help="Save preview PNGs at target resolution")
    parser.add_argument("--header-dir", default=None,
                        help="Output dir for .h file (default: same as --output)")
    args = parser.parse_args()

    src_dir = os.path.dirname(os.path.abspath(__file__))
    if args.output:
        out_dir = args.output
    else:
        out_dir = os.path.join(src_dir, "..", "saturn", "cd")

    os.makedirs(out_dir, exist_ok=True)

    header_dir = args.header_dir or os.path.join(src_dir, "..", "saturn")
    os.makedirs(header_dir, exist_ok=True)

    print("=== Coup Asset Converter ===")
    print(f"  Source:  {src_dir}")
    print(f"  Output:  {out_dir}")
    print(f"  Header:  {header_dir}")
    print()

    assets_info = []
    for asset_def in ASSETS:
        result = convert_asset(asset_def, src_dir, out_dir, preview=args.preview)
        if result:
            assets_info.append(result)

    if assets_info:
        write_c_header(assets_info, header_dir)
        write_sprite_data_header(assets_info, header_dir, out_dir)
        print(f"\n  Converted {len(assets_info)} assets")
        total = sum(a["data_size"] for a in assets_info)
        print(f"  Total sprite data: {total:,} bytes ({total/1024:.1f} KB)")
        print(f"  VDP1 VRAM budget:  512 KB (using {total*100/524288:.1f}%)")
    else:
        print("\n  No assets converted!")
        return 1

    # Convert music -> CD-DA WAV (played directly from disc, no RAM usage)
    # WAV goes in the saturn build dir (alongside game.cue), not cd/
    saturn_dir = os.path.join(src_dir, "..", "saturn")
    music_src = os.path.join(src_dir, "rebellion.mp3")
    if os.path.exists(music_src):
        print()
        music_info = convert_music_cdda(music_src, saturn_dir)
        if music_info:
            print(f"\n  Music: CD-DA track ({music_info['duration_s']:.1f}s)")
            print(f"  Played from disc — zero RAM usage")
    else:
        print("\n  SKIP: rebellion.mp3 not found (no CD-DA music track)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
