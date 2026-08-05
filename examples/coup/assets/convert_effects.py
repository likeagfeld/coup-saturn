#!/usr/bin/env python3
"""
convert_effects.py - Convert magenta-keyed sprites to Saturn VDP1 4bpp.

Handles the action effects, UI icons and card faces from the asset pack. All
arrive keyed on flat magenta (#FF00FF), which becomes palette index 0 - the
index VDP1 renders as transparent for a sprite.

This is the OPPOSITE requirement to the portraits. A portrait must be fully
opaque or the painted backdrop shows through the character; an effect must be
keyed or it draws an ugly rectangle over the table. Both are handled by
reserving index 0 and controlling exactly which pixels land on it.

VDP1 constraints (ST-013-R3):
  - 4bpp packed, two pixels per byte, high nybble is the left pixel
  - width must be a multiple of 8
  - one shared 16-colour palette per sequence, so a frame change never needs
    a CRAM upload mid-animation

Usage:
  python convert_effects.py --pack-dir <saturn_ready> --output ../saturn
"""

import argparse
import glob
import os
import sys

from PIL import Image

MAX_COLORS = 15          # index 0 is the transparency key

# A pixel counts as the key if it is strongly magenta. Generous on the green
# channel because resampling bleeds neighbouring colour into the key edge.
KEY_R_MIN, KEY_G_MAX, KEY_B_MIN = 200, 80, 200


def rgb_to_saturn555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def is_key(px):
    r, g, b = px[:3]
    return r >= KEY_R_MIN and g <= KEY_G_MAX and b >= KEY_B_MIN


def magenta_score(px):
    """How much flat magenta is mixed into this pixel, 0.0 to 1.0.

    Magenta is high red AND high blue with low green, so the green deficit
    relative to the red/blue floor is a direct measure of contamination.
    """
    r, g, b = px[:3]
    lo = min(r, b)
    if lo <= g:
        return 0.0
    return (lo - g) / 255.0


# A pixel this contaminated is more key than artwork; below it we unmix.
FRINGE_KEY = 0.45
# Any contamination at all above this gets unmixed.
FRINGE_MIN = 0.10


