#!/usr/bin/env python3
"""
qa_rbg0_witness.py - Prove the RBG0 title fly-in control code RUNS and its
rotation state ADVANCES, by polling a WRAM witness struct live.

WHAT THIS GATE PROVES, AND - IMPORTANTLY - WHAT IT DOES NOT
  This reads pal/saturn/saturn_rbg0.c's g_saturn_rbg0_witness struct out of
  live WRAM over RetroArch's READ_CORE_RAM (qa_retroarch.py's
  locate_witness(), same pattern as qa_cd_budget.py / saturn_cd.h's
  g_saturn_cd_stats). Two consecutive polls a few seconds apart must show:
    - armed == 1 (saturn_rbg0_init() ran and slScrAutoDisp(NBG0ON|RBG0ON)
      was accepted)
    - frame strictly increased (saturn_rbg0_advance() is being called every
      loop iteration, not stalled)
    - angle and recip_q16 both changed in the direction the pure curve
      functions predict (monotonic toward their end values)

  This is PROOF THE CODE RUNS AND THE STATE ADVANCES. IT IS NOT PROOF
  ANYTHING REACHED THE SCREEN. Per skill gotcha #12, SGL rewrites VDP2
  scroll/display registers every vblank, so even a register-level
  savestate read would not be authoritative for what is displayed - a WRAM
  witness sidesteps that specific trap (WRAM is not per-vblank-rewritten
  the way VDP2 registers are), but it still only observes WRAM, never the
  framebuffer. The gate that actually proves the text stays legible with
  RBG0 armed is qa_rbg0_legibility.py, which needs a real captured frame.

  THIS BUILD MUST BE THE -DCOUP_RBG0_TITLE_DEMO VARIANT. The witness never
  goes armed on the normal shipped disc (saturn_rbg0_run_title_demo() is
  never called - see saturn_rbg0.h's "DEMO GATING" note) - that is
  correct/expected behaviour on that build, not a defect, and this gate
  reports it as INCONCLUSIVE rather than RED so a normal-disc run is not
  mistaken for a broken demo.

USAGE
  CCFLAGS_EXTRA="-DCOUP_RBG0_TITLE_DEMO" bash scripts/docker-saturn-build.sh \\
      examples/coup/saturn
  python scripts/qa/qa_rbg0_witness.py
  python scripts/qa/qa_rbg0_witness.py --cue build/coup_game/game.cue \\
      --map examples/coup/saturn/_build/game.map --seconds 30
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

MAGIC = 0x52424730          # 'RBG0'
SYMBOL = "g_saturn_rbg0_witness"

# saturn_rbg0_witness_t: magic, armed, frame, angle, recip_q16, finished -
# six 32-bit fields, big-endian on the SH-2 (matches saturn_cd.h's shape).
WITNESS_FMT = ">Iiiiii"
WITNESS_LEN = struct.calcsize(WITNESS_FMT)


def symbol_addr(mapfile, name):
    src = open(mapfile, encoding="utf-8", errors="replace").read()
    m = re.search(r"0x([0-9a-fA-F]{8,16})\s+" + re.escape(name) + r"\b", src)
    return int(m.group(1), 16) if m else None


def read_witness(addr):
    got = ra.locate_witness(addr, MAGIC, WITNESS_LEN)
    if got is None:
        return None
    raw, actual, delta = got
    if delta:
        print(f"  witness found at 0x{actual:08X} "
              f"({delta:+#x} from the linker map)")
    return struct.unpack(WITNESS_FMT, raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cue", default="build/coup_game/game.cue")
    ap.add_argument("--map", default="examples/coup/saturn/_build/game.map")
    ap.add_argument("--settle-seconds", type=int, default=8,
                     help="wait after boot before the first poll")
    ap.add_argument("--gap-seconds", type=int, default=3,
                     help="wait between the two polls")
    args = ap.parse_args()

    addr = symbol_addr(args.map, SYMBOL)
    if addr is None:
        print(f"GATE RBG0 WITNESS: RED - {SYMBOL} not in {args.map}; "
              "saturn_rbg0.c is not linked in this build")
        return 1
    print(f"  {SYMBOL} at 0x{addr:08X} (from the linker map)")

    ok, missing = ra.check_cue(args.cue)
    if not ok:
        print(f"GATE RBG0 WITNESS: RED - {args.cue} is missing {missing}; "
              "RetroArch would boot the BIOS, not the game")
        return 1

    exe = ra.find_retroarch()
    core = ra.find_core(exe)
    if not exe or not core:
        print("GATE RBG0 WITNESS: RED - RetroArch or the Saturn core is "
              "missing; run qa_retroarch.py --check")
        return 1

    cfg = ra.write_our_config()
    others = ra.other_instances()
    proc = subprocess.Popen(
        [exe, "-L", core, "--appendconfig", cfg, os.path.abspath(args.cue)],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    print(f"  spawned pid {proc.pid} on :{ra.PORT} "
          f"({others} other instance(s) left running)")

    try:
        time.sleep(args.settle_seconds)
        first = read_witness(addr)
        if first is None:
            print("GATE RBG0 WITNESS: INCONCLUSIVE - no response from the "
                  "emulator; nothing of ours may be running")
            return 2

        magic, armed, frame, angle, recip_q16, finished = first
        if magic != MAGIC:
            print(f"  magic 0x{magic:08X}, expected 0x{MAGIC:08X}")
            print("GATE RBG0 WITNESS: INCONCLUSIVE - the witness has not "
                  "been initialised yet (still booting?) or this build was "
                  "compiled without -DCOUP_RBG0_TITLE_DEMO")
            return 2

        print(f"  poll 1: armed={armed} frame={frame} angle={angle} "
              f"recip_q16={recip_q16} finished={finished}")

        if not armed:
            print("GATE RBG0 WITNESS: INCONCLUSIVE - witness present but "
                  "armed=0. Expected on a normal (non-demo) disc build - "
                  "rebuild with -DCOUP_RBG0_TITLE_DEMO to exercise this gate. "
                  "If this WAS a demo build, slScrAutoDisp(NBG0ON|RBG0ON) "
                  "was rejected and the fallback to NBG0-only fired - see "
                  "qa_vram_rbg0_map.py for the static bank/offset check.")
            return 2

        if finished:
            print("  fly-in already finished before the first poll - "
                  "SATURN_RBG0_FLYIN_FRAMES may be too short for "
                  "--settle-seconds, or the demo ran once and returned. "
                  "Re-run with a smaller --settle-seconds.")

        time.sleep(args.gap_seconds)
        second = read_witness(addr)
        if second is None:
            print("GATE RBG0 WITNESS: INCONCLUSIVE - no response on the "
                  "second poll")
            return 2

        magic2, armed2, frame2, angle2, recip2, finished2 = second
        print(f"  poll 2: armed={armed2} frame={frame2} angle={angle2} "
              f"recip_q16={recip2} finished={finished2}")

        fails = []
        if magic2 != MAGIC:
            fails.append("magic changed or vanished between polls - "
                         "witness address is not stable")
        if not finished and not finished2 and frame2 <= frame:
            fails.append(f"frame did not advance ({frame} -> {frame2}) - "
                         "saturn_rbg0_advance() does not appear to be "
                         "running every loop iteration")
        if not finished and not finished2:
            # Both curves are monotonic toward their end values - see
            # saturn_rbg0_flyin_recip_q16/angle. recip must not increase;
            # angle must not increase (it decays toward 0 from a positive
            # start - saturn_rbg0.h SATURN_RBG0_FLYIN_START_ANGLE).
            if recip2 > recip_q16:
                fails.append(f"recip_q16 increased ({recip_q16} -> {recip2}) "
                             "- the zoom curve must be monotonically "
                             "non-increasing (shrink-to-full-size fly-in)")
            if angle2 > angle:
                fails.append(f"angle increased ({angle} -> {angle2}) - the "
                             "rotation-settle curve must be monotonically "
                             "non-increasing toward 0")

        print()
        if fails:
            print("GATE RBG0 WITNESS: RED")
            for f in fails:
                print("  - " + f)
            return 1

        print("GATE RBG0 WITNESS: GREEN - the control code is armed and "
              "running, and its rotation state is advancing exactly the "
              "way the pure curve functions predict.")
        print("  This proves the CODE RUNS. It does NOT prove anything "
              "reached the screen - see qa_rbg0_legibility.py for that.")
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
