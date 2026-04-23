#!/usr/bin/env python3
"""
convert_gameover.py - Convert Game Over screen to Saturn VDP1 strip sprites

Splits the 320x224 image into horizontal strips, each with its own
15-color palette (4bpp VDP1 format). This gives far better color quality
than a single 15-color palette for the entire image.

Output:
  - coup_gameover_data.h: Embedded pixel data + palettes as C arrays
  - Strip sprites registered as additional entries in the sprite system

Saturn VDP1 4bpp format:
  - Each byte = 2 pixels (high nibble = left, low nibble = right)
  - Color index 0 = transparent
  - 16-color palette per strip in Saturn RGB555
"""

import os
import struct
import sys

from PIL import Image

# Target screen resolution
SCREEN_W = 320
SCREEN_H = 224

# Strip configuration: 7 strips of 32 pixels each = 224 pixels total
STRIP_H = 32
STRIP_COUNT = SCREEN_H // STRIP_H  # 7 strips

# Transparent color threshold
BLACK_THRESHOLD = 5


def rgb_to_saturn555(r, g, b):
    """Convert 8-bit RGB to Saturn 16-bit RGB555: 0bMBBBBBGGGGGRRRRR."""
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return (b5 << 10) | (g5 << 5) | r5


def is_near_black(r, g, b):
    return r <= BLACK_THRESHOLD and g <= BLACK_THRESHOLD and b <= BLACK_THRESHOLD


def quantize_strip(img, max_colors=15):
    """Quantize a strip image to max_colors + 1 (index 0 = transparent)."""
    w, h = img.size
    pixels = list(img.getdata())

    opaque_pixels = []
    opaque_mask = []
    for r, g, b in pixels:
        if is_near_black(r, g, b):
            opaque_mask.append(False)
        else:
            opaque_mask.append(True)
            opaque_pixels.append((r, g, b))

    if not opaque_pixels:
        palette = [(0, 0, 0)] * 16
        return [0] * (w * h), palette

    temp = Image.new("RGB", (len(opaque_pixels), 1))
    temp.putdata(opaque_pixels)

    quantized = temp.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT,
                               dither=Image.Dither.FLOYDSTEINBERG)
    q_palette_flat = quantized.getpalette()[:max_colors * 3]
    q_pixels = list(quantized.getdata())

    palette = [(0, 0, 0)]  # index 0 = transparent
    for i in range(max_colors):
        idx = i * 3
        if idx + 2 < len(q_palette_flat):
            palette.append((q_palette_flat[idx], q_palette_flat[idx + 1], q_palette_flat[idx + 2]))
        else:
            palette.append((0, 0, 0))

    while len(palette) < 16:
        palette.append((0, 0, 0))

    indexed = []
    q_idx = 0
    for is_opaque in opaque_mask:
        if not is_opaque:
            indexed.append(0)
        else:
            indexed.append(q_pixels[q_idx] + 1)
            q_idx += 1

    return indexed, palette[:16]


def pack_4bpp(indexed_pixels, width, height):
    """Pack indexed pixels into VDP1 4bpp format."""
    data = bytearray()
    for y in range(height):
        for x in range(0, width, 2):
            idx = y * width + x
            left = indexed_pixels[idx] & 0x0F
            right = indexed_pixels[idx + 1] & 0x0F if (idx + 1) < len(indexed_pixels) else 0
            data.append((left << 4) | right)
    return bytes(data)


