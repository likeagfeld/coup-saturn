#!/usr/bin/env python3
"""
Convert sound effect source files to raw 16-bit PCM C arrays for Saturn SCSP.

All source sounds are CC0 (Public Domain):
- Kenney.nl UI Audio & Casino Audio (CC0)
- OpenGameArt.org CC0 sounds (coin_drop, bell, alert, win_sound, game_over)
- Cockatrice Card Game Sounds (CC0)

Output: coup_sfx_data.h with embedded PCM arrays at 11025 Hz mono 16-bit signed.
"""

import soundfile as sf
import numpy as np
import os
import sys

TARGET_RATE = 11025
# Sound RAM budget: 512KB total, music uses 0x6C000 (442368) bytes,
# leaving 81920 bytes = 40960 samples total for all SFX combined.
MAX_TOTAL_SAMPLES = 40960
MAX_SAMPLES = 8000  # ~726ms max per individual SFX

# SFX assignments - chosen for thematic fit:
#
# CONFIRM (menu select, ready up):
#   kenney_ui click3.ogg - crisp, satisfying UI confirmation click
#
# CANCEL (menu back, unready):
#   cardsounds error.wav - clear "nope" feedback sound from a card game
#
# CARD_REVEAL (showing a card / calling a bluff):
#   kenney_casino card-slide-1.ogg - physical card sliding sound
#
# COINS (income, foreign aid, tax actions):
#   coin_drop.wav - actual coin clinking sound
#
# CHALLENGE (challenging another player's claim):
#   alert.wav - dramatic alert notification, fits the tension of a challenge
#
# ELIMINATED (player loses last influence):
#   game_over.wav - ominous defeat/game-over tone
#
# VICTORY (winning the game):
#   win_sound.wav - triumphant positive success sound
#
# TURN_START (your turn begins):
#   bell.wav - gentle bell chime notification

SFX_SOURCES = [
    # (sfx_name, file_path, max_duration_sec, gain)
    # Budget: ~40960 samples total across all 8 SFX
    ("CONFIRM",     "kenney_ui/Audio/click3.ogg",            0.3,  1.0),  # ~3300 samp
    ("CANCEL",      "cardsounds/cockatrice/error.wav",       0.3,  1.0),  # ~3300 samp
    ("CARD_REVEAL", "kenney_casino/Audio/card-slide-1.ogg",  0.5,  1.2),  # ~5500 samp
    ("COINS",       "coin_drop.wav",                         0.5,  1.0),  # ~5500 samp
    ("CHALLENGE",   "alert.wav",                             0.6,  1.0),  # ~6600 samp
    ("ELIMINATED",  "game_over.wav",                         0.6,  0.8),  # ~6600 samp
    ("VICTORY",     "win_sound.wav",                         0.6,  0.7),  # ~6600 samp
    ("TURN_START",  "bell.wav",                              0.35, 1.0),  # ~3800 samp
]

def resample_simple(data, src_rate, dst_rate):
    """Simple linear-interpolation resample."""
    if src_rate == dst_rate:
        return data
    ratio = dst_rate / src_rate
    out_len = int(len(data) * ratio)
    out = np.zeros(out_len, dtype=np.float64)
    for i in range(out_len):
        src_pos = i / ratio
        idx = int(src_pos)
        frac = src_pos - idx
        if idx + 1 < len(data):
            out[i] = data[idx] * (1.0 - frac) + data[idx + 1] * frac
        elif idx < len(data):
            out[i] = data[idx]
    return out

