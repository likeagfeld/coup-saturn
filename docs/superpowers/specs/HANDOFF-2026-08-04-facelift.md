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

## Remaining (in order)

1. **Two more design cards**: `docs/design/motion/card-flip.html` (distorted-quad
   flip storyboard, ~12-frame Y-flip) and `docs/design/motion/transitions.html`
   (color-offset fade ramp, flash-white, mesh dissolve).
2. **Sync mockups to Claude Design** via the DesignSync tool: the user's project
   "Design System" `projectId 019e1bff-8c1b-7eda-aaa6-3cc368c89e78` is EMPTY and
   ready. Flow: `finalize_plan` (localDir `W:\coupsaturn\docs\design`, writes
   `foundations/*.html`, `components/*.html`, `screens/*.html`, `motion/*.html`)
   → `write_files` with localPath per file → done (cards self-index via @dsCard).
3. **Spec self-review** (placeholder/consistency/scope/ambiguity), then
   **present spec + mockups to the user for approval** (brainstorming checklist
   items 7-8).
4. After approval: invoke `superpowers:writing-plans` to produce the detailed
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
