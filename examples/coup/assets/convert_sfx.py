#!/usr/bin/env python3
"""
convert_sfx.py - Convert source WAVs into the embedded SCSP PCM headers.

Emits two headers, deliberately split the same way convert_backgrounds.py
splits its index from its pixel data:

  coup_sfx_ids.h    the COUP_SFX_* ids, the count and the byte totals.
                    Included by coup.h, so EVERY translation unit sees it -
                    which is exactly why it must carry no sample data.
  coup_sfx_data.h   the PCM arrays and the pointer table. Included by
                    coup_audio.c and the host budget test, and by nothing
                    else: `static const` arrays in a header are duplicated
                    once per translation unit, so a second includer would
                    silently double the ~78 KB of waveform in the binary.

Output format, dictated by the SCSP and by coup_audio.c:
  - signed 16-bit mono, big-endian at run time (the SH-2 is big-endian and
    the arrays are emitted as C integers, so the compiler does the swapping)
  - one of two sample rates, chosen per sample by MEASUREMENT (see
    LOW_RATE_HF_LIMIT below), because the SCSP retunes each slot
    independently via OCT/FNS - a mixed-rate set costs nothing
  - laid out consecutively from SFX_BASE_OFFSET in Sound RAM

THE BUDGET IS THE HARD PART. Sound RAM is 524,288 bytes and coup_audio.c
puts the SFX region at 0x6C000 = 442,368, leaving 81,920 bytes. Nothing
bounds-checks the upload at run time: sfx_upload_to_sound_ram() writes
straight past the end of the region if the set is too big, over whatever
the M68K driver has there. So this script REFUSES to emit an over-budget
set rather than emitting one that corrupts Sound RAM on the console.

Licensing: this repo is public. Every entry in MANIFEST carries the source
URL, pack, author and licence as stated on the source page, and all of it
is copied into the generated header. CC0 / public domain only.

Usage:
  python3 convert_sfx.py --src-dir sfx_src \
      --out-ids ../coup_sfx_ids.h --out-data ../coup_sfx_data.h
"""

import argparse
import os
import sys
import wave

import numpy as np

# ---------------------------------------------------------------------------
# Hardware constants. Every one of these is dictated by the Saturn, not chosen.
# ---------------------------------------------------------------------------

# The SCSP's playback clock. Every slot rate is 44100 * 2^OCT * (1 + FNS/1024),
# so the useful sample rates are 44100 over a power of two.
SCSP_BASE_RATE = 44100

# 44100/4. The default for anything with real content above 2.7 kHz.
RATE_HIGH = SCSP_BASE_RATE // 4          # 11025

# 44100/8. Halves the storage of a sample that has nothing up there anyway.
RATE_LOW = SCSP_BASE_RATE // 8           # 5512

# Sound RAM total, and where coup_audio.c parks the SFX region.
SOUND_RAM_BYTES = 512 * 1024             # 524,288
SFX_BASE_OFFSET = 0x6C000                # 442,368  (coup_audio.c:64)
SFX_BUDGET_BYTES = SOUND_RAM_BYTES - SFX_BASE_OFFSET   # 81,920

# The SCSP slot's loop-end register (LEA) is 16 bits, so a single sample can
# be at most 65535 words long. Nothing here comes near it, but a future
# replacement WAV could, and the failure mode is a truncated or runaway slot.
SCSP_MAX_SAMPLES = 0xFFFF

# ---------------------------------------------------------------------------
# Conversion constants, with the reasoning that set them.
# ---------------------------------------------------------------------------

