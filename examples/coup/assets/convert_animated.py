#!/usr/bin/env python3
"""
convert_animated.py - Convert Coup MP4 character videos to Saturn VDP1 animated sprites

Extracts frames from MP4 videos and converts them to:
  - 4bpp packed pixel data embedded as C arrays
  - Shared 15-color palettes per character (consistent across all frames)
  - C headers with sprite metadata for the animation loader

Each character gets 24 evenly-sampled frames from its 6-second video,
all sharing a single 16-color palette for smooth animation.

Saturn VDP1 4bpp format:
  - Each byte = 2 pixels (high nibble = left, low nibble = right)
  - Color index 0 = transparent (mapped from pure black)
  - Palette: 16 entries x 16-bit RGB555
  - Width must be multiple of 8

Usage:
  python convert_animated.py
  python convert_animated.py --source "D:\\Coup Animated Sprites" --output ../saturn
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

CHARACTERS = ["Duke", "Assassin", "Captain", "Ambassador", "Contessa"]

# Target sprite dimensions (must be multiple of 8 for VDP1 width)
SPRITE_W = 32
SPRITE_H = 48

# Number of frames to sample from each video
FRAME_COUNT = 24

# Max colors per palette (index 0 reserved for transparent)
MAX_COLORS = 15

# Transparent color threshold
BLACK_THRESHOLD = 12


# ---------------------------------------------------------------------------
# Reused from convert_assets.py
# ---------------------------------------------------------------------------

def rgb_to_saturn555(r, g, b):
    """Convert 8-bit RGB to Saturn 16-bit RGB555: 0bMBBBBBGGGGGRRRRR."""
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return (b5 << 10) | (g5 << 5) | r5


def is_near_black(r, g, b):
    """Check if a color is close enough to black to be transparent."""
    return r <= BLACK_THRESHOLD and g <= BLACK_THRESHOLD and b <= BLACK_THRESHOLD


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


# ---------------------------------------------------------------------------
# Frame extraction
# ---------------------------------------------------------------------------

def extract_frames(mp4_path, frame_count):
    """
    Extract evenly-spaced frames from an MP4 using ffmpeg.
    Returns a list of PIL Image objects.
    """
    # Get total frame count
    result = subprocess.run(
        ["ffprobe", "-v", "quiet", "-select_streams", "v:0",
         "-count_packets", "-show_entries", "stream=nb_read_packets",
         "-of", "csv=p=0", mp4_path],
        capture_output=True, text=True
    )

    # Fallback: use nb_frames from stream info
    result2 = subprocess.run(
        ["ffprobe", "-v", "quiet", "-select_streams", "v:0",
         "-show_entries", "stream=nb_frames",
         "-of", "csv=p=0", mp4_path],
        capture_output=True, text=True
    )

    total_frames = 145  # default for our videos
    for r in [result, result2]:
        try:
            val = int(r.stdout.strip().split('\n')[0])
            if val > 0:
                total_frames = val
                break
        except (ValueError, IndexError):
            continue

    print(f"    Total frames: {total_frames}, sampling {frame_count}")

    # Calculate which frame indices to extract
    if total_frames <= frame_count:
        indices = list(range(total_frames))
    else:
        indices = [int(i * (total_frames - 1) / (frame_count - 1)) for i in range(frame_count)]

    # Extract frames using ffmpeg
    frames = []
    with tempfile.TemporaryDirectory() as tmpdir:
        # Extract all frames to temp dir
        subprocess.run(
            ["ffmpeg", "-v", "quiet", "-i", mp4_path,
             "-vf", "select='not(mod(n\\,1))'",
             "-vsync", "vfr",
             os.path.join(tmpdir, "frame_%05d.png")],
            check=True
        )

        # Load the specific frames we need
        for idx in indices:
            frame_path = os.path.join(tmpdir, f"frame_{idx + 1:05d}.png")
            if os.path.exists(frame_path):
                img = Image.open(frame_path).convert("RGB")
                frames.append(img)
            else:
                # If exact frame not found, try nearby
                found = False
                for offset in [0, 1, -1, 2, -2]:
                    alt_path = os.path.join(tmpdir, f"frame_{idx + 1 + offset:05d}.png")
                    if os.path.exists(alt_path):
                        img = Image.open(alt_path).convert("RGB")
                        frames.append(img)
                        found = True
                        break
                if not found:
                    print(f"    WARNING: frame {idx} not found, using last available")
                    if frames:
                        frames.append(frames[-1].copy())

    print(f"    Extracted {len(frames)} frames")
    return frames


def quantize_frames_shared(frames, max_colors, target_w, target_h):
    """
    Quantize multiple frames to a SHARED palette.
    All frames use the same 16-color palette for consistency.

    Returns (list_of_indexed_pixels, palette) where:
      - Each element in list is a list of palette indices
      - palette is a list of 16 (r,g,b) tuples, [0] = transparent
    """
    # Resize all frames to target dimensions
    resized = []
    for f in frames:
        r = f.resize((target_w, target_h), Image.Resampling.LANCZOS)
        resized.append(r)

    # Collect ALL opaque pixels from ALL frames for shared quantization
    all_opaque = []
    frame_masks = []
    for img in resized:
        pixels = list(img.getdata())
        mask = []
        for r, g, b in pixels:
            if is_near_black(r, g, b):
                mask.append(False)
            else:
                mask.append(True)
                all_opaque.append((r, g, b))
        frame_masks.append(mask)

    if not all_opaque:
        palette = [(0, 0, 0)] * 16
        return [[0] * (target_w * target_h) for _ in resized], palette

    # Create combined image for quantization
    temp = Image.new("RGB", (len(all_opaque), 1))
    temp.putdata(all_opaque)

    quantized = temp.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT,
                               dither=Image.Dither.FLOYDSTEINBERG)
    q_palette_flat = quantized.getpalette()[:max_colors * 3]
    q_pixels = list(quantized.getdata())

    # Build final palette: index 0 = transparent black
    palette = [(0, 0, 0)]
    for i in range(max_colors):
        idx = i * 3
        if idx + 2 < len(q_palette_flat):
            palette.append((q_palette_flat[idx], q_palette_flat[idx + 1], q_palette_flat[idx + 2]))
        else:
            palette.append((0, 0, 0))
    while len(palette) < 16:
        palette.append((0, 0, 0))

    # Map each frame's pixels to the shared palette
    all_indexed = []
    q_idx = 0
    for fi, img in enumerate(resized):
        mask = frame_masks[fi]
        indexed = []
        for is_opaque in mask:
            if not is_opaque:
                indexed.append(0)
            else:
                indexed.append(q_pixels[q_idx] + 1)
                q_idx += 1
        all_indexed.append(indexed)

    return all_indexed, palette[:16]


# ---------------------------------------------------------------------------
# Header generation
# ---------------------------------------------------------------------------

def write_anim_sprites_header(chars_info, out_dir):
    """Generate coup_anim_sprites.h with metadata and palette data."""
    path = os.path.join(out_dir, "coup_anim_sprites.h")

    lines = [
        "/**",
        " * coup_anim_sprites.h - Animated sprite metadata for Coup",
        " *",
        " * Generated by convert_animated.py. Do not edit manually.",
        " *",
        " * Each character has multiple animation frames stored as 4bpp",
        " * VDP1 textures sharing a single 16-color palette.",
        " */",
        "",
        "#ifndef COUP_ANIM_SPRITES_H",
        "#define COUP_ANIM_SPRITES_H",
        "",
        "#include <stdint.h>",
        "",
        "/*============================================================================",
        " * Animation Constants",
        " *============================================================================*/",
        "",
        f"#define COUP_ANIM_W           {SPRITE_W}",
        f"#define COUP_ANIM_H           {SPRITE_H}",
        f"#define COUP_ANIM_FRAME_SIZE  {SPRITE_W * SPRITE_H // 2}  /* bytes per frame (4bpp) */",
        f"#define COUP_ANIM_FRAMES      {FRAME_COUNT}  /* frames per character */",
        f"#define COUP_ANIM_CHARS       {len(chars_info)}   /* number of animated characters */",
        f"#define COUP_ANIM_TOTAL_FRAMES ({FRAME_COUNT} * {len(chars_info)})  /* total frames */",
        f"#define COUP_ANIM_TOTAL_SIZE  ({FRAME_COUNT} * {len(chars_info)} * {SPRITE_W * SPRITE_H // 2})  /* total bytes */",
        "",
        "/*============================================================================",
        " * Character Indices (same order as COUP_CHAR_* in coup.h)",
        " *============================================================================*/",
        "",
    ]

    for i, ci in enumerate(chars_info):
        lines.append(f"#define COUP_ANIM_{ci['name'].upper()}  {i}")
    lines.append("")

    # Palette data
    lines.append("/*============================================================================")
    lines.append(" * Palette Data (Saturn RGB555, 16 colors per character)")
    lines.append(" *============================================================================*/")
    lines.append("")

    for ci in chars_info:
        pal = ci["palette_555"]
        lines.append(f"static const uint16_t coup_anim_pal_{ci['name']}[16] = {{")
        for row in range(0, 16, 4):
            vals = ", ".join(f"0x{pal[row + j]:04X}" for j in range(4) if row + j < 16)
            lines.append(f"    {vals},")
        lines.append("};")
        lines.append("")

    # Palette pointer array
    lines.append(f"static const uint16_t* const coup_anim_palettes[{len(chars_info)}] = {{")
    for ci in chars_info:
        lines.append(f"    coup_anim_pal_{ci['name']},")
    lines.append("};")
    lines.append("")

    lines.append("#endif /* COUP_ANIM_SPRITES_H */")
    lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))
    print(f"  Header: {path}")


def write_anim_data_header(chars_info, frame_data, out_dir):
    """Generate coup_anim_sprite_data.h with embedded pixel data."""
    path = os.path.join(out_dir, "coup_anim_sprite_data.h")

    lines = [
        "/**",
        " * coup_anim_sprite_data.h - Embedded animated sprite pixel data",
        " *",
        " * Generated by convert_animated.py. Do not edit manually.",
        " * This data gets copied to VDP1 VRAM at init time.",
        " */",
        "",
        "#ifndef COUP_ANIM_SPRITE_DATA_H",
        "#define COUP_ANIM_SPRITE_DATA_H",
        "",
        "#include <stdint.h>",
        "#include \"coup_anim_sprites.h\"",
        "",
    ]

    frame_size = SPRITE_W * SPRITE_H // 2

    for ci in chars_info:
        name = ci["name"]
        lines.append(f"/* {name}: {FRAME_COUNT} frames x {SPRITE_W}x{SPRITE_H} 4bpp ({frame_size} bytes each) */")

        for fi in range(FRAME_COUNT):
            data = frame_data[name][fi]
            lines.append(f"static const uint8_t coup_animdata_{name}_f{fi:02d}[{len(data)}] = {{")

            for row in range(0, len(data), 16):
                chunk = data[row:row + 16]
                vals = ", ".join(f"0x{b:02X}" for b in chunk)
                lines.append(f"    {vals},")

            lines.append("};")
        lines.append("")

    # Per-character frame pointer arrays
    for ci in chars_info:
        name = ci["name"]
        lines.append(f"static const uint8_t* const coup_animdata_{name}[{FRAME_COUNT}] = {{")
        for fi in range(FRAME_COUNT):
            lines.append(f"    coup_animdata_{name}_f{fi:02d},")
        lines.append("};")
        lines.append("")

    # Master array: all characters
    lines.append(f"static const uint8_t* const* const coup_animdata_all[{len(chars_info)}] = {{")
    for ci in chars_info:
        lines.append(f"    coup_animdata_{ci['name']},")
    lines.append("};")
    lines.append("")

    lines.append("#endif /* COUP_ANIM_SPRITE_DATA_H */")
    lines.append("")

    with open(path, "w") as f:
        f.write("\n".join(lines))
    print(f"  Data header: {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Convert Coup MP4s to Saturn animated sprites")
    parser.add_argument("--source", default=None,
                        help="Directory containing character MP4 files")
    parser.add_argument("--output", default=None,
                        help="Output directory for .h files (default: ../saturn)")
    args = parser.parse_args()

    src_dir = args.source or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "..", "..", "..", "..",
                                           "Coup Animated Sprites")

    # Normalize path
    src_dir = os.path.normpath(src_dir)

    if args.output:
        out_dir = args.output
    else:
        out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "saturn")

    os.makedirs(out_dir, exist_ok=True)

    print("=== Coup Animated Sprite Converter ===")
    print(f"  Source:  {src_dir}")
    print(f"  Output:  {out_dir}")
    print(f"  Sprite:  {SPRITE_W}x{SPRITE_H} 4bpp")
    print(f"  Frames:  {FRAME_COUNT} per character")
    print(f"  Characters: {len(CHARACTERS)}")
    frame_size = SPRITE_W * SPRITE_H // 2
    total = FRAME_COUNT * len(CHARACTERS) * frame_size
    print(f"  Total data: {total:,} bytes ({total / 1024:.1f} KB)")
    print()

    chars_info = []
    frame_data = {}  # name -> [bytes for each frame]

    for char_name in CHARACTERS:
        mp4_path = os.path.join(src_dir, f"{char_name}.mp4")
        if not os.path.exists(mp4_path):
            print(f"  SKIP: {char_name}.mp4 not found at {mp4_path}")
            continue

        print(f"  Processing {char_name}...")

        # Extract frames
        frames = extract_frames(mp4_path, FRAME_COUNT)
        if len(frames) < FRAME_COUNT:
            print(f"    WARNING: only got {len(frames)} frames, padding with duplicates")
            while len(frames) < FRAME_COUNT:
                frames.append(frames[-1].copy())

        # Quantize all frames to shared palette
        all_indexed, palette = quantize_frames_shared(frames, MAX_COLORS, SPRITE_W, SPRITE_H)

        # Pack each frame to 4bpp
        packed_frames = []
        for indexed in all_indexed:
            packed = pack_4bpp(indexed, SPRITE_W, SPRITE_H)
            packed_frames.append(packed)

        # Build palette in Saturn RGB555
        palette_555 = [rgb_to_saturn555(r, g, b) for r, g, b in palette]

        chars_info.append({
            "name": char_name.lower(),
            "palette_555": palette_555,
        })
        frame_data[char_name.lower()] = packed_frames

        print(f"    -> {len(packed_frames)} frames, {len(packed_frames[0])} bytes each")

    if not chars_info:
        print("\n  No characters converted!")
        return 1

    # Write headers
    print()
    write_anim_sprites_header(chars_info, out_dir)
    write_anim_data_header(chars_info, frame_data, out_dir)

    total_bytes = FRAME_COUNT * len(chars_info) * frame_size
    print(f"\n  Converted {len(chars_info)} characters")
    print(f"  Total animation data: {total_bytes:,} bytes ({total_bytes / 1024:.1f} KB)")
    print(f"  VDP1 VRAM budget: 512 KB (animation uses {total_bytes * 100 / 524288:.1f}%)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
