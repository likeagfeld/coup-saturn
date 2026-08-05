#!/usr/bin/env python3
"""
qa_retroarch.py - RetroArch/Beetle Saturn harness: live memory + capture.

Preferred over the Mednafen harness because it can read WRAM WHILE THE GAME
RUNS, rather than only from a savestate. That turns "did the effect fire" and
"where did it hang" into direct observations instead of bisected builds.

WHAT RETROARCH CAN AND CANNOT SEE (measured, sonicmaniasaturn 2026-07-29):
  READ_CORE_RAM exposes SYSTEM_RAM only:
      WRAM-L  phys 0x00200000 -> offset 0
      WRAM-H  phys 0x06000000 -> offset 0x100000
      bytes are BYTE-PAIR SWAPPED, same convention as the savestate
  VDP1/VDP2 VRAM, CRAM and registers are NOT in that map. For those, take a
  savestate - Beetle Saturn IS Mednafen's core, so mcs_extract.py parses it.

  Beetle Saturn does NOT implement READ_CORE_MEMORY ("no memory map defined");
  use READ_CORE_RAM.

REQUIRED CORE OPTION: beetle_saturn_cart = "Extended RAM (4MB)", else the
build stalls at the blue pre-title screen.

SHARED MACHINE - READ BEFORE EDITING
  Another agent runs RetroArch on this host out of
  D:/sonicmaniasaturn/tools/retroarch (MEASURED 2026-08-04: pid 19052 live
  while this harness was written). Two rules follow and neither is optional:

    1. NEVER enumerate-and-kill retroarch processes. The Mednafen harness in
       the Saturn skill does a blanket taskkill; copying that pattern here
       would destroy the other agent's session. We record the pid we spawn
       and terminate only that pid.

    2. Use our OWN network-command port. RetroArch's default is 55355; a read
       issued there could be answered by their emulator running a different
       game, which is not a wrong answer we would notice - it is a plausible
       one. We bind 55366 via --appendconfig so the two sessions cannot cross.

  Saturn skill gotcha #3 also applies: a second emulator steals host CPU, so
  any capture taken here is timing-contaminated and must be validated before
  a verdict is drawn from it.

Usage:
  python scripts/qa/qa_retroarch.py --check
  python scripts/qa/qa_retroarch.py --run build/coup_game/game.cue --seconds 20
  python scripts/qa/qa_retroarch.py --peek 0x0607BA50
"""

import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import time

PORT = int(os.environ.get("COUP_RA_PORT", "55366"))
RA_DEFAULT_PORT = 55355          # left alone for whoever else is on this box
HOST = "127.0.0.1"

WRAM_L_PHYS = 0x00200000
WRAM_H_PHYS = 0x06000000
WRAM_H_OFF = 0x100000


def find_retroarch():
    exe = shutil.which("retroarch")
    if exe:
        return exe
    roots = [
        # Known-good install on this machine, shared with another agent.
        r"D:\sonicmaniasaturn\tools\retroarch\RetroArch-Win64",
        r"D:\sonicmaniasaturn\tools\retroarch",
        os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages"),
        r"C:\RetroArch-Win64",
        r"C:\RetroArch",
        os.path.expandvars(r"%USERPROFILE%\scoop\apps\retroarch\current"),
    ]
    for r in roots:
        if not os.path.isdir(r):
            continue
        for dirpath, _dirs, files in os.walk(r):
            if "retroarch.exe" in files:
                return os.path.join(dirpath, "retroarch.exe")
    return None


def find_core(ra_exe):
    """Locate the Saturn core.

    It ships under BOTH names: upstream Mednafen calls it 'mednafen_saturn',
    the libretro fork is branded 'beetle_saturn'. Same emulator, same savestate
    format, so mcs_extract.py parses either. MEASURED on this machine
    (2026-08-04): the installed file is 'mednafen_saturn_libretro.dll'.
    Matching only 'beetle_saturn' is what made this harness report the core
    missing while it was sitting in the cores directory.
    """
    if not ra_exe:
        return None
    cores = os.path.join(os.path.dirname(ra_exe), "cores")
    if not os.path.isdir(cores):
        return None
    for f in sorted(os.listdir(cores)):
        low = f.lower()
        if not low.endswith((".dll", ".so", ".dylib")):
            continue
        if "beetle_saturn" in low or "mednafen_saturn" in low:
            return os.path.join(cores, f)
    return None


def other_instances():
    """Count RetroArch processes we did NOT start. Reporting only - we never
    act on this list. See the SHARED MACHINE note above."""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq retroarch.exe"],
                             capture_output=True, text=True, timeout=15).stdout
    except Exception:
        return 0
    return sum(1 for line in out.splitlines()
               if line.lower().startswith("retroarch.exe"))


def write_our_config():
    """A config fragment appended to theirs. --appendconfig does not write back
    to retroarch.cfg, so the other agent's settings are untouched."""
    cfg = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ra_coup.cfg")
    with open(cfg, "w", encoding="utf-8") as fh:
        fh.write('network_cmd_enable = "true"\n')
        fh.write('network_cmd_port = "%d"\n' % PORT)
        fh.write('video_fullscreen = "false"\n')
        fh.write('pause_nonactive = "false"\n')   # keep running unfocused
    return cfg


