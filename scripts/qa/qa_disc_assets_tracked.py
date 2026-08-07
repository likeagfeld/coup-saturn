#!/usr/bin/env python3
"""Gate: every background the game indexes must be tracked in git.

WHY THIS EXISTS
  .gitignore:67 ignores `cd/` (a build-output rule). The streamed scene
  backgrounds live in examples/coup/saturn/cd/*.BIN and are exempt only
  because someone once ran `git add -f` on them. .gitignore:89 documents
  that they are "deliberately TRACKED" - but a comment enforces nothing.

  MEASURED failure: BGSPLASH.BIN was authored, referenced by
  coup_bg_index.h:41, and shipped on the v2.0-beta disc, while never being
  added to the repo. `git status` stayed clean because the file was
  ignored, so there was no symptom anywhere. A fresh clone would build a
  disc missing that scene - and the comment at .gitignore:89 predicts
  exactly that ("every background is black, with no build-time symptom").

  The Docker build cannot regenerate these: convert_backgrounds.py needs
  Python + Pillow, which the hermetic Saturn image does not carry. So an
  untracked background is unrecoverable from a clean checkout.

WHAT IT CHECKS
  For every .BIN named in coup_bg_index.h:
    1. the file exists on disk
    2. `git ls-files` reports it as TRACKED
  And separately reports any tracked background whose working-tree bytes
  differ from HEAD, because a release built from those is not reproducible.

Exit 0 = GREEN. Exit 1 = RED.
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INDEX = os.path.join(REPO, "examples", "coup", "saturn", "coup_bg_index.h")
CD_DIR = os.path.join(REPO, "examples", "coup", "saturn", "cd")


def git(*args):
    """Run git in the repo; return (exit_code, stdout)."""
    p = subprocess.run(("git",) + args, cwd=REPO,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode("utf-8", "replace").strip()


def indexed_backgrounds():
    """Every "NAME.BIN" string literal in coup_bg_index.h, in order."""
    src = open(INDEX, encoding="utf-8", errors="replace").read()
    # Strip // and /* */ comments so a commented-out scene is not counted.
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    seen, out = set(), []
    for m in re.finditer(r'"([A-Z0-9_]+\.BIN)"', src):
        if m.group(1) not in seen:
            seen.add(m.group(1))
            out.append(m.group(1))
    return out


def main():
    names = indexed_backgrounds()
    if not names:
        print("RED: coup_bg_index.h named no .BIN files - the parser or the "
              "header changed shape.")
        return 1

    print("=== backgrounds indexed by coup_bg_index.h: %d ===" % len(names))

    missing, untracked, dirty = [], [], []
    for n in names:
        rel = "examples/coup/saturn/cd/" + n
        on_disk = os.path.isfile(os.path.join(CD_DIR, n))
        tracked = git("ls-files", "--error-unmatch", rel)[0] == 0

        state = "OK"
        if not on_disk:
            missing.append(n)
            state = "MISSING ON DISK"
        elif not tracked:
            untracked.append(n)
            state = "NOT TRACKED (ignored by cd/ rule)"
        elif git("diff", "--quiet", "HEAD", "--", rel)[0] != 0:
            dirty.append(n)
            state = "tracked, but differs from HEAD"

        print("  %-14s %s" % (n, state))

    ok = True
    if missing:
        print("\nRED: %d indexed background(s) absent from the working tree: %s"
              % (len(missing), ", ".join(missing)))
        ok = False
    if untracked:
        print("\nRED: %d indexed background(s) are NOT in git: %s"
              % (len(untracked), ", ".join(untracked)))
        print("     A fresh clone cannot build these. The Saturn image has no "
              "Pillow, so they cannot be regenerated at build time.")
        print("     Fix: git add -f examples/coup/saturn/cd/<name>.BIN")
        ok = False
    if dirty:
        print("\nRED: %d background(s) differ from HEAD: %s"
              % (len(dirty), ", ".join(dirty)))
        print("     A disc built now would not be reproducible from this "
              "commit. Commit them before publishing a release.")
        ok = False

    print("\nDISC ASSETS TRACKED: %s" % ("GREEN" if ok else "RED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
