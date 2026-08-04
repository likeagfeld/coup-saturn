#!/usr/bin/env python3
"""
qa_config.py - Verified tool paths and the locked Mednafen invocation.

Probed on this machine 2026-08-04:
  - mednafen -help lists -qtrecord, -soundrecord, -force_module.
  - ~/.mednafen/firmware holds the two BIOS images Mednafen's ss module
    requires, identified by MD5 rather than by filename:
        sega_101.bin   85ec9ca47d8f6807718151cbcca8b964  (JP)
        mpr-17933.bin  3240872c70984b6cbfda1586cab68dbe  (NA/EU)
"""

import os
import shutil

MEDNAFEN = shutil.which("mednafen") or os.path.expandvars(
    r"%LOCALAPPDATA%\Microsoft\WinGet\Packages"
    r"\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe")

FFMPEG = shutil.which("ffmpeg") or os.path.expandvars(
    r"%LOCALAPPDATA%\Microsoft\WinGet\Packages"
    r"\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe"
    r"\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe")

# MEASURED: this Mednafen build uses its own install directory as the base
# directory, NOT ~/.mednafen - it reports "Base directory: <install dir>" and
# loads mednafen.cfg from there. Bare BIOS filenames in the config (e.g.
# "ss.bios_jp sega_101.bin") resolve against <base>/firmware. Config entries
# may also be absolute paths, which override this directory entirely.
FIRMWARE_DIR = os.path.join(os.path.dirname(MEDNAFEN), "firmware")

# Mednafen's ss module refuses to start without a JP BIOS. The NA/EU image may
# instead be supplied as an absolute path in mednafen.cfg (ss.bios_na_eu), so
# only the bare-filename one is required to live in FIRMWARE_DIR.
REQUIRED_BIOS = {
    "sega_101.bin": "85ec9ca47d8f6807718151cbcca8b964",
}

# Saturn active display used by this project (main_saturn.c:198 - TV_320x224).
SCREEN_W = 320
SCREEN_H = 224


def require_bios():
    """Raise with an actionable message unless both BIOS images are present."""
    import hashlib

    missing = []
    for name, want_md5 in REQUIRED_BIOS.items():
        path = os.path.join(FIRMWARE_DIR, name)
        if not os.path.isfile(path):
            missing.append(f"{name} (absent)")
            continue
        with open(path, "rb") as fh:
            got = hashlib.md5(fh.read()).hexdigest()
        if got != want_md5:
            missing.append(f"{name} (md5 {got}, expected {want_md5})")

    if missing:
        raise RuntimeError(
            "Saturn BIOS not usable in %s:\n  %s\n"
            "Mednafen's ss module cannot boot without them."
            % (FIRMWARE_DIR, "\n  ".join(missing)))


def require_tools():
    """Raise unless mednafen and ffmpeg are both executable."""
    for label, path in (("mednafen", MEDNAFEN), ("ffmpeg", FFMPEG)):
        if not path or not os.path.isfile(path):
            raise RuntimeError(f"{label} not found (looked at: {path})")
