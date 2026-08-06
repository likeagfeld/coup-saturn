#!/usr/bin/env python3
"""
qa_web_sfx.py - Prove the web client's sound effects ARE the Saturn's.

WHY THIS GATE EXISTS
  The web client shipped with no sound effects at all - js/audio.js implements
  BGM and nothing else. The Saturn build has 21 CC0 effects and a per-character
  pitch scheme. Adding "some sounds" to the web would have been easy and wrong:
  the requirement is that the two clients sound like the SAME GAME, which means
  the same samples, fired on the same events, at the same pitches.

  Three things can drift, silently, and none of them is visible in a diff:

    1. The id table. If web `SFX.COUP_STRIKE` stops meaning what
       `COUP_SFX_COUP_STRIKE` means, the wrong sound plays and nothing errors.
    2. The pitch table. coup_sfx_pitch.h transposes each character by a fixed
       Q10 ratio (Duke lowest, Contessa highest). A web table that drifts from
       it makes the Duke sound like the Captain on one platform only.
    3. The event map. A sound can exist, be correct, and never fire.

  This gate reads the C headers and the C call sites as the source of truth and
  checks the JavaScript against them. It also checks the things that make audio
  *work* in a browser at all: a user-gesture unlock, a mute path, no throw on a
  blocked play, and no absolute /assets/ URL (which would resolve against the
  LIVE site from a /staging/ page and serve the wrong file).

WHY IT IS PYTHON WITH NO DEPENDENCIES
  scripts/smoke_web_staging.mjs imports jsdom, which is not vendored, has no
  package.json and is not installed anywhere in this repo - it cannot currently
  run, and a gate that cannot run is not a gate. Everything below is text and
  file-system analysis over files that are already checked in, exactly like
  qa_web_sprite_math.py and qa_portrait_registration.py. No install, no
  network, runs anywhere python3 does.

NEGATIVE CONTROL
  --selftest perturbs the parsed C tables (one pitch ratio, one id number) and
  requires the comparison to notice. A comparison that cannot tell a wrong
  table from a right one is not measuring anything.

Run:  python scripts/qa/qa_web_sfx.py [--selftest]
"""

import argparse
import json
import os
import re
import sys

# .../repo/scripts/qa/qa_web_sfx.py -> .../repo
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

IDS_H = os.path.join(REPO, "examples", "coup", "coup_sfx_ids.h")
PITCH_H = os.path.join(REPO, "examples", "coup", "coup_sfx_pitch.h")
GAME_C = os.path.join(REPO, "examples", "coup", "coup_game.c")

STAGING = os.path.join(REPO, "web-staging")
SFX_JS = os.path.join(STAGING, "js", "sfx.js")
AUDIO_JS = os.path.join(STAGING, "js", "audio.js")
SFX_DIR = os.path.join(STAGING, "assets", "sfx")
MANIFEST = os.path.join(SFX_DIR, "manifest.json")

JS_DIRS = [os.path.join(STAGING, "js"), os.path.join(STAGING, "js", "screens")]

# The whole added sound set must stay small next to the ~8.8 MB of art already
# in web-staging/assets. This is a ceiling, not a target - it exists so nobody
# can drop 20 MB of WAV in here without the gate saying so.
WEIGHT_BUDGET_BYTES = 512 * 1024

# Saturn fires 20 of its 21 effects. card_reveal is shipped but never played,
# on purpose: coup_game.c's INFLUENCE_LOST case says two sounds would collide
# on that event and plays the loss instead. The web client must make the SAME
# choice, so this is an expected difference of zero - the entry documents why
# the id exists without a call site rather than excusing a missing sound.
KNOWN_UNFIRED = {"CARD_REVEAL"}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


# ---------------------------------------------------------------------------
# Source of truth: the C side
# ---------------------------------------------------------------------------

