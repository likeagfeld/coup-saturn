#!/usr/bin/env python3
"""
convert_portraits.py - Build animated Saturn portraits from the official cards.

Replaces the previous animation, which was generated from MP4s that are not in
the repo and whose source art faded to black at the bottom. That fade is
border-connected, so no transparency heuristic could separate it from the
background: 56-74% of every sprite was punched out (MEASURED), the characters
lost their torsos over the painted backdrop, and a medallion had to be drawn
behind each one to hide it.

The official card illustrations are fully opaque busts on their own painted
backgrounds, which removes the problem by construction rather than by masking.

Motion comes from a slow ping-pong zoom across the still illustration - a Ken
Burns drift. It is genuine per-frame motion, it loops seamlessly because the
ramp reverses, and it needs no source video.

VDP1 constraints honoured (ST-013-R3):
  - 4bpp packed, two pixels per byte, high nybble is the left pixel
  - colour index 0 is TRANSPARENT for a sprite, so it is reserved and unused;
    the art occupies indices 1-15
  - width must be a multiple of 8
  - one shared 16-colour palette per character across all frames, so a frame
    change never needs a CRAM upload

Usage:
  python convert_portraits.py [--frames 24] [--preview-dir DIR]
"""

import argparse
import glob
import os
import sys

from PIL import Image

CHARACTERS = ["duke", "assassin", "captain", "ambassador", "contessa"]

# Rendered at NATIVE display size. The portraits were previously 32x48 drawn
# at 64x96, i.e. every pixel doubled, which is what made them look blocky -
# 87% of the official art's detail was discarded and the remainder magnified.
# Generating at 64x96 quadruples the detail for the same on-screen size.
#
# Budget: 64*96 4bpp = 3072 B/frame. At 12 frames x 5 characters that is
# 184,320 B against 273,232 B of WRAM-H headroom, leaving ~181 KB. Trading
# frame count for resolution is the right way round here: the motion is a slow
# drift, which reads fine at 12 frames, whereas blockiness is visible always.
SPRITE_W = 64
SPRITE_H = 96
FRAME_COUNT = 12
MAX_COLORS = 15          # index 0 stays transparent and unused

# Bust window inside the card, as fractions: inside the coloured border and
# above the name plate.
CROP = (0.06, 0.05, 0.94, 0.62)

# Ping-pong zoom depth. Small on purpose - at 32x48 a large zoom reads as
# juddering rather than drift.
ZOOM_MAX = 0.07


def rgb_to_saturn555(r, g, b):
    """Pack 8-bit RGB into Saturn RGB555: 0BBBBBGGGGGRRRRR."""
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def build_frames(card_path, frames, zoom_max):
    """Crop the bust and render `frames` of ping-pong zoom."""
    card = Image.open(card_path).convert("RGB")
    w, h = card.size
    box = (int(w * CROP[0]), int(h * CROP[1]),
           int(w * CROP[2]), int(h * CROP[3]))
    bust = card.crop(box)
    bw, bh = bust.size

    out = []
    for i in range(frames):
        # Triangle wave 0..1..0 so frame N-1 leads back into frame 0.
        t = i / (frames / 2.0)
        if t > 1.0:
            t = 2.0 - t
        z = 1.0 - zoom_max * t

        cw, ch = int(bw * z), int(bh * z)
        ox, oy = (bw - cw) // 2, int((bh - ch) * 0.35)   # bias toward the face
        out.append(bust.crop((ox, oy, ox + cw, oy + ch))
                       .resize((SPRITE_W, SPRITE_H), Image.LANCZOS))
    return out


# Only 15 colours survive quantization, so every one has to earn its place.
# MEASURED on the delivered portraits: assassin sits at 24.5% mean saturation,
# ambassador spans only 115 of 255 luma with a contrast of 32.9, and duke is
# 39.2% saturated - that flatness is the "washed out" report. Stretching
# BEFORE quantizing means the 15 slots are spent across the full range instead
# of clustered in the middle.
STRETCH_CLIP = 0.005      # ignore the extreme 0.5% each end (specular / noise)
SAT_BOOST = 1.35
# How far to correct a per-channel colour cast. 1.0 equalises every channel
# and strips the scene's identity; 0.0 leaves the cast untouched.
CAST_CORRECT = 0.65


