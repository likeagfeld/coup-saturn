#!/usr/bin/env python3
"""
qa_centring.py - Audit label placement on every screen.

WHAT THIS MEASURES
  For each render function in coup_render.c, every text draw is classified by
  HOW ITS X POSITION IS DERIVED:

    computed  - the position comes from a measurement of the actual string
                (text_px_w / button_centered / a (W - w) / 2 expression).
                Correct by construction; the label cannot drift when the font
                or the string changes.
    literal   - the position is a constant, or a layout field used directly.
                It was correct only for the string and font in place when it
                was written.

  A literal is not automatically wrong - a left-aligned list item is supposed
  to be literal. What is wrong is a literal label sitting on a PLATE, because
  a plate exists to frame its label and the two must stay concentric.

WHY IT IS NOT A PIXEL MEASUREMENT
  Measuring centring from a capture is the better test and is used where it
  can be: qa_title_wordmark.py works on real frames. But the label plates sit
  over floodlit backdrop art, and thresholding a flat plate out of a lit
  skyline is not reliable - an attempt to do so latched onto a 5 px strip of
  backdrop architecture instead of the plate. Most screens are also behind
  online play and cannot be reached in an offline capture at all. So the
  placement RULE is enforced statically, and the screens that can be captured
  are additionally checked on real pixels.

STRICT MODE IS A RATCHET, NOT A TARGET
  Demanding zero literal placements would be wrong and could never pass. A
  left-aligned list is SUPPOSED to be literal: the rules screen has 83
  left-aligned body lines, and the lobby's seat rows are a list. Converting
  those to centred would be a defect, not a fix.

  So --strict compares each screen against a recorded baseline and fails only
  when a screen gains literals. Reviewed placements stay put; new unmeasured
  ones cannot slip in. Lower a baseline number when you convert something -
  never raise one.

USAGE
  python scripts/qa/qa_centring.py               # audit
  python scripts/qa/qa_centring.py --strict      # fail if any screen regresses
  python scripts/qa/qa_centring.py --write-baseline
"""

import argparse
import json
import os
import re
import sys

BASELINE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "centring_baseline.json")

SRC = "examples/coup/coup_render.c"

# A position is "computed" if it is derived from the string being drawn.
COMPUTED = re.compile(
    r"text_px_w|button_centered|draw_centered|draw_centered_in|"
    r"coup_centre_x|_px_w\(|"
    r"\(\s*COUP_SCREEN_W\s*-\s*\w+\s*\)\s*/\s*2|"
    r"\(\s*\w+_w\s*-\s*\w+\s*\)\s*/\s*2|"
    r"strlen\s*\(")

TEXT_CALL = re.compile(
    r"\b(draw_at|draw_text_sprite|button_centered|draw_centered_in"
    r"|draw_centered)\s*\(")
PLATE_CALL = re.compile(r"\b(panel|panel_r|panel_lit)\s*\(")


def screens(src):
    """Split the file into (screen name, body) by render function."""
    out = []
    for m in re.finditer(r"static void coup_render_(\w+)\s*\([^)]*\)\s*\{",
                         src):
        name = m.group(1)
        i = m.end() - 1
        depth, j = 0, i
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out.append((name, src[i:j + 1], src[:i].count("\n") + 1))
    return out


def audit(body, base_line):
    """Classify each text draw in one screen body."""
    rows = []
    for ln, line in enumerate(body.split("\n")):
        s = line.strip()
        if s.startswith("*") or s.startswith("/*") or s.startswith("//"):
            continue
        if not TEXT_CALL.search(s):
            continue
        kind = "computed" if COMPUTED.search(s) else "literal"
        if ("button_centered" in s or "draw_centered" in s
                or "draw_centered_in" in s):
            kind = "computed"
        rows.append((base_line + ln, kind, s[:88]))
    return rows


# A label pushed toward the middle by padding its literal with spaces. This is
# centring that is only correct for the exact string and the exact advance in
# place when someone counted the spaces - change either and it drifts silently.
# Two leading spaces are a deliberate indent (the rules screen indents detail
# lines under their heading), so the threshold is four.
FAKE_CENTRED = re.compile(r'"\s{4,}\S')


