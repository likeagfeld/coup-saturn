# Coup Saturn Visual Facelift — Design Spec

Date: 2026-08-04
Branch: `claude/saturn-visual-facelift`
Status: **DRAFT — awaiting user approval** (no implementation until approved)
Method: sega-saturn-developer binding methodology v2.4.0 (doc-index read → subsystem
doc sweep → local code inspection → plan-first). All hardware claims below carry
citations to ST-013-R3 (VDP1), ST-058-R2 (VDP2), SGL SCROLL.TXT, SL_DEF.H, or
NOV96 DTS samples. No claim in this spec is guessed.

---

## 1. Goal & Non-Goals

**Goal:** a Saturn-native visual overhaul that makes Coup look like a polished
commercial-era Saturn release — layered painted backgrounds, gradient-lit UI,
real card-flip animation, scene fades — while preserving:

- 60 fps frame budget (netplay RX polling depends on it — 48 B/frame budget,
  `coup-reference.md:152`)
- The existing buffered-VDP1 + SGL coexistence architecture (flush → slSynch →
  slot-2 JUMP), which is proven on hardware
- The cross-platform CUI layering (no Saturn code in `core/` — CLAUDE.md layer rule)
- All game logic, protocol, and server code untouched

**Non-goals (v1):** hi-res 640/352 modes, RBG0 rotation backgrounds (deferred —
see Approach C), new music/SFX, N64/SDL visual parity.

---

## 2. Measured Baseline (current visual state)

Inspected end-to-end: `pal/saturn/saturn_pal.c`, `saturn_vdp1.{c,h}`,
`saturn_vdp2.{c,h}`, `examples/coup/coup_render.c`, sprite/anim/gameover
loaders, `main_saturn.c`.

| Subsystem | Current use | Idle capacity |
|---|---|---|
| VDP2 layers | NBG0 only: 16-color 8×8 text grid, priority 5. Char data 0x25E60000 (bank B1), PNT 0x25E76000 (`saturn_pal.c:87-92`) | NBG1, NBG2, NBG3, RBG0 all off. No color calc, no line scroll, no color offset, no line/back gradients |
| VDP2 VRAM | ~12 KB of 512 KB (text cells + PNT in B1) | Banks A0 (128 KB), B0 (128 KB), A1 (~127.7 KB, top 256 B = SGL rot-param + back color), B1 middle ~80 KB |
| VDP1 | 2048-slot buffered command list; flat RGB555 polygons for every panel; 8×8 sprite text (4bpp color-bank); 4bpp palette sprites. `grda=0` on every command — zero gouraud, zero transparency, zero distortion effects | Gouraud, half-trans, half-lum, shadow, mesh, distorted quads, zoom-point scaling — all unused |
| VDP1 texture VRAM | ~155 KB of 444 KB (fonts + 25,920 B sprites + 92,160 B anim + ~36 KB gameover strips) | ~289 KB free |
| CRAM (mode 0, 1024 colors) | 16-color banks 0-15 text, 16-24 sprites, 25-31 gameover | Banks 32-63 free (512 colors — room for two full 256-color background palettes) |
| Full-screen art assets | `CoupTitleScreen.png`, `gamescreen.png`, `Waiting_Room2.png`, `winscreen.png` exist in `examples/coup/assets/` but only the game-over image ships, squeezed through 7 VDP1 strips | The other three painted scenes are unused on Saturn |

Net: the game renders as flat rectangles + text because the entire VDP2
background engine and every VDP1 shading feature are untouched. The facelift
is almost pure headroom consumption, not restructuring.

---

## 3. Candidate Approaches

### Approach A — VDP1 polish pass (conservative)
Gouraud-gradient panels/buttons, drop shadows, mesh fades, distorted-sprite
card flips, color-offset scene fades. No new VDP2 planes.
- **Pros:** zero cycle-pattern risk; every effect is doc+sample-proven; ~1 week scale.
- **Cons:** background stays flat; the painted scene art stays unused; "nicer,"
  not "stunning."