def load_and_convert(filepath, max_dur, gain):
    """Load audio file, convert to mono 16-bit PCM at TARGET_RATE."""
    data, sr = sf.read(filepath, dtype='float64')

    # Convert to mono if stereo
    if len(data.shape) > 1:
        data = np.mean(data, axis=1)

    # Trim to max duration at source rate
    max_samples_src = int(max_dur * sr)
    if len(data) > max_samples_src:
        data = data[:max_samples_src]

    # Resample to target rate
    if sr != TARGET_RATE:
        data = resample_simple(data, sr, TARGET_RATE)

    # Apply gain
    data = data * gain

    # Trim to absolute max
    if len(data) > MAX_SAMPLES:
        data = data[:MAX_SAMPLES]

    # Fade out last 5% to avoid clicks
    fade_len = max(1, len(data) // 20)
    fade = np.linspace(1.0, 0.0, fade_len)
    data[-fade_len:] *= fade

    # Normalize to avoid clipping, then convert to int16
    peak = np.max(np.abs(data))
    if peak > 0:
        data = data / peak * 0.95  # leave 5% headroom

    pcm = np.clip(data * 32767, -32768, 32767).astype(np.int16)
    return pcm

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))

    sfx_data = []

    for name, relpath, max_dur, gain in SFX_SOURCES:
        filepath = os.path.join(base_dir, relpath)
        if not os.path.exists(filepath):
            print(f"ERROR: {filepath} not found!", file=sys.stderr)
            sys.exit(1)

        pcm = load_and_convert(filepath, max_dur, gain)
        sfx_data.append((name, pcm))
        print(f"  {name:15s}: {len(pcm):6d} samples ({len(pcm)/TARGET_RATE*1000:.0f}ms) from {relpath}")

    # Calculate total size and check budget
    total_samples = sum(len(pcm) for _, pcm in sfx_data)
    total_bytes = total_samples * 2
    print(f"\n  Total PCM data: {total_bytes:,} bytes ({total_bytes/1024:.1f} KB)")
    print(f"  Total samples:  {total_samples:,} / {MAX_TOTAL_SAMPLES:,} budget")
    if total_samples > MAX_TOTAL_SAMPLES:
        print(f"  ERROR: Exceeds Sound RAM budget by {(total_samples - MAX_TOTAL_SAMPLES) * 2:,} bytes!", file=sys.stderr)
        sys.exit(1)
    print(f"  Remaining:      {(MAX_TOTAL_SAMPLES - total_samples) * 2:,} bytes")

    # Write C header
    out_path = os.path.join(base_dir, "..", "coup_sfx_data.h")
    with open(out_path, 'w') as f:
        f.write("/**\n")
        f.write(" * coup_sfx_data.h - Embedded PCM sound effect data\n")
        f.write(" *\n")
        f.write(" * Auto-generated from real audio samples (CC0 licensed).\n")
        f.write(" * Sources:\n")
        f.write(" *   - Kenney.nl UI Audio & Casino Audio (CC0)\n")
        f.write(" *   - OpenGameArt.org (CC0): coin_drop, bell, alert, win_sound, game_over\n")
        f.write(" *   - Cockatrice Card Game Sounds (CC0)\n")
        f.write(" *\n")
        f.write(f" * All samples: {TARGET_RATE} Hz, 16-bit signed, mono\n")
        f.write(f" * Total size: {total_bytes:,} bytes\n")
        f.write(" */\n\n")
        f.write("#ifndef COUP_SFX_DATA_H\n")
        f.write("#define COUP_SFX_DATA_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write(f"#define SFX_SAMPLE_RATE {TARGET_RATE}\n\n")

        # Write sample count array
        f.write("/* Sample counts for each SFX */\n")
        f.write(f"static const uint16_t sfx_pcm_counts[{len(sfx_data)}] = {{\n")
        for i, (name, pcm) in enumerate(sfx_data):
            comma = "," if i < len(sfx_data) - 1 else ""
            f.write(f"    {len(pcm):6d}{comma}  /* {name}: {len(pcm)/TARGET_RATE*1000:.0f}ms */\n")
        f.write("};\n\n")

        # Write each PCM array
        for name, pcm in sfx_data:
            f.write(f"/* {name} - {len(pcm)} samples ({len(pcm)/TARGET_RATE*1000:.0f}ms) */\n")
            f.write(f"static const int16_t sfx_pcm_{name.lower()}[{len(pcm)}] = {{\n")

            # Write 12 values per line
            for i in range(0, len(pcm), 12):
                chunk = pcm[i:i+12]
                vals = ", ".join(f"{v:6d}" for v in chunk)
                if i + 12 < len(pcm):
                    f.write(f"    {vals},\n")
                else:
                    f.write(f"    {vals}\n")
            f.write("};\n\n")

        # Write pointer array for easy indexing
        f.write("/* Pointer array for indexed access */\n")
        f.write(f"static const int16_t* const sfx_pcm_ptrs[{len(sfx_data)}] = {{\n")
        for i, (name, _) in enumerate(sfx_data):
            comma = "," if i < len(sfx_data) - 1 else ""
            f.write(f"    sfx_pcm_{name.lower()}{comma}\n")
        f.write("};\n\n")

        f.write("#endif /* COUP_SFX_DATA_H */\n")

    print(f"\n  Wrote: {out_path}")

if __name__ == "__main__":
    main()
