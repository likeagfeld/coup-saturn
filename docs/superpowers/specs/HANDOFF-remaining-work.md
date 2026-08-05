# Remaining work — measured state, 2026-08-05

Branch `claude/saturn-visual-facelift`. Everything below is measured, not
estimated. Read `MASTER-GOAL.md` for the standing directives and
`2026-08-05-hardware-validation-plan.md` for the hardware-correctness review.

## Delivered and gated

| Area | Evidence |
|---|---|
| 8 backgrounds, streamed | 33.9-38.4 dB; panel seams trimmed; shadows lifted |
| Title wordmark | couptitlelogo.png, correlation 0.502 vs 0.179 control |
| CRAM collision | fixed; wordmark colour error 167 -> 32 |
| Magenta fringe | 45 -> 0 palette entries |
| Effect pacing | 0.30 s -> 0.90 s; drawn at 2x about their centre |
| Body font | custom Alagard 8x8 everywhere; built-in no longer drawn |
| Portraits | per-channel levels; all five now use 15/15 palette slots |
| Hand cards | official 48x72 art, no downscale |
| Game over | winner in display face, scrollable recap, winning action marked |
| CD streaming | 0.35 s/scene, 66-load stress, no handle leak |
| CD-DA restore | path proven to run; **never heard** - see below |

293/293 host tests. Gates: `verify_facelift` (11 sub-gates), `qa_fidelity`,
`qa_portraits`, `qa_magenta`, `qa_cram_map`, `qa_cram_msb`, `qa_centring
--strict`, `qa_legibility`, `qa_title_wordmark`, `qa_cd_budget`,
`qa_audio_restore`, `qa_retroarch --check`.

## Remaining

**One item: the logo font.** Specification measured and recorded at the bottom
of this file. It is an upgrade, not an outstanding fix.

## The one thing nobody has verified

**No one has ever heard this build.** The shared `retroarch.cfg` on this
machine is a headless capture rig - `video_driver = "null"`,
`audio_enable = "false"` - so every capture in this project was
audio-disabled. The CD-DA restore fix was written from documentation.
`qa_audio_restore.py` proves the path RUNS and re-issues playback on a real
scene change, which is the half that can be measured without ears; it does not
prove sound reaches the speaker. `scripts/qa/ra_coup_interactive.cfg`
overrides the drivers for a human tester.

## Gate lessons worth keeping

Three gates were found agreeing with the present state rather than testing it,
and the pattern is the same each time:

- `qa_fidelity` compared against the RAW source, so every deliberate grading
  step (shadow lift, seam trim, portrait stretch) made it report RED on art
  that had converted perfectly. The reference must carry every transform.
- `qa_title_wordmark` correlates EDGE maps and is colour-blind by design, so
  it stayed GREEN while eight sprite palettes were corrupted. A shape gate and
  a colour gate are not substitutes.
- The same gate scored a three-hour-old capture with no freshness check. A
  capture older than the disc is not evidence; it now returns INCONCLUSIVE.

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
