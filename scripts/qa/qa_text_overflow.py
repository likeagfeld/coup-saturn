#!/usr/bin/env python3
"""
qa_text_overflow.py - Prove every label fits the box it is drawn in.

WHAT THIS MEASURES
  For every text draw in coup_render.c: the label's x origin, its pixel WIDTH
  in the font that is active at that call, and the rectangle it is drawn
  inside. A label whose right edge passes its container's right edge is an
  overflow, and is reported with the numbers.

  Two separate verdicts, because they are not the same defect:
    CONTAINER  the label runs past the panel/plate it belongs to. It is
               drawn over whatever is next to it.
    SCREEN     the label runs past x=320 and is simply gone.
  A left-aligned list item that runs to the edge of the SCREEN by design is
  not a bug; one that runs past its own plate is.

WHY A REAL PARSER AND NOT A REGEX
  Two earlier regex audits of this file returned 300-character "string
  literals" that were actually C code: the pattern had matched from a quote
  inside one statement to a quote inside another. A measurement that cannot
  tell a literal from the code around it produces a confident wrong verdict,
  which is worse than no measurement. So this file is scanned by a single-pass
  state machine that classifies every byte as code / line comment / block
  comment / string / char literal, exactly once, in order - the same reason
  qa_web_sprite_math.py balances its parentheses by hand instead of using
  `[^)]*`.

  --selftest runs that scanner over a fixture containing every trap that
  breaks the naive pattern (an escaped quote, a quote inside a comment, a
  comment marker inside a string, a '"' char literal, adjacent-literal
  concatenation) and requires the exact expected output.

WIDTH IS NOT length * 8  (MEASURED)
  The registered faces are, from pal/saturn/fonts/:
      alagard_16x16   cell_width 16, advance_x 8     <- COUP_FONT_DISPLAY
      alagard_8x8     cell_width  8, advance_x 8
      coup_8x8        cell_width  8, advance_x 8     <- COUP_FONT_BODY
      buch_4x6        cell_width  8, advance_x 4     <- COUP_FONT_CONDENSED
  coup_render.c's text_px_w() therefore returns

      (n - 1) * advance + cell

  because the LAST glyph occupies its cell, not its advance. In the display
  face a 4-character label is 40 px, not 32. This gate uses that same formula,
  per font, tracking cui_saturn_font_set_active() through each function - so
  it measures what the renderer measures rather than a convenient
  approximation.

FOUR DRAW CALLS, FOUR DIFFERENT ORIGINS
  draw_at(col, row, ...)             x = col * COUP_FONT_ADVANCE   (CELLS)
  draw_text_sprite(x, y, ...)        x = x                         (PIXELS)
  draw_centered(row, ...)            x = centre of the 320px SCREEN
  draw_centered_in(x, w, y, ...)     x = x + centre within w
  button_centered(x, y, w, h, ...)   x = x + centre within w, in ITS font
  draw_text_font(x, y, text, c, f)   x = x, WHOLE label, in the face `f`
  Treating these as one shape is how an audit gets a wrong answer that looks
  right.

DYNAMIC STRINGS ARE BOUNDED, NOT GUESSED
  Most of the widest text on screen is not a literal - it is a buffer filled
  by snprintf. For those the gate walks back to the nearest preceding
  snprintf/safe_copy into that buffer and expands the format string against
  the bounds of its arguments (coup.h's COUP_MAX_NAME, COUP_LOG_LINE_LEN, the
  fixed name tables). Anything it cannot bound is reported as UNRESOLVED and
  counted - it is never silently assumed to fit.

HOW A LABEL'S BOX IS DECIDED
  The container is inferred geometrically, never declared, except for the two
  boxes that are made of ART rather than of a panel (CONTAINER_OVERRIDE).
  A plate is a candidate only if every brace block enclosing it also encloses
  the label - the lobby draws a whole naming OVERLAY inside
  `if (st->lobby_naming)`, and its panels must not be offered as containers
  for the ordinary controls underneath. Among the candidates:
    both y known   -> the NARROWEST plate containing the label in both axes.
    y loop-driven  -> rank by how many blocks the plate shares with the label,
                      then by width, and take the widest of the deepest. A
                      narrow sibling that merely shares an x (the connecting
                      screen's progress bar sits at the log's x) is not
                      evidence of anything, and choosing it would invent an
                      overflow.
  A plate whose rect does not resolve to constants is not offered at all,
  which can only WIDEN the box chosen - so the gate can miss an overflow but
  cannot manufacture one.

WHAT IT DOES NOT MODEL
  The preprocessor. Both arms of every #ifdef __SATURN__ are analysed, so a
  handful of non-Saturn fallback draws are measured too. That is the
  conservative direction and costs nothing.

RATCHET
  --strict holds each render function against scripts/qa/text_overflow_
  baseline.json. Counts may fall, never rise - for OVERFLOW and for
  UNRESOLVED, so a label cannot be hidden from the gate by making it
  unmeasurable. Same rule as qa_centring --strict, and for the same reason: a
  re-baselined ratchet is not a ratchet.

USAGE
  python scripts/qa/qa_text_overflow.py             # audit
  python scripts/qa/qa_text_overflow.py --selftest  # negative controls
  python scripts/qa/qa_text_overflow.py --strict    # ratchet
  python scripts/qa/qa_text_overflow.py --write-baseline
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(HERE, "text_overflow_baseline.json")

RENDER = "examples/coup/coup_render.c"
UI_H = "examples/coup/coup_ui.h"
COUP_H = "examples/coup/coup.h"
MAIN_SATURN = "examples/coup/saturn/main_saturn.c"
FONT_DIR = "pal/saturn/fonts"

SCREEN_W = 320


# ===========================================================================
# 1. The scanner - one pass, every byte classified exactly once
# ===========================================================================

CODE, LINE_C, BLOCK_C, STR, CHR = range(5)


def scan(src):
    """Classify every byte of `src` exactly once.

    Returns (masked, literals):
      masked   - src with the CONTENTS of comments and of string/char literals
                 replaced by spaces. Length, offsets and newlines are
                 preserved, so a line number computed on `masked` is the line
                 number in `src`. Searching `masked` for a call therefore
                 cannot match text that lives inside a comment or a string.
      literals - list of dicts {start, end, value, line} for each string
                 literal, in source order, with C escapes decoded and
                 ADJACENT literals merged ("a" "b" is one string in C).

    This is the whole reason the gate is trustworthy. A quote is only the
    start of a string if the scanner is in CODE state when it reaches it, and
    it can only be in CODE state if every comment and literal before it was
    closed properly.
    """
    out = []
    lits = []
    state = CODE
    i = 0
    n = len(src)
    line = 1
    lit_start = 0
    lit_line = 1
    buf = []

    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""

        if state == CODE:
            if c == "/" and nxt == "/":
                state = LINE_C
                out.append("  ")
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = BLOCK_C
                out.append("  ")
                i += 2
                continue
            if c == '"':
                state = STR
                lit_start = i
                lit_line = line
                buf = []
                out.append(" ")
                i += 1
                continue
            if c == "'":
                state = CHR
                out.append(" ")
                i += 1
                continue
            out.append(c)
            if c == "\n":
                line += 1
            i += 1
            continue

        if state == LINE_C:
            if c == "\n":
                state = CODE
                out.append("\n")
                line += 1
                i += 1
                continue
            out.append(" ")
            i += 1
            continue

        if state == BLOCK_C:
            if c == "*" and nxt == "/":
                state = CODE
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
            if c == "\n":
                line += 1
            i += 1
            continue

        if state == STR:
            if c == "\\":
                # An escape consumes the next byte whatever it is. This is the
                # trap the regex audits fell into: without it, "a\"b" ends at
                # the escaped quote and the scan resynchronises out of phase
                # for the rest of the file.
                buf.append(_unescape(nxt))
                out.append("  ")
                if nxt == "\n":
                    line += 1
                i += 2
                continue
            if c == '"':
                state = CODE
                lits.append({"start": lit_start, "end": i + 1,
                             "value": "".join(buf), "line": lit_line})
                out.append(" ")
                i += 1
                continue
            buf.append(c)
            out.append("\n" if c == "\n" else " ")
            if c == "\n":
                line += 1
            i += 1
            continue

        # CHR
        if c == "\\":
            out.append("  ")
            i += 2
            continue
        if c == "'":
            state = CODE
        out.append("\n" if c == "\n" else " ")
        if c == "\n":
            line += 1
        i += 1

    masked = "".join(out)

    # C concatenates adjacent string literals. "a" "b" is one 2-char string,
    # and the rules screen relies on that nowhere - but the gate must model
    # the language, not the current file, or it silently under-measures the
    # first label somebody wraps across two lines.
    merged = []
    for lit in lits:
        if merged:
            gap = src[merged[-1]["end"]:lit["start"]]
            if gap.strip() == "":
                merged[-1]["value"] += lit["value"]
                merged[-1]["end"] = lit["end"]
                continue
        merged.append(dict(lit))
    return masked, merged


_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\",
            '"': '"', "'": "'"}


def _unescape(c):
    return _ESCAPES.get(c, c)


# ===========================================================================
# 2. Integer expression evaluation over the project's #defines
# ===========================================================================

TOKEN = re.compile(r"0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|<<|>>|[-+*/%()]")


class Expr(object):
    """A tiny recursive-descent integer evaluator.

    Deliberately not eval(): the macro table is read from source, and a
    stray identifier must fail cleanly (returning None, which the caller
    reports as UNRESOLVED) rather than raise or - far worse - resolve to
    something plausible.
    """

    def __init__(self, macros, depth=0):
        self.macros = macros
        self.depth = depth

    def value(self, text):
        try:
            self.toks = TOKEN.findall(text)
        except TypeError:
            return None
        self.i = 0
        v = self._sum()
        if v is None or self.i != len(self.toks):
            return None
        return v

    def _peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def _sum(self):
        v = self._term()
        if v is None:
            return None
        while self._peek() in ("+", "-"):
            op = self.toks[self.i]
            self.i += 1
            r = self._term()
            if r is None:
                return None
            v = v + r if op == "+" else v - r
        return v

    def _term(self):
        v = self._unary()
        if v is None:
            return None
        while self._peek() in ("*", "/", "%", "<<", ">>"):
            op = self.toks[self.i]
            self.i += 1
            r = self._unary()
            if r is None:
                return None
            if op == "*":
                v = v * r
            elif op == "/":
                if r == 0:
                    return None
                # C integer division truncates toward zero.
                v = int(v / r)
            elif op == "%":
                if r == 0:
                    return None
                v = v - int(v / r) * r
            elif op == "<<":
                v = v << r
            else:
                v = v >> r
        return v

    def _unary(self):
        t = self._peek()
        if t in ("-", "+"):
            self.i += 1
            v = self._unary()
            return None if v is None else (-v if t == "-" else v)
        return self._primary()

    def _primary(self):
        t = self._peek()
        if t is None:
            return None
        if t == "(":
            self.i += 1
            v = self._sum()
            if self._peek() != ")":
                return None
            self.i += 1
            return v
        self.i += 1
        if t.lower().startswith("0x"):
            return int(t, 16)
        if t.isdigit():
            return int(t)
        if self.depth > 16:
            return None
        body = self.macros.get(t)
        if body is None:
            return None
        return Expr(self.macros, self.depth + 1).value(body)


DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+(.+)$",
                    re.M)


def read_macros(paths):
    """Object-like #defines from the given headers, comments already gone."""
    macros = {}
    for p in paths:
        if not os.path.exists(p):
            continue
        masked, _ = scan(open(p, encoding="utf-8", errors="replace").read())
        for m in DEFINE.finditer(masked):
            name, body = m.group(1), m.group(2).strip()
            if "{" in body or body.endswith("\\"):
                continue      # brace initialiser or multi-line: not an int
            macros[name] = body
    return macros


