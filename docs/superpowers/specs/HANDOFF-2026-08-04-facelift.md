# HANDOFF — Saturn Visual Facelift (2026-08-04)

Branch: `claude/saturn-visual-facelift` (from `main` @ a1f798d).
Repo `likeagfeld/coup-saturn`, working copy `W:\coupsaturn`.

**State: Phases 0, 1a, 1b, 2 and the Phase 3 panel-lighting pass are DONE,
committed and pushed. 266/266 host tests green. Binary gate green with
~405 KB of WRAM-H headroom.**

---

## How to build and test on THIS machine

There is no `make` and no host C compiler here — only Docker and Python.

```powershell
# Host unit tests (run from PowerShell, NOT Git Bash: MSYS mangles -w /src)
docker run --rm -v "W:\coupsaturn:/src" -w /src gcc:14 make test-coup

# Saturn disc. ALWAYS use pipefail - piping the script through `tail` alone
# reports tail's exit code and a hard compile error looks like success.
set -o pipefail
./scripts/docker-saturn-build.sh "$(pwd)/examples/coup/saturn" "" 2>&1 | tail -3
echo "EXIT=${PIPESTATUS[0]}"

# Gates
python scripts/qa/qa_binary.py examples/coup/saturn/_build/game.map
python scripts/qa/qa_capture.py build/coup_game/game.cue build/qa/out --seconds 20
```

Assembling a runnable disc needs `game.cue` + `track01.bin` + `rebellion.wav`
together in `build/coup_game/` — the cue references the wav relatively.

---

## What shipped

| Area | Result |
|---|---|
| Painted backdrop | `gamescreen.png` → 512×256 256-colour VDP2 bitmap on NBG1 in bank A0, shown on **every** screen via a single `screen_bg()` helper |
| VDP1 load | Saturn game-screen fill **142,624 → 70,944 px**, exactly half. The facelift made the game *cheaper* to draw |
| Portraits | Opaque brass-framed medallions, gouraud-lit |
| Typography | Alagard 16×16 registered as a display face; brass-plated **PLAY** button |
| Lighting | Gouraud gradients on panels — free, because gouraud is write-only |
| Transitions | VDP2 colour-offset fade-in on every screen change |
| Allocation | VDP1 texture offsets **and** CRAM banks now chain: fonts → sprites → game-over → animation |
| Build | `-MMD -MP` header dependency tracking |

Tests grew 248 → 266.

---

## Eight latent defects found — none visible to code inspection

1. **`COL_TYPE_*` were wrong** in `sgl_defs.h`: `0,1,2,3,4` instead of SGL's
   `0x00,0x10,0x20,0x30,0x40` (`SL_DEF.H:525-529`). Latent because the only
   caller used `COL_TYPE_16 = 0`, correct by coincidence. Would have corrupted
   any future non-16-colour layer.
2. **CRAM collision**: `coup_anim_load()` claims banks 32-36 from `0x400` and
   runs *after* `saturn_bg_init()`, silently overwriting 80 background colours.
3. **`slScrAutoDisp` polarity**: returns `Bool` where `OK = 0`, `NG = -1`
   (`SEGA_XPT.H:70-71`). The natural `== 0` failure test is inverted and would
   have fallen back to text-only on every *successful* boot. It was declared
   `void`, discarding the signal entirely. No SGL sample checks it.
4. **Hardcoded VDP1 offsets** in two of three asset loaders, assuming exactly
   one registered font. Adding a second garbled the title logo.
5. **No header dependency tracking** — editing a `.h` left stale objects linked.
6. **Missing `<stdint.h>`** in loader headers, relied on transitively.
7. **Duplicated CRAM arithmetic** across loaders — same class as #2.
8. **Over-saturated highlight palettes** in three animated portraits.

---

## Tooling findings (save the next session hours)

- **RetroArch cannot see VDP1/VDP2 VRAM.** `READ_CORE_RAM` exposes only
  `SYSTEM_RAM` (WRAM-L/H). VRAM, CRAM and registers need the **Mednafen
  savestate** path.
- **`mcs_extract`'s VDP1 VRAM dump is byte-pair-swapped.** Read raw, every
  command looks like garbage (`ctrl=0x0400` rather than `0x0004`). Un-swap
  before parsing. `--peek16` already compensates; the region dump does not.
- Savestate capture works via
  `D:\sonicmaniasaturn\tools\qa_savestate.ps1`, but it resolves the cue
  relative to its own repo root and uses its own `MEDNAFEN_HOME`. Stage a
  disc under `D:\sonicmaniasaturn\_coupqa\` with a **single-track cue** to
  avoid copying the 70 MB audio track.
- `CUI_TEST_BEGIN` / `CUI_TEST_END` **do not compile** — the macro substitutes
  its parameter into `cui_current_test.name`. Use bare `CUI_TEST(name) { }`.
- Mednafen records into a fixed 704×480 canvas; `qa_capture.py` normalises
  back to 320×224 so gate coordinates equal game coordinates.

---

## Known open item: the yellow band

Reported as "a random yellow band horizontally in the middle of the character
sprite windows". **It is pre-existing** — 189 such pixels in the pre-facelift
capture, invisible against the old black backdrop.

Precisely fingerprinted, so pick it up from here rather than re-deriving:

- Colour is exactly `(248,248,0)` = RGB555 `0x03FF` = **CRAM bank 2**
  (`SATURN_PAL_SELECTED`), i.e. the *text* palette — not a portrait palette,
  not a polygon, not the background.
- Confined to **y 129-136**, one 8-pixel text row (grid row 16), spanning
  x 16-300.
- Ruled out by measurement: no animation frame has a bright row (all 120
  scanned); all nine polygons have correct geometry and colour; the background
  art has no gold rule there; the title layout draws no text at that row
  (`portrait_y=68`, `menu_y=172`, `hint_row=26`); and
  `saturn_rgba_to_palette_slot()` never returns slot 2.
- Something draws a 4bpp part with `CMDCOLR = 2 << 4`. Next step: dump the
  VDP1 command list from a savestate and list every **sprite** command's
  `colr` (the earlier dump only decoded polygons).

Toning three over-saturated highlight palettes dropped the saturated count
509 → 281; the remainder is this band.

---

## Not done / next

1. **Card-flip animation** (spec Phase 4). Design and 12-frame storyboard with
   vertex maths already exist in `docs/design/motion/card-flip.html`.
   Needs the sprite **LUT migration** first if the flip is to be gouraud-lit —
   see spec §4.6 item 7. An unlit flip needs no migration.
2. **Lobby art.** `Waiting_Room2.png` paints P1-P8 slots but
   `COUP_MAX_PLAYERS = 7`, so it is wrong as-is. Recomposite with PIL to the
   real seat count, then calibrate the CUI layout to the painted regions.
3. **Sprite LUT migration** (spec §4.6 item 7) — unlocks lighting on portraits
   and cards.
4. **Generated art.** The user intends to wire up an external image model
   (Kimi K3) via API for new illustrated content. Everything shipped so far is
   procedural (PIL composites, computed gradients, palette edits) or existing
   assets. Genuinely new illustration is the one gap that needs it.

Every Saturn sub-agent dispatch must carry the `sega-saturn-developer` binding
preamble verbatim (`complete-doc-index.md` + RED-firing gate).
