#!/usr/bin/env python3
"""
qa_binary.py - Saturn binary size / WRAM headroom gate (G8).

Work RAM-H is 1 MB at 0x06000000-0x060FFFFF. SGL's stack sits at 0x060FFC00
and grows downward, so the end of BSS must stay well below it. This gate reads
the GNU ld map emitted by the Saturn link step (examples/coup/saturn/Makefile
passes -Xlinker -Map -Xlinker $(BUILD)/game.map) and reports the headroom.

The number this gate reports is what decides whether scene backgrounds can be
embedded in the binary (131,072 bytes each) or must stream from CD.

Usage:
  python3 scripts/qa/qa_binary.py examples/coup/saturn/_build/game.map
  python3 scripts/qa/qa_binary.py <map> --json
"""

import argparse
import json
import re
import sys

WRAM_H_BASE = 0x06000000
WRAM_H_SIZE = 0x00100000          # 1 MB
STACK_TOP = 0x060FFC00            # SGL default stack pointer
MIN_HEADROOM = 64 * 1024          # bytes that must remain below the stack

# One 512x256 256-colour VDP2 background bitmap.
BACKGROUND_BYTES = 512 * 256


def parse_map(path):
    """Extract _end and the main section sizes from a GNU ld map."""
    with open(path, "r", errors="replace") as fh:
        text = fh.read()

    # ld writes symbol lines as:  0x0000000006012345                _end
    end_match = re.search(r"0x([0-9a-fA-F]{8,16})\s+_end\b", text)
    if not end_match:
        raise SystemExit(f"qa_binary: no _end symbol found in {path}")
    end_addr = int(end_match.group(1), 16)

    # Section header lines:  .text          0x0600xxxx    0xNNNN
    sections = {}
    for name in (".text", ".data", ".bss"):
        m = re.search(
            r"^%s\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)" % re.escape(name),
            text, re.M)
        if m:
            sections[name] = {"addr": int(m.group(1), 16),
                              "size": int(m.group(2), 16)}
        else:
            sections[name] = {"addr": 0, "size": 0}

    return {"end_addr": end_addr, "sections": sections}


def check(map_path):
    info = parse_map(map_path)
    headroom = STACK_TOP - info["end_addr"]
    info["stack_headroom"] = headroom
    info["backgrounds_that_fit"] = max(0, headroom - MIN_HEADROOM) // BACKGROUND_BYTES
    info["ok"] = headroom >= MIN_HEADROOM
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map_path")
    ap.add_argument("--json", action="store_true",
                    help="emit machine-readable output")
    args = ap.parse_args()

    info = check(args.map_path)

    if args.json:
        print(json.dumps(info, indent=2))
    else:
        s = info["sections"]
        print(f"  .text {s['.text']['size']:>9,} bytes")
        print(f"  .data {s['.data']['size']:>9,} bytes")
        print(f"  .bss  {s['.bss']['size']:>9,} bytes")
        print(f"  _end  0x{info['end_addr']:08X}")
        print(f"  stack top 0x{STACK_TOP:08X}")
        print(f"  headroom {info['stack_headroom']:>9,} bytes "
              f"(minimum {MIN_HEADROOM:,})")
        print(f"  embeddable 128 KB backgrounds: "
              f"{info['backgrounds_that_fit']}")

    if not info["ok"]:
        print("GATE G8: RED - insufficient WRAM headroom", file=sys.stderr)
        return 1
    print("GATE G8: GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