def phys_to_offset(addr):
    """Map a Saturn physical address into the SYSTEM_RAM offset space."""
    if WRAM_H_PHYS <= addr < WRAM_H_PHYS + 0x100000:
        return WRAM_H_OFF + (addr - WRAM_H_PHYS)
    if WRAM_L_PHYS <= addr < WRAM_L_PHYS + 0x100000:
        return addr - WRAM_L_PHYS
    raise ValueError(f"0x{addr:08X} is not in SYSTEM_RAM; VRAM/CRAM need a "
                     "savestate (see module docstring)")


def read_ram(offset, length, timeout=2.0):
    """READ_CORE_RAM <hex offset> <len> -> list of bytes, or None."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto(f"READ_CORE_RAM {offset:x} {length}\n".encode(), (HOST, PORT))
        data, _ = s.recvfrom(4096)
    except (socket.timeout, OSError):
        return None
    finally:
        s.close()

    parts = data.decode(errors="replace").split()
    if len(parts) < 3:
        return None
    try:
        return [int(p, 16) for p in parts[2:]]
    except ValueError:
        return None


def unswap(pairs):
    """Undo the byte-pair swap RetroArch's SYSTEM_RAM view applies."""
    out = bytearray(pairs)
    out[0::2], out[1::2] = out[1::2], out[0::2]
    return bytes(out)


def cmd_check():
    ra = find_retroarch()
    core = find_core(ra)
    print(f"  retroarch : {ra or 'NOT INSTALLED'}")
    print(f"  core      : {core or 'beetle_saturn not found'}")
    if not ra:
        print()
        print("  RetroArch was not found. Install it plus the Beetle Saturn")
        print("  core, then set the core option")
        print('      beetle_saturn_cart = "Extended RAM (4MB)"')
        return 1

    n = other_instances()
    print(f"  our port  : {PORT}   (default {RA_DEFAULT_PORT} left to others)")
    if n:
        print(f"  SHARED    : {n} RetroArch instance(s) already running. This")
        print("              harness never kills processes it did not start,")
        print("              and never talks on the default port.")

    if not core:
        return 1
    alive = read_ram(0, 4, timeout=1.0)
    print(f"  udp :{PORT} : "
          f"{'responding' if alive else 'idle (nothing of ours running)'}")
    return 0