# ===========================================================================
# 3. The COUP_UI layout instance
# ===========================================================================

def split_top(text, sep=",", keep_empty=False):
    """Split on `sep` at brace/paren/bracket depth zero.

    keep_empty MATTERS. In `masked` a string literal is all blanks, so an
    argument that IS a literal strips to "". Dropping it renumbers every
    argument after it - which made the gate read the colour argument as the
    label for two thirds of the file, and report the label as UNRESOLVED. So
    argument splitting keeps empties; initialiser splitting (which has
    trailing commas) does not.
    """
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "{([":
            depth += 1
        elif ch in "})]":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    if keep_empty:
        return [p.strip() for p in parts]
    return [p.strip() for p in parts if p.strip() != ""]


def balanced(text, i, open_ch="{", close_ch="}"):
    """Return the index just past the brace group starting at text[i]."""
    depth = 0
    while i < len(text):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def parse_initialiser(body, macros, ev):
    """Turn a designated-initialiser body into a nested dict / tuple tree.

    Positional brace groups become tuples in order - a cui_rect_t is
    {x, y, w, h} and a cui_point_t is {x, y} (core/include/cui_types.h:124-135),
    so a 4-tuple is read as a rect and a 2-tuple as a point.
    """
    out = {}
    for field in split_top(body):
        if not field.startswith("."):
            continue
        if "=" not in field:
            continue
        name, val = field.split("=", 1)
        name = name.strip().lstrip(".")
        val = val.strip()
        if val.startswith("{"):
            end = balanced(val, 0)
            inner = val[1:end - 1]
            if ".": pass
            if inner.lstrip().startswith("."):
                out[name] = parse_initialiser(inner, macros, ev)
            else:
                nums = [ev.value(p) for p in split_top(inner)]
                out[name] = tuple(nums)
        else:
            # A whole-struct macro (GAME_RESPONSE_LAYOUT / COUP_FULLSCREEN_BG)
            body2 = macros.get(val)
            if body2 is not None:
                b = body2.strip()
                if b.startswith("{"):
                    inner = b[1:balanced(b, 0) - 1]
                    if inner.lstrip().startswith("."):
                        out[name] = parse_initialiser(inner, macros, ev)
                    else:
                        out[name] = tuple(ev.value(p)
                                          for p in split_top(inner))
                    continue
            out[name] = ev.value(val)
    return out


def read_layout(ui_src, macros, ev):
    """Parse the static COUP_UI instance out of coup_ui.h."""
    masked, _ = scan(ui_src)
    m = re.search(r"COUP_UI\s*=\s*\{", masked)
    if not m:
        return {}
    start = masked.index("{", m.start())
    end = balanced(masked, start)
    return parse_initialiser(masked[start + 1:end - 1], macros, ev)


