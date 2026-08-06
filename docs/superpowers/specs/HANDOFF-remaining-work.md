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

## The five reported defects, and what each actually was

Every one turned out to be a different thing from what it looked like.

| Reported | Actual cause |
|---|---|
| Effects duplicated / cut off / off-centre / partial | ONE swapped argument pair in `coup_fx_loader.c`. Source and destination sizes were passed the wrong way round, so VDP1 fetched 128x128 out of a 64x64 texture - through the next frames and off the end - and painted it at native size at a position computed for double size. Four symptoms, one swap. |
| Animations still too fast | The portrait idle was a bare `/ 8` inline in `coup_render.c`, so it was never revisited when effects were slowed 3 -> 14. Both rates are now named constants, asserted in SECONDS. |
| Text bleeding outside boxes | The lobby `controls_panel` was 6 px too short for its own third control row. Whole-screen audit now reports zero spills. |
| Game-over recap not scrollable | Wired in `3690961`. |
| Sliver of a neighbouring background | NOT the artwork. `saturn_bg_upload()` fills a 512x256 plane from a 320x224 scene and left columns 320-511 and rows 224-255 holding the PREVIOUS backdrop. The "neighbour" was the last scene, which is why trimming the art never moved it. |

## Remaining

**Nobody has heard this build.** `qa_audio_restore` now proves the restore path
runs 24x and re-issues playback 24x, so execution is established - but the
shared `retroarch.cfg` is a headless rig and no capture in this project has
ever had audio enabled. `scripts/qa/ra_coup_interactive.cfg` overrides the
drivers for a human tester. This needs ears, not another gate.

**The logo font** is an upgrade, not an outstanding fix. Spec at the bottom of
this file.

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
- `qa_audio_restore` and `qa_cd_budget` both read their witness at the linker
  map address, which is a uniform 0x40 off from where the image loads. Both
  were reading 64 bytes past their own struct. `qa_cd_budget` called the
  garbage INCONCLUSIVE; `qa_audio_restore` called it RED - "every backdrop
  load has silently killed the music" - on a build whose restore path had run
  24 times. A gate that cannot distinguish "not booted yet" from "the path
  failed" must report the former. Witnesses are now located by their MAGIC
  within a bounded window, and the delta is printed so drift stays visible.
- `test_fx_trigger` capped effect duration at 2.00 s, written when the longest
  effect ran 1.87 s. A bound set just above the value it measures can only
  ratify the present state - and this one went on to fail a FIX rather than a
  defect. Bounds are now derived from something external (the server's 12 s
  window), and asserted in one place only.

The shape is always the same: **a gate that agrees with the present state
instead of testing it.** Five instances now. When writing one, ask what
reading would make it fire, and produce that reading deliberately.

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

---

# Open items as of 2026-08-06

## 1. RBG0 (Approach C) — needs two emulator runs, nothing else

Built, host-gated, committed, and OFF by default behind
`-DCOUP_RBG0_TITLE_DEMO`. The shipped disc is unaffected.

**Build the demo to a SCRATCH path, not over `build/coup_game/`** — that
directory holds the disc the user is currently testing, and overwriting it
with a demo build would silently change what they are looking at.

```
CCFLAGS_EXTRA="-DCOUP_RBG0_TITLE_DEMO" bash scripts/docker-saturn-build.sh examples/coup/saturn
mkdir -p build/rbg0_demo && cp examples/coup/saturn/_build/{track01.bin,game.cue} build/rbg0_demo/
cp build/coup_game/rebellion.wav build/rbg0_demo/
python scripts/qa/qa_rbg0_witness.py          # execution only, not display
python scripts/qa/qa_retroarch.py --shot build/rbg0_demo/game.cue --seconds 15 \
       --out build/qa/screens/title_rbg0.png
python scripts/qa/qa_rbg0_legibility.py       # THE gate
```
Then rebuild WITHOUT the flag before shipping anything.

`qa_rbg0_legibility.py` currently returns INCONCLUSIVE because no capture
exists. Its self-test already proved the measure discriminates (RED 9.6 on
corrupted text, GREEN 141.0 on legible, threshold 60.0), so either verdict
from a real capture is trustworthy. A RED is a FINDING — priority/palette
worth one iteration — not automatically a bug to chase.

## 2. The sheen and rim-light are a DECISION, not a task

`saturn_vdp1_draw_sprite_gouraud()` exists and is tested. It cannot be
pointed at the current sprites: ST-013-R3 (VDP1_Manual.txt:4091-4094) says
colour calculation on a colour-bank sprite is "not guaranteed", and every
sprite here is 4bpp bank mode.

Lighting the wordmark sheen therefore costs a texture re-encode to RGB555:
**8,192 -> 32,768 bytes** for a 256x64 wordmark, +24 KB of VDP1 VRAM for one
cosmetic effect. The portrait rim-light needs that AND a distorted-sprite
gouraud variant (portraits draw through the scaled path).

`COUP_GRD_SHEEN` stays listed as undrawn in `qa_animations_wired.py` so the
gap keeps announcing itself. Do not pay the 24 KB without deciding it is
worth it.

## 3. Still unbuilt

Winner-portrait zoom, game-over mesh dissolve, challenge flash-white.

## 4. Web staging

`deploy/nginx-saturncoup.conf` has an additive `/staging/` block; the
redesigned client goes in `web-staging/` and `web/` must stay untouched.
Deploy is a human step: copy the tree to `/opt/coup-server/web-staging/`,
then `nginx -t && systemctl reload nginx`.
