# Saturn Visual Facelift — Phase 0 & 1a Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **REQUIRED SKILL FOR EVERY TASK:** invoke `sega-saturn-developer` before touching
> Saturn code. Read
> `C:\Users\gary\.claude\skills\sega-saturn-developer\references\complete-doc-index.md`
> end-to-end, catalogue every applicable doc + sample for the subsystem you are
> touching, read all of them (not the first match), measure quantitatively before
> hypothesising, and write the RED-firing gate BEFORE the fix.

**Goal:** Stand up a measurable QA harness for the Saturn client, then prove the
VDP2 NBG1 painted-background pipeline end-to-end by putting the real table art
behind the game screen — while *reducing* VDP1 load.

**Architecture:** Phase 0 adds host-side gates (they need no emulator and no BIOS)
plus an emulator capture harness. Phase 1a adds one new rendering layer: a
512×256 256-colour VDP2 bitmap on NBG1 in VRAM bank A0, armed alongside the
existing NBG0 text layer, with the full-screen VDP1 background polygon removed so
the change is a net fill-rate *saving*.

**Tech Stack:** C99 (host tests + Saturn), bare SGL, Python 3 + Pillow (asset
conversion, gates), Docker (Saturn toolchain), Mednafen + ffmpeg (capture).

## Global Constraints

Copied from the spec. Every task's requirements implicitly include this section.

- **Do not modify** `examples/coup/coup_protocol.h`, `examples/coup/coup_rules.c`,
  `examples/coup/coup_game.c`, `pal/saturn/saturn_netlink.c`, or anything under
  `tools/`. The server must stay turnkey — no protocol change, no version bump.
- **Frame-rate floor:** the client must sustain ≥ 55 fps on the worst-case frame.
  Client timers are frame-counted; the server's are wall-clock, and the tightest
  pair is challenge/block (client 10 s vs server 12.0 s = a 50 fps floor).
- **No Saturn-specific code in `core/`** — that is a layer violation (CLAUDE.md).
- **VDP2 bank ownership:** `A0` = NBG1 bitmap (exactly 0x20000 bytes), `A1` = SGL
  back-colour/rotation reserve at its top (`VDP2_VRAM_A1 + 0x1FFFE` is live —
  `main_saturn.c:202`), `B0` = reserved/unused, `B1` = text cells (0x25E60000) and
  PNT (0x25E76000) — **do not touch B1.**
