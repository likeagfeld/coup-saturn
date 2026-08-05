#!/usr/bin/env python3
"""
qa_cd_budget.py - Prove a streamed scene load meets its time budget.

WHY A BUDGET EXISTS
  Backgrounds are streamed off the disc rather than linked in, because three
  resident scenes cost 215,040 B of WRAM and left 1,032 B of slack - not
  enough for the remaining four at any colour depth (MASTER-GOAL.md section 9).
  Streaming is only acceptable if a scene change is not perceptible as a stall
  and cannot breach the server contract, which must stay turnkey.

  The binding deadline is the server's, measured from coup_server/server.py:

      CHALLENGE_TIMEOUT / BLOCK_TIMEOUT   12.0 s   <-- tightest
      INFLUENCE / EXCHANGE_TIMEOUT        30.0 s
      TURN_TIMEOUT / HEARTBEAT_TIMEOUT    60.0 s

  Scene changes happen at phase boundaries and never inside a challenge
  window, and TCP buffers during a stall, so a brief block costs latency
  rather than messages. The budget is set far tighter than the deadline
  anyway: 1.0 second, i.e. 60 vblanks at NTSC.

HOW IT MEASURES
  saturn_cd.c counts VDP2 TVSTAT vblank edges across each load and stores the
  result in g_saturn_cd_stats. This reads that struct out of live WRAM over
  RetroArch's READ_CORE_RAM - the measurement is of the running console, not
  arithmetic from a disc-speed datasheet.

  The struct address comes from the linker map, so it cannot drift out of
  sync with the build.

USAGE
  python scripts/qa/qa_cd_budget.py
  python scripts/qa/qa_cd_budget.py --budget-frames 60 --seconds 45
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_retroarch as ra   # noqa: E402

MAGIC = 0x43445354          # 'CDST'
SYMBOL = "g_saturn_cd_stats"
NTSC_HZ = 60.0

# struct saturn_cd_stats_t: magic, last_frames, last_bytes, last_result,
# loads, worst_frames - six 32-bit fields, big-endian on the SH-2.
STATS_FMT = ">Iiiiii"
STATS_LEN = struct.calcsize(STATS_FMT)


def symbol_addr(mapfile, name):
    src = open(mapfile, encoding="utf-8", errors="replace").read()
    m = re.search(r"0x([0-9a-fA-F]{8,16})\s+" + re.escape(name) + r"\b", src)
    return int(m.group(1), 16) if m else None


def read_stats(addr):
    """Locate the stats block by its magic - see ra.locate_witness(). The
    linker map address is 0x40 off from where the image actually loads, and
    reading it directly returned uninitialised memory just past the block."""
    got = ra.locate_witness(addr, MAGIC, STATS_LEN)
    if got is None:
        return None
    raw, actual, delta = got
    if delta:
        print(f"  witness found at 0x{actual:08X} "
              f"({delta:+#x} from the linker map)")
    return struct.unpack(STATS_FMT, raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cue", default="build/coup_game/game.cue")
    ap.add_argument("--map", default="examples/coup/saturn/_build/game.map")
    ap.add_argument("--seconds", type=int, default=45)
    ap.add_argument("--budget-frames", type=int, default=60)
    args = ap.parse_args()

    addr = symbol_addr(args.map, SYMBOL)
    if addr is None:
        print(f"GATE CD BUDGET: RED - {SYMBOL} not in {args.map}; the CD "
              "timing instrumentation is not linked in")
        return 1
    print(f"  {SYMBOL} at 0x{addr:08X} (from the linker map)")

    ok, missing = ra.check_cue(args.cue)
    if not ok:
        print(f"GATE CD BUDGET: RED - {args.cue} is missing {missing}; "
              "RetroArch would boot the BIOS, not the game")
        return 1

    exe = ra.find_retroarch()
    core = ra.find_core(exe)
    if not exe or not core:
        print("GATE CD BUDGET: RED - RetroArch or the Saturn core is missing; "
              "run qa_retroarch.py --check")
        return 1

    cfg = ra.write_our_config()
    others = ra.other_instances()
    proc = subprocess.Popen(
        [exe, "-L", core, "--appendconfig", cfg, os.path.abspath(args.cue)],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    print(f"  spawned pid {proc.pid} on :{ra.PORT} "
          f"({others} other instance(s) left running)")

    try:
        time.sleep(args.seconds)
        stats = read_stats(addr)
        if stats is None:
            print("GATE CD BUDGET: INCONCLUSIVE - no response from the "
                  "emulator; nothing of ours may be running")
            return 2

        magic, frames, nbytes, result, loads, worst = stats
        if magic != MAGIC:
            print(f"  magic 0x{magic:08X}, expected 0x{MAGIC:08X}")
            print("GATE CD BUDGET: INCONCLUSIVE - the stats block has not "
                  "been initialised yet. The build may still be booting; "
                  "re-run with a longer --seconds.")
            return 2

        secs = frames / NTSC_HZ
        worst_s = worst / NTSC_HZ
        print(f"  loads completed   {loads}")
        print(f"  last load         {nbytes:,} B in {frames} vblanks "
              f"({secs:.2f} s)")
        print(f"  worst load        {worst} vblanks ({worst_s:.2f} s)")
        print(f"  last result       {result} "
              f"({'ok' if result == 0 else 'ERROR'})")
        print(f"  budget            {args.budget_frames} vblanks "
              f"({args.budget_frames / NTSC_HZ:.2f} s)")

        fails = []
        if loads < 1:
            fails.append("no scene was ever loaded from disc - streaming is "
                         "not running")
        if result != 0:
            fails.append(f"the last load returned {result}")
        if worst > args.budget_frames:
            fails.append(f"worst load {worst} vblanks ({worst_s:.2f} s) "
                         f"exceeds the {args.budget_frames}-vblank budget")

        print()
        if fails:
            print("GATE CD BUDGET: RED")
            for f in fails:
                print("  - " + f)
            return 1
        print(f"GATE CD BUDGET: GREEN - worst streamed scene load {worst_s:.2f} s, "
              f"inside the {args.budget_frames / NTSC_HZ:.2f} s budget and far "
              "inside the server's 12 s window")
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
        print(f"  stopped pid {proc.pid}; other instances untouched")


if __name__ == "__main__":
    sys.exit(main())
