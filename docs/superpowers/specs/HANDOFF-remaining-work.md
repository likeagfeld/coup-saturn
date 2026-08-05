# Remaining work — measured state, 2026-08-05

Branch `claude/saturn-visual-facelift`. Everything below is measured, not
estimated. Read `MASTER-GOAL.md` for the standing directives and
`2026-08-05-hardware-validation-plan.md` for the hardware-correctness review.

## Delivered and gated

| Area | Evidence |
|---|---|
| 8 backgrounds, streamed from disc | 33.9–38.4 dB, all seams trimmed, all shadows lifted |
| Title wordmark | on-screen correlation 0.400 at offset (0,0) vs 0.097 control |
| CRAM collision | fixed; wordmark colour error 167→32 |
| Magenta fringe | 45 → 0 palette entries |
| CD streaming | 0.35 s/scene, 66-load stress, no handle leak |
| Blended UI plates | RGB-code MSB condition, doc-verified |
| CD-DA restore after load | implemented, **never heard** — see below |

Gates: `verify_facelift` (11), `qa_fidelity`, `qa_portraits`, `qa_centring
--strict`, `qa_legibility`, `qa_title_wordmark`, `qa_cram_map`,
`qa_cram_msb`, `qa_magenta`, `qa_cd_budget`. 292/292 host tests.

## Remaining — five items

### 1. Animations too fast and too small

Reported from hardware. Not yet measured.

- Speed lives in `COUP_FX_HOLD_FRAMES` (`coup_render.c`) — frames held per
  effect frame. Effects are 6–8 frames, so total duration is
  `frames * HOLD / 60` seconds.
- Size: `coup_fx_draw()` passes the sprite's authored w/h. Effects are
  32×32 to 64×64. `saturn_vdp1_draw_sprite_scaled()` exists and is already
  used for portraits, so scaling up is a call-site change, not new code.
- **Measure first**: count the actual on-screen duration from a capture
  sequence before changing the constant, so the change is against a number.

### 2. Body font is still the built-in 8×8

The single biggest remaining visual complaint. `COUP_FONT_BODY` (index 0) is
the PAL's built-in face; `COUP_FONT_DISPLAY` (index 1) is Alagard 16×16 and is
used ONLY on headings and the PLAY button.

- Registration order in `main_saturn.c` defines the indices — body first.
- `draw_at()` and `draw_centered()` do not switch fonts, so every body line
  renders in the built-in face.
- Alagard advances **8 px** despite a 16×16 cell (locked by
  `test_centring.c`). Switching body text to it changes no layout arithmetic,
  but doubles glyph height — rows are 8 px apart, so lines WILL collide.
  Row spacing must change with it.
- VDP1 budget has room: worst frame is 468 of 2048 commands, 47% of draw time.

### 3. Portraits washed out and pixelated

- 15 colours each (4bpp), 29.9–34.8 dB, palette use 13–14/15.
- Authored 64×96, drawn at 64×96 on the title and scaled to 32×48 in the hand.
- Washed-out is a quantization/contrast issue: try a per-portrait contrast
  stretch before quantizing, the same shape as `lift_shadows`.
- Pixelated in the hand is the 32×48 downscale. Cards are now available as
  48×72 official art (`COUP_UI_DUKE` etc.) and would look better there.

### 4. Game-over recap

Feature work, nothing exists yet. Wants: winner name much larger, a scrollable
recap of the match's actions, the winning action prominent.

- `coup_event_log.c` already keeps the action log; `st->winner_name` and
  `st->winner_id` are snapshotted at game-over time.
- Backdrop is already victory/defeat art chosen by the same win test.
- Scrolling needs input wiring — `CUI_INPUT_PAGE_UP/DOWN` are free on this
  screen (currently only `[A] Return to Lobby`).

### 5. Custom font from `couptitlelogo.png`

