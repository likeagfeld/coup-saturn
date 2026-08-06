#!/usr/bin/env python3
"""
build_web_sfx.py - Web sound-effect pipeline for web-staging/

Turns the 21 CC0 placeholder WAVs in `examples/coup/assets/sfx_src/` - the ones
the Saturn build embeds - into browser-playable audio under
`web-staging/assets/sfx/`, plus a provenance manifest.

WHY THIS EXISTS RATHER THAN A HAND-COPY

  * The two clients must sound like the SAME GAME. That is only true if the web
    plays the same samples, cut the same way. So the MANIFEST and the trimming
    DSP are IMPORTED from examples/coup/assets/convert_sfx.py rather than
    restated here. There is one list of sounds and one set of envelope rules;
    a change to either lands on both platforms or on neither.

  * ... but NOT the resampling. convert_sfx.py drops each effect to 11025 or
    5512 Hz because Sound RAM leaves the Saturn 81,920 bytes for the whole set
    (coup_audio.c SFX_BASE_OFFSET). That is a storage constraint, not a
    decision about how the sound should be. The browser has no such budget, so
    the web keeps the 44,100 Hz source. Same sample, same envelope, same
    length, same relative pitch - just not band-limited to 2.7 kHz for a
    hardware limit that does not exist here.

  * WAV is not shippable. 4.6 s of 16-bit mono 44.1 kHz is ~400 KB; the
    encoded set below is a fraction of that.

FORMATS, AND WHY BOTH

  Opus-in-WebM at 48 kbit/s mono is the smallest thing that sounds right on
  material this short, and Chrome, Edge and Firefox decode it through
  decodeAudioData without complaint.

  Safari is the reason for the second format. Its Opus-in-WebM support arrived
  late and is still uneven across the iOS versions people actually run, and the
  failure mode is not a fallback - it is decodeAudioData rejecting and the game
  going permanently silent on iPhones while sounding fine on every machine a
  developer tests on. AAC-in-MP4 is decodable by every browser that can run
  this client's ES modules.

  Shipping both costs ~90 KB. sfx.js picks ONE at load time and fetches only
  that, so no user downloads both. Against the ~8.8 MB of art already in
  web-staging/assets, the choice not to gamble on Safari is cheap.

LICENSING
  Every sample is CC0 (Kenney.nl). The per-file provenance - pack, file inside
  the pack, author, source URL, licence - is copied from convert_sfx.py's
  MANIFEST into assets/sfx/manifest.json, so it survives these placeholders
  being replaced by the team's custom audio. scripts/qa/qa_web_sfx.py fails if
  any entry loses it or is not CC0.

Run:  python scripts/build_web_sfx.py
Idempotent; safe to re-run. Writes only under web-staging/assets/sfx/.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import wave

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "examples", "coup", "assets")
SRC = os.path.join(ASSETS, "sfx_src")
OUT = os.path.join(REPO, "web-staging", "assets", "sfx")

sys.path.insert(0, ASSETS)
try:
    import numpy as np
except ImportError:
    sys.exit("numpy is required:  pip install numpy")

try:
    import convert_sfx
except ImportError as exc:                                   # noqa: BLE001
    sys.exit(f"cannot import examples/coup/assets/convert_sfx.py: {exc}")

# The web keeps the source rate. See the module docstring.
WEB_RATE = 44100

# Opus: 48k mono VBR. Below ~40k these transients start to smear; above ~64k
# nothing changes on material under half a second.
OPUS_BITRATE = "48k"
# AAC: 64k mono. AAC needs more bits than Opus for the same result at this
# length, and this is the fallback, not the common case.
AAC_BITRATE = "64k"


def encode(pcm_path, out_path, args):
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", pcm_path]
        + args + [out_path],
        check=True,
    )


def write_wav(path, pcm, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def prepare(entry):
    """Trim / cap / fade / normalise exactly as the Saturn build does.

    Everything here is convert_sfx.py's, called rather than copied. The one
    step deliberately skipped is resample() - see the module docstring.
    """
    path = os.path.join(SRC, entry["name"] + ".wav")
    if not os.path.exists(path):
        sys.exit(f"missing source WAV: {path}")

    a, rate = convert_sfx.load_wav(path)
    raw_ms = a.size * 1000.0 / rate

    a, _lead, _tail = convert_sfx.trim(a)
    if a.size == 0:
        sys.exit(f"{entry['name']}: source is silent")

    cap = int(round(entry["max_ms"] * rate / 1000.0))
    capped = a.size > cap
    if capped:
        a = a[:cap]

    a = convert_sfx.fade_out(a, rate)

    peak = float(np.abs(a).max())
    if peak > 0:
        a = a * (convert_sfx.NORMALISE_PEAK / peak)

    pcm = np.clip(np.round(a * 32767.0), -32768, 32767).astype(np.int16)
    return pcm, rate, raw_ms, capped


def main():
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg is required and was not found on PATH")
    os.makedirs(OUT, exist_ok=True)

    effects = []
    total_src = 0
    per_ext = {}

    print(f"{'effect':<16} {'source':>9} {'web':>8}  {'webm':>7} {'m4a':>7}")
    print("-" * 56)

    with tempfile.TemporaryDirectory() as tmp:
        for idx, entry in enumerate(convert_sfx.MANIFEST):
            name = entry["name"]
            pcm, rate, raw_ms, capped = prepare(entry)
            ms = pcm.size * 1000.0 / rate

            wav = os.path.join(tmp, name + ".wav")
            write_wav(wav, pcm, rate)

            webm = os.path.join(OUT, name + ".webm")
            m4a = os.path.join(OUT, name + ".m4a")
            encode(wav, webm, ["-c:a", "libopus", "-b:a", OPUS_BITRATE,
                               "-vbr", "on", "-application", "audio",
                               "-ac", "1"])
            encode(wav, m4a, ["-c:a", "aac", "-b:a", AAC_BITRATE, "-ac", "1",
                              "-movflags", "+faststart"])

            src_bytes = os.path.getsize(os.path.join(SRC, name + ".wav"))
            total_src += src_bytes
            for p in (webm, m4a):
                e = os.path.splitext(p)[1]
                per_ext[e] = per_ext.get(e, 0) + os.path.getsize(p)

            print(f"{name:<16} {raw_ms:>7.0f}ms {ms:>6.0f}ms  "
                  f"{os.path.getsize(webm):>7,} {os.path.getsize(m4a):>7,}"
                  f"{'  (capped)' if capped else ''}")

            effects.append({
                "id": idx,
                "name": name,
                "ms": round(ms, 1),
                "rate": rate,
                "character": entry["character"],
                "pack": entry["pack"],
                "file": entry["file"],
                "author": entry["author"],
                "url": entry["url"],
                "licence": entry["licence"],
                "source_wav": f"examples/coup/assets/sfx_src/{name}.wav",
            })

    manifest = {
        "_comment": (
            "Provenance for every web sound effect. Generated by "
            "scripts/build_web_sfx.py from the same MANIFEST the Saturn build "
            "uses (examples/coup/assets/convert_sfx.py), so the two clients "
            "cannot disagree about what a sound is or where it came from. "
            "PLACEHOLDER AUDIO: replacing one is a data change - drop the new "
            "WAV over examples/coup/assets/sfx_src/<name>.wav, update its "
            "MANIFEST provenance, re-run both converters. Paths are relative "
            "to this file; never absolute, because an absolute /assets/ URL "
            "resolves against the LIVE site from a /staging/ page."
        ),
        "generator": "scripts/build_web_sfx.py",
        "source_rate_hz": WEB_RATE,
        "formats": [
            {"ext": ".webm", "mime": 'audio/webm; codecs="opus"',
             "codec": "Opus", "bitrate": OPUS_BITRATE},
            {"ext": ".m4a", "mime": 'audio/mp4; codecs="mp4a.40.2"',
             "codec": "AAC-LC", "bitrate": AAC_BITRATE},
        ],
        "licence_summary": (
            "All effects are Creative Commons Zero (CC0 1.0), by Kenney "
            "Vleugels (kenney.nl). Free for personal and commercial use; "
            "credit appreciated, not required. Pack licence text is in "
            "LICENSE-kenney.txt beside this file."
        ),
        "effects": effects,
    }
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    shutil.copyfile(os.path.join(SRC, "LICENSE-kenney.txt"),
                    os.path.join(OUT, "LICENSE-kenney.txt"))

    total = sum(os.path.getsize(os.path.join(OUT, f))
                for f in os.listdir(OUT))
    print("-" * 56)
    print(f"  {len(effects)} effects")
    print(f"  source WAVs   {total_src:>9,} bytes")
    for e in sorted(per_ext):
        print(f"  {e:<13} {per_ext[e]:>9,} bytes "
              f"({100.0 * per_ext[e] / total_src:.1f}% of source)")
    print(f"  written       {total:>9,} bytes into "
          f"{os.path.relpath(OUT, REPO)}")
    print(f"  a visitor downloads ONE format: "
          f"{min(per_ext.values()):,}-{max(per_ext.values()):,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