def stretch_frames(frames):
    """Auto-level the whole sequence together, then lift saturation.

    Levelling each frame independently would make the character pulse between
    frames, because the histogram shifts as the art animates. One set of
    limits taken across every frame keeps the sequence stable.
    """
    import numpy as np

    stack = np.concatenate([np.asarray(f.convert("RGB")).reshape(-1, 3)
                            for f in frames]).astype(float)

    # PER-CHANNEL limits, not one luma scale. A single luma scale preserves
    # whatever colour cast the render already had - the delivered portraits sit
    # under a heavy warm key light, so red is near full while blue never leaves
    # the bottom third. Levelling each channel to its own range removes the
    # cast and hands the quantizer a genuinely wider gamut to spend 15 slots
    # on, which is what "better colours" needs. Luma alone cannot do it.
    lo = np.percentile(stack, STRETCH_CLIP * 100, axis=0)
    hi = np.percentile(stack, 100 - STRETCH_CLIP * 100, axis=0)
    span = np.maximum(hi - lo, 1.0)

    # Do not fully equalise: pushing every channel to the same range strips
    # the scene's colour identity. Blend the per-channel limits toward the
    # common ones so the cast is reduced, not erased.
    common_lo, common_hi = lo.mean(), hi.mean()
    lo = lo * CAST_CORRECT + common_lo * (1.0 - CAST_CORRECT)
    hi = hi * CAST_CORRECT + common_hi * (1.0 - CAST_CORRECT)
    span = np.maximum(hi - lo, 1.0)

    out = []
    for f in frames:
        a = np.asarray(f.convert("RGB")).astype(float)
        a = np.clip((a - lo) * (255.0 / span), 0, 255)
        grey = a.mean(axis=2, keepdims=True)
        a = np.clip(grey + (a - grey) * SAT_BOOST, 0, 255)
        out.append(Image.fromarray(a.astype("uint8"), "RGB"))
    return out, float(np.mean(255.0 / span))


def quantize_shared(frames, max_colors):
    """One palette for every frame of a character.

    Deliberately does NOT treat dark pixels as transparent. These portraits are
    opaque by design; masking near-black is exactly what destroyed the previous
    sprites.
    """
    strip = Image.new("RGB", (SPRITE_W * len(frames), SPRITE_H))
    for i, f in enumerate(frames):
        strip.paste(f, (i * SPRITE_W, 0))

    # MAXCOVERAGE picks colours by how much of the image they actually cover,
    # rather than by median-cut's split order. At 15 slots that difference is
    # visible: medcut spends slots on rare highlights, maxcoverage on skin and
    # cloth, which is where the eye goes.
    q = strip.quantize(colors=max_colors, method=Image.MAXCOVERAGE,
                       dither=Image.Dither.FLOYDSTEINBERG)
    flat = q.getpalette()[: max_colors * 3]
    idx = q.tobytes()

    # Shift every index up by one so 0 stays reserved for transparency.
    palette = [(0, 0, 0)]
    for i in range(max_colors):
        palette.append((flat[i * 3], flat[i * 3 + 1], flat[i * 3 + 2]))
    while len(palette) < 16:
        palette.append((0, 0, 0))

    per_frame = []
    sw = SPRITE_W * len(frames)
    for n in range(len(frames)):
        px = []
        for y in range(SPRITE_H):
            row = y * sw + n * SPRITE_W
            px.extend(idx[row: row + SPRITE_W])
        per_frame.append([p + 1 for p in px])
    return per_frame, palette


def pack_4bpp(indices):
    """Two pixels per byte, high nybble is the left pixel."""
    data = bytearray()
    for y in range(SPRITE_H):
        for x in range(0, SPRITE_W, 2):
            i = y * SPRITE_W + x
            data.append(((indices[i] & 0xF) << 4) | (indices[i + 1] & 0xF))
    return bytes(data)


def verify(per_frame, palette, name):
    """Assert the hardware invariants before anything is written."""
    assert palette[0] == (0, 0, 0), f"{name}: index 0 must stay reserved"
    for n, px in enumerate(per_frame):
        assert len(px) == SPRITE_W * SPRITE_H, f"{name} f{n}: wrong pixel count"
        assert 0 not in px, (
            f"{name} f{n}: uses the transparent index - the backdrop would "
            "show through the character")
        assert max(px) <= 15, f"{name} f{n}: index out of 4bpp range"
    assert SPRITE_W % 8 == 0, "VDP1 sprite width must be a multiple of 8"