def c_sfx_ids(text, count):
    """{NAME: number} from coup_sfx_ids.h, real ids only.

    coup_sfx_ids.h also #defines COUNT, TOTAL_BYTES, BUDGET_BYTES, RATE_HIGH
    and RATE_LOW with the same COUP_SFX_ prefix. Rather than keep a hand-list
    of those in step with the header, take the definition of an id straight
    from the header's own contract: the ids are exactly the values 0..COUNT-1.
    A new budget constant cannot land in that range; a new id cannot land
    outside it without COUNT moving too, which is separately asserted.
    """
    ids = {}
    for m in re.finditer(r"^#define\s+COUP_SFX_([A-Z0-9_]+)\s+(\d+)\b",
                         text, re.M):
        v = int(m.group(2))
        if 0 <= v < count:
            ids[m.group(1)] = v
    return ids


def c_sfx_aliases(text):
    """{ALIAS: TARGET} for the compatibility #defines at the foot of the file."""
    out = {}
    for m in re.finditer(r"^#define\s+COUP_SFX_([A-Z0-9_]+)\s+COUP_SFX_([A-Z0-9_]+)\s*$",
                         text, re.M):
        out[m.group(1)] = m.group(2)
    return out


def c_pitch_ratios(text):
    """The ratio_q10[5] table from coup_sfx_pitch.h."""
    m = re.search(r"ratio_q10\[5\]\s*=\s*\{(.*?)\}", text, re.S)
    if not m:
        return []
    return [int(v) for v in re.findall(r"(\d+)\s*,", m.group(1))]


def c_fired_ids(text, aliases):
    """Effect names actually passed to coup_audio_play_sfx*() in coup_game.c."""
    fired = set()
    for m in re.finditer(r"coup_audio_play_sfx(?:_as)?\s*\(([^;]*?)\)\s*;",
                         text, re.S):
        for name in re.findall(r"COUP_SFX_([A-Z0-9_]+)", m.group(1)):
            fired.add(aliases.get(name, name))
    return fired


# ---------------------------------------------------------------------------
# The JS side
# ---------------------------------------------------------------------------

def js_sfx_ids(text):
    """{NAME: number} from the SFX map in sfx.js."""
    m = re.search(r"export\s+const\s+SFX\s*=\s*(?:Object\.freeze\()?\{(.*?)\}",
                  text, re.S)
    if not m:
        return {}
    out = {}
    for k, v in re.findall(r"([A-Z0-9_]+)\s*:\s*(\d+)", m.group(1)):
        out[k] = int(v)
    return out


def js_pitch_ratios(text):
    m = re.search(r"CHAR_PITCH_Q10\s*=\s*(?:Object\.freeze\()?\[(.*?)\]",
                  text, re.S)
    if not m:
        return []
    return [int(v) for v in re.findall(r"\d+", m.group(1))]


def js_fired_ids(js_files):
    """Effect names referenced as SFX.<NAME> in real code, not in comments.

    sfx-map.js explains, in a comment, that SFX.CARD_REVEAL deliberately never
    fires. Counting that mention as a call site made this gate report the exact
    opposite of what the code does - so the comment ranges are excluded, the
    same way the absolute-URL check excludes them.
    """
    fired = set()
    for path, text in js_files:
        if os.path.basename(path) == "sfx.js":
            continue        # the definition site, not a call site
        spans = comment_spans(text)
        for m in re.finditer(r"\bSFX\.([A-Z0-9_]+)\b", text):
            if in_comment(spans, m.start()):
                continue
            fired.add(m.group(1))
    return fired


def comment_spans(text):
    """(start, end) of every // and /* */ comment in a JS source.

    Written as a scanner rather than a regex because the two things it has to
    tell apart both contain "//": a line comment, and the "https://" inside a
    string literal. Stripping comments with a regex would eat the rest of any
    line containing a URL - and this file's whole job is to read another file
    correctly, so it does not get to guess. Template literals are treated as
    plain strings; none in this client nest an expression containing a comment.
    """
    spans = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "'\"`":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                j = text.find("\n", i)
                j = n if j < 0 else j
                spans.append((i, j))
                i = j
                continue
            if text[i + 1] == "*":
                j = text.find("*/", i + 2)
                j = n if j < 0 else j + 2
                spans.append((i, j))
                i = j
                continue
        i += 1
    return spans


def in_comment(spans, pos):
    return any(a <= pos < b for a, b in spans)


