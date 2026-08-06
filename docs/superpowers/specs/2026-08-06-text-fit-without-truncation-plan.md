# Fit every label WITHOUT removing characters — plan

Branch `claude/saturn-visual-facelift`. Supersedes the truncation-based
fixes of the previous pass. The user has ruled out truncation: no player
name, no log line, no label may lose a character.

## 0. Docs / samples consulted (skill methodology steps 1-4)

- `references/complete-doc-index.md` (read end-to-end, this session).
- Subsystem rows taken from its lookup table:
  - **VDP1 sprite drawing** — text on this client is one 8x8 VDP1 Normal
    Sprite per character (`coup-reference.md` "Text rendering"), so a
    label's width is a sprite-count question, not a font-metric guess:
    ST-013-R3 (VDP1 manual) + `vdp1-reference.md`.
  - **VDP2 scroll/background** — the panels the text sits on are VDP1
    polygons over the VDP2 backdrop; ST-058-R2 + `vdp2-reference.md`.
  - **SGL** — ST-238-R1 for the `slDispSprite`/command-slot contract the
    PAL buffers into (`coup-reference.md` "VDP1 Command Coexistence").
  - **Coup working example** — `references/coup-reference.md`, which
    states the display target: **Saturn 320x224, 8x8 char cells, 40x28
    grid, 16 px safe margins for overscan** ("Layout System"). That
    16 px figure is the reason a full-bleed 320 px text row is not an
    acceptable answer on a CRT.
- Local code path read end-to-end before editing: `examples/coup/coup_render.c`
  (draw helpers, all 15 render functions), `examples/coup/coup_ui.h`
  (every layout rect), `examples/coup/coup.h` (bounds),
  `examples/coup/coup_game.c` (`coup_log`, log line construction),
  `examples/coup/saturn/main_saturn.c` (font registration),
  `pal/saturn/fonts/saturn_font_buch_4x6.c`, `scripts/qa/qa_text_overflow.py`.

## 1. The measurement that decides every site

Font metrics MEASURED from the font sources (the same formula
`coup_render.c:text_px_w()` uses, `(n-1)*advance + cell`):

| face | file | cell_w | advance_x | 39 chars | 15 chars |
|---|---|---|---|---|---|
| coup_8x8 (COUP_FONT_BODY) | `saturn_font_coup_8x8.c` | 8 | 8 | **312 px** | 120 px |
| alagard_8x8 | `saturn_font_alagard_8x8.c` | 8 | 8 | 312 px | 120 px |
| alagard_16x16 (DISPLAY) | `saturn_font_alagard_16x16.c` | 16 | 8 | 320 px | 128 px |
| buch_4x6 | `saturn_font_buch_4x6.c:69-71` | **8** | **4** | **160 px** | 64 px |

`COUP_LOG_LINE_LEN = 39` (`coup.h:29`) is a REAL bound, not a
theoretical one: `coup_game.c:693-708` builds
`"<name> plays <action> on <name>"` = 15+7+11+4+15 = **52 chars**, which
`coup_log()` (`coup_game.c:410`) caps at 39. Long-named games hit the cap
routinely.

**Structural conclusion.** A 39-char line is 312 px in every 8 px face.
The screen is 320 px. Therefore a container can hold a full log line only
if it has at most 8 px of total chrome — no border, no scroll arrow, no
`>` marker column, and no CRT-safe inset. The game log's panel is
*already* `{0, 0, 320, 54}` (`coup_ui.h:712`), the widest a panel can be,
and it still cannot hold the line (312) plus its scroll arrow (8) plus
any margin. **Widening is provably insufficient for the 39-char class.**

Word-wrap is possible but costs entries: measured log lines routinely
exceed 30 chars, so most entries would take 2 rows, cutting the game log
from 5 visible entries to 2-3, the recap likewise, and it would force
three separate rewrites of the ring/scroll arithmetic that has already
had one index bug (`coup_render.c:3207-3214`).