def defringe(im):
    """Remove magenta bleed from the key edge.

    The strict key catches flat #FF00FF, but resampling the source produced
    PARTIAL magenta along every edge - measured examples (231,33,165),
    (247,99,156), (198,41,181). Those fail the key test, survive as opaque
    artwork, and the quantizer then spends real palette slots on them:
    MEASURED 45 magenta palette entries across 16 sprites, every one of which
    paints a visible fringe.

    Widening the key would be the wrong fix - it eats genuine purple and red
    artwork. Instead each contaminated pixel is UNMIXED: replaced by the
    average of its uncontaminated neighbours, so the edge keeps its real
    colour. Pixels that are mostly key become key.

    Returns a new image; the caller keys it as usual afterwards.
    """
    w, h = im.size
    src = im.load()
    out = Image.new("RGB", (w, h))
    dst = out.load()

    for y in range(h):
        for x in range(w):
            px = src[x, y]
            if is_key(px):
                dst[x, y] = px          # leave flat key alone; keyed later
                continue
            score = magenta_score(px)
            if score < FRINGE_MIN:
                dst[x, y] = px
                continue
            if score >= FRINGE_KEY:
                dst[x, y] = (255, 0, 255)   # mostly key - make it key
                continue
            # Unmix from clean neighbours.
            acc = [0, 0, 0]
            n = 0
            for dy in (-2, -1, 0, 1, 2):
                for dx in (-2, -1, 0, 1, 2):
                    nx, ny = x + dx, y + dy
                    if not (0 <= nx < w and 0 <= ny < h):
                        continue
                    q = src[nx, ny]
                    if is_key(q) or magenta_score(q) >= FRINGE_MIN:
                        continue
                    acc[0] += q[0]
                    acc[1] += q[1]
                    acc[2] += q[2]
                    n += 1
            if n:
                dst[x, y] = (acc[0] // n, acc[1] // n, acc[2] // n)
            else:
                dst[x, y] = (255, 0, 255)   # nothing clean nearby - key it
    return out


# The logo art is the one asset NOT delivered on magenta - it sits on a dark
# backing (MEASURED: L1_wordmark.png is 256x64, 15 colours, no alpha channel).
# A plain "dark pixels are transparent" threshold is wrong here: it also erases
# the dark interior of the emblem and the letters' own shading. Keying only the
# dark region REACHABLE FROM THE BORDER removes the backing and leaves enclosed
# darks alone (MEASURED at T=56: 277 interior pixels survive that a global
# threshold destroys).
#
# T=56 is not a taste call. Sweeping the threshold, the keyed fraction is flat
# between 56 and 64 (63.0% at both) - a plateau means the cut falls in a gap in
# the colour distribution rather than through a cluster.
LOGO_DARK_MAX = 56


def dark_border_mask(im, thresh=LOGO_DARK_MAX):
    """Keep-mask: False only for dark pixels connected to the image border."""
    from collections import deque

    w, h = im.size
    px = im.load()
    keyed = bytearray(w * h)
    q = deque()

    def seed(x, y):
        if not keyed[y * w + x] and max(px[x, y][:3]) <= thresh:
            keyed[y * w + x] = 1
            q.append((x, y))

    for x in range(w):
        seed(x, 0)
        seed(x, h - 1)
    for y in range(h):
        seed(0, y)
        seed(w - 1, y)

    while q:
        cx, cy = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if 0 <= nx < w and 0 <= ny < h:
                seed(nx, ny)

    return [not k for k in keyed]


def all_opaque(im):
    """Keep every pixel. For art that must NOT be keyed.

    A card face is a complete picture with its own border - there is no
    background to remove, and keying one would punch holes in the artwork.
    Index 0 still goes unused, because quantize_sequence shifts every colour
    up by one, so the sprite is fully opaque on VDP1 where index 0 is the
    transparent slot.
    """
    w, h = im.size
    return [True] * (w * h)


def quantize_sequence(frames, name, mask_fn=None):
    """Shared palette across a sequence; the keyed colour becomes index 0.

    mask_fn(image) -> list of bool (True = keep). Defaults to the magenta key.
    """
    w, h = frames[0].size
    opaque = []
    masks = []
    for im in frames:
        rgb = im.convert("RGB")
        if mask_fn is None:                 # magenta-keyed art
            rgb = defringe(rgb)
        px = list(rgb.getdata())
        if mask_fn is None:
            m = [not is_key(p) for p in px]
        else:
            m = mask_fn(rgb)
        masks.append(m)
        opaque.extend(p for p, keep in zip(px, m) if keep)

    if not opaque:
        raise SystemExit(f"convert_effects: {name} is entirely keyed out")

    strip = Image.new("RGB", (len(opaque), 1))
    strip.putdata(opaque)
    q = strip.quantize(colors=MAX_COLORS, method=Image.MEDIANCUT)
    flat = q.getpalette()[: MAX_COLORS * 3]
    qpx = q.tobytes()

    # The quantizer returns FEWER entries than requested when the source has
    # fewer distinct colours - common for flat UI icons. Read only what it
    # actually produced and pad the rest.
    available = len(flat) // 3
    palette = [0x0000]
    for i in range(MAX_COLORS):
        if i < available:
            palette.append(rgb_to_saturn555(flat[i * 3], flat[i * 3 + 1],
                                            flat[i * 3 + 2]))
        else:
            palette.append(0x0000)
    while len(palette) < 16:
        palette.append(0x0000)

    out, cursor = [], 0
    for m in masks:
        idx = []
        for keep in m:
            if keep:
                idx.append(qpx[cursor] + 1)
                cursor += 1
            else:
                idx.append(0)
        out.append(idx)
    return out, palette, w, h


def pack_4bpp(indices, w, h):
    data = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            i = y * w + x
            data.append(((indices[i] & 0xF) << 4) | (indices[i + 1] & 0xF))
    return bytes(data)


def verify(per_frame, palette, w, h, name, require_key=True):
    assert w % 8 == 0, f"{name}: width {w} is not a multiple of 8"
    assert palette[0] == 0x0000, f"{name}: index 0 must be the transparency key"
    bad = [p for p in palette if p > 0x7FFF]
    assert not bad, f"{name}: {len(bad)} palette words have bit 15 set"
    for n, px in enumerate(per_frame):
        assert len(px) == w * h, f"{name} f{n}: wrong pixel count"
        assert max(px) <= 15, f"{name} f{n}: index out of 4bpp range"
    # An effect that is entirely opaque was never keyed and will draw a
    # rectangle over the table.
    keyed = sum(p.count(0) for p in per_frame)
    total = sum(len(p) for p in per_frame)
    if require_key:
        assert keyed > 0, (f"{name}: nothing is transparent - the key was not "
                           "applied")
    else:
        assert keyed == 0, (f"{name}: {keyed} pixel(s) landed on the "
                            "transparent index in art that must be opaque")
    return keyed / total


def load_sequence(d):
    files = sorted(glob.glob(os.path.join(d, "*.png")))
    return [Image.open(f).convert("RGB") for f in files]


def emit(sequences, singles, out_dir):
    path = os.path.join(out_dir, "coup_fx_data.h")
    with open(path, "w") as f:
        f.write("/* Generated by convert_effects.py. Do not edit. */\n")
        f.write("#ifndef COUP_FX_DATA_H\n#define COUP_FX_DATA_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write(f"#define COUP_FX_COUNT {len(sequences)}\n")
        f.write(f"#define COUP_UI_COUNT {len(singles)}\n\n")

        f.write("enum {\n")
        for i, (n, _, _, _, _) in enumerate(sequences):
            f.write(f"    COUP_FX_{n.upper()} = {i},\n")
        f.write("};\n\n")
        f.write("enum {\n")
        for i, (n, _, _, _, _) in enumerate(singles):
            f.write(f"    COUP_UI_{n.upper()} = {i},\n")
        f.write("};\n\n")

        f.write("typedef struct {\n"
                "    uint16_t w, h;\n"
                "    uint16_t frames;\n"
                "    uint16_t frame_bytes;\n"
                "} coup_fx_info_t;\n\n")

        for group, tag in ((sequences, "fx"), (singles, "ui")):
            for name, blobs, pal, w, h in group:
                for n, blob in enumerate(blobs):
                    f.write(f"static const uint8_t coup_{tag}_{name}_f{n:02d}"
                            f"[{len(blob)}] = {{\n")
                    for i in range(0, len(blob), 16):
                        f.write("    " + ", ".join(f"0x{b:02X}"
                                                   for b in blob[i:i + 16]) + ",\n")
                    f.write("};\n")
                f.write(f"static const uint8_t* const coup_{tag}_{name}"
                        f"[{len(blobs)}] = {{\n")
                for n in range(len(blobs)):
                    f.write(f"    coup_{tag}_{name}_f{n:02d},\n")
                f.write("};\n")
                f.write(f"static const uint16_t coup_{tag}_pal_{name}[16] = {{\n")
                for i in range(0, 16, 4):
                    f.write("    " + ", ".join(f"0x{v:04X}"
                                               for v in pal[i:i + 4]) + ",\n")
                f.write("};\n\n")

        for group, tag in ((sequences, "fx"), (singles, "ui")):
            cnt = "COUP_FX_COUNT" if tag == "fx" else "COUP_UI_COUNT"
            f.write(f"static const uint8_t* const* const coup_{tag}_all[{cnt}] = {{\n")
            for name, _, _, _, _ in group:
                f.write(f"    coup_{tag}_{name},\n")
            f.write("};\n\n")
            f.write(f"static const uint16_t* const coup_{tag}_palettes[{cnt}] = {{\n")
            for name, _, _, _, _ in group:
                f.write(f"    coup_{tag}_pal_{name},\n")
            f.write("};\n\n")
            f.write(f"static const coup_fx_info_t coup_{tag}_info[{cnt}] = {{\n")
            for name, blobs, _, w, h in group:
                f.write(f"    {{ {w}, {h}, {len(blobs)}, {len(blobs[0])} }},\n")
            f.write("};\n\n")

        f.write("#endif /* COUP_FX_DATA_H */\n")
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack-dir", required=True)
    ap.add_argument("--out", default="../saturn")
    ap.add_argument("--skip", action="append", default=[],
                    help="effect name to omit, e.g. card_flip")
    args = ap.parse_args()

    sequences, singles = [], []
    total = 0

    for d in sorted(glob.glob(os.path.join(args.pack_dir, "effects", "*"))):
        if not os.path.isdir(d):
            continue
        name = os.path.basename(d).split("_", 1)[1]
        if name in args.skip:
            print(f"  fx {name:12} SKIPPED - VDP1 does this in hardware")
            continue
        frames = load_sequence(d)
        idx, pal, w, h = quantize_sequence(frames, name)
        keyed = verify(idx, pal, w, h, name)
        blobs = [pack_4bpp(p, w, h) for p in idx]
        sequences.append((name, blobs, pal, w, h))
        total += sum(len(b) for b in blobs)
        print(f"  fx {name:12} {len(blobs):2} frames {w:3}x{h:<3} "
              f"{keyed*100:4.1f}% keyed  {sum(len(b) for b in blobs):>6,} B")

    for p in sorted(glob.glob(os.path.join(args.pack_dir, "ui", "*.png"))):
        name = os.path.splitext(os.path.basename(p))[0].split("_", 1)[1]
        im = Image.open(p).convert("RGB")
        idx, pal, w, h = quantize_sequence([im], name)
        keyed = verify(idx, pal, w, h, name)
        blobs = [pack_4bpp(idx[0], w, h)]
        singles.append((name, blobs, pal, w, h))
        total += len(blobs[0])
        print(f"  ui {name:12}  1 frame  {w:3}x{h:<3} "
              f"{keyed*100:4.1f}% keyed  {len(blobs[0]):>6,} B")

    # The logo, keyed on its dark backing rather than magenta. It lives in
    # logo/ rather than ui/ in the pack, so it is handled explicitly.
    logo = os.path.join(args.pack_dir, "logo", "L1_wordmark.png")
    if os.path.exists(logo):
        im = Image.open(logo).convert("RGB")
        idx, pal, w, h = quantize_sequence([im], "wordmark",
                                           mask_fn=dark_border_mask)
        keyed = verify(idx, pal, w, h, "wordmark")
        blobs = [pack_4bpp(idx[0], w, h)]
        singles.append(("wordmark", blobs, pal, w, h))
        total += len(blobs[0])
        print(f"  ui {'wordmark':12}  1 frame  {w:3}x{h:<3} "
              f"{keyed*100:4.1f}% keyed  {len(blobs[0]):>6,} B  (dark key)")

    # Card faces and the card back. Opaque, not keyed: each is a complete
    # picture with its own border, so there is no background to remove.
    for p in sorted(glob.glob(os.path.join(args.pack_dir, "cards", "*.png"))):
        name = os.path.splitext(os.path.basename(p))[0].split("_", 1)[1]
        im = Image.open(p).convert("RGB")
        idx, pal, w, h = quantize_sequence([im], name, mask_fn=all_opaque)
        keyed = verify(idx, pal, w, h, name, require_key=False)
        blobs = [pack_4bpp(idx[0], w, h)]
        singles.append((name, blobs, pal, w, h))
        total += len(blobs[0])
        print(f"  ui {name:12}  1 frame  {w:3}x{h:<3} "
              f"{keyed*100:4.1f}% keyed  {len(blobs[0]):>6,} B  (opaque card)")

    path = emit(sequences, singles, args.out)
    print(f"-> {path}")
    print(f"   {len(sequences)} effect sequences, {len(singles)} UI sprites, "
          f"{total:,} bytes total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