def collect_js():
    out = []
    for d in JS_DIRS:
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if fn.endswith(".js"):
                p = os.path.join(d, fn)
                out.append((p, read(p)))
    return out


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    for p in (IDS_H, PITCH_H, GAME_C):
        if not os.path.exists(p):
            print(f"GATE WEB SFX: INCONCLUSIVE - {p} not found")
            return 2

    fails = []

    ids_h = read(IDS_H)
    pitch_h = read(PITCH_H)
    game_c = read(GAME_C)

    m = re.search(r"#define\s+COUP_SFX_COUNT\s+(\d+)", ids_h)
    c_count = int(m.group(1)) if m else -1

    c_ids = c_sfx_ids(ids_h, c_count)
    aliases = c_sfx_aliases(ids_h)
    c_ratios = c_pitch_ratios(pitch_h)
    c_fired = c_fired_ids(game_c, aliases)

    print("SATURN, as the source of truth")
    print(f"  coup_sfx_ids.h      : {len(c_ids)} effects, COUP_SFX_COUNT={c_count}")
    print(f"  compat aliases      : {len(aliases)} ({', '.join(sorted(aliases))})")
    print(f"  coup_sfx_pitch.h    : ratio_q10 = {c_ratios}")
    print(f"  fired in coup_game.c: {len(c_fired)} of {len(c_ids)}")

    if len(c_ids) != c_count:
        fails.append(f"coup_sfx_ids.h defines {len(c_ids)} ids but "
                     f"COUP_SFX_COUNT is {c_count}")
    if len(c_ratios) != 5:
        fails.append(f"could not parse ratio_q10[5] from coup_sfx_pitch.h "
                     f"(got {c_ratios})")

    if args.selftest:
        # Perturb the source of truth. Every comparison below must notice.
        print()
        print("  --selftest: corrupting the parsed C tables by one id and one "
              "pitch ratio; every comparison below MUST fail")
        any_id = sorted(c_ids)[0]
        c_ids[any_id] = c_ids[any_id] + 100
        if c_ratios:
            c_ratios[0] += 7
        c_fired = set(list(c_fired)[:-1]) | {"NOT_A_REAL_EFFECT"}

    # --- the JS must exist at all ------------------------------------------
    print()
    print("WEB CLIENT")
    if not os.path.exists(SFX_JS):
        print(f"  MISSING  {os.path.relpath(SFX_JS, REPO)}")
        fails.append("web-staging/js/sfx.js does not exist - the web client "
                     "has no sound effects")
        js_sfx = ""
    else:
        js_sfx = read(SFX_JS)
        print(f"  found    {os.path.relpath(SFX_JS, REPO)} "
              f"({len(js_sfx)} bytes)")

    js_files = collect_js()

    # --- 1. id table parity -------------------------------------------------
    w_ids = js_sfx_ids(js_sfx)
    print(f"  SFX id table        : {len(w_ids)} entries")
    if not w_ids:
        fails.append("sfx.js exports no parseable `export const SFX = { NAME: n }` "
                     "table")
    else:
        missing = sorted(set(c_ids) - set(w_ids))
        extra = sorted(set(w_ids) - set(c_ids) - set(aliases))
        wrong = sorted(n for n in set(c_ids) & set(w_ids)
                       if c_ids[n] != w_ids[n])
        if missing:
            fails.append(f"SFX table is missing {len(missing)} Saturn effect(s): "
                         f"{', '.join(missing)}")
        if extra:
            fails.append(f"SFX table invents {len(extra)} effect(s) the Saturn "
                         f"does not have: {', '.join(extra)}")
        for n in wrong:
            fails.append(f"SFX.{n} = {w_ids[n]} but COUP_SFX_{n} = {c_ids[n]}")
        if not (missing or extra or wrong):
            print(f"    all {len(c_ids)} ids match coup_sfx_ids.h by name AND number")

    # --- 2. pitch table parity ---------------------------------------------
    w_ratios = js_pitch_ratios(js_sfx)
    print(f"  CHAR_PITCH_Q10      : {w_ratios}")
    if w_ratios != c_ratios:
        fails.append(f"CHAR_PITCH_Q10 {w_ratios} != coup_sfx_pitch.h ratio_q10 "
                     f"{c_ratios} - the same character would sound different on "
                     f"the two clients")
    else:
        semis = ", ".join(f"{n}:{r}" for n, r in zip(
            ["Duke", "Assassin", "Captain", "Ambassador", "Contessa"], w_ratios))
        print(f"    matches ratio_q10 exactly ({semis})")

    # playbackRate must be derived from the Q10 table, not hardcoded floats.
    if "playbackRate" not in js_sfx:
        fails.append("sfx.js never sets playbackRate - the per-character pitch "
                     "scheme is not implemented")
    if re.search(r"playbackRate[^\n]*=\s*[\d.]+\s*;", js_sfx):
        fails.append("sfx.js assigns a literal playbackRate; it must be derived "
                     "from CHAR_PITCH_Q10 so the two clients cannot drift")

    # --- 3. event map parity ------------------------------------------------
    w_fired = js_fired_ids(js_files)
    resolved = {aliases.get(n, n) for n in w_fired}
    print(f"  effects fired in JS : {len(resolved)}")
    c_should_fire = c_fired - KNOWN_UNFIRED
    not_on_web = sorted(c_should_fire - resolved)
    not_on_saturn = sorted(resolved - c_fired)
    if not_on_web:
        fails.append(f"{len(not_on_web)} effect(s) the Saturn plays never fire on "
                     f"the web: {', '.join(sorted(not_on_web))}")
    if not_on_saturn:
        fails.append(f"{len(not_on_saturn)} effect(s) fire on the web that the "
                     f"Saturn never plays: {', '.join(not_on_saturn)}")
    unfired_both = sorted(set(c_ids) - c_fired - resolved)
    if not (not_on_web or not_on_saturn):
        print(f"    the two clients fire the same {len(resolved)} effects; "
              f"{unfired_both or 'none'} shipped-but-unfired on both")
    if sorted(KNOWN_UNFIRED) != unfired_both and not args.selftest:
        fails.append(f"shipped-but-unfired set is {unfired_both}, expected "
                     f"{sorted(KNOWN_UNFIRED)} - update KNOWN_UNFIRED and say why")

    # --- 4. assets ----------------------------------------------------------
    print()
    print("ASSETS")
    if not os.path.isdir(SFX_DIR):
        print(f"  MISSING  {os.path.relpath(SFX_DIR, REPO)}/")
        fails.append("web-staging/assets/sfx/ does not exist - there are no "
                     "sound files to play")
        total = 0
        manifest = []
    else:
        exts = sorted({os.path.splitext(f)[1] for f in os.listdir(SFX_DIR)
                       if os.path.splitext(f)[1] not in (".json", ".txt")})
        total = 0
        per_ext = {}
        for fn in os.listdir(SFX_DIR):
            p = os.path.join(SFX_DIR, fn)
            if os.path.isfile(p):
                sz = os.path.getsize(p)
                total += sz
                per_ext[os.path.splitext(fn)[1]] = \
                    per_ext.get(os.path.splitext(fn)[1], 0) + sz
        print(f"  formats             : {', '.join(exts) or 'none'}")
        for e in sorted(per_ext):
            print(f"    {e:<6}            {per_ext[e]:>8,} bytes")
        print(f"  total added weight  : {total:,} bytes "
              f"({total / 1024.0:.1f} KB)")
        if total > WEIGHT_BUDGET_BYTES:
            fails.append(f"sfx assets total {total:,} bytes, over the "
                         f"{WEIGHT_BUDGET_BYTES:,}-byte ceiling")

        for name in sorted(c_ids):
            base = name.lower()
            got = [e for e in exts
                   if os.path.exists(os.path.join(SFX_DIR, base + e))]
            if not got:
                fails.append(f"no audio file for COUP_SFX_{name} "
                             f"(expected assets/sfx/{base}.*)")

        # --- 5. provenance --------------------------------------------------
        if not os.path.exists(MANIFEST):
            fails.append("assets/sfx/manifest.json is missing - per-file "
                         "provenance must survive the placeholders being "
                         "replaced")
            manifest = []
        else:
            try:
                doc = json.loads(read(MANIFEST))
            except Exception as exc:                       # noqa: BLE001
                fails.append(f"manifest.json is not valid JSON: {exc}")
                doc = {}
            manifest = doc.get("effects", []) if isinstance(doc, dict) else []
            print(f"  manifest entries    : {len(manifest)}")
            names = {e.get("name", "").upper() for e in manifest}
            miss = sorted(set(c_ids) - names)
            if miss:
                fails.append(f"manifest.json has no provenance for: "
                             f"{', '.join(miss)}")
            bad_lic = [e.get("name") for e in manifest
                       if "CC0" not in (e.get("licence") or e.get("license") or "")]
            if bad_lic:
                fails.append(f"{len(bad_lic)} manifest entries are not CC0: "
                             f"{', '.join(str(b) for b in bad_lic)}")
            incomplete = [e.get("name") for e in manifest
                          if not all(e.get(k) for k in
                                     ("pack", "file", "author", "url"))]
            if incomplete:
                fails.append(f"{len(incomplete)} manifest entries are missing "
                             f"pack/file/author/url: "
                             f"{', '.join(str(b) for b in incomplete)}")
            if manifest and not (miss or bad_lic or incomplete):
                packs = sorted({e.get("pack") for e in manifest})
                print(f"    all {len(manifest)} entries carry pack/file/author/"
                      f"url and a CC0 licence")
                print(f"    packs: {', '.join(packs)}")

    # --- 6. browser realities ----------------------------------------------
    print()
    print("BROWSER POLICY")
    checks = [
        (r"\.resume\s*\(", "an AudioContext.resume() call (autoplay unlock)"),
        (r"catch\s*[\(\{]", "a catch on the play/decode path (a blocked play "
                            "must never throw)"),
        (r"\bmuted\b", "a mute check"),
    ]
    for pattern, label in checks:
        if re.search(pattern, js_sfx):
            print(f"  present  {label}")
        else:
            fails.append(f"sfx.js is missing {label}")

    if os.path.exists(AUDIO_JS):
        a = read(AUDIO_JS)
        if "sfx" not in a:
            fails.append("audio.js does not talk to sfx.js, so the existing "
                         "Mute button cannot silence the new effects")
        else:
            print("  present  audio.js routes the existing Mute control to sfx.js")

    # --- 7. the /staging/ path trap ----------------------------------------
    abs_urls = []
    for path, text in js_files:
        spans = comment_spans(text)
        for mm in re.finditer(r"""['"`](/assets/[^'"`]*)['"`]""", text):
            # assets.js documents the trap by quoting an example of it inside a
            # comment. Flagging that would make the gate fail on the file that
            # exists to prevent the failure.
            if in_comment(spans, mm.start()):
                continue
            abs_urls.append((os.path.relpath(path, REPO), mm.group(1)))
    if os.path.exists(MANIFEST):
        for mm in re.finditer(r'"(/assets/[^"]*)"', read(MANIFEST)):
            abs_urls.append(("assets/sfx/manifest.json", mm.group(1)))
    if abs_urls:
        for f, u in abs_urls:
            fails.append(f"absolute asset URL {u} in {f} - from a /staging/ "
                         f"page that resolves against the LIVE site")
    else:
        print("  present  no absolute /assets/ URL outside a comment anywhere "
              "in the client")

    # -----------------------------------------------------------------------
    print()
    if args.selftest:
        if not fails:
            print("GATE WEB SFX: RED - the corrupted C tables passed every "
                  "comparison, so this gate cannot tell a drifted web client "
                  "from a correct one and a GREEN from it means nothing")
            return 1
        print(f"GATE WEB SFX: negative control OK - the corrupted tables trip "
              f"{len(fails)} comparison(s), so the comparisons bite")
        return 0

    if fails:
        print(f"GATE WEB SFX: RED - {len(fails)} problem(s)")
        for f in fails:
            print("  - " + f)
        return 1
    print("GATE WEB SFX: GREEN - the web client plays the Saturn's effects, at "
          "the Saturn's pitches, on the Saturn's events, all CC0 and all "
          "provenanced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