- **New VDP2 data goes in bank A, never B1.** SGL's auto-arbiter
  non-deterministically drops a standalone NBG whose data lives in B1
  (skill gotcha #7, MEASURED).
- **Visual claims are proven by rendered frames**, never by savestate register
  reads — SGL rewrites VDP2 registers every vblank (skill gotcha #12).
- **Every gate must fire RED on a known-bad fixture before it counts.**
- Saturn C is built with the joengine SH-2 toolchain via
  `scripts/docker-saturn-build.sh`; host tests build with `-Wall -Wextra -Werror
  -std=c99 -pedantic`.

## Scope: why this plan stops at Phase 1a

The spec defines six phases. This plan covers **Phase 0 and Phase 1a only**,
deliberately:

1. Phase 0 *measures* the numbers that later phases' designs depend on — binary
   size headroom, per-screen VDP1 command counts, WRAM map. Writing detailed
   tasks for Phases 2–5 now would mean assuming those numbers, which is exactly
   what this project's methodology forbids.
2. Phase 1a (one background, on one screen) proves the entire NBG1 pipeline —
   converter, VRAM upload, CRAM palette, cycle pattern, priority — as a
   self-contained, shippable improvement. Multi-scene support and CD loading are
   a separate concern and get their own plan once Phase 0 reports real headroom.

**Deviation from the spec, recorded:** the spec's Phase 1 says "scene-swap under
fade", but the fade module is Phase 2 — a circular dependency. Resolved here by
scoping Phase 1a to a single background loaded once at boot; swapping and fading
land together in the next plan.

## Prerequisite — blocking, user action required

**The Mednafen Saturn BIOS is not installed.** `C:\Users\gary\.mednafen\firmware`
exists but is empty, so Mednafen's `ss` module cannot boot anything. Task 3 (the
capture harness) and gate G2 are blocked until Saturn BIOS ROMs are placed in
that directory. BIOS images are copyrighted and cannot be fetched automatically.

**Tasks 1, 2, 4 and 5 have no such dependency and can proceed immediately.**
Task 6's host tests also pass without it; only its on-screen verification needs
the BIOS.

Verified available on this machine: Docker 29.5.3; ffmpeg at
`C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_…\bin\ffmpeg.exe`;
Mednafen at
`C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_…\mednafen.exe`
with `-qtrecord`, `-soundrecord` and `-force_module` flags confirmed present.

## Environment notes (MEASURED 2026-08-04 — read before executing)

- **There is no `make` and no host C compiler on this machine** (no gcc, cc,
  clang or mingw32-make). Every `make test-coup` in this plan must be run in a
  container, using the same image the repo already uses for `coup-server`:

  ```powershell
  docker run --rm -v "W:\coupsaturn:/src" -w /src gcc:14 make test-coup
  ```

  Run it from PowerShell, not Git Bash — Git Bash's MSYS path conversion mangles
  the `-w /src` argument into a Windows path and Docker rejects it.

- **`CUI_TEST_BEGIN` / `CUI_TEST_END` do not compile.** The macro body expands
  `cui_current_test.name = name;`, so the macro parameter is substituted into
  the struct member as well, producing "expected identifier before string
  constant" for any argument. No test in the suite uses them. Use the bare
  `CUI_TEST(name) { ... }` form with `CUI_ASSERT*` only. The working samples are
  authoritative over the framework header's documentation.

- **`coup_render.c` had never been compiled under `-Werror`**, and
  `coup_render_title()` leaves `st` unused on non-Saturn builds. Adding the
  renderer to the test build surfaces this; it is fixed with a guarded
  `(void)st;`. Expect similar host-only warnings if more render code is pulled
  into the test build later.

- **`test_coup_game_stubs.c` no longer stubs `coup_render_screen`** — the real
  renderer is linked in. Tests reach it through `game_setup()`, which already
  registers the mock PAL.

## File Structure

| File | Responsibility |
|---|---|
| `tests/framework/mocks/mock_pal.{c,h}` | *(modify)* raise the draw-call cap, expose per-index rect geometry |
| `tests/coup/test_render_budget.c` | *(create)* per-screen VDP1 command + fill-area budget assertions (gate G4-host) |
| `Makefile` | *(modify)* add `coup_render.c` to the test build; add `qa-binary` target |
| `scripts/qa/qa_binary.py` | *(create)* parse the Saturn linker map; assert size/WRAM headroom (gate G8) |
| `scripts/qa/qa_config.py` | *(create)* verified tool paths and the locked Mednafen invocation |
| `scripts/qa/qa_capture.py` | *(create)* boot the ISO, record, extract frames, validate the capture |
| `scripts/qa/qa_gates.py` | *(create)* image measurements: region pixel-mass, colour histogram (gate G2) |
| `scripts/qa/baseline.json` | *(create)* recorded Phase 0 baseline |
| `examples/coup/assets/convert_backgrounds.py` | *(create)* PNG → 512×256 8bpp bitmap + 256-colour RGB555 palette |
| `examples/coup/saturn/coup_bg_data.h` | *(generated)* bitmap bytes + palette |
| `pal/saturn/saturn_bg.{c,h}` | *(create)* NBG1 bitmap layer: pure index/palette logic + the SGL/VRAM arming |
| `tests/coup/test_saturn_bg.c` | *(create)* host tests for `saturn_bg` pure logic |
| `pal/saturn/sgl_defs.h` | *(modify)* add bitmap-mode externs and constants, correct `slScrAutoDisp` return type |
| `pal/saturn/saturn_pal.c` | *(modify)* priority renumber; call background init |
| `examples/coup/coup_render.c` | *(modify)* suppress the full-screen background rect on the game screen |

---

### Task 1: Expose draw-call geometry in the mock PAL

The host mock already records every `draw_rect` with full geometry but exports
only the *count*, and caps recording at 100 calls — far below a real game frame.
Both must change before any budget can be measured.

**Files:**
- Modify: `tests/framework/mocks/mock_pal.c:17` (cap), and add one accessor
- Modify: `tests/framework/mocks/mock_pal.h` (declare the accessor)
- Test: `tests/coup/test_render_budget.c` (created here, expanded in Task 2)

**Interfaces:**
- Consumes: nothing.
- Produces: `mock_rect_call_t mock_pal_get_rect_call(int index);` — returns a
  zeroed struct when `index` is out of range. `mock_rect_call_t` has fields
  `int x, y, w, h; uint32_t color;` and is already declared in `mock_pal.h`.

- [ ] **Step 1: Write the failing test**

Create `tests/coup/test_render_budget.c`:

```c
/**
 * test_render_budget.c - VDP1 draw-call budget gate (G4-host)
 *
 * The Saturn renderer buffers one VDP1 command per draw_rect and per
 * non-space character. Command count and total fill area are pure
 * functions of the render path, so they are measured here on the host
 * rather than on hardware.
 */

#include "cui_test_framework.h"
#include "mock_pal.h"
#include "cui_pal.h"

CUI_TEST(mock_records_rect_geometry)
{
    CUI_TEST_BEGIN("mock records rect geometry");

    cui_pal_register(cui_mock_platform());
    mock_pal_reset();

    CUI_DISPLAY()->draw_rect(11, 22, 33, 44, 0x11223344u);

    CUI_ASSERT_EQ(1, mock_pal_get_rect_call_count());

    mock_rect_call_t r = mock_pal_get_rect_call(0);
    CUI_ASSERT_EQ(11, r.x);
    CUI_ASSERT_EQ(22, r.y);
    CUI_ASSERT_EQ(33, r.w);
    CUI_ASSERT_EQ(44, r.h);

    CUI_TEST_END();
}

CUI_TEST(mock_rect_index_out_of_range_is_zeroed)
{
    CUI_TEST_BEGIN("mock rect out-of-range returns zeroed struct");

    cui_pal_register(cui_mock_platform());
    mock_pal_reset();

    mock_rect_call_t r = mock_pal_get_rect_call(0);
    CUI_ASSERT_EQ(0, r.x);
    CUI_ASSERT_EQ(0, r.w);

    CUI_TEST_END();
}
```

Confirm the assertion macro spellings against
`tests/framework/cui_test_framework.h` before running; if the framework uses
`CUI_ASSERT_INT_EQ` or similar, use its actual name throughout this plan.

- [ ] **Step 2: Run the test to verify it fails**

```bash
make test-coup
```

Expected: compile failure — `mock_pal_get_rect_call` is not declared.

- [ ] **Step 3: Raise the recording cap**

In `tests/framework/mocks/mock_pal.c`, change the cap so a full game frame fits.
The Saturn command budget is 2048 (`SATURN_VDP1_MAX_CMDS`, `saturn_vdp1.h:45`);
match it so the mock can never be the limiting factor:

```c
#define MOCK_MAX_DRAW_CALLS 2048
```

- [ ] **Step 4: Add the accessor**

Append to `tests/framework/mocks/mock_pal.c`, mirroring the existing
`mock_pal_get_last_rect_call` style (return by value, so no internal pointer
escapes):

```c
mock_rect_call_t mock_pal_get_rect_call(int index)
{
    mock_rect_call_t out;
    out.x = 0;
    out.y = 0;
    out.w = 0;
    out.h = 0;
    out.color = 0;

    if (index < 0 || index >= mock_state.rect_call_count) {
        return out;
    }

    out.x     = mock_state.rect_calls[index].x;
    out.y     = mock_state.rect_calls[index].y;
    out.w     = mock_state.rect_calls[index].w;
    out.h     = mock_state.rect_calls[index].h;
    out.color = mock_state.rect_calls[index].color;
    return out;
}
```

Declare it in `tests/framework/mocks/mock_pal.h`, next to
`mock_pal_get_last_rect_call`:

```c
/**
 * Get a specific draw_rect call by index.
 * Returns a zeroed struct if index is out of range.
 */
mock_rect_call_t mock_pal_get_rect_call(int index);
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
make test-coup
```

Expected: PASS, including the two new tests.

- [ ] **Step 6: Commit**

```bash
git add tests/framework/mocks/mock_pal.c tests/framework/mocks/mock_pal.h tests/coup/test_render_budget.c
git commit -m "test: expose per-index rect geometry in mock PAL and raise draw-call cap"
```

---

### Task 2: Lock the per-screen VDP1 budget baseline (gate G4-host)

Drive the real renderer against the mock and record what every screen costs. This
is the gate that later phases must not blow, and the number Task 6 must *reduce*.

**Files:**
- Modify: `Makefile:129-133` (add `coup_render.c` to `COUP_TEST_SRCS`)
- Modify: `tests/coup/test_render_budget.c`
- Create: `scripts/qa/baseline.json`

**Interfaces:**
- Consumes: `mock_pal_get_rect_call(int)` from Task 1.
- Produces: `scripts/qa/baseline.json` with a `"vdp1_budget"` object keyed by
  screen name, each `{"rects": int, "sprite_chars": int, "fill_px": int}`.

- [ ] **Step 1: Add the renderer to the test build**

In `Makefile`, extend `COUP_TEST_SRCS` (currently line 129) so the render path is
compiled into the test runner:

```make
COUP_TEST_SRCS := $(wildcard $(COUP_TEST_DIR)/*.c) \
                  $(COUP_GAME_SRCS) $(COUP_RULES_SRCS) $(COUP_BOT_SRCS) $(COUP_VIEW_SRCS) \
                  $(COUP_SRC_DIR)/coup_render.c \
                  $(TESTS_FW)/cui_test_framework.c \
                  $(TESTS_FW)/mocks/mock_pal.c \
                  $(CORE_SRC)/cui_pal.c
```

- [ ] **Step 2: Write the measurement test**

Append to `tests/coup/test_render_budget.c`. `coup_render_screen()` takes a
`const coup_state_t*`; build a minimal populated state so the game screen renders
its full complement of seats.

```c
#include "coup.h"

/* Sum the pixel area of every rect the renderer emitted this frame. */
static long mock_total_fill_px(void)
{
    long total = 0;
    int n = mock_pal_get_rect_call_count();
    for (int i = 0; i < n; i++) {
        mock_rect_call_t r = mock_pal_get_rect_call(i);
        total += (long)r.w * (long)r.h;
    }
    return total;
}

/* Populate a 6-player mid-game state: the worst case for seat rendering. */
static void build_busy_game_state(coup_state_t* st)
{
    memset(st, 0, sizeof(*st));
    st->screen = COUP_SCREEN_GAME;
    st->phase = COUP_PHASE_SELECT_ACTION;
    st->player_count = 6;
    for (int i = 0; i < 6; i++) {
        st->players[i].id = i;
        st->players[i].coins = 3;
        st->players[i].cards[0] = 5;
        st->players[i].cards[1] = 5;
        st->players[i].alive = true;
        snprintf(st->players[i].name, sizeof(st->players[i].name), "PLAYER%d", i);
    }
    st->players[0].is_self = true;
    st->my_id = 0;
    st->my_cards[0] = 0;
    st->my_cards[1] = 1;
    st->current_turn_id = 0;
}

CUI_TEST(game_screen_stays_within_vdp1_budget)
{
    CUI_TEST_BEGIN("game screen VDP1 budget");

    coup_state_t st;
    build_busy_game_state(&st);

    cui_pal_register(cui_mock_platform());
    mock_pal_reset();
    coup_render_screen(&st);

    int rects = mock_pal_get_rect_call_count();
    long fill = mock_total_fill_px();

    printf("  [budget] game screen: rects=%d fill_px=%ld\n", rects, fill);

    /* Deliberately-tight thresholds; Step 3 replaces them with measured values. */
    CUI_ASSERT(rects <= 1);
    CUI_ASSERT(fill <= 1);

    CUI_TEST_END();
}
```

Verify the enum spellings (`COUP_SCREEN_GAME`, `COUP_PHASE_SELECT_ACTION`) and
the `coup_render_screen` declaration against `examples/coup/coup.h` and
`coup_ui.h`; use the real names.

- [ ] **Step 3: Run to verify it fails and reveals the real numbers**

```bash
make test-coup
```

Expected: FAIL, with the `[budget]` line printing the actual rect count and fill
area. Record both.

- [ ] **Step 4: Set the real thresholds**

Replace the two placeholder assertions with the measured values plus 15%
headroom, and state where the numbers came from:

```c
    /* Measured 2026-08-04 on the pre-facelift build; +15% headroom.
     * Phase 1a must REDUCE fill_px by ~71,680 (the full-screen background
     * rect moves to VDP2 NBG1). Update deliberately, never to make a
     * regression pass. */
    CUI_ASSERT(rects <= <measured_rects_plus_15pct>);
    CUI_ASSERT(fill  <= <measured_fill_plus_15pct>);
```

Repeat the same test body for the title, lobby and game-over screens, so every
screen carries a budget. Then run `make test-coup` and confirm PASS.

- [ ] **Step 5: Record the baseline**

Create `scripts/qa/baseline.json` with the measured values:

```json
{
  "recorded": "2026-08-04",
  "build": "pre-facelift",
  "vdp1_budget": {
    "title":     { "rects": 0, "fill_px": 0 },
    "lobby":     { "rects": 0, "fill_px": 0 },
    "game":      { "rects": 0, "fill_px": 0 },
    "game_over": { "rects": 0, "fill_px": 0 }
  },
  "notes": "fill_px is the sum of w*h over every draw_rect in one frame. The Saturn draws 1 px per 28.6 MHz clock, so ~477000 px-clocks is the 60 fps ceiling (ST-013-R3 txt:1114-1115)."
}
```

Fill in the real numbers from Step 3.

- [ ] **Step 6: Commit**

```bash
git add Makefile tests/coup/test_render_budget.c scripts/qa/baseline.json
git commit -m "test: lock per-screen VDP1 command and fill-area budget baseline"
```

---

### Task 3: Saturn binary size and WRAM headroom gate (G8)

**Files:**
- Create: `scripts/qa/qa_binary.py`
- Modify: `Makefile` (add a `qa-binary` phony target)

**Interfaces:**
- Consumes: the Saturn build output produced by `make coup-saturn`.
- Produces: `qa_binary.check(map_path) -> dict` with keys `text`, `data`, `bss`,
  `end_addr`, `stack_headroom`; exit code 1 when headroom is negative.

- [ ] **Step 1: Find what the Saturn build actually emits**

```bash
cat scripts/docker-saturn-build.sh
ls examples/coup/saturn/_build/
```

Record the linker-map filename and the `.cof`/`.elf` name. If the link step does
not currently emit a map, add `-Wl,-Map,game.map` to the link flags in
`examples/coup/saturn/Makefile` — the map is what this gate reads.

- [ ] **Step 2: Write the failing gate**

Create `scripts/qa/qa_binary.py`:

```python
#!/usr/bin/env python3
"""
qa_binary.py - Saturn binary size / WRAM headroom gate (G8).

Work RAM-H is 1 MB at 0x06000000-0x060FFFFF. SGL's stack sits at
0x060FFC00 and grows down, so _end must stay well below it.
"""

import argparse
import re
import sys

WRAM_H_BASE = 0x06000000
STACK_TOP = 0x060FFC00
MIN_HEADROOM = 64 * 1024  # bytes that must remain between _end and the stack


def parse_map(path):
    """Return {'end_addr': int, 'sections': {name: size}} from a GNU ld map."""
    text = open(path, "r", errors="replace").read()

    end_match = re.search(r"^\s*0x([0-9a-fA-F]+)\s+_end\b", text, re.M)
    if not end_match:
        raise SystemExit(f"qa_binary: no _end symbol found in {path}")
    end_addr = int(end_match.group(1), 16)

    sections = {}
    for name in (".text", ".data", ".bss"):
        m = re.search(
            r"^%s\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)" % re.escape(name),
            text, re.M)
        sections[name] = int(m.group(2), 16) if m else 0

    return {"end_addr": end_addr, "sections": sections}


def check(map_path):
    info = parse_map(map_path)
    end_addr = info["end_addr"]
    headroom = STACK_TOP - end_addr
    info["stack_headroom"] = headroom
    info["ok"] = headroom >= MIN_HEADROOM
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map_path")
    args = ap.parse_args()

    info = check(args.map_path)
    s = info["sections"]
    print(f"  .text {s['.text']:>8} bytes")
    print(f"  .data {s['.data']:>8} bytes")
    print(f"  .bss  {s['.bss']:>8} bytes")
    print(f"  _end  0x{info['end_addr']:08X}")
    print(f"  stack headroom {info['stack_headroom']:>8} bytes "
          f"(minimum {MIN_HEADROOM})")

    if not info["ok"]:
        print("GATE G8: RED - insufficient WRAM headroom", file=sys.stderr)
        return 1
    print("GATE G8: GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Prove the gate fires RED**

Temporarily set `MIN_HEADROOM = 8 * 1024 * 1024` (larger than all of WRAM-H) and
run it against the real map:

```bash
make coup-saturn
python3 scripts/qa/qa_binary.py examples/coup/saturn/_build/game.map
```

Expected: `GATE G8: RED`, exit 1. Then restore `MIN_HEADROOM = 64 * 1024` and
re-run — expected `GATE G8: GREEN`. This is the RED-fixture proof; do not skip it.

- [ ] **Step 4: Wire it into the Makefile**

```make
.PHONY: qa-binary
qa-binary:
	@python3 scripts/qa/qa_binary.py $(COUP_SATURN_DIR)/_build/game.map
```

- [ ] **Step 5: Record the numbers in the baseline**

Add a `"binary"` object to `scripts/qa/baseline.json` with the measured `.text`,
`.data`, `.bss`, `_end` and `stack_headroom`. **This is the number that decides
whether backgrounds can be embedded or must stream from CD** — note the remaining
headroom explicitly against the 131,072 bytes one background costs.

- [ ] **Step 6: Commit**

```bash
git add scripts/qa/qa_binary.py Makefile scripts/qa/baseline.json examples/coup/saturn/Makefile
git commit -m "test: add Saturn binary size and WRAM headroom gate (G8)"
```

---

### Task 4: Mednafen capture harness

**Blocked on the Saturn BIOS prerequisite above.** Implement and commit the code;
mark the run steps blocked if the firmware directory is still empty.

**Files:**
- Create: `scripts/qa/qa_config.py`, `scripts/qa/qa_capture.py`

**Interfaces:**
- Produces: `qa_capture.capture(cue_path, out_dir, seconds=20) -> list[str]`
  returning the extracted PNG frame paths, and raising `RuntimeError` when the
  capture is contaminated (too few frames, or every frame identical).

- [ ] **Step 1: Write the config module**

Create `scripts/qa/qa_config.py`:

```python
#!/usr/bin/env python3
"""Verified tool paths and the locked Mednafen invocation.

Probed on this machine 2026-08-04:
  mednafen -help lists -qtrecord, -soundrecord, -force_module.
  ~/.mednafen/firmware exists; Saturn BIOS must be placed there by hand.
"""

import os
import shutil

MEDNAFEN = shutil.which("mednafen") or os.path.expandvars(
    r"%LOCALAPPDATA%\Microsoft\WinGet\Packages"
    r"\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe")

FFMPEG = shutil.which("ffmpeg") or os.path.expandvars(
    r"%LOCALAPPDATA%\Microsoft\WinGet\Packages"
    r"\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe"
    r"\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe")

FIRMWARE_DIR = os.path.expanduser("~/.mednafen/firmware")

# Saturn active display used by this project (main_saturn.c:198).
SCREEN_W = 320
SCREEN_H = 224


def require_bios():
    """Raise with an actionable message when the Saturn BIOS is absent."""
    if not os.path.isdir(FIRMWARE_DIR) or not os.listdir(FIRMWARE_DIR):
        raise RuntimeError(
            f"Saturn BIOS missing. Place Saturn BIOS ROMs in {FIRMWARE_DIR} "
            "and set ss.bios_* in ~/.mednafen/mednafen.cfg. Mednafen's ss "
            "module cannot boot without them.")
```

- [ ] **Step 2: Write the capture module**

Create `scripts/qa/qa_capture.py`:

```python
#!/usr/bin/env python3
"""Boot a Saturn disc in Mednafen, record it, and extract PNG frames."""

import argparse
import glob
import os
import subprocess
import sys

from PIL import Image, ImageChops

import qa_config as cfg


def _record(cue_path, mov_path, seconds):
    proc = subprocess.Popen(
        [cfg.MEDNAFEN, "-force_module", "ss", "-qtrecord", mov_path, cue_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        proc.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()


def _extract(mov_path, out_dir, fps=2):
    os.makedirs(out_dir, exist_ok=True)
    subprocess.run(
        [cfg.FFMPEG, "-y", "-i", mov_path, "-vf", f"fps={fps}",
         os.path.join(out_dir, "frame-%04d.png")],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    return sorted(glob.glob(os.path.join(out_dir, "frame-*.png")))


def _validate(frames):
    """Reject contaminated captures (skill gotcha #3: host load steals time)."""
    if len(frames) < 4:
        raise RuntimeError(
            f"capture produced only {len(frames)} frames - emulator likely "
            "failed to boot (check BIOS) or the host was too loaded")

    first = Image.open(frames[0]).convert("RGB")
    changed = False
    for f in frames[1:]:
        img = Image.open(f).convert("RGB")
        if ImageChops.difference(first, img).getbbox() is not None:
            changed = True
            break
    if not changed:
        raise RuntimeError(
            "every captured frame is identical - the emulator is frozen or "
            "showing a static error screen, not running the game")
    return frames


def capture(cue_path, out_dir, seconds=20):
    cfg.require_bios()
    mov_path = os.path.join(out_dir, "capture.mov")
    os.makedirs(out_dir, exist_ok=True)
    _record(cue_path, mov_path, seconds)
    if not os.path.exists(mov_path):
        raise RuntimeError(f"Mednafen produced no recording at {mov_path}")
    return _validate(_extract(mov_path, out_dir))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cue_path")
    ap.add_argument("out_dir")
    ap.add_argument("--seconds", type=int, default=20)
    args = ap.parse_args()

    frames = capture(args.cue_path, args.out_dir, args.seconds)
    print(f"captured {len(frames)} frames -> {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Prove the validator fires RED**

Run `capture()` against a deliberately invalid disc path, and separately against
a directory of identical copied frames, confirming both raise `RuntimeError` with
the intended message. A capture validator that cannot reject a frozen emulator is
worthless — this is its RED fixture.

- [ ] **Step 4: Capture the pre-facelift baseline**

Blocked without the BIOS. Once available:

```bash
make coup-saturn
python3 scripts/qa/qa_capture.py build/coup_game/game.cue build/qa/baseline-frames
```

Commit a handful of representative frames under `docs/saturn/captures/baseline/`
as the before-images for the final comparison.

- [ ] **Step 5: Commit**

```bash
git add scripts/qa/qa_config.py scripts/qa/qa_capture.py
git commit -m "test: add Mednafen capture harness with contamination validation"
```

---

### Task 5: Background converter — PNG to VDP2 bitmap

**Files:**
- Create: `examples/coup/assets/convert_backgrounds.py`
- Generated: `examples/coup/saturn/coup_bg_data.h`

**Interfaces:**
- Produces: `coup_bg_data.h` defining
  `const uint8_t coup_bg_table[131072];` (512×256, one byte per pixel) and
  `const uint16_t coup_bg_palette[256];` (Saturn RGB555).
  **Palette index 0 is reserved and unused** — VDP2 treats colour 0 in a scroll
  screen as transparent, so every image pixel maps to 1..255.

- [ ] **Step 1: Write the converter**

Create `examples/coup/assets/convert_backgrounds.py`:

```python
#!/usr/bin/env python3
"""
convert_backgrounds.py - Convert a Coup scene PNG to a VDP2 NBG1 bitmap.

Output format (ST-058-R2 section 4.9):
  - 512x256 bitmap, 1 byte per pixel (256-colour mode)
  - the visible 320x224 window occupies the top-left corner
  - palette index 0 is TRANSPARENT for a scroll screen and is therefore
    reserved: image colours occupy indices 1..255
  - palette entries are Saturn RGB555, 0BBBBBGGGGGRRRRR

Usage:
  python3 convert_backgrounds.py gamescreen.png --name game \
      --output ../saturn/coup_bg_data.h
"""

import argparse
import os
import sys

from PIL import Image

BITMAP_W = 512
BITMAP_H = 256
VISIBLE_W = 320
VISIBLE_H = 224
RESERVED_INDICES = 1          # index 0 = transparent
MAX_COLORS = 256 - RESERVED_INDICES


def to_rgb555(r, g, b):
    """Pack 8-bit RGB into Saturn RGB555 (bit 15 clear)."""
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def convert(src_path, name):
    img = Image.open(src_path).convert("RGB")
    img = img.resize((VISIBLE_W, VISIBLE_H), Image.LANCZOS)

    quant = img.quantize(colors=MAX_COLORS, method=Image.MEDIANCUT)
    src_palette = quant.getpalette()[: MAX_COLORS * 3]
    indices = list(quant.getdata())

    # Shift every index up by one so index 0 stays transparent.
    palette = [0x0000] * 256
    for i in range(MAX_COLORS):
        r, g, b = src_palette[i * 3: i * 3 + 3]
        palette[i + RESERVED_INDICES] = to_rgb555(r, g, b)

    bitmap = bytearray(BITMAP_W * BITMAP_H)   # zero = transparent everywhere
    for y in range(VISIBLE_H):
        row = y * BITMAP_W
        for x in range(VISIBLE_W):
            bitmap[row + x] = indices[y * VISIBLE_W + x] + RESERVED_INDICES

    return bytes(bitmap), palette


def emit_header(bitmap, palette, name, out_path):
    guard = "COUP_BG_DATA_H"
    with open(out_path, "w") as f:
        f.write("/* Generated by convert_backgrounds.py. Do not edit. */\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define COUP_BG_W {BITMAP_W}\n")
        f.write(f"#define COUP_BG_H {BITMAP_H}\n")
        f.write(f"#define COUP_BG_SIZE {len(bitmap)}\n")
        f.write(f'#define COUP_BG_NAME "{name}"\n\n')

        f.write("static const uint16_t coup_bg_palette[256] = {\n")
        for i in range(0, 256, 8):
            row = ", ".join(f"0x{c:04X}" for c in palette[i:i + 8])
            f.write(f"    {row},\n")
        f.write("};\n\n")

        f.write(f"static const uint8_t coup_bg_table[{len(bitmap)}] = {{\n")
        for i in range(0, len(bitmap), 16):
            row = ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16])
            f.write(f"    {row},\n")
        f.write("};\n\n")
        f.write(f"#endif /* {guard} */\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--name", default="game")
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    bitmap, palette = convert(args.src, args.name)

    assert len(bitmap) == BITMAP_W * BITMAP_H, "bitmap must be exactly 128 KB"
    assert palette[0] == 0x0000, "index 0 must stay reserved/transparent"
    assert all(c <= 0x7FFF for c in palette), "RGB555 bit 15 must be clear"
    assert 0 not in bitmap[:VISIBLE_H * BITMAP_W][:VISIBLE_W], \
        "visible pixels must never use the transparent index"

    emit_header(bitmap, palette, args.name, args.output)
    print(f"wrote {args.output}: {len(bitmap)} bytes bitmap, 256-colour palette")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it and verify the invariants fail loudly**

Temporarily change `RESERVED_INDICES` to `0` and run:

```bash
cd examples/coup/assets
python3 convert_backgrounds.py gamescreen.png --name game --output ../saturn/coup_bg_data.h
```

Expected: the `palette[0] == 0x0000` assertion fails — proving the reserved-index
invariant is actually enforced. Restore `RESERVED_INDICES = 1`.

- [ ] **Step 3: Generate the real header**

```bash
cd examples/coup/assets
python3 convert_backgrounds.py gamescreen.png --name game --output ../saturn/coup_bg_data.h
ls -la ../saturn/coup_bg_data.h
```

Expected: a header of roughly 800 KB of ASCII producing 131,072 bytes of data.
**Compare that 131,072 against the WRAM headroom Task 3 measured** — if it does
not fit, stop and report; the streaming-from-CD design is then required and
belongs in the next plan, not here.

- [ ] **Step 4: Save a preview for eyeball comparison**

Add a `--preview` flag that writes the quantized 320×224 result back out as a
PNG, and generate one into `docs/saturn/captures/` so the palette reduction can
be judged before it ever reaches hardware.

- [ ] **Step 5: Commit**

```bash
git add examples/coup/assets/convert_backgrounds.py examples/coup/saturn/coup_bg_data.h docs/saturn/captures/
git commit -m "feat: add VDP2 background converter and generate the game-table bitmap"
```

---

### Task 6: Arm NBG1 and retire the VDP1 background rect

**Files:**
- Create: `pal/saturn/saturn_bg.c`, `pal/saturn/saturn_bg.h`
- Create: `tests/coup/test_saturn_bg.c`
- Modify: `pal/saturn/sgl_defs.h`, `pal/saturn/saturn_pal.c`,
  `examples/coup/coup_render.c`, `examples/coup/saturn/Makefile`

**Interfaces:**
- Consumes: `coup_bg_table`, `coup_bg_palette` from Task 5.
- Produces:
  - `void saturn_bg_init(void);` — uploads the bitmap and palette, arms NBG1.
  - `bool saturn_bg_is_armed(void);`
  - `uint32_t saturn_bg_cram_addr(int index);` — pure: CRAM byte address for
    256-colour-bank-2 entry `index`, clamped to 0..255. Host-testable.

- [ ] **Step 1: Look up the real SGL constants**

`sgl_defs.h` currently declares no bitmap-mode symbols. Find the authoritative
values rather than guessing, exactly as the existing file does (it cites
`SL_DEF.H` line numbers — see its `scnNBG0`/`scnNBG1` comment):

```bash
grep -n "SGL\|joengine\|COPY\|ADD" scripts/saturn-build.Dockerfile
# then, using the SGL include path that reveals:
docker run --rm <image-from-dockerfile> sh -c \
  "grep -n 'BM_512x256\|BM_512x512\|slBitMapNbg1\|slBMPaletteNbg1\|slScrAutoDisp' /path/to/sgl/INC/SL_DEF.H"
```

Record the exact values and the `SL_DEF.H` line numbers; you will cite them in
the header.

- [ ] **Step 2: Add the declarations**

In `pal/saturn/sgl_defs.h`, add alongside the existing scroll declarations, with
citations in the established style:

```c
/* Bitmap-mode scroll screens (SGL 3.02j SL_DEF.H; SCROLL.TXT lines 859-906).
 * bmadr must sit on a 0x20000 boundary. */
extern void slBitMapNbg1(Uint16 col_type, Uint16 bmsize, void *bmadr);
extern void slBMPaletteNbg1(Uint16 pal);

/* Bitmap size selectors - values taken from SL_DEF.H, do not invent. */
#define BM_512x256      <value-from-SL_DEF.H>
#define BM_512x512      <value-from-SL_DEF.H>
```

Also correct the `slScrAutoDisp` prototype: SGL returns `Bool` (NG when no legal
cycle pattern exists), and gate G1 depends on reading it. The current
declaration at line 226 says `void`.

```c
/* Returns NG (0) when no legal VRAM cycle pattern can be built for the
 * requested screens - SCROLL.TXT lines 100-116. */
extern Bool slScrAutoDisp(Uint32 flags);
```

- [ ] **Step 3: Write the failing host test**

Create `tests/coup/test_saturn_bg.c`. Only the pure address arithmetic is
host-testable; the VRAM writes are hardware-only and are covered by the
on-screen check in Step 8.

```c
/**
 * test_saturn_bg.c - Host tests for the NBG1 background layer's pure logic.
 */

#include "cui_test_framework.h"
#include "saturn_bg.h"

CUI_TEST(bg_palette_lands_in_256_colour_bank_2)
{
    CUI_TEST_BEGIN("background palette CRAM addressing");

    /* CRAM base 0x25F00000. The background owns 256-colour bank 2, which is
     * 16-colour banks 32-47, i.e. colour index 512 = byte offset 1024. */
    CUI_ASSERT_EQ(0x25F00400u, saturn_bg_cram_addr(0));
    CUI_ASSERT_EQ(0x25F00402u, saturn_bg_cram_addr(1));
    CUI_ASSERT_EQ(0x25F005FEu, saturn_bg_cram_addr(255));

    CUI_TEST_END();
}

CUI_TEST(bg_starts_unarmed)
{
    CUI_TEST_BEGIN("background starts unarmed");
    CUI_ASSERT(!saturn_bg_is_armed());
    CUI_TEST_END();
}
```

Run `make test-coup`. Expected: compile failure — `saturn_bg.h` does not exist.

- [ ] **Step 4: Write the module**

Create `pal/saturn/saturn_bg.h`:

```c
/**
 * saturn_bg.h - VDP2 NBG1 painted background layer.
 *
 * A 512x256 256-colour bitmap living in VDP2 VRAM bank A0 (0x25E00000,
 * exactly 0x20000 bytes). Palette occupies 256-colour bank 2 in CRAM.
 *
 * Bank A0 is used because SGL's auto-arbiter non-deterministically drops
 * a standalone NBG whose data lives in bank B1, where the text layer's
 * character data already sits (MEASURED; sega-saturn-developer gotcha #7).
 */

#ifndef SATURN_BG_H
#define SATURN_BG_H

#include <stdint.h>
#include <stdbool.h>

/* VDP2 CRAM base and the background's palette home. */
#define SATURN_BG_CRAM_BASE     0x25F00000u
#define SATURN_BG_CRAM_OFFSET   0x400u   /* colour index 512, 256-col bank 2 */
#define SATURN_BG_PALETTE_BANK  2

/* VDP2 VRAM bank A0 - must be a 0x20000 boundary for bitmap mode. */
#define SATURN_BG_VRAM          0x25E00000u

/**
 * CRAM byte address of background palette entry `index` (0-255).
 * Pure function; host-testable.
 */
uint32_t saturn_bg_cram_addr(int index);

/**
 * Upload the bitmap and palette, then arm NBG1.
 * Safe to call once, after cui_saturn_init().
 */
void saturn_bg_init(void);

/** True once saturn_bg_init() has armed the layer. */
bool saturn_bg_is_armed(void);

#endif /* SATURN_BG_H */
```

Create `pal/saturn/saturn_bg.c`:

```c
#include "saturn_bg.h"

#ifdef __SATURN__
#include "sgl_defs.h"
#include "../../examples/coup/saturn/coup_bg_data.h"
#endif

static bool s_armed = false;

uint32_t saturn_bg_cram_addr(int index)
{
    if (index < 0) {
        index = 0;
    }
    if (index > 255) {
        index = 255;
    }
    return SATURN_BG_CRAM_BASE + SATURN_BG_CRAM_OFFSET
         + (uint32_t)index * 2u;
}

bool saturn_bg_is_armed(void)
{
    return s_armed;
}

void saturn_bg_init(void)
{
#ifdef __SATURN__
    volatile uint8_t*  vram = (volatile uint8_t*)SATURN_BG_VRAM;
    volatile uint16_t* cram;
    uint32_t i;

    /* Bitmap pixels -> VDP2 VRAM bank A0. */
    for (i = 0; i < COUP_BG_SIZE; i++) {
        vram[i] = coup_bg_table[i];
    }

    /* Palette -> CRAM 256-colour bank 2. Index 0 stays transparent. */
    for (i = 0; i < 256u; i++) {
        cram = (volatile uint16_t*)saturn_bg_cram_addr((int)i);
        *cram = coup_bg_palette[i];
    }

    /* Arm the layer. Priority 3 puts it under the sprites (6) and text (7). */
    slBitMapNbg1(COL_TYPE_256, BM_512x256, (void*)SATURN_BG_VRAM);
    slBMPaletteNbg1(SATURN_BG_PALETTE_BANK);
    slPriorityNbg1(3);
#endif
    s_armed = true;
}
```

- [ ] **Step 5: Run the host tests**

Add `pal/saturn/saturn_bg.c` to `COUP_TEST_SRCS` in the `Makefile` and add
`-Ipal/saturn` to the coup test compile rule so `saturn_bg.h` resolves. Then:

```bash
make test-coup
```

Expected: PASS. The `__SATURN__` guard means the host build compiles only the
pure logic.

- [ ] **Step 6: Renumber priorities and call the initialiser**

In `pal/saturn/saturn_pal.c`, the text layer currently sits at priority 5 and
sprites at 4 (`saturn_vdp1.c:315`). Move text to 7 and sprites to 6 so the
background at 3 has room beneath, and initialise the background after the PAL is
up. In `saturn_display_init()`, after `saturn_vdp1_init()`:

```c
    /* Layer order, front to back: text 7 > sprites 6 > background 3.
     * See docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md. */
    slPriorityNbg0(7);
    slPrioritySpr0(6);
    saturn_bg_init();
```

Add `#include "saturn_bg.h"` at the top. In `examples/coup/saturn/main_saturn.c`,
extend the display arm from `slScrAutoDisp(NBG0ON)` (line 204) to include the new
layer, and assert the result:

```c
    if (slScrAutoDisp(NBG0ON | NBG1ON) == 0) {
        /* GATE G1: no legal VRAM cycle pattern. Fall back to text-only so the
         * game still runs, and leave the background disarmed. */
        slScrAutoDisp(NBG0ON);
    }
```

Add `saturn_bg.c` to the Saturn build in `examples/coup/saturn/Makefile`.

- [ ] **Step 7: Drop the full-screen VDP1 background rect on the game screen**

In `examples/coup/coup_render.c`, `coup_render_game()` fills the screen with a
VDP1 polygon before anything else. On Saturn that is now NBG1's job, and removing
it is where the fill-rate refund comes from. Guard it so SDL and N64 are
unaffected:

```c
#ifndef __SATURN__
    /* On Saturn the painted background lives on VDP2 NBG1; drawing it here
     * would cover that layer and waste ~71,680 px of VDP1 fill per frame. */
    panel_r(G->bg, COUP_BG_DARK);
#endif
```

Apply the identical treatment to whichever background rect `coup_render_game()`
actually emits — read the function and match its real variable names.

- [ ] **Step 8: Verify the budget dropped and the plane is visible**

```bash
make test-coup
```

Expected: the game-screen `[budget]` line now reports `fill_px` lower by roughly
71,680 and one fewer rect. **Tighten the threshold in
`tests/coup/test_render_budget.c` to the new value** and update
`scripts/qa/baseline.json` — a budget that is never ratcheted down stops being a
gate.

Then, if the BIOS prerequisite is satisfied:

```bash
make coup-saturn
python3 scripts/qa/qa_capture.py build/coup_game/game.cue build/qa/phase1-frames
```

Gate G2: a frame showing the game screen must contain the table artwork. Compare
a captured frame's colour histogram against the converter's `--preview` PNG; the
dominant hues must match. **Its RED fixture:** rebuild with
`slScrAutoDisp(NBG0ON)` only, confirm the check fails, then restore.

Gate G9: capture once with `saturn_bg_init()` called and once with it stubbed
out; the frames must differ. If they are identical the layer is not reaching the
screen, regardless of what any register says.

- [ ] **Step 9: Commit**

```bash
git add pal/saturn/saturn_bg.c pal/saturn/saturn_bg.h pal/saturn/sgl_defs.h \
        pal/saturn/saturn_pal.c examples/coup/coup_render.c \
        examples/coup/saturn/Makefile examples/coup/saturn/main_saturn.c \
        tests/coup/test_saturn_bg.c tests/coup/test_render_budget.c \
        Makefile scripts/qa/baseline.json
git commit -m "feat: paint the game table from a VDP2 NBG1 bitmap and drop the VDP1 background rect"
```

---

## Definition of done

- `make test-coup` passes, with per-screen VDP1 budgets asserted and the game
  screen's fill area measurably lower than the Phase 0 baseline.
- `make qa-binary` reports GREEN, and the recorded headroom is compared against
  the 131,072 bytes one background costs.
- `scripts/qa/baseline.json` records real measured numbers, not estimates.
- Every gate has been shown firing RED on its stated fixture before being
  accepted.
- With the BIOS present: a captured frame shows the painted table behind the UI,
  and a background-disabled build produces a visibly different frame.
- No file under `tools/`, `coup_protocol.h`, `coup_rules.c` or `coup_game.c` has
  been modified.

## Follow-on plans (not this plan)

1. Remaining scenes + the embed-versus-CD-stream decision, driven by Task 3's
   measured headroom.
2. Phase 2 — fade/transition module, at which point scene swapping moves under
   the fade.
3. Phase 3 — sprite palette migration to LUT/RGB mode, then the gouraud lighting
   pass. **Nothing can be lit until that migration lands** (spec section 4.6,
   item 7).
4. Phases 4-5 — card-flip animation and per-screen polish.