def cmd_run(cue, seconds):
    ra = find_retroarch()
    core = find_core(ra)
    if not ra or not core:
        print("qa_retroarch: RetroArch or the Beetle Saturn core is missing; "
              "run --check", file=sys.stderr)
        return 1

    before = other_instances()
    cfg = write_our_config()
    proc = subprocess.Popen(
        [ra, "-L", core, "--appendconfig", cfg, os.path.abspath(cue)],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    print(f"  spawned pid {proc.pid} on port {PORT} "
          f"({before} other instance(s) left alone)")

    try:
        time.sleep(seconds)
        alive = read_ram(WRAM_H_OFF, 16)
        print(f"  live WRAM-H read: {'OK' if alive else 'no response'}")
        return 0 if alive else 1
    finally:
        # Terminate ONLY our pid. Never a name-based kill.
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
        print(f"  stopped pid {proc.pid}; other instances untouched")


def check_cue(cue):
    """Every FILE named by the cue must exist beside it.

    MEASURED 2026-08-04: capturing from examples/coup/saturn/_build/game.cue
    produced the Saturn BIOS CD-player screen, not the game. The cue declares
    a second audio track (rebellion.wav) that the build directory does not
    contain; RetroArch could not resolve the sheet and booted BIOS with no
    usable disc. Nothing in that frame is evidence about the build, but it
    looks exactly like a boot failure. Fail loudly here instead.

    Returns (ok, [missing filenames]).
    """
    d = os.path.dirname(os.path.abspath(cue))
    missing = []
    try:
        with open(cue, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if not s.upper().startswith("FILE "):
                    continue
                q = s.find('"')
                if q < 0:
                    continue
                name = s[q + 1:s.find('"', q + 1)]
                if not os.path.exists(os.path.join(d, name)):
                    missing.append(name)
    except OSError as e:
        return False, [f"<cue unreadable: {e}>"]
    return not missing, missing


def send_cmd(text, timeout=1.0):
    """Fire-and-forget network command (SCREENSHOT, PAUSE_TOGGLE, ...)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto((text + "\n").encode(), (HOST, PORT))
        return True
    except OSError:
        return False
    finally:
        s.close()


def cmd_shot(cue, seconds, out_path):
    """Boot our own instance, let it settle, screenshot, shut ours down.

    Saturn skill gotcha #3: a second emulator on the host steals CPU and slows
    the compute-bound boot, so a fixed wall-clock capture can land in the wrong
    phase. Another agent IS running RetroArch here, so this is not theoretical.
    The caller must validate the frame's content rather than assume the timing
    was right - a black or half-drawn frame is contention, not a build defect.
    """
    ra = find_retroarch()
    core = find_core(ra)
    if not ra or not core:
        print("qa_retroarch: RetroArch or the Saturn core is missing; run "
              "--check", file=sys.stderr)
        return 1

    ok, missing = check_cue(cue)
    if not ok:
        print(f"qa_retroarch: {cue} references files that are not present: "
              f"{', '.join(missing)}", file=sys.stderr)
        print("  RetroArch would boot the BIOS CD player instead of the game, "
              "which is NOT a build failure. Assemble the full disc first.",
              file=sys.stderr)
        return 1

    shots = os.path.join(os.path.dirname(ra), "screenshots")
    os.makedirs(shots, exist_ok=True)
    before = set(os.listdir(shots))

    cfg = write_our_config()
    others = other_instances()
    proc = subprocess.Popen(
        [ra, "-L", core, "--appendconfig", cfg, os.path.abspath(cue)],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    print(f"  spawned pid {proc.pid} on :{PORT} "
          f"({others} other instance(s) left running)")

    try:
        time.sleep(seconds)
        if not send_cmd("SCREENSHOT"):
            print("qa_retroarch: SCREENSHOT command not accepted",
                  file=sys.stderr)
            return 1
        time.sleep(2.5)                       # let the PNG land
        new = [f for f in os.listdir(shots) if f not in before
               and f.lower().endswith(".png")]
        if not new:
            print("qa_retroarch: no screenshot produced - our instance may not "
                  "have booted", file=sys.stderr)
            return 1
        newest = max(new, key=lambda f: os.path.getmtime(os.path.join(shots, f)))
        src = os.path.join(shots, newest)
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        with open(src, "rb") as a, open(out_path, "wb") as b:
            b.write(a.read())
        print(f"  captured {out_path} (from {newest})")
        return 0
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
        print(f"  stopped pid {proc.pid}; other instances untouched")


def cmd_peek(addr):
    off = phys_to_offset(addr)
    raw = read_ram(off, 4)
    if raw is None:
        print(f"qa_retroarch: no response on :{PORT} - is OUR instance running "
              "with network commands enabled?", file=sys.stderr)
        return 1
    b = unswap(raw)
    print(f"  0x{addr:08X} = 0x{int.from_bytes(b, 'big'):08X}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--run", metavar="CUE")
    ap.add_argument("--seconds", type=int, default=20)
    ap.add_argument("--peek", metavar="ADDR")
    ap.add_argument("--shot", metavar="CUE", help="boot and capture a frame")
    ap.add_argument("--out", default="build/qa/frame.png")
    args = ap.parse_args()

    if args.check:
        return cmd_check()
    if args.shot:
        return cmd_shot(args.shot, args.seconds, args.out)
    if args.run:
        return cmd_run(args.run, args.seconds)
    if args.peek:
        return cmd_peek(int(args.peek, 0))
    ap.print_help()
    return 0


# Bytes either side of the linker-map address to search for a witness magic.
# The observed discrepancy is a uniform 0x40; this allows generous slack in
# both directions without being wide enough to collide with an unrelated
# struct that happens to share the magic.
WITNESS_SEARCH = 0x200


def locate_witness(map_addr, magic, length):
    """Read a witness struct, locating it by its MAGIC rather than trusting
    the linker map address. Returns (bytes, actual_addr, delta) or None.

    WHY THIS EXISTS
      The linker map and the loaded image disagree by a uniform 0x40 bytes.
      MEASURED in one emulator session, reading three addresses back to back:

        0608AE4C (map says)   01000000 06036160 ...   magic absent
        0608AE0C (map-0x40)   41554457 00000018 ...   magic at offset 0

      Both witnesses in the build show the same -0x40 delta, so it is an
      image-wide load-vs-link offset, not a per-symbol accident.

      Reading the map address blindly made two gates report on uninitialised
      memory 64 bytes past their own witness. qa_cd_budget called it
      INCONCLUSIVE; qa_audio_restore called it RED - "every backdrop load has
      silently killed the music" - on a build whose restore path had in fact
      run 24 times and re-issued playback 24 times. The gate was reading the
      wrong 24 bytes and reporting a defect that did not exist.

      Hard-coding -0x40 would work until the next toolchain change moves it.
      A magic exists so a struct can identify itself; using it to LOCATE the
      struct is what makes the address self-correcting. The map still supplies
      the starting point - this only searches nearby, and reports the delta so
      a drifting offset is visible rather than silently absorbed.
    """
    lo = max(0, map_addr - WITNESS_SEARCH)
    span = WITNESS_SEARCH * 2 + length
    raw = read_ram(phys_to_offset(lo), span)
    if raw is None:
        return None
    buf = unswap(bytes(raw))
    idx = buf.find(struct.pack(">I", magic))
    if idx < 0 or idx + length > len(buf):
        return None
    actual = lo + idx
    return buf[idx:idx + length], actual, actual - map_addr


if __name__ == "__main__":
    sys.exit(main())
