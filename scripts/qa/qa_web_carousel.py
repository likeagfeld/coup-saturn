#!/usr/bin/env python3
"""
qa_web_carousel.py - The title screen's 3D card ring, checked for the two
things about it that break without saying so.

WHY THIS GATE EXISTS

  1. PATH SAFETY.  The staging client is served from
     https://saturncoup.duckdns.org/staging/ while the LIVE client owns the
     site root.  An absolute "/assets/cards/duke.webp" resolves against the
     LIVE root, so staging silently renders the OLD art.  Nothing errors,
     nothing 404s, the page looks fine - you are simply testing a different
     build from the one you shipped.  js/assets.js exists to make that
     impossible by deriving every path from import.meta.url; this gate is
     what stops someone reintroducing a literal.

  2. FLATTENED DEPTH.  A 3D carousel is correct only while ONE unbroken 3D
     rendering context runs from the element carrying `perspective` down to
     the card faces.  Per CSS Transforms 2, `transform-style: preserve-3d` is
     forced to `flat` on any element that also carries a GROUPING property -
     overflow other than visible, opacity < 1, filter, backdrop-filter,
     clip-path, clip, mask, mix-blend-mode, isolation, contain: paint, or a
     will-change naming one of those.

     When that happens the ring does not throw and does not disappear.  It
     collapses into a flat row painted in DOM ORDER, so the card at the BACK
     paints over the card at the front.  On a title screen nobody reloads, a
     reviewer reads that as "the animation looks a bit odd" and it ships.

     The trap is that the offending property is almost always added for an
     innocent reason: an `overflow: hidden` to stop a shadow bleeding, an
     `opacity` to dim the far cards, a `filter: drop-shadow` to lift the
     front one.  Each is the obvious thing to reach for and each one silently
     destroys the effect.  So the rule the stylesheet holds to is mechanical,
     and this is what enforces it:

       - .tc-stage / .tc-ring / .tc-card / .tc-inner declare NO grouping
         property.  (will-change: transform is fine - transform is not one.)
       - all fading is opacity on .tc-face, a LEAF, where there is no 3D
         context below to lose.
       - no z-index anywhere in the ring: inside a 3D rendering context the
         paint order comes from the transformed z, and a z-index is only an
         invitation to fight it.

  3. PHASE ARITHMETIC.  Each card's scale/fade cycle is offset by --tc-step,
     which must stay equal to -(--tc-spin / card count) or the cards stop
     being largest at the moment they are actually nearest.  Changing the
     spin duration and forgetting the step is a one-character regression with
     no error attached to it.

  4. THE CARD SET.  The ring's faces are CHARACTERS + the back, and the
     layout hard-codes the 2:3 aspect (--tc-h = --tc-w * 1.5).  A renamed,
     added or re-exported card file breaks one of those quietly - a missing
     background-image is just an empty card, and a card that is no longer 2:3
     is cropped by background-size: cover.

WHY IT IS PYTHON AND NOT PART OF THE JSDOM SMOKE TEST
  scripts/smoke_web_staging.mjs needs jsdom, which is not vendored, has no
  package.json, and is not installed anywhere in this repo - it cannot
  currently run, and a gate that cannot run is not a gate.  Everything here
  is stdlib text and byte analysis, including the WebP header reader, so it
  has no dependencies at all and actually executes.

NEGATIVE CONTROL
  --selftest re-runs every check against deliberately broken copies - an
  absolute /assets/ URL, an overflow: hidden on .tc-ring, a mismatched
  --tc-step - and requires each one to be caught.  A gate that passes both
  ways measures nothing.
"""

import argparse
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")

CSS = "web-staging/css/style.css"
TITLE_JS = "web-staging/js/screens/title.js"
ASSETS_JS = "web-staging/js/assets.js"
CARD_DIR = "web-staging/assets/cards"

# Where the carousel block starts in the stylesheet. Everything from here to
# the end of the file is the block this gate owns.
CSS_MARKER = "TITLE CARD CAROUSEL"

# The four elements that must keep an unbroken 3D rendering context.
CHAIN = (".tc-stage", ".tc-ring", ".tc-card", ".tc-inner")

CARD_W, CARD_H = 288, 432          # the 2:3 the layout assumes
RING_SPACING_DEG = 60              # 360 / 6 cards


def path(rel):
    return os.path.normpath(os.path.join(ROOT, rel))