# Fraction of a sample's spectral energy that has to sit above RATE_LOW's
# Nyquist (2756 Hz) before it is worth paying double to store it at 11025.
#
# The plan's lever 2 is "a d-pad tick carries no content above ~4 kHz, so
# 11025 -> 5512 halves those files with no audible loss". Rather than guess
# which sounds those are, this MEASURES each one: several of the tonal cues
# turn out to be far more low-passed than the UI clicks are. On the shipped
# set, ui_confirm has 0.1% of its energy above 2756 Hz and coup_strike has
# 0.4%, while ui_move - the archetypal "pure transient" the plan expected to
# downsample - has 77%, and downsampling it WOULD have been audible.
LOW_RATE_HF_LIMIT = 0.15

# Anything quieter than this fraction of the sample's peak, at either end, is
# lead-in or decay tail and is cut. The current shipped set averages 0.44 s
# per effect and a large part of that is inaudible tail sitting in Sound RAM.
TRIM_FLOOR = 0.008

# Cutting a decaying tail dead leaves a click, which on a 40 ms UI blip is
# louder than the blip. Fade the last few ms instead.
FADE_OUT_MS = 8.0

# Peak-normalise here rather than in the sound driver: the SCSP's TL register
# is the player's volume control and must not be spent making up for a quiet
# source. Not 1.0 - the resampler's interpolation can overshoot the input
# peak slightly, and a wrapped sample is a loud crack.
NORMALISE_PEAK = 0.96


# ---------------------------------------------------------------------------
# The set. Ordered: this list IS the COUP_SFX_* id order.
#
# Every entry records the source page URL, the pack, the author and the
# licence exactly as the source page states it, plus the file inside the pack
# so a replacement can be traced back. `character` is the plan's description
# of what the sound has to convey; `max_ms` is the cap this placeholder is
# trimmed to.
#
# The whole point of this table is that swapping in the team's custom audio
# is a data change: drop a new WAV over sfx_src/<name>.wav, update the
# provenance fields, re-run. No C changes, no enum changes, no call sites.
# ---------------------------------------------------------------------------

KENNEY = "Kenney Vleugels (Kenney.nl)"
CC0 = "Creative Commons Zero (CC0 1.0) - as stated on the asset page and in the pack's License.txt"