### Approach B — Full dual-VDP facelift (RECOMMENDED)
Everything in A, **plus** the VDP2 background stack: painted full-screen
backgrounds on NBG1 (the existing PNGs, converted to 256-color bitmaps),
line-scroll shimmer effects, line-color-screen gradient tinting, per-line back
gradient, color-offset fades unified across sprites + all layers. Game-over
image moves from 7 VDP1 strips to the same VDP2 path (frees VDP1 VRAM + CRAM
banks 25-31).
- **Pros:** transforms every screen; frees VDP1 fill-rate (the full-screen
  background rect moves off VDP1 entirely); all features doc-proven, most
  sample-proven; risk contained by RED-firing gates (below).
- **Cons:** must own the VDP2 cycle pattern (gotcha: SGL auto-arbiter drops
  standalone NBGs whose char/bitmap data sits in bank B1 — mitigated by placing
  all new data in bank A0/B0 and gating with a plane-visible pixel-mass check).

### Approach C — Mode-7 showpiece (maximal)
B plus RBG0: rotating/zooming table felt on the game screen, perspective title
fly-in.
- **Pros:** peak-Saturn wow factor; sample-proven (SEGA3D_3, BIPLANE).
- **Cons:** RBG0 claims whole VRAM banks via RAMCTL RDBSxx and its banks'
  cycle registers are ignored (ST-058-R2 §6, txt:6437-6452) — forces a full
  re-plan of all four banks including the text layer's home; coefficient
  tables eat another bank or half of CRAM. Highest regression risk to the
  proven text/sprite stack for the least gameplay-relevant payoff.

**Recommendation: Approach B now; C staged later as an isolated experiment
(title screen only) once B's bank plan is stable.**

---

## 4. Recommended Design (Approach B)

### 4.1 Layer stack (320×224, all screens)

| Priority | Layer | Content |
|---|---|---|
| 7 | NBG0 (16-color text) | UI text — unchanged pipeline, now on top |
| 6 | VDP1 sprite screen (`slPrioritySpr0`) | Cards, portraits, panels, effects |
| 3 | NBG1 (256-color bitmap, bank A0) | Painted per-screen background |
| 1 | Back screen (per-line table) | Vertical gradient behind everything |

(Text moves from 5→7 and sprites 4→6 so a future NBG2 overlay layer can slot
between; renumbering is register-only via `slPriority`/`slPrioritySpr0` —
SCROLL.TXT:751-760.)

### 4.2 Per-screen treatments

**Title** — `CoupTitleScreen.png` as NBG1 bitmap; logo sprite with animated
gouraud sheen (table values swept −16..+15); portrait parade gains per-sprite
gouraud rim-light; menu button = gouraud-gradient polygon + polyline gradient
border; subtle sine line-scroll shimmer on NBG1 (`slLineScrollTable` animation,
sample-proven SEGA2D_1/MAIN.C:40-63).

**Lobby** — `Waiting_Room2.png` background. **The art already contains painted
P1–P8 slot frames and the three button plates**, so VDP1 does not redraw that
chrome: the CUI layout is calibrated to the art's measured pixel regions
(Phase 1 deliverable) and VDP1 draws only the *fill/state* inside each frame —
a gouraud wash for occupied slots, half-luminance for empty ones. Ready pulse =
palette cycling on the slot's CRAM bank.

**Game** — `gamescreen.png` table background; seats = gouraud panels whose
gradient itself carries the depth cue (light at upper-left → dark at
lower-right), with an optional opaque dark plate offset +3,+3 drawn *before*
the panel where more separation is wanted; **card reveal/loss =
distorted-sprite Y-axis flip** (trapezoid collapse to the centre line over
12 frames, texture swap at the midpoint, then expand — ST-013-R3 §7.6, samples
S_4_3_1/2); influence-loss = card flip to back + mesh-dissolve out; timer bar
= gouraud gradient that shifts green→amber→red; coin payouts = zoom-point
scaled sprite pop (ZP=0xA centre anchor); current-turn seat is marked by a
**gouraud halo band drawn as part of the panel** (opaque, amber→panel-colour
ramp) — not a translucent overlay, for the reason in §4.6 items 7–8.

**Game over** — `winscreen.png`/game-over art via the NBG1 bitmap path
(retires the 7-strip VDP1 hack); winner portrait scaled up with gouraud
spotlight; mesh + color-offset dissolve into it.