def load_pack_frames(pack_dir, name, frames):
    """Load pre-rendered animation frames from the delivered asset pack.

    The pack ships real per-frame animation authored at the display size, which
    is strictly better than the synthesised Ken Burns drift: it has actual
    motion - breathing, blinks, cloth drift - rather than a pan across a still.
    Frames are already 64x96, so no resampling happens here and no detail is
    lost a second time.
    """
    found = sorted(glob.glob(os.path.join(pack_dir, name, "*.png")))
    if not found:
        raise SystemExit(f"convert_portraits: no frames for {name} in {pack_dir}")

    out = []
    for path in found[:frames]:
        im = Image.open(path).convert("RGB")
        if im.size != (SPRITE_W, SPRITE_H):
            im = im.resize((SPRITE_W, SPRITE_H), Image.LANCZOS)
        out.append(im)

    # Loop back through the supplied frames if fewer were delivered than asked.
    while len(out) < frames:
        out.append(out[len(out) % len(found)].copy())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=FRAME_COUNT)
    ap.add_argument("--preview-dir")
    ap.add_argument("--src", default="card_src")
    ap.add_argument("--pack-dir",
                    help="directory of pre-rendered frames, one subdir per "
                         "character; overrides the synthesised drift")
    ap.add_argument("--out", default="../saturn")
    args = ap.parse_args()

    all_data, all_pal = {}, {}
    for name in CHARACTERS:
        if args.pack_dir:
            frames = load_pack_frames(args.pack_dir, name, args.frames)
        else:
            frames = build_frames(os.path.join(args.src, f"{name}.png"),
                                  args.frames, ZOOM_MAX)
        frames, _scale = stretch_frames(frames)
        per_frame, palette = quantize_shared(frames, MAX_COLORS)
        verify(per_frame, palette, name)
        all_data[name] = [pack_4bpp(p) for p in per_frame]
        all_pal[name] = palette

        used = len(set(i for p in per_frame for i in p))
        print(f"  {name:11} {args.frames} frames, {used:2}/{MAX_COLORS} colours, "
              f"{len(all_data[name][0])} bytes/frame")

        if args.preview_dir:
            os.makedirs(args.preview_dir, exist_ok=True)
            frames[0].resize((SPRITE_W * 4, SPRITE_H * 4), Image.NEAREST).save(
                os.path.join(args.preview_dir, f"portrait-{name}.png"))

    fsize = len(all_data[CHARACTERS[0]][0])

    data_path = os.path.join(args.out, "coup_anim_sprite_data.h")
    with open(data_path, "w") as f:
        f.write("/* Generated by convert_portraits.py from the official card "
                "art. Do not edit. */\n")
        f.write("#ifndef COUP_ANIM_SPRITE_DATA_H\n"
                "#define COUP_ANIM_SPRITE_DATA_H\n\n#include <stdint.h>\n\n")
        for name in CHARACTERS:
            for n, blob in enumerate(all_data[name]):
                f.write(f"static const uint8_t coup_animdata_{name}_f{n:02d}"
                        f"[{fsize}] = {{\n")
                for i in range(0, len(blob), 16):
                    f.write("    " + ", ".join(f"0x{b:02X}"
                                               for b in blob[i:i + 16]) + ",\n")
                f.write("};\n\n")
            f.write(f"static const uint8_t* const coup_animdata_{name}"
                    f"[{args.frames}] = {{\n")
            for n in range(args.frames):
                f.write(f"    coup_animdata_{name}_f{n:02d},\n")
            f.write("};\n\n")
        f.write("static const uint8_t* const* const coup_animdata_all[5] = {\n")
        for name in CHARACTERS:
            f.write(f"    coup_animdata_{name},\n")
        f.write("};\n\n#endif /* COUP_ANIM_SPRITE_DATA_H */\n")

    spr_path = os.path.join(args.out, "coup_anim_sprites.h")
    with open(spr_path, "w") as f:
        f.write("/* Generated by convert_portraits.py. Do not edit. */\n")
        f.write("#ifndef COUP_ANIM_SPRITES_H\n#define COUP_ANIM_SPRITES_H\n\n"
                "#include <stdint.h>\n\n")
        f.write(f"#define COUP_ANIM_W           {SPRITE_W}\n")
        f.write(f"#define COUP_ANIM_H           {SPRITE_H}\n")
        f.write(f"#define COUP_ANIM_FRAME_SIZE  {fsize}\n")
        f.write(f"#define COUP_ANIM_FRAMES      {args.frames}\n")
        f.write(f"#define COUP_ANIM_CHARS       {len(CHARACTERS)}\n")
        f.write(f"#define COUP_ANIM_TOTAL_FRAMES ({args.frames} * {len(CHARACTERS)})\n")
        f.write(f"#define COUP_ANIM_TOTAL_SIZE  "
                f"({args.frames} * {len(CHARACTERS)} * {fsize})\n\n")
        for i, name in enumerate(CHARACTERS):
            f.write(f"#define COUP_ANIM_{name.upper()}  {i}\n")
        f.write("\n")
        for name in CHARACTERS:
            f.write(f"static const uint16_t coup_anim_pal_{name}[16] = {{\n")
            vals = [rgb_to_saturn555(*c) for c in all_pal[name]]
            for i in range(0, 16, 4):
                f.write("    " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 4])
                        + ",\n")
            f.write("};\n\n")
        f.write("static const uint16_t* const coup_anim_palettes[5] = {\n")
        for name in CHARACTERS:
            f.write(f"    coup_anim_pal_{name},\n")
        f.write("};\n\n#endif /* COUP_ANIM_SPRITES_H */\n")

    total = args.frames * len(CHARACTERS) * fsize
    print(f"-> {data_path} and {spr_path}")
    print(f"   {total:,} bytes of sprite data "
          f"({len(CHARACTERS)} chars x {args.frames} frames x {fsize})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