`buch_4x6` has **cell_width 8 and cell_height 8** — the same cell box as
the body face — and only halves `advance_x`. It therefore drops into the
existing 8 px rows and 10 px pitch with zero layout change, and renders a
full 39-char line in 160 px (52% of the container's own width).

## 2. Strategy per site (choice + the number that justifies it)

| # | site | content max | 8px width | container avail | strategy |
|---|---|---|---|---|---|
| 1 | `coup_render.c:1755` connecting log row | 39 | 312 | x=40 → 292 = **252 px** | **3. condensed face** (160 px) |
| 2 | `coup_render.c:2929` game log row | 39 | 312 | x=4 → 306 = **302 px** | **3. condensed face** (160 px) |
| 3 | `coup_render.c:3238` recap row | 39 | 312 | x=38 → 296 = **258 px** | **3. condensed face** (160 px) |
| 4 | `coup_render.c:2765,2774` hand name | 15 | 120 | inter-card gap **≤ 80 px** | **3. condensed face** (64 px) |
| 5 | `coup_render.c:2252` seat name (`safe_copy` to 8) | 15 | 120 | seat box **60 px** | **3. condensed face** (64 px) + widen seat text budget |
| 6 | `coup_render.c:2511` select_target row `%-10.10s` | 22 | 176 | plate 76..244 = 160 px | **1. widen** column to 72..248 → 168 px, format to 21 chars |
| 7 | `coup_render.c:2736` idle `%.8s` | 27 | 216 | 168 px | **2. wrap** to 2 rows |
| 8 | `:2544,2577,2579,2615` response titles `%.10s` | 33-36 | 264-288 | 168 px | **2. wrap** to 2 rows |
| 9 | `:2358,2486` `safe_copy(title_buf, …, 21)` | — | — | — | delete the clip; wrapping replaces it |

Sites 4 and 5 are the two where widening and wrapping BOTH fail, with
numbers: the hand's inter-card gap can be at most
`GAME_CENTER_W - 2*COUP_CARD_ART_W = 176 - 96 = 80 px` even with the card
faces pushed flush to the panel edges, and a 15-char name is 120 px in
any 8 px face; the seat box is `GAME_SEAT_W = 68 px` total. A name is one
token, so it cannot be word-wrapped. Condensed face is the only
non-truncating option, and it is stated as such.

## 3. Container widening, and the proof it collides with nothing

`GAME_CONTENT_X` 76 → 72 (`= GAME_CENTER_X`), `GAME_CONTENT_W` 168 → 176
(`= GAME_CENTER_W`), `GAME_TEXT_X` 80 → 76 (`= GAME_CONTENT_X + 4`).

The phase panels become flush with `center_panel = {72, 56, 176, 92}`,
which is the column they are drawn inside. Horizontal neighbours are the
seat columns: `GAME_LEFT_X = 0` width 68 (ends **68**) and
`GAME_RIGHT_X = 252` (starts **252**). The widened column is 72..248, so
the 4 px `COUP_BG_GRID` gutter that draws every panel border is preserved
on both sides — the widening consumes the phase panels' own inset, not
the gutter. `GAME_CONTENT_X/W/GAME_TEXT_X` appear nowhere outside
`coup_ui.h` (verified by grep), so the change is contained.

Vertical: the idle panel grows 24 → 38 for its second text row; it sits at
y=58 inside `center_panel` (56..148), so 58+38 = 96 ≤ 148. The response
title bar grows 12 → 22 when its title wraps, pushing 2-4 items down 10 px
(challenge_wait items 84..100 → 94..110, still ≤ 148). `render_phase_select_action`
has 7 items but a fixed 14-char title ("Select Action:") that never wraps,
so its item block does not move.

## 4. Changes

**`examples/coup/coup_ui.h`** — `COUP_FONT_CONDENSED 4`; widen
`GAME_CONTENT_*`/`GAME_TEXT_X`; idle panel height/bar offset; seat
`max_name_chars` retired as a clip.

**`examples/coup/saturn/main_saturn.c`** — register
`saturn_font_buch_4x6_desc()` as registry index 4.

**`examples/coup/coup_render.c`** —
- delete `draw_text_fit()` (it is the clipping primitive) and every call;
- add `draw_text_font()` (draw in an explicit face, restore the previous);
- add `coup_wrap_row()` — pure, host-testable word wrap on spaces, never
  mid-word, sharing a home with `coup_log_ring_index()`;
- remove every `%.Ns` precision and both `safe_copy(title_buf, …)` clips.

## 5. Acceptance criteria (measurable, gate-first)

1. `qa_text_overflow.py --strict` GREEN, baseline unchanged or lower —
   0 overflows, unresolved count not raised.
2. **New gate `qa_text_overflow.py --no-truncation`**: zero
   `%.Ns` / `%-N.Ns` precision specifiers on user-visible strings, zero
   clipping calls (`draw_text_fit`, `safe_copy` into a display buffer) in
   `coup_render.c`. Negative control: injecting a `%.8s` must make it RED.
   Must fire RED on the pre-fix tree (6 precisions + 4 fits + 2 safe_copy).
3. `qa_centring.py --strict` GREEN.
4. `make test-coup` 477/477 or higher, plus new wrap tests.
5. `scripts/docker-saturn-build.sh examples/coup/saturn` exit 0.
