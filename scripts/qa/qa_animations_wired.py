#!/usr/bin/env python3
"""
qa_animations_wired.py - Prove every animation module is actually CALLED.

WHY THIS GATE EXISTS
  Four animation modules were built, unit-tested and linked into the binary
  while being called from nowhere. Every existing check passed: they compiled,
  their tests were green, the Saturn build was clean, verify_facelift was
  green, and the binary grew. None of that is evidence that a single pixel
  moved on screen, because nothing invoked them.

  Unit tests prove a module WORKS. This proves it RUNS. They are different
  questions and only one of them was being asked.

HOW IT MEASURES
  Statically. For each animation, the entry point that must appear in shipped
  (non-test) source is named, and the source tree is searched for a real call
  to it. Declarations, the module's own definition, comments and test files
  do not count - only a call from code that ships.

NEGATIVE CONTROL
  --selftest re-runs the search for entry points that deliberately do not
  exist. Those must all report MISSING; a gate that finds everything, finds
  nothing.
"""

import argparse
import os
import re
import sys

# entry point -> what stops working if it is never called
REQUIRED = {
    "coup_render_update_shading": "animated gouraud tables are never refreshed",
    "coup_shading_sheen":         "the title wordmark sheen never sweeps",
    "coup_shading_halo":          "the current-turn seat never gets its halo",
    "coup_shading_pulse":         "a readied lobby slot never pulses",
    "coup_shading_wash":          "lobby slots never separate occupied from empty",
    "coup_shading_spotlight":     "the winner never gets a spotlight",
    "saturn_linescroll_arm":      "the title shimmer is never switched on",
    "saturn_linescroll_advance":  "the shimmer is armed but never advances",
    "saturn_coinfx_payout":       "coins never launch on a payout",
    "saturn_coinfx_tick":         "coins in flight never move",
    "saturn_coinfx_draw":         "coins in flight are never drawn",
    "saturn_coinfx_timer_colors": "the timer bar never ramps green->amber->red",
    "saturn_distort_draw_flip":
        "a card reveal never flips - it just changes, instantly",
    "saturn_distort_draw_mesh_dissolve":
        "a lost influence never dissolves off the table",
    "coup_reveal_observe":     "no reveal is ever detected, so no card animates",
    "coup_reveal_tick":        "a reveal starts and then freezes on frame 0",
    "saturn_vdp1_reserve_cmd_slot":
        "the distort commands have nowhere on the VDP1 chain to land",
}

# Built and tested but not yet driven by a game event. Listed explicitly so
# the gap is VISIBLE rather than silently absent - the whole point of this
# gate. Move an entry up into REQUIRED when its trigger is written.
KNOWN_UNWIRED = {}

# A gouraud table can be generated and uploaded every frame and still never
# reach the screen, because nothing passes its SLOT to a draw. That is a
# strictly stronger property than "the generator is called", and checking
# only the generator missed it: COUP_GRD_SHEEN was being computed and
# uploaded 60 times a second while no draw referenced it.
SLOTS_MUST_DRAW = [
    "COUP_GRD_HALO", "COUP_GRD_PULSE", "COUP_GRD_OCCUPIED",
    "COUP_GRD_EMPTY", "COUP_GRD_SPOTLIGHT", "COUP_GRD_TIMER",
]

SLOTS_KNOWN_UNDRAWN = {
    "COUP_GRD_SHEEN":
        "the title wordmark is a keyed SPRITE and the gouraud helpers draw "
        "POLYGONS; a plate behind it would show through the letters. Needs a "
        "gouraud-enabled sprite path (CMDPMOD 2-0 = 100b) in saturn_vdp1.c",
}

RENDER_C = "examples/coup/coup_render.c"

SEARCH_DIRS = ["examples/coup", "pal/saturn", "core/src"]
SKIP_DIR_PARTS = ("tests", "_build", "build")


