#!/usr/bin/env python3
"""
qa_web_screen_repaint.py - Every screen must paint from cached state on entry.

WHY THIS GATE EXISTS
  Reported: "when returning to the lobby, i cant see the list of folks until
  i press the ready button."

  The players were there the whole time. createLobbyScreen() builds an EMPTY
  roster, and the only thing that ever filled it was the COUP_MSG_LOBBY_STATE
  handler, which populates immediately after calling changeScreen('lobby').
  That covers arriving at the lobby BECAUSE a message arrived. It does not
  cover RETURNING to it from the game or game-over screen, where
  changeScreen('lobby') is called on its own - so the roster stayed blank
  until the server next broadcast, which is precisely what pressing Ready
  causes.

  main.js's changeScreen already handles this for the game screen:
      case 'game': el = createGameScreen(this);
                   setTimeout(() => renderGameState(this), 50); break;
  The lobby case simply never got its equivalent.

THE RULE THIS ENFORCES
  A screen that has a separate "update from state" function must repaint
  itself on creation, from state the client already holds. Otherwise the
  screen is correct only on the path where a message and a screen change
  happen to coincide - which is the kind of bug that looks like it works
  right up until someone navigates back.

NEGATIVE CONTROL
  --selftest re-runs the check against the code with the repaint call
  stripped out, and requires it to fail.
"""

import argparse
import os
import re
import sys

# screen module -> (create fn, updater fn, cached state the create fn must read)
SCREENS = {
    "web-staging/js/screens/lobby.js":
        ("createLobbyScreen", "updateLobbyPlayers", "_lobbyPlayers"),
}

# The game screen repaints from main.js rather than its own module, so it is
# checked there instead.
MAIN = "web-staging/js/main.js"
MAIN_REPAINTS = ["renderGameState"]


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    return src


def body_of(src, fn):
    """Source text of `fn`, by brace matching from its declaration."""
    m = re.search(r"function\s+" + re.escape(fn) + r"\s*\(", src)
    if not m:
        return None
    i = src.find("{", m.end())
    if i < 0:
        return None
    depth, j = 0, i
    while j < len(src):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
        j += 1
    return None


def check(mutate=None):
    fails = []
    for path, (create, updater, cached) in SCREENS.items():
        if not os.path.exists(path):
            fails.append(f"{path} missing")
            continue
        src = open(path, encoding="utf-8", errors="replace").read()
        if mutate:
            src = mutate(src)
        src = strip_comments(src)
        body = body_of(src, create)
        if body is None:
            fails.append(f"{path}: could not locate {create}()")
            continue
        if updater not in body:
            fails.append(f"{path}: {create}() never calls {updater}() - a "
                         f"screen entered without a fresh message renders "
                         f"empty")
        elif cached not in body:
            fails.append(f"{path}: {create}() calls {updater}() but not from "
                         f"{cached} - it must repaint from state the client "
                         f"already holds")
        else:
            print(f"  OK      {os.path.basename(path):16s} {create}() "
                  f"repaints via {updater}({cached})")

    if os.path.exists(MAIN):
        msrc = strip_comments(open(MAIN, encoding="utf-8",
                                   errors="replace").read())
        for fn in MAIN_REPAINTS:
            if fn in msrc:
                print(f"  OK      {'main.js':16s} repaints via {fn}()")
            else:
                fails.append(f"{MAIN}: no call to {fn}()")
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    fails = check()

    if args.selftest:
        print()
        # Remove the repaint call and require the check to notice.
        def strip_repaint(s):
            return s.replace("updateLobbyPlayers(app._lobbyPlayers", "void(0; (")
        ctrl = check(mutate=strip_repaint)
        print(f"  control: repaint call removed -> {len(ctrl)} failure(s)")
        if not ctrl:
            print()
            print("GATE WEB SCREEN REPAINT: RED - the check passes even with "
                  "the repaint removed, so a GREEN from it means nothing")
            return 1

    print()
    if fails:
        print("GATE WEB SCREEN REPAINT: RED")
        for f in fails:
            print("  - " + f)
        return 1
    print("GATE WEB SCREEN REPAINT: GREEN - every screen repaints from state "
          "it already holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