def find_fake_centred(src):
    hits = []
    for i, line in enumerate(src.split("\n"), 1):
        s = line.strip()
        if s.startswith("*") or s.startswith("//"):
            continue
        if not (TEXT_CALL.search(s) or "snprintf" in s):
            continue
        if FAKE_CENTRED.search(s):
            hits.append((i, s[:88]))
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--write-baseline", action="store_true")
    ap.add_argument("--file", default=SRC,
                    help="source to audit (use a git-extracted copy to prove "
                         "the gate fires RED on known-bad input)")
    args = ap.parse_args()

    src = open(args.file, encoding="utf-8", errors="replace").read()

    print("=== SPACE-PADDED LABELS (fake centring) ===")
    fake = find_fake_centred(src)
    if fake:
        for ln, text in fake:
            print(f"  line {ln:5}  {text}")
        print(f"  {len(fake)} label(s) centred by padding rather than "
              "measurement")
    else:
        print("  none - every label offset is derived from the string drawn")
    print()

    total_c = total_l = 0
    plate_screens = []
    print("=== LABEL PLACEMENT BY SCREEN ===")
    print(f"  {'screen':<14} {'computed':>9} {'literal':>8} {'plates':>7}")
    for name, body, line in screens(src):
        rows = audit(body, line)
        c = sum(1 for r in rows if r[1] == "computed")
        l = sum(1 for r in rows if r[1] == "literal")
        plates = len(PLATE_CALL.findall(body))
        total_c += c
        total_l += l
        flag = ""
        if plates and l:
            flag = "  <-- plates with literal labels"
            plate_screens.append((name, plates, l))
        print(f"  {name:<14} {c:>9} {l:>8} {plates:>7}{flag}")

    print()
    print(f"  totals: {total_c} computed, {total_l} literal, "
          f"{total_c + total_l} text draws")
    print()

    if fake:
        print(f"GATE CENTRING: RED - {len(fake)} label(s) are centred by "
              "padding a string literal with spaces. Use draw_centered(), "
              "which measures the string.")
        return 1

    counts = {n: l for n, l, _ in
              [(n, sum(1 for r in audit(b, ln) if r[1] == "literal"),
                None) for n, b, ln in screens(src)]}

    if args.write_baseline:
        with open(BASELINE, "w", encoding="utf-8") as fh:
            json.dump(counts, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"wrote {BASELINE}")
        return 0

    if not args.strict:
        print("GATE CENTRING: AUDIT - no label is centred by padding. Run "
              "--strict to hold each screen against its baseline.")
        return 0

    if not os.path.exists(BASELINE):
        print(f"GATE CENTRING: RED - no baseline at {BASELINE}; run "
              "--write-baseline once the current state is reviewed")
        return 1
    base = json.load(open(BASELINE, encoding="utf-8"))

    regressions = []
    improvements = []
    for name, n in sorted(counts.items()):
        b = base.get(name)
        if b is None and n == 0:
            # A new function that draws NO text is not a screen, so it has
            # nothing to centre and does not belong in the baseline. This
            # fired on coup_render_update_shading(), which only uploads
            # gouraud tables. Admitting it would have meant re-baselining -
            # and a ratchet that gets re-baselined to clear a false positive
            # stops being a ratchet.
            continue
        if b is None:
            regressions.append(f"{name}: new screen with {n} literal "
                               "placement(s) and no baseline")
        elif n > b:
            regressions.append(f"{name}: {n} literal placements, baseline {b} "
                               f"(+{n - b})")
        elif n < b:
            improvements.append(f"{name}: {n}, was {b} (-{b - n})")

    for line in improvements:
        print(f"  improved: {line}")
    print()
    if regressions:
        print("GATE CENTRING: RED - screen(s) gained unmeasured label "
              "placements")
        for r in regressions:
            print("  - " + r)
        print()
        print("  If the new placement is a left-aligned list item, it is fine "
              "- lower nothing, just re-run --write-baseline after review.")
        return 1
    if improvements:
        print("GATE CENTRING: GREEN - no padding, no screen regressed, and "
              f"{len(improvements)} screen(s) improved. Re-run "
              "--write-baseline to lock the gains in.")
        return 0
    print("GATE CENTRING: GREEN - no label is centred by padding and no "
          "screen has gained an unmeasured placement")
    return 0


if __name__ == "__main__":
    sys.exit(main())