**All transitions** — unified fade module: `slColOffsetA(k,k,k)` ramp with
`slColOffsetAUse(NBG0ON|NBG1ON|SPRON)` fades text + sprites + background
together (ST-058-R2 §13.1). Flash-white for challenge results (+k ramp).

### 4.3 Effect catalog with hard costs

| Effect | Mechanism | Cost | Proof |
|---|---|---|---|
| Gradient panels | Untextured polygon + gouraud table | +8 B VRAM/style; write-only mode, **no fill-rate penalty** (ST-013-R3 txt:4070-4074) | SMPSPR20.C:219-245 |
| Gouraud-lit sprites | RGB-mode textured cmd, ccalc=100, CMDGRDA | same | SMPSPR20.C:177-189 |
| Depth / drop shadow | Opaque dark polygon offset +3,+3 drawn before the panel (default), **or** ccalc=001 shadow polygon where it lands on other VDP1 RGB content | opaque plate: 1× · shadow mode: **6×**, and a **no-op over transparent framebuffer** (§4.6 item 8) | txt:4026-4051; SMPSPR20.C:164-175 |
| Selection glow | Gouraud halo band inside the panel command (amber→panel ramp). True half-transparency (ccalc=011) only where it overlaps opaque VDP1 RGB content | gouraud: free · half-trans: **6×**, ≤ ~8 k px/frame | txt:4058-4074; SMPSPR20.C:135-147 |
| Card flips | Distorted sprite quad | ~1× fill of quad area | ST-013-R3 §7.6; S_4_3_1/2 |
| Mesh dissolve | CMDPMOD bit 8 | free; works on palette text too | txt:3538-3554 |
| Dim/disable art | Half-luminance (ccalc=010) | no FB read — cheap | txt:4053-4056 |
| Scene fades | VDP2 color offset A/B | registers only | ST-058-R2 §13.1 |
| BG shimmer | NBG1 line scroll table | 896 B–2 KB table in A1 | SEGA2D_1/MAIN.C:40-63 |
| Gradient tint | Line color screen + CC ratio | 448 B table + registers | ST-058-R2 §7.1/§11.3 (doc-proven; no local sample — gate it) |
| BG gradient | Per-line back screen table | 448 B in A1 | SCROLL.TXT:148-157 |

### 4.4 VDP1 fill budget (why this stays 60 fps)

Draw rate is 1 px/28.6 MHz clock → ~477 k px-clocks/frame at 60 fps
(ST-013-R3 txt:1114-1115). Today the game burns ~72 k on the full-screen
background rect alone plus ~10-30 k UI. Moving backgrounds to VDP2 **refunds**
that 72 k.

After the §4.6 corrections the facelift is dominated by 1× work: gouraud panels,
halos and gradient borders cost the same as today's flat rectangles (gouraud is
write-only, txt:4070-4074); opaque shadow plates are ordinary polygons; flips
are one quad each (≈4 k). The only 6× spend left is optional panel-over-panel
half-transparency, capped at **≤ 8 k blended px/frame (≈48 k px-clocks)**.
Projected total ≤ 120 k px-clocks — about 25% of budget, and *lower than the
current build* because of the background refund. Gate G4 (BEF transfer-over +
true-vblank fps) enforces this empirically rather than by arithmetic.

### 4.5 Memory plan

**VDP2 VRAM**
- A0 (0x25E00000): NBG1 bitmap, 512×256 @8bpp = exactly 128 KB (BM_512x256,
  0x20000-aligned — SCROLL.TXT:859-906). One scene resident at a time;
  swapped during fade-outs (~128 KB CPU copy spread over fade frames).