# Multi-line macros that expand to a whole struct body. read_macros() skips
# anything with a brace, so these two are recovered here with their line
# continuations joined.
def read_brace_macros(paths):
    out = {}
    for p in paths:
        if not os.path.exists(p):
            continue
        masked, _ = scan(open(p, encoding="utf-8", errors="replace").read())
        joined = masked.replace("\\\n", " ")
        for m in re.finditer(
                r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+(\{.*)$",
                joined, re.M):
            out[m.group(1)] = m.group(2).strip()
    return out


# ===========================================================================
# 4. Fonts - MEASURED from the font sources, not assumed
# ===========================================================================

def read_fonts(root):
    """(cell_width, advance_x) for every registered face, by registry index.

    Index order is REGISTRATION order, which main_saturn.c fixes and
    coup_ui.h's COUP_FONT_* names mirror:
      0  the PAL's built-in 8x8   (pal/saturn/saturn_font.c:269-271)
      1  COUP_FONT_DISPLAY        alagard 16x16
      2  COUP_FONT_ALAGARD        alagard 8x8
      3  COUP_FONT_BODY           coup 8x8
      4  COUP_FONT_CONDENSED      buch 4x6 - same 8 px CELL, HALF the
                                  advance, so it is the only face in which a
                                  39-character log line (160 px) fits a
                                  container that also has a border.
    """
    def desc(path):
        if not os.path.exists(path):
            return None
        src = open(path, encoding="utf-8", errors="replace").read()
        cw = re.search(r"cell_width\s*=\s*(\d+)", src)
        ax = re.search(r"advance_x\s*=\s*(\d+)", src)
        if not (cw and ax):
            return None
        return (int(cw.group(1)), int(ax.group(1)))

    fonts = {
        0: desc(os.path.join(os.path.dirname(root), "saturn_font.c")),
        1: desc(os.path.join(root, "saturn_font_alagard_16x16.c")),
        2: desc(os.path.join(root, "saturn_font_alagard_8x8.c")),
        3: desc(os.path.join(root, "saturn_font_coup_8x8.c")),
        4: desc(os.path.join(root, "saturn_font_buch_4x6.c")),
    }
    return {k: v for k, v in fonts.items() if v}


def px_w(s, font):
    """coup_render.c's text_px_w(), to the letter.

    Width is (n-1) advances plus one full CELL, because the last glyph
    occupies its cell. For a face whose cell equals its advance this is
    n * advance, so the 8x8 faces are unaffected - but the display face is
    16 px wide on an 8 px advance and is 8 px wider than n*advance for every
    non-empty string.
    """
    cell, adv = font
    n = len(s) if isinstance(s, str) else int(s)
    if n <= 0:
        return 0
    return (n - 1) * adv + cell


def chars_for_px(avail, font):
    """How many characters of `font` fit in `avail` pixels."""
    cell, adv = font
    if avail < cell:
        return 0
    return 1 + (avail - cell) // adv


# ===========================================================================
# 5. Bounds for the strings that are not literals
# ===========================================================================
#
# Every entry is a MEASURED bound with the constant it comes from. An
# expression that is not in here is reported UNRESOLVED - never assumed.

def arg_bounds(macros):
    name_max = (macros_int(macros, "COUP_MAX_NAME") or 16) - 1
    log_max = macros_int(macros, "COUP_LOG_LINE_LEN") or 39
    return {
        # Player names: coup.h `char name[COUP_MAX_NAME]`, so MAX_NAME-1 chars.
        "p->name": name_max,
        "self->name": name_max,
        "winner_name": name_max,
        "st->winner_name": name_max,
        # A wrapped row. coup_wrap_row() breaks on spaces only and writes at
        # most `max_chars` characters into a `char row[GAME_TITLE_MAX_CHARS+1]`
        # - every caller passes GAME_TITLE_MAX_CHARS as the width, and the
        # buffer is declared from the same macro, so the two cannot drift.
        #
        # The one case that could exceed it is a single token longer than the
        # wrap width, which is emitted whole rather than split (splitting a
        # word would be the truncation this whole change removes). No caller
        # can build one: tests/coup/test_text_wrap.c walks every name length
        # COUP_MAX_NAME allows against every character and action name for all
        # four title formats - 270 reachable titles - and requires each to
        # wrap into at most 2 rows of at most GAME_TITLE_MAX_CHARS.
        "row": macros_int(macros, "GAME_TITLE_MAX_CHARS") or 21,
        # Log ring: coup.h COUP_LOG_LINE_LEN, enforced by coup_log()'s copy.
        "st->log[idx]": log_max,
        "st->log[ring_idx]": log_max,
        # Fixed tables in coup.h.
        "coup_action_names[action_id]": 11,      # "Foreign Aid"
        "coup_action_names[st->declared_action]": 11,
        "act_name": 11,
        "coup_char_names[ch]": 10,               # "Ambassador"
        "cname": 10,
        "name": 10,
        "claim_name": 10,
        "block_char_name": 10,
        "coup_char_short[c0]": 2,
        "coup_char_short[c1]": 2,
        "c0": 2,
        "c1": 2,
        "diff_names[d]": 4,                      # "Hard" / "Medium" handled
        "diff_names[j]": 6,                      # settings: "Medium"
        # Single-character cursors.
        "cur": 1,
        "cursor": 1,
        "cursor_str": 1,
        "cur_char": 1,
        # Actor names truncated by the format's own %.10s.
        "actor_name": name_max,
        "blocker_name": name_max,
        "turn_name": name_max,
        "st->players[ti].name": name_max,
        "st->players[ai].name": name_max,
        "st->players[bi].name": name_max,
        "st->players[pid].name": name_max,
        # render_selection_list() takes its label list and its hint from its
        # callers, so their bounds are the widest any caller builds:
        #   "Block as Ambassador"        render_phase_block_wait, 19 chars
        #   "%-12s %s" + "Swap"          render_phase_select_action, 17
        #   " %-12s"                     lose_influence / exchange_pick, 13
        #   "Challenge"                  challenge / block_challenge, 9
        "items[i].label": 19,
        #   " %d/%d  [A] Toggle"         render_phase_exchange_pick, 18 chars
        "hint": 18,
        'items[i].selected ? "[X]" : "[ ]"': 3,
        # Small integers.
        "p->coins": 3,
        "self->coins": 3,
        "st->player_count": 1,
        "ready_count": 1,
        "cards_to_keep": 1,
        "st->exchange_count": 1,
        "sel_count": 1,
        "st->auth_retries": 1,
        "pg + 1": 1,
        "COUP_RULES_PAGES": 1,
        "i + 1": 1,
        "5": 1,
    }


def macros_int(macros, name):
    return Expr(macros).value(name)


FMT = re.compile(r"%([-+ #0]*)(\d*)(?:\.(\d+))?(?:hh|h|ll|l|z)?([diouxXcsfgep%])")


def fmt_max_chars(fmt, resolve, nargs):
    """Longest string the format can produce, or None if an arg is unbounded.

    `resolve(i)` returns the bound of the i'th variadic argument.

    A %-12s does NOT truncate - it PADS. Reading it as a truncation is how a
    15-character name gets measured as 12 and a real overflow is missed. Only
    a precision (%.10s) truncates.
    """
    total = 0
    pos = 0
    ai = 0
    for m in FMT.finditer(fmt):
        total += m.start() - pos
        pos = m.end()
        conv = m.group(4)
        if conv == "%":
            total += 1
            continue
        width = int(m.group(2)) if m.group(2) else 0
        prec = int(m.group(3)) if m.group(3) else None
        if ai >= nargs:
            return None
        b = resolve(ai)
        ai += 1
        if conv == "c":
            got = 1
        elif conv == "s":
            if b is None:
                return None
            got = min(b, prec) if prec is not None else b
        elif conv in "diouxX":
            if b is None:
                return None
            got = b
        else:
            return None
        total += max(width, got)
    total += len(fmt) - pos
    return total


# A bound that depends on WHICH format string an argument is used in.
#
# render_selection_list() builds its row two ways from the same
# `items[i].label`. The single-select form is reachable from every caller, so
# it takes the widest label any of them builds - "Block as Ambassador", 19
# chars, from render_phase_block_wait(). The MULTI-select form only runs when
# a hint is passed, and render_phase_exchange_pick() is the only caller that
# passes one; its labels are built with " %-12s" and so are 13 chars. Bounding
# both at 19 would report a 28 px overflow on a row that cannot occur.
FORMAT_SCOPED_BOUNDS = {
    ("%s%s %s", "items[i].label"): 13,
    ("%s%s", "items[i].label"): 19,
}

# Buffers this file fills a character at a time rather than with snprintf.
# Each bound is derived from the loop that fills it, not from sizeof - a
# sizeof bound would be a large over-estimate and would manufacture overflows
# that do not exist, which is the failure mode this whole gate exists to
# avoid.
BUFFER_BOUNDS = {
    # coup_render_name_entry / the lobby's naming overlay: two leading spaces
    # then exactly one character per iteration of
    #   for (i = 0; i < COUP_MAX_NAME - 1; i++)
    ("coup_render_name_entry", "name_display"): ("2 + COUP_MAX_NAME - 1", 17),
    ("coup_render_lobby", "name_display"): ("2 + COUP_MAX_NAME - 1", 17),
    # coup_render_game_over: the winner's name uppercased, then " WINS!".
    # The di<40 / di<46 guards are buffer safety, not the real bound - a name
    # is at most COUP_MAX_NAME - 1 = 15 characters.
    ("coup_render_game_over", "line"): ("COUP_MAX_NAME - 1 + len(' WINS!')",
                                        21),
    # coup_render_connecting: dots = (anim_timer / 20) % 4, so 0..3.
    ("coup_render_connecting", "dotstr"): ("(anim_timer / 20) % 4", 3),
}


# ===========================================================================
# 6. Call extraction
# ===========================================================================

def find_calls(masked, name):
    """Every call to `name` in `masked`, with balanced-paren argument split.

    `masked` has comment and literal CONTENT blanked, so a call cannot be
    matched inside either - but the offsets still line up with the original,
    which is how the literal at an argument position is recovered.
    """
    out = []
    pat = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    for m in pat.finditer(masked):
        end = balanced(masked, m.end() - 1, "(", ")")
        if end < 0:
            continue
        out.append({"start": m.start(), "arg_start": m.end(),
                    "arg_end": end - 1,
                    "args": split_top(masked[m.end():end - 1],
                                      keep_empty=True),
                    "line": masked[:m.start()].count("\n") + 1})
    return out


def functions(masked):
    """(name, start, end) for every function definition body in the file."""
    out = []
    for m in re.finditer(r"\b(?:static\s+)?(?:void|int|bool)\s+(\w+)\s*"
                         r"\([^;{]*\)\s*\{", masked):
        i = masked.index("{", m.end() - 1)
        j = balanced(masked, i)
        if j < 0:
            continue
        out.append((m.group(1), i, j))
    return out


# ===========================================================================
# 7. Containers that are art, not panels
# ===========================================================================
#
# The gate infers a container by geometry: the NARROWEST panel/plate drawn in
# the same function whose x-span contains the label's origin. A panel whose
# rect does not resolve to constants (one positioned by a loop variable) is
# simply not offered as a container, which can only ever WIDEN the container
# chosen - it can make the gate miss an overflow, never invent one.
#
# Two containers are not panels at all and so must be declared:

CONTAINER_OVERRIDE = {
    # The hand's name and coin counter live in the gap BETWEEN the two 48x72
    # card faces, not in the whole hand panel. coup_ui.h documents the intent
    # at .card0_x: "Panel is x 72..248; 4 px inset each side leaves a 72 px
    # gap in the middle, which the name and coin counter already fit."
    # card0 ends at card0_x + 48 = 124; card1 begins at 196. The cards are
    # drawn AFTER the name, so a name past 196 is not merely tight - it is
    # painted over by the card art.
    ("render_your_hand", "H->name_x"): ("hand card gap", 124, 72),
    ("render_your_hand", "H->coins_x"): ("hand card gap", 124, 72),
}

# render_single_seat() is called with a seat box computed in render_seats().
# Only the COLUMN varies in x, so there are exactly two cases. Formula from
# render_seats(): col_x = right_x for i>=3 else left_x; name/cards/coins x are
# col_x + the layout insets.
SEAT_COLUMNS = ("left", "right")

# render_selection_list() takes its layout as a parameter. Every call site
# passes the same symbol; the gate asserts that rather than assuming it.
PARAM_LAYOUT = {
    "render_selection_list": ("layout", "game.select_action"),
    "render_timer_bar": ("layout", "game.select_action"),
    "render_single_seat": ("layout", "game.seats"),
}


# ===========================================================================
# 8. The analysis
# ===========================================================================

TEXT_CALLS = ("draw_at", "draw_centered", "draw_centered_in",
              "button_centered", "draw_text_font", "draw_text_sprite")

PANEL_CALLS = ("panel", "panel_grd", "panel_lit")

# One character cell of the widest registered face (alagard 16x16). Nothing
# narrower can be a text container - see pick_container().
MIN_CONTAINER_W = 16

# The generic draw helpers at the top of coup_render.c are plumbing, not
# screens: draw_at() forwards to draw_text_sprite() with no label of its own.
HELPERS = ("draw_at", "draw_centered", "draw_centered_in", "button_centered",
           "draw_text_font", "panel", "panel_r", "panel_lit", "panel_grd",
           "hline", "vline", "screen_bg")


class Site(object):
    def __init__(self, fn, line, kind, label, chars, font, x, container,
                 note="", y=None):
        self.y = y
        self.fn = fn
        self.line = line
        self.kind = kind
        self.label = label
        self.chars = chars
        self.font = font
        self.x = x
        self.container = container      # (name, x, w) or None
        self.note = note

    @property
    def w(self):
        return px_w(self.chars, self.font)

    @property
    def right(self):
        return self.x + self.w


class Analyser(object):
    def __init__(self, render_src, macros, layout, fonts, bounds):
        self.src = render_src
        self.masked, self.lits = scan(render_src)
        self.macros = macros
        self.layout = layout
        self.fonts = fonts
        self.bounds = bounds
        self.ev = Expr(macros)
        self.default_font = 3       # main_saturn.c: set_active(COUP_FONT_BODY)
        self.local_consts = {}
        self.cur_blocks = []
        self.cur_draw = 0
        self.sites = []
        self.unresolved = []

    # -- helpers ---------------------------------------------------------

    def lit_at(self, start, end):
        for l in self.lits:
            if start <= l["start"] and l["end"] <= end:
                return l
        return None

    def resolve_layout_path(self, path):
        cur = self.layout
        for part in path.split("."):
            if not isinstance(cur, dict) or part not in cur:
                return None
            cur = cur[part]
        return cur

    def local_ints(self, body):
        """`const int panel_x = 24;` -> {"panel_x": "24"}.

        Only names DECLARED with an initialiser and never assigned again, so a
        loop counter or an accumulator can never be mistaken for a constant.
        The game-over recap panel is built entirely from locals like these
        (panel_x / panel_w / panel_y / row_h), so without this its rows cannot
        be measured at all.
        """
        out = {}
        for m in re.finditer(r"\b(?:const\s+)?int\s+([^;{}()]*);", body):
            for decl in split_top(m.group(1)):
                if "=" not in decl:
                    continue
                name, expr = decl.split("=", 1)
                name, expr = name.strip(), expr.strip()
                if not re.match(r"^\w+$", name):
                    continue
                writes = len(re.findall(r"\b" + re.escape(name) +
                                        r"\s*(?:=[^=]|\+\+|--|\+=|-=)", body))
                if writes != 1:
                    continue
                if re.search(r"(?:\+\+|--)\s*\b" + re.escape(name), body):
                    continue
                out[name] = expr
        return out

    def scope_symbols(self, body):
        """`const coup_x_layout_t* L = &COUP_UI.a.b;` -> {"L": "a.b"}."""
        syms = {}
        for m in re.finditer(r"(\w+)\s*=\s*&\s*COUP_UI\.([\w.]+)\s*;", body):
            syms[m.group(1)] = m.group(2)
        return syms

    def evaluate(self, expr, syms, extra=None):
        """Numeric value of an expression, or None.

        Layout member accesses are substituted first, then the result is fed
        to the integer evaluator - so `GAME_TEXT_X + 8` and `L->main_panel.x`
        and `SA->item_x + COUP_ITEM_TEXT_INSET` all resolve, and anything
        containing a genuinely runtime term (a loop index, a measured text
        width) resolves to None and is reported rather than guessed.
        """
        e = expr.strip()
        if extra:
            for k, v in extra.items():
                e = re.sub(r"\b" + re.escape(k) + r"\b", str(v), e)

        def sub(m):
            sym, member = m.group(1), m.group(2)
            path = syms.get(sym)
            if path is None:
                return m.group(0)
            val = self.resolve_layout_path(path + "." + member.split(".")[0])
            if val is None:
                return m.group(0)
            rest = member.split(".")[1:]
            if isinstance(val, tuple):
                if not rest:
                    return m.group(0)
                idx = {"x": 0, "y": 1, "w": 2, "h": 3}.get(rest[0])
                if idx is None or idx >= len(val) or val[idx] is None:
                    return m.group(0)
                return str(val[idx])
            if rest:
                return m.group(0)
            return str(val) if isinstance(val, int) else m.group(0)

        e = re.sub(r"\b(\w+)\s*->\s*([\w.]+)", sub, e)
        e = re.sub(r"\bCOUP_UI\.([\w.]+)", lambda m: self._ui(m.group(1)), e)

        # Local constants last, and iteratively: the game-over recap is built
        # entirely out of them (panel_x / panel_w / panel_y / row_h) and they
        # refer to one another.
        def loc(m):
            v = self.local_consts.get(m.group(1))
            return "(%s)" % v if v is not None else m.group(0)

        for _ in range(8):
            prev = e
            e = re.sub(r"\b([A-Za-z_]\w*)\b", loc, e)
            if e == prev:
                break
        return self.ev.value(e)

    def _ui(self, path):
        parts = path.split(".")
        val = self.resolve_layout_path(".".join(parts[:-1]))
        if isinstance(val, tuple):
            idx = {"x": 0, "y": 1, "w": 2, "h": 3}.get(parts[-1])
            if idx is not None and idx < len(val) and val[idx] is not None:
                return str(val[idx])
        v = self.resolve_layout_path(path)
        return str(v) if isinstance(v, int) else path

    def rect_of(self, expr, syms, extra=None):
        """A rect-valued expression (`L->main_panel`, `hdr`) -> (x,y,w,h)."""
        e = expr.strip()
        m = re.match(r"^(\w+)\s*->\s*(\w+)$", e)
        if m and m.group(1) in syms:
            v = self.resolve_layout_path(syms[m.group(1)] + "." + m.group(2))
            if isinstance(v, tuple) and len(v) == 4:
                return v
        m = re.match(r"^COUP_UI\.([\w.]+)$", e)
        if m:
            v = self.resolve_layout_path(m.group(1))
            if isinstance(v, tuple) and len(v) == 4:
                return v
        return None

    # -- containers ------------------------------------------------------

    def containers(self, fn, body, offset, syms, extra=None):
        """Every resolvable plate drawn in this function.

        (name, x, w, y, h) - y and h may be None when the plate is positioned
        by a loop variable. A plate whose rect does not resolve at all is not
        offered as a container, which can only ever WIDEN the container chosen
        and so can make the gate MISS an overflow, never invent one.
        """
        out = [("screen", 0, SCREEN_W, 0, 224, -1)]
        for call in PANEL_CALLS:
            for c in find_calls(body, call):
                if len(c["args"]) < 4:
                    continue
                x = self.evaluate(c["args"][0], syms, extra)
                w = self.evaluate(c["args"][2], syms, extra)
                if x is None or w is None or w <= 0:
                    continue
                y = self.evaluate(c["args"][1], syms, extra)
                h = self.evaluate(c["args"][3], syms, extra)
                out.append(("%s@%d" % (call, offset + c["line"] - 1),
                            x, w, y, h, c["start"]))
        for c in find_calls(body, "panel_r"):
            if not c["args"]:
                continue
            r = self.rect_of(c["args"][0], syms, extra)
            if r and r[0] is not None and r[2]:
                out.append(("panel_r@%d" % (offset + c["line"] - 1),
                            r[0], r[2], r[1], r[3], c["start"]))
        # local `cui_rect_t foo = {x, y, w, h};`
        for m in re.finditer(r"cui_rect_t\s+(\w+)\s*=\s*\{([^}]*)\}", body):
            nums = [self.evaluate(p, syms, extra)
                    for p in split_top(m.group(2))]
            if len(nums) == 4 and None not in nums and nums[2] > 0:
                out.append((m.group(1), nums[0], nums[2], nums[1], nums[3],
                            m.start()))
        return out

    def blocks(self, body):
        """(start, end) of every brace block in the function body."""
        out = []
        stack = []
        for i, ch in enumerate(body):
            if ch == "{":
                stack.append(i)
            elif ch == "}" and stack:
                out.append((stack.pop(), i))
        return out

    def in_scope(self, blocks, cand_off, draw_off):
        """Is a plate drawn at cand_off a container for text at draw_off?

        Only if every brace block that encloses the plate also encloses the
        text. The lobby draws a whole naming OVERLAY inside
        `if (st->lobby_naming) { ... }`; its 232 px controls panel is
        geometrically the narrowest rect containing the ordinary "[Z] Online"
        hint, but the two are never on screen together. Without this test the
        gate reports an overflow against a panel from a mutually exclusive
        branch - a confident wrong answer.
        """
        if cand_off < 0:
            return True
        for b0, b1 in blocks:
            if b0 <= cand_off < b1 and not (b0 <= draw_off < b1):
                return False
        return True

    def pick_container(self, cands, x, y, override, blocks=(), draw_off=0):
        """The box a label belongs to, in two tiers.

        TIER 1 - the narrowest plate that contains the label's origin in BOTH
        axes. This is the only geometrically honest answer, and it is the one
        used whenever the plate's y and the label's y both resolve.

        TIER 2 - when a y does not resolve (a plate or a row positioned by a
        loop index), fall back to x-containment, but only among plates at
        least MIN_CONTAINER_W wide.

        The width floor is not a fudge factor, it is a type test. The lobby's
        ready indicator is a panel_grd 8 px wide by 12 tall drawn at the same
        x as the slot text; on x alone it is the "narrowest containing plate"
        for every control label on the screen, and the gate duly reported
        fifteen 200-px overflows against an 8 px bar. A rect narrower than one
        character CELL of the widest registered face cannot be the box that
        frames a label - it is a rule, a bar or a colour band.
        """
        if override:
            return override

        usable = [c for c in cands
                  if c[1] <= x < c[1] + c[2]
                  and self.in_scope(blocks, c[5], draw_off)]

        # TIER 1: both y coordinates known - the narrowest plate containing
        # the label's origin in both axes.
        t1 = [c for c in usable
              if y is not None and None not in (c[3], c[4])
              and c[3] <= y < c[3] + c[4]]
        if t1:
            b = min(t1, key=lambda c: c[2])
            return (b[0], b[1], b[2])

        # TIER 2: the label's y is loop-driven (a list row), so no plate can be
        # confirmed by geometry alone. Rank by BLOCK DEPTH first and width
        # second:
        #
        #   depth - how many brace blocks enclose both the plate and the
        #           label. The panel that frames a list is written in the same
        #           block as the loop that fills it, so it shares more blocks
        #           with the rows than a screen-wide background declared at
        #           function scope does. Without this the game-over recap rows
        #           were measured against the full-screen background instead of
        #           the 272 px recap panel they are actually inside.
        #   width - widest at equal depth. With the row's y unknown, a NARROW
        #           sibling elsewhere on the screen (the connecting screen's
        #           progress bar shares the log's x) is not evidence of
        #           anything, and picking it would invent an overflow. The
        #           enclosing plate is the only claim the geometry supports.
        t2 = [c for c in usable
              if c[5] >= 0 and c[2] >= MIN_CONTAINER_W]
        if t2:
            def depth(c):
                return sum(1 for b0, b1 in blocks
                           if b0 <= c[5] < b1 and b0 <= draw_off < b1)
            b = max(t2, key=lambda c: (depth(c), c[2]))
            return (b[0], b[1], b[2])
        return ("screen", 0, SCREEN_W)

    # -- string bounds ---------------------------------------------------

    # Call offsets come from find_calls() over a FUNCTION BODY, while literal
    # offsets are absolute in the file. `base` is the function's start offset
    # and every comparison between the two must add it. Getting this wrong
    # made every literal look like it belonged to no argument at all.
    base = 0

    def arg_index_at(self, call, off):
        """Which top-level argument of `call` contains source offset `off`.

        Counting commas at depth zero is the only correct answer here.
        `split_top` drops empty parts, so using its length as an index was
        wrong - and wrong in the silent direction: it made the gate read the
        COLOUR argument as the label for most of the file.
        """
        depth = 0
        idx = 0
        lo = self.base + call["arg_start"]
        hi = min(off, self.base + call["arg_end"])
        for i in range(lo, hi):
            ch = self.masked[i]
            if ch in "({[":
                depth += 1
            elif ch in ")}]":
                depth -= 1
            elif ch == "," and depth == 0:
                idx += 1
        return idx

    def lits_in_arg(self, call, argi):
        lo = self.base + call["arg_start"]
        hi = self.base + call["arg_end"]
        return [l for l in self.lits
                if lo <= l["start"] < hi
                and self.arg_index_at(call, l["start"]) == argi]

    def buffer_bound(self, body, upto, var, syms, fn):
        """Longest `var` can be, from the nearest preceding fill of it."""
        key = (fn, var)
        if key in BUFFER_BOUNDS:
            why, n = BUFFER_BOUNDS[key]
            return n, "%s (%s)" % (var, why)

        # Which fill of `var` reaches this draw?
        #
        # "the nearest preceding one" is wrong, because the fills are often in
        # the arms of an if/else that the draw sits AFTER:
        #   render_selection_list() builds its row one way for a multi-select
        #   list and another for a single-select one, then draws once. Taking
        #   the nearer arm under-measures the wider one.
        # "the widest of all preceding ones" is equally wrong: the lobby fills
        # one `line` buffer six times down the function, and the widest of
        # those has nothing to do with the narrow difficulty tag drawn at
        # x=232.
        #
        # The fill that reaches a draw is the one written in the most specific
        # scope the two SHARE. Rank every preceding fill by how many brace
        # blocks enclose both it and the draw, keep those at the maximum, and
        # take the widest of them - so sibling arms of one if/else are all
        # kept, and a fill from an outer scope that a nearer arm overwrote is
        # dropped.
        fills = []
        for call, kind in (("snprintf", "fmt"), ("safe_copy", "copy"),
                           ("coup_strcpy", "copy")):
            for c in find_calls(body[:upto], call):
                if not c["args"] or c["args"][0].strip() != var:
                    continue
                shared = sum(1 for b0, b1 in self.cur_blocks
                             if b0 <= c["start"] < b1
                             and b0 <= self.cur_draw < b1)
                fills.append((shared, c, kind))
        if not fills:
            return None, None
        top = max(f[0] for f in fills)
        fills = [f for f in fills if f[0] == top]

        best_n, best_note = None, None
        for _shared, c, kind in fills:
            n, note = self.one_fill(body, var, c, kind, syms, fn)
            if n is not None and (best_n is None or n > best_n):
                best_n, best_note = n, note
        return best_n, best_note

    def one_fill(self, body, var, c, kind, syms, fn):

        if kind == "copy":
            # safe_copy(dst, src, max) / coup_strcpy(dst, src, max) keep at
            # most max-1 characters, but a SHORTER source keeps fewer.
            n = self.evaluate(c["args"][2], syms)
            if n is None:
                m = re.search(r"sizeof\s*\(\s*(\w+)\s*\)", c["args"][2])
                if m:
                    n = self.decl_size(body, m.group(1))
            if n is None:
                return None, None
            src, _lbl = self.str_bound(body, c, 1, syms, fn)
            cap = max(0, n - 1)
            got = cap if src is None else min(cap, src)
            return got, "%s (safe_copy to %d)" % (var, cap)

        lits = self.lits_in_arg(c, 2)
        if not lits:
            return None, None
        fmt = lits[0]["value"]
        nargs = max(0, len(c["args"]) - 3)

        def resolve(i):
            key = (fmt, c["args"][3 + i].strip() if 3 + i < len(c["args"])
                   else "")
            if key in FORMAT_SCOPED_BOUNDS:
                return FORMAT_SCOPED_BOUNDS[key]
            n, _l = self.str_bound(body, c, 3 + i, syms, fn)
            return n

        got = fmt_max_chars(fmt, resolve, nargs)
        if got is None:
            return None, None
        cap = self.evaluate(c["args"][1], syms)
        if cap is None:
            m = re.search(r"sizeof\s*\(\s*([\w\[\]]+)\s*\)", c["args"][1])
            if m:
                cap = self.decl_size(body, m.group(1).split("[")[0])
        if cap is not None:
            got = min(got, cap - 1)
        return got, "%s = %r (%d chars)" % (var, fmt, got)

    def decl_size(self, body, var):
        m = re.search(r"\bchar\s+" + re.escape(var) + r"\s*\[([^\]]*)\]", body)
        if not m:
            m = re.search(r"\bchar\s+" + re.escape(var) +
                          r"\s*\[\d*\]\s*\[([^\]]*)\]", body)
        if not m:
            return None
        return self.evaluate(m.group(1), {})

    def value_parts_off(self, rhs):
        """[(part, offset)] - the value-producing arms of `cond ? A : B`.

        The condition is NOT a value, so it must not be scanned for a bound;
        `is_ready ? " READY" : "------"` has two arms, not three operands.
        """
        depth = 0
        q = -1
        for i, ch in enumerate(rhs):
            if ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            elif ch == "?" and depth == 0:
                q = i
                break
        if q < 0:
            return [(rhs, 0)]
        rest = rhs[q + 1:]
        depth = 0
        for i, ch in enumerate(rest):
            if ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            elif ch == ":" and depth == 0:
                return [(rest[:i], q + 1), (rest[i + 1:], q + 1 + i + 1)]
        return [(rhs, 0)]

    def local_bound(self, body, var, syms, fn):
        """Bound of a `const char*` local from every assignment to it.

        A variable assigned BOTH a literal and something else must be bounded
        by the something else too - taking only the literals would silently
        under-measure (`turn_name` starts as "" and is then set to a player's
        name).
        """
        # `=(?!=)([^;]*)` and NOT `=\s*([^;]*)`. In `masked` a string literal
        # is all spaces, so a greedy \s* swallows the whole right-hand side
        # and every `x = "literal";` captures the empty string - which is how
        # `stage_text` and every other literal-assigned local came back
        # UNRESOLVED.
        pat = re.compile(r"\b" + re.escape(var) + r"\s*=(?!=)([^;]*);")
        best = None
        found = False
        for m in pat.finditer(body):
            found = True
            base = self.base + m.start(1)
            rhs = m.group(1)
            for part, off in self.value_parts_off(rhs):
                # In `masked` a string literal is all blanks, so a value part
                # with no word character left in it IS a literal.
                if re.search(r"\w", part) is None:
                    lit = [l for l in self.lits
                           if base + off <= l["start"] < base + off + len(part)]
                    if lit:
                        n = max(len(l["value"]) for l in lit)
                        best = n if best is None else max(best, n)
                        continue
                b = self.bounds.get(part.strip())
                if b is None:
                    return None, None
                best = b if best is None else max(best, b)
        if not found or best is None:
            return None, None
        return best, "%s (local, widest assignment)" % var

    def str_bound(self, body, call, argi, syms, fn):
        """(chars, label) for argument `argi` of `call`. None if unbounded."""
        if argi >= len(call["args"]):
            return None, None
        arg = call["args"][argi].strip()

        lits = self.lits_in_arg(call, argi)
        if lits:
            # A ternary of literals takes the WIDEST arm - that is the one
            # that has to fit.
            best = max(lits, key=lambda l: len(l["value"]))
            note = repr(best["value"])
            if len(lits) > 1:
                note += " (widest of %d arms)" % len(lits)
            return len(best["value"]), note

        b = self.bounds.get(arg)
        if b is not None:
            return b, arg + " (bounded)"

        n, note = self.buffer_bound(body, call["start"], arg, syms, fn)
        if n is not None:
            return n, note

        n, note = self.local_bound(body, arg, syms, fn)
        if n is not None:
            return n, note

        return None, arg

    def text_chars(self, body, call, argi, syms, fn):
        return self.str_bound(body, call, argi, syms, fn)

    # -- main ------------------------------------------------------------

    def run(self):
        for fn, fs, fe in functions(self.masked):
            if fn in HELPERS:
                continue
            body = self.masked[fs:fe]
            offset = self.masked[:fs].count("\n") + 1
            syms = dict(self.scope_symbols(body))
            for pname, path in PARAM_LAYOUT.items():
                if fn == pname:
                    syms[path[0]] = path[1]
            contexts = [({}, None)]
            if fn == "render_single_seat":
                contexts = self.seat_contexts()
            for extra, ov in contexts:
                self.analyse(fn, body, fs, offset, syms, extra, ov)
        return self.sites, self.unresolved

    def seat_contexts(self):
        s = self.resolve_layout_path("game.seats") or {}
        out = []
        for col in SEAT_COLUMNS:
            bx = s.get("right_x" if col == "right" else "left_x")
            w = s.get("w")
            if bx is None or w is None:
                continue
            inset = s.get("text_inset", 0)
            rci = s.get("right_cards_inset", inset)
            extra = {
                "seat->name_x": bx + inset,
                "seat->cards_x": bx + (rci if col == "right" else inset),
                "seat->coins_x": bx + inset,
                "seat->name_y": 0, "seat->cards_y": 0, "seat->coins_y": 0,
            }
            out.append((extra, ("seat box (%s)" % col, bx, w)))
        return out

    def analyse(self, fn, body, fs, offset, syms, extra, override):
        self.base = fs
        self.local_consts = self.local_ints(body)
        cands = self.containers(fn, body, offset, syms, extra)
        blocks = self.blocks(body)
        font = self.default_font

        # Font tracking, including the save/restore idiom.
        #
        #     int prev = cui_saturn_font_get_active();
        #     cui_saturn_font_set_active(COUP_FONT_DISPLAY);
        #     ... draw the winner in the display face ...
        #     cui_saturn_font_set_active(prev);
        #
        # `prev` is a runtime value, so a tracker that only understands
        # COUP_FONT_* macros never leaves the display face and measures every
        # later label 8 px too wide - the game-over recap rows were reported
        # at cell 16 when they are drawn in the 8 px body face. A set_active
        # whose argument is a variable saved from font_get_active() restores
        # the font that was in effect where it was saved.
        saved = {}
        for m in re.finditer(r"(\w+)\s*=\s*cui_saturn_font_get_active\s*\("
                             r"\s*\)", body):
            saved[m.group(1)] = m.start()

        events = []
        for c in find_calls(body, "cui_saturn_font_set_active"):
            arg = c["args"][0].strip() if c["args"] else ""
            idx = Expr(self.macros).value(arg)
            if idx is None and arg in saved:
                events.append((c["start"], ("restore", saved[arg])))
            elif idx is not None:
                events.append((c["start"], idx))
        events.sort()

        def font_index_at(pos):
            cur = self.default_font
            for p, ev in events:
                if p >= pos:
                    break
                if isinstance(ev, tuple):
                    cur = font_index_at(ev[1])
                else:
                    cur = ev
            return cur

        def font_at(pos):
            return self.fonts.get(font_index_at(pos),
                                  self.fonts.get(3, (8, 8)))

        for kind in TEXT_CALLS:
            for c in find_calls(body, kind):
                self.cur_blocks = blocks
                self.cur_draw = c["start"]
                site = self.build(fn, body, c, kind, syms, extra, font_at,
                                  offset)
                if site is None:
                    continue
                if site.chars is None or site.x is None:
                    self.unresolved.append(site)
                    continue
                ov = override
                key = (fn, c["args"][0].strip())
                if key in CONTAINER_OVERRIDE:
                    ov = CONTAINER_OVERRIDE[key]
                site.container = self.pick_container(cands, site.x, site.y,
                                                    ov, blocks, c["start"])
                self.sites.append(site)

    def build(self, fn, body, c, kind, syms, extra, font_at, offset):
        line = offset + c["line"] - 1
        a = c["args"]
        f = font_at(c["start"])

        if kind == "draw_at":
            if len(a) < 3:
                return None
            col = self.evaluate(a[0], syms, extra)
            adv = Expr(self.macros).value("COUP_FONT_ADVANCE") or 8
            n, label = self.text_chars(body, c, 2, syms, fn)
            row = self.evaluate(a[1], syms, extra)
            rh = Expr(self.macros).value("COUP_FONT_ROW_H") or 8
            x = None if col is None else col * adv
            y = None if row is None else row * rh
            return Site(fn, line, kind, label, n, f, x,
                        None, "col %s x %s" % (a[0].strip(), adv), y)

        if kind == "draw_centered":
            if len(a) < 2:
                return None
            n, label = self.text_chars(body, c, 1, syms, fn)
            row = self.evaluate(a[0], syms, extra)
            rh = Expr(self.macros).value("COUP_FONT_ROW_H") or 8
            y = None if row is None else row * rh
            if n is None:
                return Site(fn, line, kind, label, None, f, None, None, "", y)
            x = max(0, (SCREEN_W - px_w(n, f)) // 2)
            return Site(fn, line, kind, label, n, f, x, None,
                        "screen-centred", y)

        if kind == "draw_centered_in":
            if len(a) < 4:
                return None
            bx = self.evaluate(a[0], syms, extra)
            bw = self.evaluate(a[1], syms, extra)
            n, label = self.text_chars(body, c, 3, syms, fn)
            y = self.evaluate(a[2], syms, extra)
            if None in (bx, bw) or n is None:
                return Site(fn, line, kind, label, n, f, None, None, "", y)
            x = bx + max(0, (bw - px_w(n, f)) // 2)
            s = Site(fn, line, kind, label, n, f, x, None,
                     "centred in %d..%d" % (bx, bx + bw), y)
            s.container = ("its own box", bx, bw)
            return s

        if kind == "button_centered":
            if len(a) < 6:
                return None
            bx = self.evaluate(a[0], syms, extra)
            bw = self.evaluate(a[2], syms, extra)
            fi = Expr(self.macros).value(a[5].strip())
            bf = self.fonts.get(fi, f)
            n, label = self.text_chars(body, c, 4, syms, fn)
            y = self.evaluate(a[1], syms, extra)
            if None in (bx, bw) or n is None:
                return Site(fn, line, kind, label, n, bf, None, None, "", y)
            x = bx + (bw - px_w(n, bf)) // 2
            s = Site(fn, line, kind, label, n, bf, x, None,
                     "centred in its own %d px plate" % bw, y)
            s.container = ("its own plate", bx, bw)
            return s

        if kind == "draw_text_font":
            # draw_text_font(x, y, text, color, font) - the whole label, in an
            # EXPLICIT face. Replaced draw_text_fit(), which drew as much of
            # the label as fitted and measured only what it drew; this one
            # draws every character, so `n` is the real character count and
            # the width is whatever that costs in the face NAMED AT THE CALL.
            #
            # Taking the face from argument 4 rather than from the ambient
            # cui_saturn_font_set_active() tracking is the point: the helper
            # sets and restores the face around a single draw, so the ambient
            # font at the call site is the BODY face and measuring in it would
            # report a 39-character log line at 312 px when it is drawn at
            # 160. Getting this wrong in the other direction would be worse -
            # it would hide a real overflow.
            if len(a) < 5:
                return None
            x = self.evaluate(a[0], syms, extra)
            y = self.evaluate(a[1], syms, extra)
            n, label = self.text_chars(body, c, 2, syms, fn)
            idx = Expr(self.macros).value(a[4].strip())
            if idx is None:
                return Site(fn, line, kind, label, None, f, x, None,
                            "font %s does not resolve" % a[4].strip(), y)
            face = self.fonts.get(idx)
            if face is None:
                return Site(fn, line, kind, label, None, f, x, None,
                            "font index %d is not registered" % idx, y)
            return Site(fn, line, kind, label, n, face, x, None,
                        "explicit font %d" % idx, y)

        # draw_text_sprite: the raw PAL call.
        if len(a) < 3:
            return None
        x = self.evaluate(a[0], syms, extra)
        y = self.evaluate(a[1], syms, extra)
        n, label = self.text_chars(body, c, 2, syms, fn)
        return Site(fn, line, kind, label, n, f, x, None, "pixel x", y)


# ===========================================================================
# 9. Selftest - the negative controls
# ===========================================================================

FIXTURE = r'''
/* a block comment with a " quote and a /* nested opener */
const char* a = "plain";
const char* b = "esc\"aped";          // a line comment with " in it
const char* c = "concat" " enated";
char q = '"';                          /* a quote CHAR literal */
char bs = '\\';
const char* d = "//not a comment";
const char* e = "/*not a comment*/";
'''

FIXTURE_EXPECT = ["plain", 'esc"aped', "concat enated",
                  "//not a comment", "/*not a comment*/"]


def selftest_parser():
    _, lits = scan(FIXTURE)
    got = [l["value"] for l in lits]
    ok = got == FIXTURE_EXPECT
    print("  parser fixture: %d literal(s)" % len(got))
    for g in got:
        print("      %r" % g)
    if not ok:
        print("    EXPECTED: %r" % (FIXTURE_EXPECT,))
    return ok


def analyse_source(render_src, macros, layout, fonts, bounds):
    an = Analyser(render_src, macros, layout, fonts, bounds)
    return an, an.run()


# ===========================================================================
# 9b. Anti-truncation - a label that FITS because it was CUT is not fixed
# ===========================================================================
#
# The audit above proves a label ends before its box does. It cannot tell the
# difference between a label that fits because the box holds it and one that
# fits because the renderer threw characters away - and the second is not a
# fix. A player shown "Bartholom" has not been shown their name, and a log
# line stopped mid-word has lost the part that said what happened.
#
# That distinction is invisible to a width measurement, so it needs its own
# check. This one reads the SAME masked source - so it cannot match inside a
# comment or mistake code for a literal - and reports every construct in the
# renderer that can shorten a string on its way to the screen:
#
#   PRECISION  "%.10s", "%-10.10s". A field WIDTH pads and is harmless; a
#              PRECISION truncates. Only the precision is flagged, which is
#              why this matches the printf grammar rather than searching for
#              a '%'. "%-12s" must NOT be reported, and the selftest proves
#              it is not.
#   CLIPPER    a call whose job is to cut to a container or to a fixed cap:
#              draw_text_fit(), safe_copy(). A function's own DEFINITION is
#              not a use of it and is excluded, or the check would flag the
#              very helper it is asking you to delete.
#
# The renderer's allow-list is empty on purpose. A genuinely bounded copy is
# a game-layer concern (coup_log() owns COUP_LOG_LINE_LEN); presentation code
# that finds a label too big for its box must move, wrap or condense it.

TRUNC_CALLS = ("draw_text_fit", "safe_copy")


# printf string conversion carrying a PRECISION - the truncating form.
TRUNC_FMT = re.compile(r"%[-+ #0]*\d*\.\d+(?:hh|h|ll|l|z)?s")


def _definition_offsets(masked):
    """Offset of the `name(` that opens each function DEFINITION.

    `static int safe_copy(char* dst, ...)` is a definition, not a call to
    safe_copy. find_calls() cannot tell them apart - both are `name(` with a
    balanced argument list - so the definition sites are located through
    functions() and subtracted.
    """
    out = set()
    for name, brace_i, _end in functions(masked):
        head = masked.rfind(name, 0, brace_i)
        while head >= 0:
            if masked[head + len(name):].lstrip().startswith("("):
                out.add(head)
                break
            head = masked.rfind(name, 0, head)
    return out


def truncations(src):
    """Every construct in `src` that can shorten a user-visible string.

    Returns a list of (kind, line, what, context).
    """
    masked, lits = scan(src)
    out = []

    for l in lits:
        for m in TRUNC_FMT.finditer(l["value"]):
            out.append(("PRECISION", l["line"], m.group(0), l["value"]))

    defs = _definition_offsets(masked)
    for name in TRUNC_CALLS:
        for c in find_calls(masked, name):
            if c["start"] in defs:
                continue
            arg0 = c["args"][0].strip() if c["args"] else ""
            out.append(("CLIPPER", c["line"], name + "()",
                        "%s(%s, ...)" % (name, arg0)))

    return sorted(out, key=lambda t: t[1])


def selftest_truncation():
    """Negative and positive controls for the anti-truncation check."""
    ok = True

    # Control C: a field WIDTH is not a truncation and must stay silent.
    width_only = 'void f(void) { snprintf(b, n, "%-12s %s", a, c); }\n'
    found = truncations(width_only)
    print("  control WIDTH-ONLY: %r -> %d finding(s)"
          % ("%-12s", len(found)))
    if found:
        print("      %r" % (found,))
        print("    a field WIDTH pads, it does not cut - flagging it would "
              "make the check unusable")
        ok = False

    # Control D: a precision on a string MUST be caught.
    precision = 'void f(void) { snprintf(b, n, "Waiting: %.8s...", nm); }\n'
    found = truncations(precision)
    kinds = [k for k, _l, _w, _c in found]
    print("  control PRECISION: injected %r -> %d finding(s) %s"
          % ("%.8s", len(found), kinds))
    if "PRECISION" not in kinds:
        print("    an injected %.8s was NOT detected, so a GREEN from this "
              "check means nothing")
        ok = False

    # Control E: a clipping CALL must be caught, and its own DEFINITION
    # must not be.
    clipper = ('static void draw_text_fit(int x, int y, int r,\n'
               '                          const char* t, int c) { (void)x; }\n'
               'void g(void) { draw_text_fit(4, 8, 300, s, col); }\n')
    found = truncations(clipper)
    kinds = [k for k, _l, _w, _c in found]
    print("  control CLIPPER: one definition + one call -> %d finding(s) %s"
          % (len(found), kinds))
    if kinds != ["CLIPPER"]:
        print("    expected exactly one CLIPPER (the call); a definition is "
              "not a use and must not be counted")
        ok = False

    # Control F: a comment that talks about %.8s is not code.
    commented = 'void f(void) { /* was "%.8s" once */ int q = 0; (void)q; }\n'
    found = truncations(commented)
    print("  control COMMENT: %r inside a comment -> %d finding(s)"
          % ("%.8s", len(found)))
    if found:
        print("    the scanner matched inside a comment, so every finding "
              "below is suspect")
        ok = False

    return ok


def overflows(sites):
    out = []
    for s in sites:
        if s.x is None or s.chars is None or s.container is None:
            continue
        cname, cx, cw = s.container
        over_c = s.right - (cx + cw)
        over_s = s.right - SCREEN_W
        if over_c > 0 or over_s > 0:
            out.append((s, over_c, over_s))
    return out


# ===========================================================================
# 10. main
# ===========================================================================

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--write-baseline", action="store_true")
    ap.add_argument("--file", default=RENDER,
                    help="source to audit (point at a git-extracted copy to "
                         "prove the gate fires RED on known-bad input)")
    args = ap.parse_args()

    for p in (args.file, UI_H, COUP_H):
        if not os.path.exists(p):
            print("GATE TEXT OVERFLOW: INCONCLUSIVE - %s not found" % p)
            return 2

    macros = read_macros([UI_H, COUP_H])
    macros.update(read_brace_macros([UI_H]))
    ev = Expr(macros)
    layout = read_layout(open(UI_H, encoding="utf-8", errors="replace").read(),
                         macros, ev)
    fonts = read_fonts(FONT_DIR)
    bounds = arg_bounds(macros)
    render_src = open(args.file, encoding="utf-8", errors="replace").read()

    # ---- prove the parser before trusting a single number ----------------
    print("=== PARSER ===")
    an = Analyser(render_src, macros, layout, fonts, bounds)
    lits = an.lits
    print("  string literals extracted from %s: %d" % (args.file, len(lits)))
    bad = [l for l in lits if "\n" in l["value"]]
    longest = sorted(lits, key=lambda l: -len(l["value"]))[:5]
    print("  longest five (spot-check these against the source):")
    for l in longest:
        print("    line %-5d %2d chars  %r" % (l["line"], len(l["value"]),
                                               l["value"]))
    if bad:
        print("GATE TEXT OVERFLOW: RED - %d extracted literal(s) span a "
              "newline, so the scanner ran past a closing quote and every "
              "width below is worthless" % len(bad))
        for l in bad[:5]:
            print("    line %d: %r" % (l["line"], l["value"][:80]))
        return 1
    print("  fonts (cell, advance) MEASURED from pal/saturn/fonts: %s"
          % {k: v for k, v in sorted(fonts.items())})
    print("  layout screens parsed from coup_ui.h: %s"
          % ", ".join(sorted(layout)))
    print()

    if args.selftest:
        print("=== NEGATIVE CONTROLS ===")
        if not selftest_parser():
            print()
            print("GATE TEXT OVERFLOW: RED - the scanner cannot tell a string "
                  "literal from the code around it")
            return 1

        # Control A: a label known to fit must NOT be flagged.
        _, (sites, _u) = analyse_source(render_src, macros, layout, fonts,
                                        bounds)
        good = [s for s in sites if s.fn == "render_corners"]
        flagged = [s for s, _a, _b in overflows(sites)
                   if s.fn == "render_corners"]
        print("  control GOOD: render_corners draws %d label(s); %d flagged"
              % (len(good), len(flagged)))
        for s in good:
            print("      %s  x=%s w=%s -> %s  container %s"
                  % (s.label, s.x, s.w, s.right, s.container))
        if flagged:
            print()
            print("GATE TEXT OVERFLOW: RED - a label that provably fits its "
                  "plate was flagged, so a RED from this gate means nothing")
            return 1

        # Control B: inject an over-long label and require detection.
        injected = render_src.replace(
            'C->right_text_y, "Rules"',
            'C->right_text_y, "RULES AND CHARACTER REFERENCE CARD"', 1)
        if injected == render_src:
            print()
            print("GATE TEXT OVERFLOW: INCONCLUSIVE - the injection site "
                  "moved; update the control")
            return 2
        _, (isites, _iu) = analyse_source(injected, macros, layout, fonts,
                                          bounds)
        caught = [(s, a, b) for s, a, b in overflows(isites)
                  if s.fn == "render_corners"]
        print("  control BAD: injected a %d-char label into the same 68 px "
              "plate; %d flagged"
              % (len("RULES AND CHARACTER REFERENCE CARD"), len(caught)))
        for s, a, b in caught:
            print("      %s  x=%d w=%d -> %d  past container by %d px"
                  % (s.label, s.x, s.w, s.right, a))
        if not caught:
            print()
            print("GATE TEXT OVERFLOW: RED - an obviously over-long label was "
                  "NOT detected, so a GREEN from this gate means nothing")
            return 1
        print()

        if not selftest_truncation():
            print()
            print("GATE TEXT OVERFLOW: RED - the anti-truncation check failed "
                  "its own controls")
            return 1
        print()

    sites, unresolved = an.run()
    cuts = truncations(render_src)

    # ---- nothing may CUT a label -----------------------------------------
    print("=== TRUNCATION (a label that fits because it was cut) ===")
    if cuts:
        for kind, line, what, ctx in cuts:
            print("  %-9s %s:%-5d %-16s %s"
                  % (kind, os.path.basename(args.file), line, what, ctx))
    else:
        print("  none - no precision specifier and no clipping call reaches "
              "a drawn label")
    print()

    # ---- the audit -------------------------------------------------------
    over = overflows(sites)
    print("=== TEXT DRAWS BY SCREEN ===")
    print("  %-28s %7s %9s %10s" % ("function", "draws", "overflow",
                                    "unresolved"))
    per_fn = {}
    for s in sites:
        per_fn.setdefault(s.fn, [0, 0, 0])[0] += 1
    for s, _a, _b in over:
        per_fn.setdefault(s.fn, [0, 0, 0])[1] += 1
    for s in unresolved:
        per_fn.setdefault(s.fn, [0, 0, 0])[2] += 1
    for fn in sorted(per_fn):
        d, o, u = per_fn[fn]
        flag = "   <-- overflows" if o else ""
        print("  %-28s %7d %9d %10d%s" % (fn, d, o, u, flag))
    print()
    print("  totals: %d measured draws, %d overflowing, %d unresolved"
          % (len(sites), len(over), len(unresolved)))
    print()

    if over:
        print("=== OVERFLOWS ===")
        for s, oc, osx in sorted(over, key=lambda t: -t[1]):
            cname, cx, cw = s.container
            what = []
            if oc > 0:
                what.append("%d px past its CONTAINER (%s, %d..%d)"
                            % (oc, cname, cx, cx + cw))
            if osx > 0:
                what.append("%d px past the SCREEN" % osx)
            print("  %s:%d  %s" % (s.fn, s.line, s.kind))
            print("      label   %s" % s.label)
            print("      %d chars, cell/adv %s -> %d px, x %d .. %d"
                  % (s.chars, s.font, s.w, s.x, s.right))
            print("      %s" % "; ".join(what))
        print()

    if unresolved:
        print("=== UNRESOLVED (bound or origin not derivable) ===")
        for s in unresolved:
            print("  %s:%d  %s  %s" % (s.fn, s.line, s.kind, s.label))
        print()

    counts = {}
    for fn, (d, o, u) in per_fn.items():
        counts[fn] = {"overflow": o, "unresolved": u}

    if args.write_baseline:
        with open(BASELINE, "w", encoding="utf-8") as fh:
            json.dump(counts, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("wrote %s" % BASELINE)
        return 0

    # The anti-truncation result is NOT ratcheted against a baseline. There is
    # no acceptable non-zero count to ratchet down from: one cut label is a
    # cut label, and a baseline would only record which ones we have agreed to
    # keep cutting.
    if cuts:
        print("GATE TEXT OVERFLOW: RED - %d construct(s) shorten a label "
              "instead of fitting it" % len(cuts))
        print("  Move the container, wrap onto another row, or draw that one "
              "label in COUP_FONT_CONDENSED. Do not cut it.")
        return 1

    if not args.strict:
        if over:
            print("GATE TEXT OVERFLOW: RED - %d label(s) run past the box "
                  "they are drawn in" % len(over))
            return 1
        print("GATE TEXT OVERFLOW: GREEN - every measured label fits its "
              "container and the screen, and none of them was cut to do it")
        return 0

    if not os.path.exists(BASELINE):
        print("GATE TEXT OVERFLOW: RED - no baseline at %s; run "
              "--write-baseline once the current state is reviewed" % BASELINE)
        return 1
    base = json.load(open(BASELINE, encoding="utf-8"))

    regressions, improvements = [], []
    for fn in sorted(counts):
        b = base.get(fn)
        cur = counts[fn]
        if b is None:
            if cur["overflow"] or cur["unresolved"]:
                regressions.append("%s: new function with %d overflow(s), %d "
                                   "unresolved and no baseline"
                                   % (fn, cur["overflow"], cur["unresolved"]))
            continue
        for k in ("overflow", "unresolved"):
            if cur[k] > b.get(k, 0):
                regressions.append("%s: %d %s, baseline %d (+%d)"
                                   % (fn, cur[k], k, b.get(k, 0),
                                      cur[k] - b.get(k, 0)))
            elif cur[k] < b.get(k, 0):
                improvements.append("%s: %d %s, was %d (-%d)"
                                    % (fn, cur[k], k, b.get(k, 0),
                                       b.get(k, 0) - cur[k]))

    for line in improvements:
        print("  improved: %s" % line)
    if regressions:
        print()
        print("GATE TEXT OVERFLOW: RED - screen(s) gained text that does not "
              "fit")
        for r in regressions:
            print("  - " + r)
        return 1
    print()
    print("GATE TEXT OVERFLOW: GREEN - no screen has gained an overflowing or "
          "unmeasurable label")
    return 0


if __name__ == "__main__":
    sys.exit(main())