`examples/coup/assets/Official Art/couptitlelogo.png` (1 MB) is delivered and
unused. Wants a full game font derived from its lettering, cascaded everywhere.

- Existing font pipeline: `pal/saturn/fonts/saturn_font_alagard_16x16.c`,
  generated. Match that format.
- A font needs 95 printable ASCII glyphs; the logo supplies four letters.
  Everything else has to be drawn in the same style — this is the largest
  item on the list and should not be started casually.

## Two things that are NOT verified

**No one has ever heard this build.** The shared `retroarch.cfg` on this
machine is a headless capture rig — `video_driver = "null"`,
`audio_enable = "false"`. Every capture in this project was audio-disabled.
The CD-DA restore fix (music survives a backdrop load) is implemented from
documentation and has never been listened to. `scripts/qa/ra_coup_interactive.cfg`
overrides the drivers for a human tester.

**The wordmark art is cropped in the source.** `L1_wordmark.png` is 256×64
with ink on row 63; the 1024×256 variant in the other pack is cropped too.
Nothing in code recovers the missing baseline. New art is required — the
prompt is in the conversation and asks for 512×160 with 10% empty margin.

## How to work on this

- Rebuild: `bash scripts/docker-saturn-build.sh examples/coup/saturn`
- Assemble the playable disc: copy `_build/track01.bin` and `game.cue` into
  `build/coup_game/` (which holds `rebellion.wav`, the CD-DA track).
- Photograph every screen: `bash scripts/qa/capture_all_screens.sh`
- Any screen directly: `CCFLAGS_EXTRA="-DCOUP_QA_SCREEN=<0-7>"` then capture.
  Gate J fails any build that ships with that defined.
- **Another agent uses RetroArch on this machine.** The harness never
  enumerates or kills retroarch processes and binds port 55366, not 55355.

---

# Logo font — construction parameters (MEASURED 2026-08-05)

`couptitlelogo.png` is now the shipped wordmark. It is also the reference for
a full game font. The style is strictly geometric and the rules are measured,
so the remaining work is drawing glyphs to a spec, not deriving one.

## Measured from the four letters

| parameter | value | ratio to cap |
|---|---|---|
| cap height | 477 px | 1.0 |
| stroke width | 105 px | **cap ÷ 4.5** |
| letter box | 431 × 477 | width = **0.90 × cap** |
| letter gap | 43, 46, 46 px | **cap ÷ 10.6** |

All four letters fall within 4 px of the same width. It is a MONOSPACED face:
advance ≈ letter box + gap ≈ cap height.

Style features, from the artwork: rounded OUTER corners, square INNER corners,
and a diagonal slash cut into the C and the P.

## Which cell size to build at

```
cell   cap  stroke  width  counter   verdict
 8px    7      2      6     2x3      works, and fits the existing 8px grid
12px   11      2     10     6x7      works
16px   15      3     14     8x9      works, but needs advance 16
```

**Build at 8×8.** The display font advances 8 on a 16 cell; a 16 px logo face
would need advance 16, doubling the width of every heading and forcing a
re-wrap of the rules pages and the game log. An 8×8 face drops into the grid
untouched, exactly as `saturn_font_alagard_8x8.c` already does.

## What is actually left

95 glyphs drawn to those rules. A 7-segment or geometric-skeleton generator
was considered and rejected: at a 6×7 box with 2 px strokes it produces
acceptable digits and poor letters — K, M, W, X and S in particular. This
needs hand-drawn glyphs in the measured style.

Format to match: `pal/saturn/fonts/saturn_font_alagard_8x8.c` — 1bpp, 8 bytes
per glyph, 95 glyphs from ASCII 32, `bytes_per_row_1bpp = 1`. Register it in
`main_saturn.c` and point `COUP_FONT_BODY` (or a new `COUP_FONT_LOGO`) at it.

**This is an upgrade, not an outstanding fix.** Body text is already the
custom Alagard face everywhere; the built-in 8×8 is no longer drawn anywhere.