def main():
    src_path = os.path.join(os.path.dirname(__file__), "Coup Game Over.png")
    if not os.path.exists(src_path):
        print(f"ERROR: {src_path} not found!", file=sys.stderr)
        sys.exit(1)

    print(f"Loading: {src_path}")
    img = Image.open(src_path).convert("RGB")
    print(f"  Source: {img.size[0]}x{img.size[1]}")

    # Resize to 320x224, cropping to maintain aspect ratio
    src_w, src_h = img.size
    target_aspect = SCREEN_W / SCREEN_H  # 1.4286
    src_aspect = src_w / src_h

    if src_aspect > target_aspect:
        # Source is wider: crop sides
        new_w = int(src_h * target_aspect)
        left = (src_w - new_w) // 2
        img = img.crop((left, 0, left + new_w, src_h))
    else:
        # Source is taller: crop top/bottom
        new_h = int(src_w / target_aspect)
        top = (src_h - new_h) // 2
        img = img.crop((0, top, src_w, top + new_h))

    img = img.resize((SCREEN_W, SCREEN_H), Image.Resampling.LANCZOS)
    print(f"  Resized: {SCREEN_W}x{SCREEN_H}")

    # Process strips
    strips = []
    total_bytes = 0
    for s in range(STRIP_COUNT):
        y_start = s * STRIP_H
        strip_img = img.crop((0, y_start, SCREEN_W, y_start + STRIP_H))

        indexed, palette = quantize_strip(strip_img, max_colors=15)
        sprite_data = pack_4bpp(indexed, SCREEN_W, STRIP_H)

        palette_555 = [rgb_to_saturn555(r, g, b) for r, g, b in palette]

        strips.append({
            "index": s,
            "y": y_start,
            "w": SCREEN_W,
            "h": STRIP_H,
            "data": sprite_data,
            "palette_555": palette_555,
            "data_size": len(sprite_data),
        })
        total_bytes += len(sprite_data)
        print(f"  Strip {s}: y={y_start:3d} h={STRIP_H} data={len(sprite_data)} bytes")

    print(f"\n  Total: {total_bytes:,} bytes ({total_bytes/1024:.1f} KB)")
    print(f"  Strips: {STRIP_COUNT}")

    # Write C header
    out_path = os.path.join(os.path.dirname(__file__), "..", "saturn", "coup_gameover_data.h")
    with open(out_path, 'w') as f:
        f.write("/**\n")
        f.write(" * coup_gameover_data.h - Game Over screen background image data\n")
        f.write(" *\n")
        f.write(" * Auto-generated by convert_gameover.py. Do not edit manually.\n")
        f.write(f" * {STRIP_COUNT} horizontal strips, each {SCREEN_W}x{STRIP_H} at 4bpp.\n")
        f.write(f" * Total size: {total_bytes:,} bytes.\n")
        f.write(" */\n\n")
        f.write("#ifndef COUP_GAMEOVER_DATA_H\n")
        f.write("#define COUP_GAMEOVER_DATA_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write(f"#define GAMEOVER_STRIP_COUNT  {STRIP_COUNT}\n")
        f.write(f"#define GAMEOVER_STRIP_W      {SCREEN_W}\n")
        f.write(f"#define GAMEOVER_STRIP_H      {STRIP_H}\n")
        f.write(f"#define GAMEOVER_TOTAL_SIZE   {total_bytes}\n\n")

        # Pixel data arrays
        for s in strips:
            idx = s["index"]
            f.write(f"/* Strip {idx}: y={s['y']}, {s['w']}x{s['h']} ({s['data_size']} bytes) */\n")
            f.write(f"static const uint8_t gameover_strip_{idx}[{s['data_size']}] = {{\n")
            data = s["data"]
            for row in range(0, len(data), 16):
                chunk = data[row:row + 16]
                vals = ", ".join(f"0x{b:02X}" for b in chunk)
                if row + 16 < len(data):
                    f.write(f"    {vals},\n")
                else:
                    f.write(f"    {vals}\n")
            f.write("};\n\n")

        # Palette arrays
        for s in strips:
            idx = s["index"]
            pal = s["palette_555"]
            f.write(f"static const uint16_t gameover_pal_{idx}[16] = {{\n")
            for row in range(0, 16, 4):
                vals = ", ".join(f"0x{pal[row+i]:04X}" for i in range(4) if row + i < 16)
                f.write(f"    {vals},\n")
            f.write("};\n\n")

        # Pointer arrays
        f.write("static const uint8_t* const gameover_strip_data[GAMEOVER_STRIP_COUNT] = {\n")
        for s in strips:
            f.write(f"    gameover_strip_{s['index']},\n")
        f.write("};\n\n")

        f.write("static const uint16_t* const gameover_strip_palettes[GAMEOVER_STRIP_COUNT] = {\n")
        for s in strips:
            f.write(f"    gameover_pal_{s['index']},\n")
        f.write("};\n\n")

        # Strip sizes for loading
        f.write("static const uint16_t gameover_strip_sizes[GAMEOVER_STRIP_COUNT] = {\n")
        for s in strips:
            f.write(f"    {s['data_size']},\n")
        f.write("};\n\n")

        f.write("#endif /* COUP_GAMEOVER_DATA_H */\n")

    print(f"\n  Wrote: {out_path}")


if __name__ == "__main__":
    main()