def read(rel):
    with open(path(rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()


# ---------------------------------------------------------------------------
# CSS: comment stripping and a brace-aware rule walker
# ---------------------------------------------------------------------------

def strip_css_comments(css):
    return re.sub(r"/\*.*?\*/", " ", css, flags=re.S)


def walk_rules(css, media=()):
    """Yield (selector, body, media_chain) for every declaration block.

    Descends into @media. @keyframes bodies are yielded whole, tagged by
    their prelude, because their percentage stops are not selectors.
    """
    i, n = 0, len(css)
    prelude = []
    while i < n:
        c = css[i]
        if c == "{":
            head = "".join(prelude).strip()
            depth, j = 1, i + 1
            while j < n and depth:
                if css[j] == "{":
                    depth += 1
                elif css[j] == "}":
                    depth -= 1
                j += 1
            body = css[i + 1:j - 1]
            if head.startswith("@media"):
                for r in walk_rules(body, media + (head,)):
                    yield r
            else:
                yield head, body, media
            prelude = []
            i = j
            continue
        if c == "}":
            prelude = []
            i += 1
            continue
        prelude.append(c)
        i += 1


def subject(selector):
    """The compound selector the rule actually STYLES - its last part.

    `.title-carousel.tc-motion .tc-ring` styles .tc-ring, not
    .title-carousel; a grouping property in that rule lands on the ring.
    """
    parts = re.split(r"[\s>+~]+", selector.strip())
    return parts[-1] if parts else ""


def declarations(body):
    """(property, value) for each declaration, ignoring nested blocks."""
    flat = re.sub(r"\{[^{}]*\}", " ", body)
    out = []
    for chunk in flat.split(";"):
        if ":" not in chunk:
            continue
        prop, _, val = chunk.partition(":")
        out.append((prop.strip().lower(), val.strip().lower()))
    return out


# ---------------------------------------------------------------------------
# The grouping properties that force transform-style: flat
# ---------------------------------------------------------------------------

GROUPING_NAMES = (
    "filter", "backdrop-filter", "clip-path", "clip", "mask", "mask-image",
    "mask-border", "-webkit-mask", "-webkit-mask-image", "-webkit-filter",
    "-webkit-backdrop-filter",
)


def grouping_violation(prop, val):
    """Why this declaration would flatten a preserve-3d element, or None."""
    if prop in ("overflow", "overflow-x", "overflow-y", "overflow-block",
                "overflow-inline"):
        if val.replace("visible", "").strip():
            return f"{prop}: {val} (any overflow but visible groups)"
        return None
    if prop == "opacity":
        try:
            if float(val) < 1.0:
                return f"opacity: {val} (< 1 groups)"
        except ValueError:
            return f"opacity: {val} (not provably 1)"
        return None
    if prop in GROUPING_NAMES:
        if val not in ("none", ""):
            return f"{prop}: {val}"
        return None
    if prop == "mix-blend-mode":
        return None if val == "normal" else f"mix-blend-mode: {val}"
    if prop == "isolation":
        return None if val == "auto" else f"isolation: {val}"
    if prop == "contain":
        if re.search(r"\b(paint|content|strict)\b", val):
            return f"contain: {val}"
        return None
    if prop == "will-change":
        named = [t.strip() for t in val.split(",")]
        bad = [t for t in named
               if t in GROUPING_NAMES + ("opacity", "overflow", "isolation",
                                         "mix-blend-mode", "contain")]
        if bad:
            return f"will-change: {val} (names grouping propert(y/ies) {bad})"
        return None
    return None


# ---------------------------------------------------------------------------
# WebP dimensions, stdlib only (no Pillow)
# ---------------------------------------------------------------------------

def webp_size(fpath):
    """(width, height) of a WebP, from the RIFF header. None if unreadable."""
    with open(fpath, "rb") as fh:
        head = fh.read(32)
    if len(head) < 16 or head[0:4] != b"RIFF" or head[8:12] != b"WEBP":
        return None
    fourcc = head[12:16]
    if fourcc == b"VP8X":
        # 4 flag bytes, then canvas width-1 and height-1 as 24-bit LE.
        w = int.from_bytes(head[24:27], "little") + 1
        h = int.from_bytes(head[27:30], "little") + 1
        return w, h
    if fourcc == b"VP8 ":
        # 3-byte frame tag, then the 0x9d 0x01 0x2a start code.
        if head[23:26] != b"\x9d\x01\x2a":
            return None
        w, h = struct.unpack("<HH", head[26:30])
        return w & 0x3FFF, h & 0x3FFF
    if fourcc == b"VP8L":
        if head[20] != 0x2F:
            return None
        bits = int.from_bytes(head[21:25], "little")
        return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
    return None


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

# An ABSOLUTE asset reference: /assets/... not preceded by a path character,
# so "../assets/" and "js/assets.js" are correctly left alone.
ABSOLUTE_ASSET = re.compile(r"""(?:^|[\s("'`=,:])(/assets/[^\s"'`)]*)""")

SCAN_GLOBS = ("web-staging/css/style.css", "web-staging/index.html")


def strip_prose(rel, src):
    """Blank out comments, keeping line numbering intact.

    Every file in this client carries a header explaining why an absolute
    /assets/ URL is forbidden - and says "/assets/..." to do it. Scanning the
    comments would flag the warning as the offence.
    """
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    if rel.endswith(".html"):
        return re.sub(r"<!--.*?-->", blank, src, flags=re.S)
    src = re.sub(r"/\*.*?\*/", blank, src, flags=re.S)
    if rel.endswith(".js"):
        # Not preceded by ':' or a word char, so "https://" inside a string
        # survives.
        src = re.sub(r"(?<![:\w])//[^\n]*", blank, src)
    return src


def js_files():
    out = []
    base = path("web-staging/js")
    for dirpath, _dirs, names in os.walk(base):
        for nm in sorted(names):
            if nm.endswith(".js"):
                full = os.path.join(dirpath, nm)
                out.append(os.path.relpath(full, ROOT).replace("\\", "/"))
    return sorted(out)


def check(css_mutate=None, js_mutate=None):
    fails = []
    notes = []

    css_raw = read(CSS)
    if css_mutate:
        css_raw = css_mutate(css_raw)
    title_raw = read(TITLE_JS)
    if js_mutate:
        title_raw = js_mutate(title_raw)

    # --- 1. no absolute /assets/ anywhere the client is served from --------
    scanned = 0
    for rel in list(SCAN_GLOBS) + js_files():
        src = css_raw if rel == CSS else (
            title_raw if rel == TITLE_JS else read(rel))
        src = strip_prose(rel, src)
        scanned += 1
        for m in ABSOLUTE_ASSET.finditer(src):
            line = src[:m.start()].count("\n") + 1
            fails.append(
                f"{rel}:{line}: absolute asset URL {m.group(1)!r} - under "
                f"/staging/ this resolves against the LIVE site root and "
                f"serves the old art. Derive it from assets.js instead.")
    notes.append(f"no absolute /assets/ URL in {scanned} client file(s)")

    # --- 2. the ring's card set matches the files on disk ------------------
    assets_src = strip_css_comments(read(ASSETS_JS))
    m = re.search(r"export\s+const\s+CHARACTERS\s*=\s*\[(.*?)\];",
                  assets_src, re.S)
    if not m:
        fails.append(f"{ASSETS_JS}: cannot find the CHARACTERS table")
        keys = []
    else:
        keys = re.findall(r"key\s*:\s*'([^']+)'", m.group(1))

    # title.js must build the ring from that table, not from a literal list.
    if "CHARACTERS.map" not in title_raw or "cardArt(" not in title_raw:
        fails.append(
            f"{TITLE_JS}: the ring's faces are not built from "
            f"CHARACTERS.map(... cardArt(i)) - a hand-written list drifts "
            f"from assets.js the first time a character is added")
    if "CARD_BACK" not in title_raw:
        fails.append(f"{TITLE_JS}: the ring never references CARD_BACK, so "
                     f"the sixth card and every reverse face is missing")

    expected = {k + ".webp" for k in keys} | {"back.webp"}
    actual = set(os.listdir(path(CARD_DIR)))
    if expected - actual:
        fails.append(f"{CARD_DIR}: the ring asks for {sorted(expected - actual)}"
                     f" which do not exist - those cards render blank")
    if actual - expected:
        fails.append(f"{CARD_DIR}: {sorted(actual - expected)} present but not "
                     f"in the ring - either the file is dead or the ring is "
                     f"missing a face")
    if expected and not (expected ^ actual):
        notes.append(f"ring faces {sorted(expected)} all present, none orphaned")

    ring_len = len(expected)
    if ring_len and 360 % ring_len:
        fails.append(f"{ring_len} cards do not divide 360 deg evenly")

    # --- 3. every card is the 2:3 the layout hard-codes --------------------
    for nm in sorted(actual & expected):
        size = webp_size(os.path.join(path(CARD_DIR), nm))
        if size is None:
            fails.append(f"{CARD_DIR}/{nm}: cannot read WebP dimensions")
            continue
        w, h = size
        if (w, h) != (CARD_W, CARD_H):
            if w * CARD_H != h * CARD_W:
                fails.append(
                    f"{CARD_DIR}/{nm}: {w}x{h} is not 2:3 - the ring sets "
                    f"--tc-h to --tc-w * 1.5 and background-size: cover, so "
                    f"this card is silently cropped")
            else:
                notes.append(f"{nm} is {w}x{h} (2:3, not the usual "
                             f"{CARD_W}x{CARD_H})")
    notes.append(f"all {len(actual & expected)} card faces are {CARD_W}x{CARD_H} (2:3)")

    # --- 4. the stylesheet's 3D contract ----------------------------------
    idx = css_raw.find(CSS_MARKER)
    if idx < 0:
        fails.append(f"{CSS}: the {CSS_MARKER} block is gone")
        block = ""
    else:
        block = strip_css_comments(css_raw[idx:])

    rules = list(walk_rules(block))
    tc_rules = [(s, b, md) for s, b, md in rules
                if ".tc-" in s or ".title-carousel" in s]
    if not tc_rules:
        fails.append(f"{CSS}: no carousel rules found after {CSS_MARKER}")

    seen_3d = {name: False for name in CHAIN}
    perspective_on_stage = False
    opacity_animations = set()
    fails_before_css = len(fails)

    for sel, body, md in tc_rules:
        subj = subject(sel)
        decls = declarations(body)
        in_chain = subj in CHAIN

        for prop, val in decls:
            if in_chain:
                why = grouping_violation(prop, val)
                if why:
                    fails.append(
                        f"{CSS}: `{sel}` sets {why}. That forces "
                        f"transform-style to flat on {subj}, and the ring "
                        f"collapses into a flat row painted in DOM order - "
                        f"the BACK card over the front one, silently.")
            if prop == "z-index":
                fails.append(
                    f"{CSS}: `{sel}` sets z-index: {val}. Inside a 3D "
                    f"rendering context the paint order comes from the "
                    f"transformed z; a z-index only fights it.")
            if prop == "filter" and val not in ("none", ""):
                fails.append(
                    f"{CSS}: `{sel}` sets filter: {val} - use box-shadow. "
                    f"filter is a grouping property.")
            if prop == "transform-style" and "preserve-3d" in val:
                if subj in seen_3d:
                    seen_3d[subj] = True
            if prop == "perspective" and subj == ".tc-stage":
                perspective_on_stage = True
            if prop.startswith("animation") and "tcfade" in val:
                opacity_animations.add(subj)

    if not perspective_on_stage:
        fails.append(f"{CSS}: .tc-stage never sets `perspective`, so there is "
                     f"no 3D rendering context and the ring is flat")
    for name in (".tc-ring", ".tc-card", ".tc-inner"):
        if not seen_3d[name]:
            fails.append(f"{CSS}: `{name}` never sets transform-style: "
                         f"preserve-3d, so its children are flattened into it")
    # .tc-face-front and .tc-face-back are SIBLING LEAVES of .tc-face -
    # nothing is nested below either, so an opacity animation on them groups
    # nothing and cannot flatten a 3D context. The back carries its own
    # shallower fade (tcFadeBack) because it is a flat repeating pattern and
    # is the one face that turns into a featureless rectangle when dimmed.
    LEAF_FACES = {".tc-face", ".tc-face-front", ".tc-face-back"}
    if opacity_animations - LEAF_FACES:
        fails.append(
            f"{CSS}: the tcFade opacity animation is applied to "
            f"{sorted(opacity_animations - LEAF_FACES)}. Animated opacity "
            f"below 1 is a grouping property - it must stay on the leaf "
            f"faces.")
    if opacity_animations:
        notes.append("all fading is opacity on the .tc-face leaves")
    if len(fails) == fails_before_css and tc_rules:
        notes.append(f"no grouping property on {', '.join(CHAIN)}")

    # --- 5. the 3D block is gated on reduced motion ------------------------
    gated = [md for sel, body, md in tc_rules
             if any("preserve-3d" in v for p, v in declarations(body)
                    if p == "transform-style")]
    for md in gated:
        if not any("prefers-reduced-motion" in q and "no-preference" in q
                   for q in md):
            fails.append(
                f"{CSS}: a preserve-3d rule sits outside @media "
                f"(prefers-reduced-motion: no-preference) - a viewer with "
                f"the OS setting on would get the ring frozen mid-rotation "
                f"instead of the static layout")
            break
    else:
        if gated:
            notes.append("the 3D ring is gated on "
                         "prefers-reduced-motion: no-preference")
    if "reducedMotion" not in title_raw:
        fails.append(f"{TITLE_JS}: never consults fx.js's reducedMotion()")

    # --- 6. phase arithmetic ----------------------------------------------
    spin = re.search(r"--tc-spin:\s*([\d.]+)s", block)
    step = re.search(r"--tc-step:\s*(-?[\d.]+)s", block)
    if not (spin and step):
        fails.append(f"{CSS}: --tc-spin / --tc-step not both declared")
    elif ring_len:
        want = -float(spin.group(1)) / ring_len
        got = float(step.group(1))
        if abs(want - got) > 1e-6:
            fails.append(
                f"{CSS}: --tc-step is {got}s but --tc-spin {spin.group(1)}s "
                f"over {ring_len} cards needs {want}s. The cards would stop "
                f"being largest at the moment they are nearest.")
        else:
            notes.append(f"--tc-step {got}s == -(--tc-spin {spin.group(1)}s "
                         f"/ {ring_len} cards)")

    spacing = re.search(r"rotateY\(calc\(var\(--i\)\s*\*\s*([\d.]+)deg\)\)",
                        block)
    if not spacing:
        fails.append(f"{CSS}: the per-card rotateY(var(--i) * Ndeg) is gone")
    elif ring_len and abs(float(spacing.group(1)) - 360.0 / ring_len) > 1e-6:
        fails.append(
            f"{CSS}: cards are spaced {spacing.group(1)}deg apart but "
            f"{ring_len} of them need {360.0 / ring_len}deg - the ring has a "
            f"gap in it")
    elif spacing:
        notes.append(f"{ring_len} cards at {spacing.group(1)}deg = a closed ring")

    # --- 7. perspective must sit in front of the orbit --------------------
    r = re.search(r"--tc-r:\s*calc\(var\(--tc-w\)\s*\*\s*([\d.]+)\)", block)
    p = re.search(r"--tc-p:\s*calc\(var\(--tc-w\)\s*\*\s*([\d.]+)\)", block)
    if r and p:
        if float(p.group(1)) <= float(r.group(1)):
            fails.append(
                f"{CSS}: perspective {p.group(1)}W is not in front of the "
                f"orbit radius {r.group(1)}W - the nearest card passes "
                f"through the camera plane and inverts")
        else:
            notes.append(f"camera sits "
                         f"{float(p.group(1)) - float(r.group(1)):.3f}W in "
                         f"front of the nearest card")

    return fails, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    fails, notes = check()
    for n in notes:
        print(f"  OK      {n}")

    if args.selftest:
        print()
        controls = [
            ("an absolute /assets/ URL in title.js",
             None,
             lambda s: s.replace("CARD_BACK,", "CARD_BACK, '/assets/cards/back.webp',")),
            ("overflow: hidden on .tc-ring",
             lambda s: s.replace(".title-carousel.tc-motion .tc-ring {",
                                 ".title-carousel.tc-motion .tc-ring "
                                 "{ overflow: hidden;"),
             None),
            ("opacity on .tc-card",
             lambda s: s.replace(".title-carousel.tc-motion .tc-card {",
                                 ".title-carousel.tc-motion .tc-card "
                                 "{ opacity: 0.9;"),
             None),
            ("filter: drop-shadow on .tc-inner",
             lambda s: s.replace(".title-carousel.tc-motion .tc-inner {",
                                 ".title-carousel.tc-motion .tc-inner "
                                 "{ filter: drop-shadow(0 0 2px #000);"),
             None),
            ("--tc-spin changed without --tc-step",
             lambda s: s.replace("--tc-spin: 24s;", "--tc-spin: 18s;"),
             None),
            ("a z-index inside the ring",
             lambda s: s.replace(".tc-face {", ".tc-face { z-index: 3;"),
             None),
        ]
        blind = []
        for label, cm, jm in controls:
            got, _ = check(css_mutate=cm, js_mutate=jm)
            new = len(got) - len(fails)
            print(f"  control: {label:44s} -> "
                  f"{'caught' if new > 0 else 'MISSED'}")
            if new <= 0:
                blind.append(label)
        if blind:
            print()
            print("GATE WEB CAROUSEL: RED - the check does not notice "
                  + "; ".join(blind) + ", so a GREEN from it means nothing")
            return 1

    print()
    if fails:
        print("GATE WEB CAROUSEL: RED")
        for f in fails:
            print("  - " + f)
        return 1
    print("GATE WEB CAROUSEL: GREEN - the ring's paths are relative, its card "
          "set matches the disc, and nothing in its 3D chain flattens it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