def ship_sources():
    out = []
    for root in SEARCH_DIRS:
        for dirpath, dirnames, files in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_PARTS]
            if any(p in dirpath.replace("\\", "/").split("/")
                   for p in SKIP_DIR_PARTS):
                continue
            for f in files:
                if f.endswith(".c"):
                    out.append(os.path.join(dirpath, f))
    return out


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    return src


def find_calls(name, files):
    """Files containing a real CALL to `name` - not its definition, not a
    declaration, not a mention in a comment."""
    hits = []
    call = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    for path in files:
        try:
            raw = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        src = strip_comments(raw)
        for m in call.finditer(src):
            line_start = src.rfind("\n", 0, m.start()) + 1
            line = src[line_start:src.find("\n", m.start())]
            # A definition or declaration has a return type before the name on
            # the same line and is not preceded by '=' or an operator.
            if re.search(r"\b(?:void|bool|int|uint\w*|static|extern)\s+[\w\*\s]*"
                         + re.escape(name) + r"\s*\(", line):
                continue
            if line.rstrip().endswith(";") and "=" not in line and \
               re.match(r"^\s*(?:void|bool|int|uint)", line):
                continue
            hits.append((path.replace("\\", "/"),
                         src[:m.start()].count("\n") + 1))
            break
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    files = ship_sources()
    print(f"  searched {len(files)} shipped source files "
          f"(tests and build dirs excluded)")
    print()

    missing = []
    for name, consequence in sorted(REQUIRED.items()):
        hits = find_calls(name, files)
        if hits:
            path, line = hits[0]
            print(f"  OK      {name:28s} {path}:{line}")
        else:
            print(f"  MISSING {name:28s} -> {consequence}")
            missing.append((name, consequence))

    if KNOWN_UNWIRED:
        print()
        print("  Built and tested, not yet driven by a game event:")
        for name, why in sorted(KNOWN_UNWIRED.items()):
            still = "still unwired" if not find_calls(name, files) \
                    else "NOW WIRED - move it into REQUIRED"
            print(f"    {name:34s} {why}  [{still}]")

    # --- gouraud slots must reach an actual draw -------------------------
    print()
    src = strip_comments(open(RENDER_C, encoding="utf-8",
                              errors="replace").read())

    def slot_drawn(slot):
        """The slot appears in a draw call's argument list, either directly
        or through a local assigned from it."""
        for fn in ("panel_grd", "panel_lit", "draw_rect_gouraud"):
            if re.search(fn + r"\s*\([^;]*?\b" + slot + r"\b", src, re.S):
                return True
        return bool(re.search(r"=\s*" + slot + r"\s*;", src))

    for slot in SLOTS_MUST_DRAW:
        if slot_drawn(slot):
            print(f"  OK      {slot:28s} reaches a draw")
        else:
            print(f"  MISSING {slot:28s} -> uploaded every frame but no draw "
                  f"uses it, so it never reaches the screen")
            missing.append((slot, "uploaded but never drawn"))

    for slot, why in SLOTS_KNOWN_UNDRAWN.items():
        state = "NOW DRAWN - move it into SLOTS_MUST_DRAW" \
                if slot_drawn(slot) else "still undrawn"
        print(f"  note    {slot:28s} {why}  [{state}]")

    if args.selftest:
        print()
        fake = ["coup_shading_nonexistent", "saturn_linescroll_imaginary"]
        found = [f for f in fake if find_calls(f, files)]
        print(f"  negative control: {len(fake)} entry points that do not "
              f"exist -> {len(found)} found")
        if found:
            print()
            print("GATE ANIMATIONS WIRED: RED - the search matches names that "
                  "are not there, so a GREEN from it means nothing")
            return 1

    print()
    if missing:
        print("GATE ANIMATIONS WIRED: RED")
        for name, consequence in missing:
            print(f"  - {name} is never called: {consequence}")
        return 1
    print(f"GATE ANIMATIONS WIRED: GREEN - all {len(REQUIRED)} animation "
          f"entry points are called from shipped code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