- A1: line-scroll table + back-screen gradient table + line-color table
  (≤ 4 KB, placed below SGL's reserved top 256 B at 0x1FF00).
- B0: reserved for a future NBG2 parallax/overlay layer (Approach C staging).
- B1: text cells + PNT — untouched.
- **Cycle pattern**: NBG0 16-color (1 PN + 1 CG in B1) + NBG1 256-color bitmap
  (2 CG in A0) is legal per ST-058-R2 §3.3 access-count tables
  (HTML p.52 authoritative — the .txt extraction of Table 3.3 is garbled;
  PDF wins). Set explicitly with `slScrCycleSet` if `slScrAutoDisp` returns
  NG or the plane-visible gate fires (SGL auto-arbiter B1 drop gotcha,
  skill gotcha #7 / SGLFAQ §2-8).

**CRAM — MEASURED map, correcting the original estimate.** The first draft of
this section said "banks 32-47 free" for the background palette. That was
wrong: it accounted for the sprite and game-over loaders but missed
`coup_anim_loader.c`, which claims banks 32-36 and runs *after* the background
is uploaded, silently overwriting its first 80 colours. The real map is:

| CRAM bytes | 16-col banks | Owner |
|---|---|---|
| 0x000-0x1FF | 0-15 | text palettes (`saturn_pal.c`) |
| 0x200-0x31F | 16-24 | sprite palettes (`coup_sprite_loader.c`) |
| 0x3E0-0x3FF | 25-31 | game-over strips (`coup_gameover_loader.c`) |
| 0x400-0x49F | 32-36 | animated portraits (`coup_anim_loader.c`) |
| **0x600-0x7FF** | **48-63** | **background palette (256-colour bank 3)** |

A 256-colour bitmap palette must start on a 256-colour boundary, so the only
legal homes are 0x000, 0x200, 0x400 and 0x600. The first three are all taken,
which makes bank 3 the only available slot — not a preference but a constraint.
Any future 256-colour layer will have to share it or reclaim banks 25-31 by
retiring the game-over strips.

CRAM stays in its current mode for v1. If per-scene palettes ever exceed one
bank, `slColRAMMode(CRM16_2048)` is the escape hatch; it costs the blur
function, which this design does not use.

**VDP1 VRAM**
- Gouraud table pool: 64 static tables (512 B) + 16 double-buffered animated
  tables (256 B, frame-parity A/B addresses) at a fixed reserved offset inside
  the texture area — NOT in 0x80-0x10080 (rewritten every frame) and NOT in
  0x7FF00-0x7FFFF (SGL `GouraudRAM` lighting reserve, SL_DEF.H:225).
  CMDGRDA patched at buffer time (address/8), same as CMDSRCA today.
- Card-face textures for flips: reuse existing 64×96 portraits + 48×72 back.
- Freed: ~36 KB game-over strips.

**WRAM / binary size — MEASURED 2026-08-04, superseding the original estimate.**
Phase 0 ran the gate against the real linker map and the assumption behind this
paragraph turned out to be false:

| Measurement | Value |
|---|---|
| `.text` / `.data` / `.bss` | 81,248 / 28,496 / 87,520 bytes |
| `_end` | `0x0607BA50` |
| WRAM-H headroom below the stack (`0x060FFC00`) | **541,104 bytes** |
| 128 KB backgrounds that fit embedded (64 KB reserve) | **3** |

`examples/coup/saturn/coup_music_data.h` — 2.7 MB of ASCII hex — **is included
by no translation unit.** It is an orphan file; the music plays from the CD-DA
track. There is no ~700 KB of resident music, and therefore no forced CD
streaming.

Revised design: **embed the backgrounds.** One fits trivially (Phase 1a), and
three of the four fit. Only the fourth needs another approach — a lower colour
depth, a smaller bitmap, or streaming that one scene — decided when Phase 1b
adds the remaining scenes. CD streaming is no longer a prerequisite for any of
this work.

### 4.6 Known constraints designed around (from the doc sweep)

1. **Font sprites are color-bank code** (`SATURN_VDP1_SPR_PMOD`,
   `saturn_vdp1.h:107`): their framebuffer pixels have MSB=0, so
   half-transparency/shadow will **replace, not blend** glyphs
   (ST-013-R3 txt:4058-4063). Rule: no half-trans/shadow surface may be drawn
   over text; text fades use VDP2 color offset or mesh. (Optional later:
   switch fonts to LUT mode with RGB entries — txt:2646-2649 — out of v1 scope.)
2. **Draw order = blend order** (txt:2894-2898): background-dependent effects
   (shadow, glow) must be emitted after what they cover; the render code
   already draws back-to-front.
3. **Back screen reaches only regions where every layer is transparent**
   (skill gotcha #6, measured): the felt/table color is NBG1's job, never the
   back screen.
4. **SGL VDP2 registers are per-vblank rewrite transients** (skill gotcha #12):
   all visual gates measure rendered frames (screenshots), never savestate
   register snapshots. VRAM/CRAM savestate reads remain valid.
5. **ZP codes restricted to {5,6,7,9,A,B,D,E,F}**, display width ≥ 0
   (ST-013-R3 precautions 6847-6848) — the zoom animation clamps at 0.
6. **END command must stay reachable every frame** (txt:2335-2350) or the
   flush spin-wait deadlocks — effect code never rewrites `ctrl` link fields.
7. **Gouraud / half-luminance / half-transparency require an RGB-code source**
   — colour-bank results "cannot be guaranteed" (txt:2739-2743, 3950-3959).
   Every game sprite today is colour-bank-16 (`SATURN_VDP1_SPR_PMOD`,
   `saturn_vdp1.h:107`), so **no sprite can be gouraud-lit as-is.** Any sprite
   receiving gouraud/half-lum (portraits, card faces, logo, coins) migrates to
   **colour mode 1 (16-colour LUT)** with RGB555 entries, MSB=1: 32 bytes per
   palette, 32-byte aligned, not at address 0 (txt:2641-2644), CMDCOLR = LUT
   address/8. Cost ≈ 14 × 32 B = 448 B of VDP1 VRAM, and it *frees* CRAM banks
   16-24. Untouched sprites stay colour-bank. This is a Phase 3 deliverable,
   not an optional extra — the headline lighting effects depend on it.
   The 4bpp **font** stays colour-bank in v1 (text is never lit; see item 1).
8. **Shadow and half-transparency read the framebuffer, so they do nothing
   over the VDP2 background.** Both act only where the framebuffer pixel has
   MSB=1, i.e. where earlier VDP1 RGB content was drawn; MSB=0 (the transparent
   erase colour, which is what reveals NBG1) is left untouched by shadow and
   *replaced* by half-transparency (txt:4026-4041, 4058-4063). A drop shadow
   cast onto the painted table is therefore a **no-op**, and a translucent glow
   over it turns **opaque**. Hence the opaque-plate and gouraud-halo designs
   in §4.2/§4.3.
9. **VDP1 blending ≠ VDP2 blending, and sprite-screen colour calculation is
   global.** To blend the whole VDP1 output over VDP2 layers you must use
   VDP2 sprite colour calculation (`slColorCalcOn(SPRON)` +
   `slSpriteCCalcCond(CC_MSB)` + `slColRateSpr0`) — but RGB-format sprite
   pixels carry no priority/CC bits and therefore **always use sprite priority
   register 0 and CC ratio register 0** (ST-058-R2 txt:8575-8576, 8628-8629).
   There is no per-command translucency against the background: enabling it
   makes *every* sprite translucent. v1 therefore leaves sprite-screen colour
   calculation **off**, and reserves it as a deliberate whole-screen effect
   (e.g. a dimmed table behind a modal) if a later phase wants it.

---

## 5. Verification Plan (RED-firing gates — binding, per skill Step 6)

Harness: Docker ISO build + Mednafen boot + screenshot capture (existing
skill-standard harness), host quiet during captures (skill gotcha #3). Every
gate must fire RED on a known-bad fixture before it enters the suite (meta-QA
rule). Gates run per phase in a `verify_facelift` script.

| Gate | Measures | RED fixture proof |
|---|---|---|
| G1 cycle-pattern | `slScrAutoDisp` return == OK **and** SGL cycle table at 0x060FFC00+0xD0..0xDF matches a hand-computed legal pattern | run with NBG1 bitmap deliberately placed in B1 |
| G2 plane-visible | pixel-mass + color histogram in a region owned solely by NBG1 vs converted-asset reference (SSIM ≥ threshold) | capture with NBG1 display bit off |
| G3 fade | mean screen luminance sweeps monotonically to 0 across a scripted color-offset ramp, sampled ≥ 5 points | omit SPRON from the offset mask (sprites stay lit → non-monotone region) |
| G4 frame budget | VDP1 BEF transfer-over bit + frame-time (true-vblank fps) on the busiest scripted game frame | inflate glow polygon to full-screen (forces 6× overrun) |
| G5 text legibility | glyph-region pixel diff vs baseline on every screen (text must be pixel-identical when no fade active) | draw a half-trans polygon over the log area |
| G6 flip geometry | card-flip midpoint frame: quad width ≤ 2 px at frame N/2, full art restored at frame N | skip the degenerate-quad clamp |
| G7 gouraud address | static analyzer over the frame's command buffer: every nonzero CMDGRDA×8 falls inside the reserved pool, 8-byte aligned | point one table into the command-slot region |
| G8 binary/WRAM | linker map: `_end` + stack headroom vs 0x060FFC00; ISO boots to title in Mednafen | embed all four bitmaps (expected overflow) |
| G9 effect-efficacy | for each declared effect, capture the same frame with the effect enabled vs disabled; the pixel delta in the effect's own region must exceed a threshold (an effect that changes nothing has silently failed) | apply gouraud to a colour-bank sprite (the pre-migration state) — delta is zero, gate fires RED |

G9 exists because this spec's own review found three effects that would have
compiled, run, and produced **no visible change**: gouraud on colour-bank
sprites, shadow cast onto the transparent framebuffer, and translucency over
the VDP2 background (§4.6 items 7-9). "It builds and looks unchanged" is the
failure mode this class of work produces; G9 is the gate that catches it
mechanically instead of by eye.

Per skill gotcha #4: G2/G3/G5/G6 are rendered-frame measures because the
symptoms are visual; register/memory reads are only used where the question is
register/memory-shaped (G1, G7, G8).

---

## 6. Implementation Phases (each ends with its gates GREEN + a committed capture)

- **Phase 0 — Baseline & harness:** capture reference screenshots of every
  screen; record fps, VDP1 command counts, binary size, WRAM map; stand up
  `verify_facelift` with G4/G8 live. *(No visual change.)*
- **Phase 1 — VDP2 background stack:** asset converter (PNG → 8bpp bitmap +
  shared 256-color palette), NBG1 bitmap path, cycle pattern, scene-swap under
  fade, game-over strip retirement, **and layout calibration**: measure the
  painted UI regions in `Waiting_Room2.png` (P1–P8 slot frames, three button
  plates) and `gamescreen.png` (header/log/hand divisions) and align the CUI
  layout constants to them, so drawn UI lands inside the painted chrome instead
  of fighting it. Gates G1/G2/G8 + a region-alignment capture diff.
- **Phase 2 — Fade/transition module:** color-offset ramps, mesh dissolve,
  flash. Gate G3.
- **Phase 3 — VDP1 lighting pass:** **sprite palette migration to LUT/RGB mode
  first** (§4.6 item 7 — prerequisite for every lit effect, verified by a
  colour-fidelity capture diff against the pre-migration frame), then the
  gouraud table pool + gradient panels/borders/halos, opaque shadow plates,
  half-lum dimming; render-code migration from flat `panel()` to a styled panel
  API. Gates G4/G5/G7 + G9 (below).
- **Phase 4 — Card & object animation:** distorted-sprite flips, zoom-point
  coin/portrait pops, timer-bar gradient states. Gate G6.
- **Phase 5 — Screen-by-screen polish:** title shimmer, lobby pulse,
  line-color tint, per-line back gradient; final capture set vs Phase 0
  before/after. All gates.

Every Saturn sub-agent dispatched during implementation carries the skill's
verbatim binding preamble (complete-doc-index.md read + RED-firing gate rule),
per methodology v2.5.0.

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| SGL auto-arbiter drops NBG1 (B1-class bug resurfacing in A0) | G2 pixel-mass gate + explicit `slScrCycleSet` fallback, pattern hand-computed from ST-058-R2 §3.3 |
| Fill-rate overrun from 6× effects | G4 BEF gate; effects budgeted in §4.4; mesh as free fallback |
| WRAM/binary overflow from embedded art | G8; CD-loading path decided by Phase 0 measurement |
| CRAM shared-palette quality too low across 4 scenes | quantizer quality report in converter; fallback = CRAM mode 1 (2048 colors), gate-verified |
| Scene-swap upload visible | swap only during full-black fade hold (G3 verifies black) |
| Frame-rate drop erodes client/server timer slack (§8) | G10 frame-rate floor ≥55 fps on the worst-case frame; fill budget projected *below* current build |
| Scene-swap copy stalls the network poll | copy chunked across fade frames; `coup_tick()` must run every frame during transitions |
| slPriority renumbering regressions | Phase 1 capture-diff on all screens before any effect work |

## 8. Server & Protocol Compatibility (turnkey requirement)

**Requirement: the existing server, bridge, bot, and web client must run
unmodified against a faceflifted Saturn client.** Verified against the code,
not assumed:

**Untouched by this work.** The facelift changes only `pal/saturn/*` (rendering),
`examples/coup/coup_render.c`, `examples/coup/coup_ui.h` layout constants, and
asset data. It does not touch `coup_protocol.h` (SNCP wire format),
`coup_rules.c` (the shared engine the server loads as `libcoup_rules.so`),
`coup_game.c` (state machine / message handlers), or the transport. No message
is added, removed, or re-encoded; no rule or seed behaviour changes. Rendering
never serialises anything. **Protocol compatibility is therefore structural,
not merely tested.**

**The one real coupling is frame rate**, because several client timers are
frame-counted while the server's are wall-clock. Measured margins:

| Timer | Client (frames @60 fps) | Server (wall-clock) | Frame-rate floor to stay correct |
|---|---|---|---|
| Heartbeat | 600 frames = 10 s (`coup_game.c:31`) | `HEARTBEAT_TIMEOUT = 60.0` (`server.py:71`) | **≥ 10 fps** (6× margin) |
| Challenge / block | 10 s (`coup.h:34-35`) | 12.0 s (`server.py:109-110`) | **≥ 50 fps** |
| Influence / exchange | 15 s (`coup.h:36-37`) | 30.0 s (`server.py:111-112`) | **≥ 30 fps** |

The client windows are already deliberately shorter than the server's, so the
displayed bar expires *before* the server acts — the existing design has slack
built in. A sustained frame-rate drop stretches the client's countdown in
wall-clock terms and erodes that slack; the binding constraint is the
challenge/block pair at **50 fps**. Disconnection (heartbeat) needs a
catastrophic drop below 10 fps and is not a realistic risk.

This is comfortably satisfied: §4.4 projects total VDP1 fill *lower than the
current build* because moving the full-screen background to VDP2 refunds
~72 k px-clocks/frame. Two things are nonetheless gated:

- **G10 frame-rate floor** — scripted worst-case game frame (max players, all
  effects, mid-flip) must sustain ≥ 55 fps true-vblank, with the RED fixture
  being a deliberately unbudgeted full-screen half-transparency pass. This gate
  is the server-compatibility contract expressed as a measurement.
- **Scene-swap stall** — the 128 KB NBG1 bitmap copy is the only long operation
  introduced. It runs only during screen transitions, is spread across the
  black-hold frames of the fade, and must keep `coup_tick()` (network RX/heartbeat)
  running each frame. Phase 0 measures the real copy rate; if a single-frame
  copy would exceed the budget it is chunked. No transition may block the
  network poll.

**Net answer: turnkey.** No server, bridge, bot, or web-client change is
required, and no protocol version bump. The facelift is a client-side
presentation change bounded by a frame-rate contract that G10 enforces.

## 9. Citations index

ST-013-R3 (VDP1 manual, .txt line refs above); ST-058-R2 (VDP2 manual, .txt
line refs; Table 3.3 resolved from HTML p.52 — PDF-derived source wins over
garbled .txt extraction); SGL302 `DOC/210A_US/SCROLL.TXT`; `INC/SL_DEF.H`;
SGLFAQ_F.TXT §2-6/§2-8/§3-1; samples SMPSPR20/70/80.C, S_4_3_1..4,
SAMPLE2/SEGA2D_1, SEGA3D_3, BIPLANE, SBLSGL04; skill references
vdp1-reference.md / vdp2-reference.md / coup-reference.md; field gotchas #3,
#4, #6, #7, #8, #12 (sega-saturn-developer skill v2.7.0). Full agent sweep
reports preserved in the session transcript.
