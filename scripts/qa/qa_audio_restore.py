#!/usr/bin/env python3
"""
qa_audio_restore.py - Prove the CD-DA restore path runs on a scene change.

WHY THIS GATE EXISTS
  There is exactly one CD pickup and it is exclusive. Streaming a backdrop
  issues CDC_CdPlay against the file's FAD range with CDC_PM_DFL, which
  replaces both the play range AND the endless-repeat mode - the music stops,
  and nothing in GFS, STM or SGL puts it back (SEGALIB/MAN/MANGFS.TXT:
  "CD-DA files and CD-ROM files cannot be accessed simultaneously").

  coup_audio_restore_music() re-issues playback AFTER each load. That fix was
  written from documentation and has never been HEARD: the shared RetroArch
  config on this machine is a headless capture rig with audio_driver "null",
  so every capture in this project was audio-disabled.

  This closes the half of the claim that can be measured without ears - that
  the path executes, on a real scene change, the expected number of times.
  It does NOT prove sound comes out of the console. Only a person can do that.

USAGE
  python scripts/qa/qa_audio_restore.py
"""
import os, re, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_retroarch as ra

MAGIC = 0x41554457
FMT = ">Iiiii"


def main():
    mapf = "examples/coup/saturn/_build/game.map"
    cue = "build/coup_game/game.cue"
    src = open(mapf, encoding="utf-8", errors="replace").read()
    m = re.search(r"0x([0-9a-fA-F]{8,16})\s+g_coup_audio_witness\b", src)
    if not m:
        print("GATE AUDIO RESTORE: RED - witness not linked in")
        return 1
    addr = int(m.group(1), 16)
    print(f"  g_coup_audio_witness at 0x{addr:08X}")

    ok, missing = ra.check_cue(cue)
    if not ok:
        print(f"GATE AUDIO RESTORE: RED - {cue} missing {missing}")
        return 1
    exe, core = ra.find_retroarch(), None
    core = ra.find_core(exe)
    if not exe or not core:
        print("GATE AUDIO RESTORE: RED - emulator or core missing")
        return 1

    cfg = ra.write_our_config()
    proc = subprocess.Popen([exe, "-L", core, "--appendconfig", cfg,
                             os.path.abspath(cue)],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.STDOUT)
    print(f"  spawned pid {proc.pid} on :{ra.PORT}")
    try:
        time.sleep(int(sys.argv[1]) if len(sys.argv) > 1 else 45)
        raw = ra.read_ram(ra.phys_to_offset(addr), struct.calcsize(FMT))
        if raw is None:
            print("GATE AUDIO RESTORE: INCONCLUSIVE - no response")
            return 2
        magic, calls, reissued, nr, np_ = struct.unpack(
            FMT, ra.unswap(bytes(raw)))
        if magic != MAGIC:
            print(f"  magic 0x{magic:08X}, expected 0x{MAGIC:08X}")
            print("GATE AUDIO RESTORE: RED - the restore path NEVER RAN. "
                  "Every backdrop load has silently killed the music.")
            return 1
        print(f"  restore called      {calls}")
        print(f"  playback re-issued  {reissued}")
        print(f"  skipped, not ready  {nr}")
        print(f"  skipped, no music   {np_}")
        print()
        if calls < 1:
            print("GATE AUDIO RESTORE: RED - never called")
            return 1
        if reissued < 1:
            print("GATE AUDIO RESTORE: RED - called but never re-issued; the "
                  "music would stay dead after the first backdrop load")
            return 1
        print(f"GATE AUDIO RESTORE: GREEN - path ran {calls}x and re-issued "
              f"playback {reissued}x. NOTE: this proves execution, not audio "
              "- nobody has heard this build.")
        return 0
    finally:
        try:
            proc.terminate(); proc.wait(timeout=10)
        except Exception:
            proc.kill()
        print(f"  stopped pid {proc.pid}; other instances untouched")


if __name__ == "__main__":
    sys.exit(main())
