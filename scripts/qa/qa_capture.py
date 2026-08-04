#!/usr/bin/env python3
"""
qa_capture.py - Boot a Saturn disc in Mednafen, record it, extract PNG frames.

Every capture is validated before it is trusted. A contended host or a failed
boot produces frames that look plausible in isolation but mean nothing; the
Saturn skill records this failure mode explicitly (gotcha #3), so a capture
that is too short or completely static is rejected rather than measured.

Usage:
  python3 scripts/qa/qa_capture.py <game.cue> <out_dir> [--seconds 20]
"""

import argparse
import glob
import os
import subprocess
import sys

from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_config as cfg  # noqa: E402


def _stop_gracefully(proc):
    """Ask Mednafen to quit so it finalizes the QuickTime recording.

    MEASURED: force-killing the process (Popen.terminate, which is
    TerminateProcess on Windows) leaves the .mov without its moov atom, and
    ffmpeg then rejects the whole file - "moov atom not found" - even though
    hundreds of MB of frames were written. On Windows, taskkill WITHOUT /F
    posts WM_CLOSE to the process window, which Mednafen handles as a clean
    quit and which flushes the index.
    """
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(proc.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        proc.terminate()


def _record(cue_path, mov_path, seconds):
    """Run Mednafen for `seconds`, recording to mov_path, then stop it."""
    proc = subprocess.Popen(
        [cfg.MEDNAFEN,
         "-force_module", "ss",
         # Record at the Saturn's native raster. Without these the user's
         # config pixel-doubles anything narrower than 384 and applies aspect
         # correction, so frames arrive at a scaled, non-320x224 geometry that
         # the pixel gates cannot address by coordinate.
         "-qtrecord.w_double_threshold", "0",
         "-qtrecord.h_double_threshold", "0",
         "-ss.correct_aspect", "0",
         "-ss.stretch", "0",
         "-ss.videoip", "0",
         "-qtrecord", mov_path,
         cue_path],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        out, _ = proc.communicate(timeout=seconds)
        return out or ""
    except subprocess.TimeoutExpired:
        _stop_gracefully(proc)
        try:
            out, _ = proc.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()          # last resort; recording will be unusable
            out, _ = proc.communicate()
        return out or ""


def _extract(mov_path, out_dir, fps=2):
    """Extract frames, normalised to the Saturn's native 320x224.

    Mednafen records into a fixed 704x480 canvas (its maximum Saturn raster)
    and scales the active display to fill it. Downscaling back to 320x224 here
    means gate coordinates are game coordinates - a seat panel the renderer
    draws at (10,10,104,44) is measurable at (10,10,104,44) in the capture.
    """
    subprocess.run(
        [cfg.FFMPEG, "-y", "-i", mov_path,
         "-vf", f"fps={fps},scale={cfg.SCREEN_W}:{cfg.SCREEN_H}:flags=area",
         os.path.join(out_dir, "frame-%04d.png")],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    return sorted(glob.glob(os.path.join(out_dir, "frame-*.png")))


def validate(frames):
    """Reject captures that cannot support a verdict."""
    if len(frames) < 4:
        raise RuntimeError(
            f"capture produced only {len(frames)} frames - the emulator "
            "likely failed to boot, or the host was too loaded to run it")

    first = Image.open(frames[0]).convert("RGB")
    for f in frames[1:]:
        img = Image.open(f).convert("RGB")
        if ImageChops.difference(first, img).getbbox() is not None:
            return frames

    raise RuntimeError(
        "every captured frame is identical - the emulator is frozen or showing "
        "a static error screen, not running the game")


def capture(cue_path, out_dir, seconds=20):
    cfg.require_tools()
    cfg.require_bios()

    os.makedirs(out_dir, exist_ok=True)
    mov_path = os.path.abspath(os.path.join(out_dir, "capture.mov"))

    log = _record(os.path.abspath(cue_path), mov_path, seconds)
    if not os.path.exists(mov_path):
        raise RuntimeError(
            "Mednafen produced no recording at %s\n--- emulator output ---\n%s"
            % (mov_path, log[-2000:]))

    frames = validate(_extract(mov_path, out_dir))

    # Mednafen records uncompressed: ~8 MB per second of Saturn video. Keep the
    # extracted PNGs, drop the recording.
    try:
        os.remove(mov_path)
    except OSError:
        pass

    return frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cue_path")
    ap.add_argument("out_dir")
    ap.add_argument("--seconds", type=int, default=20)
    args = ap.parse_args()

    frames = capture(args.cue_path, args.out_dir, args.seconds)
    print(f"captured {len(frames)} frames -> {args.out_dir}")
    for f in frames[:3]:
        print(f"  {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
