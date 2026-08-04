# HANDOFF — Saturn Visual Facelift (2026-08-04)

For the next session (any model). Branch: `claude/saturn-visual-facelift`
(created from `main` @ a1f798d, repo `likeagfeld/coup-saturn` cloned at `W:\coupsaturn`).

## Where this stands

The user asked for a data-driven, no-guessing plan (sega-saturn-developer
methodology + superpowers brainstorming + Claude Design) for a Saturn visual
facelift on a new branch. **The design spec is written but NOT yet approved by
the user. No implementation has started — the brainstorming HARD-GATE is
active: do not write feature code until the user approves the spec.**

## Done

1. **Methodology steps 1-4 complete**: doc index read; VDP1 + VDP2 full
   citation sweeps done by subagents; entire Saturn rendering path of the repo
   inspected (`saturn_pal.c`, `saturn_vdp1.{c,h}`, `saturn_vdp2.{c,h}`,
   `coup_render.c`, loaders, `main_saturn.c`).
2. **Design spec**: `docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md`
   — 3 approaches (A polish / B dual-VDP RECOMMENDED / C Mode-7 deferred),
   per-screen design, VRAM/CRAM/cycle budgets, 8 RED-firing gates (G1-G8),
   6 phases, risks.
3. **Hardware findings preserved**: `docs/saturn/facelift-hw-reference.md`
   (condensed from the two sweep reports — encodings, SGL calls, constraints,
   gotchas — sufficient to implement without re-running the sweeps).
4. **Art direction settled by data**: the four shipped PNGs
   (`examples/coup/assets/{CoupTitleScreen,gamescreen,Waiting_Room2,winscreen}.png`)
   already define a "Royal Court" language (gold filigree / midnight damask /
   leather / candle glow). The facelift adopts it; Catppuccin chrome colors are
   replaced (status colors retained). NOTE: Waiting_Room2.png has P1-P8 slot
   frames and button plates PAINTED INTO the art — the CUI layout must be
   calibrated to those regions (added to plan Phase 1).
5. **Claude Design mockups** (5 of 7 cards) in `docs/design/`:
   `foundations/palette.html`, `foundations/layer-stack.html`,
   `components/panels.html`, `screens/title.html`, `screens/game-table.html`.
   Each has the `<!-- @dsCard group="..." -->` first-line marker.

## Done (continued — Opus 5 session)

6. **Motion cards written**: `docs/design/motion/card-flip.html` (12-frame
   trapezoid storyboard with vertex math) and `motion/transitions.html`
   (colour-offset ramps, flash, mesh limitation stated honestly).
7. **All 7 cards synced to Claude Design** — project "Design System"
   `019e1bff-8c1b-7eda-aaa6-3cc368c89e78`, planId
   `plan_019e1bff8c1b7eda_0f0c504db51f`, localDir `docs/design`. Re-sync with a
   fresh `finalize_plan` using the same globs.
8. **Spec self-review found and fixed three hardware contradictions** — this was
   the substantive work of the session, not a formality:
   - **Gouraud/half-lum/half-trans need an RGB-code source**; every game sprite
     is colour-bank-16, so *no sprite could have been lit as designed*. Fix:
     Phase 3 migrates lit sprites to colour mode 1 (16-colour LUT, RGB entries,
     32 B each); frees CRAM banks 16-24. Now spec §4.6 item 7.
   - **Shadow/half-trans read the framebuffer**, so a shadow cast onto the VDP2
     background is a no-op and a glow over it turns opaque. Fix: opaque shadow
     plates + gouraud halos. Spec §4.6 item 8.
   - **Sprite-screen colour calc is global** (RGB sprites all use priority/CC
     register 0) — no per-seat translucency exists. Spec §4.6 item 9.
   - Added **gate G9 (effect-efficacy)**: every effect must measurably change
     pixels vs an effect-disabled capture. This is the gate that catches the
     "compiles, runs, looks identical" class the three bugs above belong to.
   - Added Phase 1 **layout calibration** to the painted UI regions in
     `Waiting_Room2.png` / `gamescreen.png`.
   - §4.4 fill budget recomputed: now ≤120 k px-clocks (~25%), *below* the
     current build.
9. **Server compatibility verified and documented** (new spec §8, answering the
   user's turnkey question): protocol/rules/server files are structurally
   untouched. Only coupling is frame rate — client timers are frame-counted,
   server's are wall-clock. Measured floors: heartbeat ≥10 fps (600 frames vs
   `HEARTBEAT_TIMEOUT=60.0`), challenge/block ≥50 fps (client 10 s vs server
   12.0 s), influence/exchange ≥30 fps. Added **gate G10** (≥55 fps worst-case
   frame + network poll must survive transitions). Verdict: turnkey, no server
   change, no protocol bump.

## Remaining

1. **User approval of the spec** — still the open gate. Not yet given.
2. After approval: invoke `superpowers:writing-plans` to produce the detailed
   implementation plan from the spec, then implement phase-by-phase with the
   gates. Every Saturn subagent dispatch MUST carry the sega-saturn-developer
   skill-binding preamble verbatim (complete-doc-index.md read + RED-firing
   gate rule — see skill §SUB-AGENT DISPATCH REQUIREMENT).

## Key session facts a fresh context needs

- Working dir `W:\coupsaturn` IS the clone (was empty, cloned in place).
- Baseline numbers: VDP2 uses ~12 KB of 512 KB (NBG0 text only, B1); VDP1
  textures ~155 KB of 444 KB; CRAM banks 32-63 free; every VDP1 command today
  has grda=0. Full-screen bg is a 71,680 px VDP1 polygon — moving it to NBG1
  refunds that fill budget.
- The four scene PNGs are 1536×1024 sources → converter must produce 512×256
  8bpp bitmaps (visible 320×224 window) + ONE shared 256-color palette
  (CRAM banks 32-47).
- WRAM cannot hold all four backgrounds embedded (+512 KB vs ~700 KB music
  already embedded) → CD loading decision is measurement-gated in Phase 0 (G8).
- Skills to invoke when resuming: `sega-saturn-developer` (mandatory),
  then follow brainstorming flow from checklist item 7.