MANIFEST = [
    # --- Menu ------------------------------------------------------------
    dict(name="ui_move", max_ms=60,
         character="short dry tick, no tail - d-pad movement",
         pack="Interface Sounds", file="Audio/tick_002.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),
    dict(name="ui_confirm", max_ms=190,
         character="rising two-tone, affirmative - A / Start",
         pack="Interface Sounds", file="Audio/confirmation_001.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),
    dict(name="ui_cancel", max_ms=110,
         character="falling, the confirm inverted - B / back",
         pack="Interface Sounds", file="Audio/minimize_001.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),
    dict(name="ui_challenge", max_ms=150,
         character="sharper and more urgent than confirm - C shortcut",
         pack="Interface Sounds", file="Audio/error_005.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),

    # --- Cards and coins --------------------------------------------------
    dict(name="card_deal", max_ms=150,
         character="one paper slide - a card dealt",
         pack="Casino Audio", file="Audio/card-slide-1.ogg", author=KENNEY,
         url="https://kenney.nl/assets/casino-audio", licence=CC0),
    dict(name="deck_place", max_ms=170,
         character="heavier, a stack landing - the court deck",
         pack="Casino Audio", file="Audio/card-place-1.ogg", author=KENNEY,
         url="https://kenney.nl/assets/casino-audio", licence=CC0),
    dict(name="card_reveal", max_ms=170,
         character="a card turned face up, with weight",
         pack="Casino Audio", file="Audio/card-place-2.ogg", author=KENNEY,
         url="https://kenney.nl/assets/casino-audio", licence=CC0),
    dict(name="card_shuffle", max_ms=320,
         character="cards moving, riffled - Ambassador exchange",
         pack="Casino Audio", file="Audio/card-shuffle.ogg", author=KENNEY,
         url="https://kenney.nl/assets/casino-audio", licence=CC0),
    dict(name="coin_gain", max_ms=170,
         character="bright metal, upward - income, foreign aid, tax",
         pack="RPG Audio", file="Audio/handleCoins2.ogg", author=KENNEY,
         url="https://kenney.nl/assets/rpg-audio", licence=CC0),
    dict(name="coin_spend", max_ms=130,
         character="duller, downward - paying out",
         pack="Casino Audio", file="Audio/chip-lay-1.ogg", author=KENNEY,
         url="https://kenney.nl/assets/casino-audio", licence=CC0),

    # --- General actions --------------------------------------------------
    dict(name="coup_strike", max_ms=280,
         character="heavy and final - the most expensive act in the game",
         pack="Impact Sounds", file="Audio/impactWood_heavy_002.ogg", author=KENNEY,
         url="https://kenney.nl/assets/impact-sounds", licence=CC0),

    # --- Per-character, played through coup_audio_play_sfx_as() -----------
    dict(name="turn_start", max_ms=140,
         character="short low chime, expectant - whose turn it is",
         pack="Interface Sounds", file="Audio/bong_001.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),
    dict(name="influence_lost", max_ms=330,
         character="descending, damaged - an influence is gone",
         pack="Interface Sounds", file="Audio/minimize_006.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),
    dict(name="exiled", max_ms=380,
         character="terminal, lower and longer than losing influence",
         pack="Impact Sounds", file="Audio/impactBell_heavy_002.ogg", author=KENNEY,
         url="https://kenney.nl/assets/impact-sounds", licence=CC0),
    dict(name="challenge", max_ms=220,
         character="sharp, confrontational, rising",
         pack="Digital Audio", file="Audio/phaserUp5.ogg", author=KENNEY,
         url="https://kenney.nl/assets/digital-audio", licence=CC0),
    dict(name="counter", max_ms=210,
         character="firm, closing - a door shutting",
         pack="RPG Audio", file="Audio/doorClose_1.ogg", author=KENNEY,
         url="https://kenney.nl/assets/rpg-audio", licence=CC0),
    dict(name="act_success", max_ms=240,
         character="resolved, upward",
         pack="Interface Sounds", file="Audio/confirmation_004.ogg", author=KENNEY,
         url="https://kenney.nl/assets/interface-sounds", licence=CC0),
    dict(name="act_fail", max_ms=240,
         character="deflating, downward",
         pack="Digital Audio", file="Audio/highDown.ogg", author=KENNEY,
         url="https://kenney.nl/assets/digital-audio", licence=CC0),

    # --- Character abilities ---------------------------------------------
    dict(name="assassinate", max_ms=230,
         character="a blade - quiet, then sharp",
         pack="RPG Audio", file="Audio/knifeSlice.ogg", author=KENNEY,
         url="https://kenney.nl/assets/rpg-audio", licence=CC0),
    dict(name="steal", max_ms=170,
         character="a snatch - coin plus a quick drag",
         pack="Casino Audio", file="Audio/chips-handle-3.ogg", author=KENNEY,
         url="https://kenney.nl/assets/casino-audio", licence=CC0),

    # --- End of game ------------------------------------------------------
    # NOT in the plan's tables, which omit victory entirely even though
    # coup_game.c has played one since before this work. Dropping it to match
    # the plan would have made the game QUIETER at its single biggest moment.
    dict(name="victory", max_ms=380,
         character="a three-note figure - the game is won",
         pack="Digital Audio", file="Audio/threeTone1.ogg", author=KENNEY,
         url="https://kenney.nl/assets/digital-audio", licence=CC0),
]


# ---------------------------------------------------------------------------
# WAV loading and DSP
# ---------------------------------------------------------------------------

def load_wav(path):
    """Read a WAV into float mono in [-1, 1], with its sample rate."""
    with wave.open(path, "rb") as w:
        nch = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())

    if width == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 1:
        a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    else:
        raise SystemExit(f"{path}: {width * 8}-bit WAV not supported, "
                         f"use 8- or 16-bit PCM")

    if nch > 1:
        a = a.reshape(-1, nch).mean(axis=1)
    return a, rate


def hf_energy_fraction(a, rate, cutoff):
    """Share of spectral magnitude above `cutoff`.

    Drives the per-sample rate choice. Magnitude rather than power, because
    what matters is whether the top octave is AUDIBLE, and loudness tracks
    magnitude far better than it tracks energy.
    """
    if a.size < 64:
        return 1.0
    sp = np.abs(np.fft.rfft(a * np.hanning(a.size)))
    fr = np.fft.rfftfreq(a.size, 1.0 / rate)
    total = sp.sum()
    if total <= 0:
        return 0.0
    return float(sp[fr > cutoff].sum() / total)


def trim(a, floor=TRIM_FLOOR):
    """Cut lead-in and decay tail below `floor` of the peak."""
    peak = float(np.abs(a).max()) if a.size else 0.0
    if peak <= 0:
        return a, 0, 0
    idx = np.nonzero(np.abs(a) > peak * floor)[0]
    if idx.size == 0:
        return a, 0, 0
    return a[idx[0]:idx[-1] + 1], int(idx[0]), int(a.size - 1 - idx[-1])


def resample(a, src_rate, dst_rate):
    """Anti-aliased rate conversion.

    A windowed-sinc low-pass followed by linear interpolation. Not the
    sharpest resampler in existence, but the destination is an 11 kHz
    one-shot on a 1994 console - the low-pass is what matters, because
    decimating a 44.1 kHz card slide without it folds its 8 kHz hiss back
    down into the audible band as a metallic whistle.
    """
    if src_rate == dst_rate:
        return a
    if dst_rate < src_rate:
        # 0.45 * dst_rate, not 0.5: leave the filter a transition band that
        # fits below Nyquist instead of straddling it.
        cutoff = 0.45 * dst_rate / src_rate          # cycles/sample
        taps = 63
        n = np.arange(taps) - (taps - 1) / 2.0
        h = np.sinc(2 * cutoff * n) * np.blackman(taps)
        h /= h.sum()
        a = np.convolve(a, h, mode="same")

    n_out = int(round(a.size * dst_rate / float(src_rate)))
    if n_out < 1:
        n_out = 1
    x = np.arange(n_out) * (float(src_rate) / dst_rate)
    return np.interp(x, np.arange(a.size), a)


def fade_out(a, rate, ms=FADE_OUT_MS):
    """Taper the last `ms` so a hard cut cannot click."""
    n = min(a.size, max(1, int(rate * ms / 1000.0)))
    if n < 2:
        return a
    a = a.copy()
    a[-n:] *= np.linspace(1.0, 0.0, n)
    return a


def convert_one(entry, src_dir):
    """Return a dict describing one converted sample."""
    path = os.path.join(src_dir, entry["name"] + ".wav")
    if not os.path.exists(path):
        raise SystemExit(f"missing source WAV: {path}")

    a, rate = load_wav(path)
    raw_ms = a.size * 1000.0 / rate

    a, _lead, _tail = trim(a)
    if a.size == 0:
        raise SystemExit(f"{entry['name']}: source is silent")
    trimmed_ms = a.size * 1000.0 / rate

    # Rate choice is made on the TRIMMED signal - a long silent lead-in would
    # otherwise drag the measured spectrum toward whatever noise is in it.
    hf = hf_energy_fraction(a, rate, RATE_LOW / 2.0)
    dst_rate = RATE_LOW if hf < LOW_RATE_HF_LIMIT else RATE_HIGH

    # Cap AFTER the trim, so max_ms budgets real audio and not leading silence.
    cap = int(round(entry["max_ms"] * rate / 1000.0))
    capped = a.size > cap
    peak_pos_ms = float(np.argmax(np.abs(a))) * 1000.0 / rate
    if capped:
        a = a[:cap]

    a = resample(a, rate, dst_rate)
    a = fade_out(a, dst_rate)

    peak = float(np.abs(a).max())
    if peak > 0:
        a = a * (NORMALISE_PEAK / peak)

    pcm = np.clip(np.round(a * 32767.0), -32768, 32767).astype(np.int16)

    # The SCSP reads two bytes per sample and coup_audio.c lays the effects
    # out consecutively from an even base, so an odd count would leave the
    # NEXT effect on an odd address. Keep every sample even-length.
    if pcm.size % 2:
        pcm = np.append(pcm, np.int16(0))

    if pcm.size > SCSP_MAX_SAMPLES:
        raise SystemExit(
            f"{entry['name']}: {pcm.size} samples exceeds the SCSP's 16-bit "
            f"LEA register ({SCSP_MAX_SAMPLES}); lower max_ms")

    return dict(
        entry,
        pcm=pcm,
        rate=dst_rate,
        ms=pcm.size * 1000.0 / dst_rate,
        raw_ms=raw_ms,
        trimmed_ms=trimmed_ms,
        hf=hf,
        capped=capped,
        cap_before_peak=capped and peak_pos_ms > entry["max_ms"],
        src_wav=os.path.relpath(path, os.path.dirname(src_dir)).replace("\\", "/"),
    )


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------

def provenance_block(samples, total_bytes):
    """The comment block that has to survive the placeholders it describes."""
    lines = []
    lines.append(" * Auto-generated by examples/coup/assets/convert_sfx.py.")
    lines.append(" * Do not edit - edit the WAVs in assets/sfx_src/ and re-run.")
    lines.append(" *")
    lines.append(" * PLACEHOLDER AUDIO. Every sample below is CC0 / public domain and")
    lines.append(" * is here so that no game action is silent until the team's own")
    lines.append(" * custom SFX land. Replacing one is a DATA change: drop the new WAV")
    lines.append(" * over assets/sfx_src/<name>.wav, update its MANIFEST provenance in")
    lines.append(" * convert_sfx.py, re-run. No C changes, no enum changes.")
    lines.append(" *")
    lines.append(f" * Signed 16-bit mono. {RATE_HIGH} Hz unless marked {RATE_LOW} Hz,")
    lines.append(" * which is used only where under "
                 f"{int(LOW_RATE_HF_LIMIT * 100)}% of the sample's energy sits")
    lines.append(f" * above {RATE_LOW // 2} Hz - the SCSP retunes each slot with its own")
    lines.append(" * OCT/FNS, so a mixed-rate set costs nothing to play back.")
    lines.append(" *")
    lines.append(f" * Total {total_bytes:,} bytes of {SFX_BUDGET_BYTES:,} available "
                 f"({100.0 * total_bytes / SFX_BUDGET_BYTES:.1f}%).")
    lines.append(f" * The budget is Sound RAM ({SOUND_RAM_BYTES:,}) minus "
                 f"SFX_BASE_OFFSET (0x{SFX_BASE_OFFSET:X}).")
    lines.append(" *")
    lines.append(" * Sources - CC0 / public domain only, this repo is public:")
    for s in samples:
        lines.append(" *")
        lines.append(f" *   {s['name']}  ({s['rate']} Hz, {s['pcm'].size} samples, "
                     f"{s['ms']:.0f} ms)")
        lines.append(f" *     character : {s['character']}")
        lines.append(f" *     pack      : {s['pack']} - {s['file']}")
        lines.append(f" *     author    : {s['author']}")
        lines.append(f" *     url       : {s['url']}")
        lines.append(f" *     licence   : {s['licence']}")
    lines.append(" *")
    lines.append(" * Pack licence texts as shipped: assets/sfx_src/LICENSE-kenney.txt")
    return "\n".join(lines)


def emit_ids(samples, total_bytes, out_path):
    guard = "COUP_SFX_IDS_H"
    with open(out_path, "w", newline="\n") as f:
        f.write("/**\n * coup_sfx_ids.h - Sound effect ids and budget figures.\n *\n")
        f.write(" * Auto-generated by examples/coup/assets/convert_sfx.py. "
                "Do not edit.\n *\n")
        f.write(" * Split out of coup_sfx_data.h on purpose: coup.h includes THIS\n")
        f.write(" * header so every call site can name an effect, while the PCM\n")
        f.write(" * arrays stay in coup_sfx_data.h. `static const` arrays in a header\n")
        f.write(" * are duplicated once per translation unit, so putting the waveform\n")
        f.write(" * data here would put a copy of it in every object file.\n */\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(f"#define COUP_SFX_COUNT {len(samples)}\n\n")
        f.write("/* Bytes the generated set occupies in Sound RAM, and the space\n"
                " * available to it. coup_audio.c static-asserts the first against\n"
                " * the second: sfx_upload_to_sound_ram() has no run-time bound and\n"
                " * would write straight over the sound driver's memory. */\n")
        f.write(f"#define COUP_SFX_TOTAL_BYTES  {total_bytes}\n")
        f.write(f"#define COUP_SFX_BUDGET_BYTES {SFX_BUDGET_BYTES}\n\n")
        f.write(f"#define COUP_SFX_RATE_HIGH {RATE_HIGH}\n")
        f.write(f"#define COUP_SFX_RATE_LOW  {RATE_LOW}\n\n")
        width = max(len(s["name"]) for s in samples) + len("COUP_SFX_")
        for i, s in enumerate(samples):
            macro = "COUP_SFX_" + s["name"].upper()
            f.write(f"#define {macro:<{width}} {i:<3}"
                    f" /* {s['character']} */\n")
        f.write("\n")
        f.write("/* Compatibility aliases for the pre-expansion 8-effect names.\n"
                " * Kept so an old call site cannot silently pick up a different\n"
                " * sound; new code should use the names above. */\n")
        f.write(f"#define COUP_SFX_CONFIRM     COUP_SFX_UI_CONFIRM\n")
        f.write(f"#define COUP_SFX_CANCEL      COUP_SFX_UI_CANCEL\n")
        f.write(f"#define COUP_SFX_COINS       COUP_SFX_COIN_GAIN\n")
        f.write(f"#define COUP_SFX_ELIMINATED  COUP_SFX_EXILED\n\n")
        f.write(f"#endif /* {guard} */\n")


def emit_data(samples, total_bytes, out_path):
    guard = "COUP_SFX_DATA_H"
    with open(out_path, "w", newline="\n") as f:
        f.write("/**\n * coup_sfx_data.h - Embedded PCM sound effect data\n *\n")
        f.write(provenance_block(samples, total_bytes))
        f.write("\n */\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write('#include "coup_sfx_ids.h"\n\n')

        f.write("/* Samples per effect. coup_audio.c writes count-1 into the SCSP\n"
                " * loop-end register, so this must be the exact length. */\n")
        f.write("static const uint16_t sfx_pcm_counts[COUP_SFX_COUNT] = {\n")
        for s in samples:
            f.write(f"    {s['pcm'].size:>6},  /* {s['name']}: "
                    f"{s['ms']:.0f} ms @ {s['rate']} Hz */\n")
        f.write("};\n\n")

        f.write("/* Authored rate of each effect, in Hz. Feeds the SCSP OCT/FNS\n"
                " * calculation, which is why a mixed-rate set plays correctly. */\n")
        f.write("static const uint16_t sfx_pcm_rates[COUP_SFX_COUNT] = {\n")
        for s in samples:
            f.write(f"    {s['rate']:>6},  /* {s['name']} */\n")
        f.write("};\n\n")

        for s in samples:
            pcm = s["pcm"]
            f.write(f"/* {s['name']} - {pcm.size} samples "
                    f"({s['ms']:.0f} ms @ {s['rate']} Hz)\n")
            f.write(f" * {s['character']}\n")
            f.write(f" * {s['pack']} / {s['file']} - {s['url']} (CC0) */\n")
            f.write(f"static const int16_t sfx_pcm_{s['name']}[{pcm.size}] = {{\n")
            for i in range(0, pcm.size, 12):
                row = ", ".join(f"{v:6d}" for v in pcm[i:i + 12])
                f.write(f"    {row},\n")
            f.write("};\n\n")

        f.write("static const int16_t* const sfx_pcm_ptrs[COUP_SFX_COUNT] = {\n")
        for s in samples:
            f.write(f"    sfx_pcm_{s['name']},\n")
        f.write("};\n\n")
        f.write(f"#endif /* {guard} */\n")


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Convert source WAVs into the Coup SCSP PCM headers.")
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--src-dir", default=os.path.join(here, "sfx_src"),
                    help="directory of <name>.wav source files")
    ap.add_argument("--out-ids", default=os.path.join(here, "..", "coup_sfx_ids.h"))
    ap.add_argument("--out-data", default=os.path.join(here, "..", "coup_sfx_data.h"))
    ap.add_argument("--dry-run", action="store_true",
                    help="measure and report, write nothing")
    args = ap.parse_args()

    samples = [convert_one(e, args.src_dir) for e in MANIFEST]

    total_bytes = sum(s["pcm"].size * 2 for s in samples)

    print(f"{'effect':<16} {'rate':>6} {'samples':>8} {'bytes':>7} {'ms':>6} "
          f"{'src ms':>7} {'>2.7k':>6}")
    for s in samples:
        flag = ""
        if s["capped"]:
            flag += " capped"
        if s["cap_before_peak"]:
            flag += " CAP-BEFORE-PEAK"
        print(f"{s['name']:<16} {s['rate']:>6} {s['pcm'].size:>8} "
              f"{s['pcm'].size * 2:>7} {s['ms']:>6.0f} {s['raw_ms']:>7.0f} "
              f"{s['hf'] * 100:>5.1f}%{flag}")

    # A cap that lands before the sample's loudest moment throws away the
    # sound's whole point. Worth shouting about, not worth failing over -
    # some sources genuinely swell in and the operator may have meant it.
    for s in samples:
        if s["cap_before_peak"]:
            print(f"   WARNING: {s['name']} is capped at {s['max_ms']} ms but "
                  f"peaks later than that", file=sys.stderr)

    pct = 100.0 * total_bytes / SFX_BUDGET_BYTES
    print()
    print(f"  effects        {len(samples)}")
    print(f"  total          {total_bytes:,} bytes")
    print(f"  budget         {SFX_BUDGET_BYTES:,} bytes "
          f"(Sound RAM {SOUND_RAM_BYTES:,} - SFX_BASE_OFFSET 0x{SFX_BASE_OFFSET:X})")
    print(f"  used           {pct:.1f}%")
    print(f"  free           {SFX_BUDGET_BYTES - total_bytes:,} bytes")

    if total_bytes > SFX_BUDGET_BYTES:
        print()
        print(f"FATAL: the set is {total_bytes - SFX_BUDGET_BYTES:,} bytes over "
              f"budget.", file=sys.stderr)
        print("sfx_upload_to_sound_ram() has no bound check - emitting this "
              "would overwrite\nSound RAM past the SFX region, which is the "
              "M68K sound driver's memory. Refusing.", file=sys.stderr)
        print("Lower max_ms on the longest effects, or drop one.", file=sys.stderr)
        return 1

    if args.dry_run:
        print("\n(dry run, nothing written)")
        return 0

    emit_ids(samples, total_bytes, args.out_ids)
    emit_data(samples, total_bytes, args.out_data)
    print()
    print(f"-> {args.out_ids}   ({os.path.getsize(args.out_ids):,} bytes)")
    print(f"-> {args.out_data}  ({os.path.getsize(args.out_data):,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
