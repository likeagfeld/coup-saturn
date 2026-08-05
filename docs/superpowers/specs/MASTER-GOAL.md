# Coup Saturn — Master Goal and Completion Spec

Single source of truth. Every directive given, the current measured state, and
what remains. Work top to bottom; nothing is "done" until its gate is green.

Branch `claude/saturn-visual-facelift`.

---

## 1. Standing directives (all of them, verbatim in intent)

| # | Directive | Status |
|---|---|---|
| D1 | Data-driven only. No guessing, no assuming, no "looks right" | **BINDING** |
| D2 | Adversarial QA on everything generated; gates must be proven RED first | **BINDING** |
| D3 | Use RetroArch, not Mednafen, for live memory and snapshots | **BLOCKED** — not installed; harness written and waiting |
| D4 | Replace ALL original artwork with the official/generated art | in progress |
| D5 | Animations and effects for every action | done, unit-tested |
| D6 | Studio-grade visual quality; nothing pixelated or over-reduced | measured: 36–38 dB backgrounds |
| D7 | Assets cleanly placed; text centred on buttons | done for PLAY; audit rest |
| D8 | Complete the whole game end to end, every screen | **IN PROGRESS** |
| D9 | Server must stay turnkey — no protocol or rules changes | held; §8 unbroken |
| D10 | Slight compression acceptable to fit more assets | done losslessly (45% saved) |

---

## 2. Measured state (facts, not claims)

```
host tests            278 / 278 green
verify_facelift       10 / 10 gates green
fidelity              bg 36.4–38.3 dB, 255/255 palette, detail 105–107%
                      portraits 29.9–34.8 dB, 13–14/15 palette, detail 97–104%
WRAM-H headroom       205,360 B  (measured hang point 181,312 — G8 lies)
VDP1 textures         280,352 B, ends 0x055720 of 0x80000
backgrounds resident  3 of 7 delivered (game, title, rules)
```

**G8 IS NOT TRUSTWORTHY ALONE.** A build hung with 181,312 B headroom while G8
reported GREEN. Gate F in `verify_facelift.py` enforces a 15 KB margin above
that measured point. Never ship on G8 alone.

---

## 3. Screen-by-screen completion checklist

Each screen is done when: correct backdrop, correct art, no duplicated or
misplaced element, text centred, and a gate proves it.

| Screen | Backdrop | Art | Layout | Done |
|---|---|---|---|---|
| Title | skyline ✅ | portraits ✅, wordmark ⚠️ | PLAY centred ✅ | **no** |
| Lobby | falls back to title ⚠️ | seat panels | needs audit | no |
| Connecting | title fallback ⚠️ | — | needs audit | no |
| Rules | rules table ✅ | official overlay ✅ | hints only ✅ | **yes** |
| Game | council chamber ✅ | portraits ✅, coins ✅, effects ✅ | needs audit | near |
| Game over | own image | VICTORY/DEFEAT ✅ | needs audit | near |
| Settings | title fallback ⚠️ | — | needs audit | no |
| Name entry | title fallback ⚠️ | — | needs audit | no |

---

## 4. Open work, in priority order

### P1 — Title wordmark (REGRESSION, my error)
I removed the logo sprite after mis-measuring: I counted gold pixels in the
title backdrop and read the **sunset** as an embedded wordmark. `B1_title.png`
is a pure skyline with no logo. The title currently has **no branding**.

Fix: draw the pack's `L1_wordmark.png` (256×64, 15 colours) as a sprite with
its dark backing keyed out (MEASURED: threshold `max(rgb) <= 56` keys 63%),
and **no header panel behind it** — the panel was the real thing that looked
wrong over the art.

### P2 — Layout audit, every screen
Text centred on every button and panel, using `text_px_w()` — never an assumed
advance. The Alagard face advances **8 px**, not 16; assuming otherwise put
labels 16 px off. Add a gate that fails if any label is positioned by a literal.

### P3 — Remaining backgrounds
Four delivered scenes do not fit: lobby, connecting, victory, defeat. Options,
in order of preference:
1. CD streaming — load the active scene from disc on transition. New subsystem.
2. Reduce to 4bpp (16 colours) for the less detailed scenes — halves to 35,840 B.
3. Accept the title fallback for minor screens.

### P4 — RetroArch harness
`scripts/qa/qa_retroarch.py` is written and ready: live `READ_CORE_RAM`, the
byte-pair-swap correction, and the SYSTEM_RAM offset map. It needs RetroArch +
the Beetle Saturn core installed, with `beetle_saturn_cart = "Extended RAM
(4MB)"` and network commands enabled on port 55355.
**VRAM/CRAM are not in SYSTEM_RAM** — those still need a savestate, which the
same core produces and `mcs_extract.py` parses.

### P5 — Boot splash
`L2_boot_splash.png` (320×224, 255 colours) is delivered and unused.

---

## 5. Hardware rules already paid for — do not relearn

1. **VDP1 polygon colours must set bit 15** or VDP2 reads them as CRAM indices.
   This corrupted every polygon colour in the game until found.
2. **`slScrAutoDisp` returns OK = 0, NG = −1.** A `== 0` failure test is
   inverted.
3. **`COL_TYPE_*` are bit fields**: `0x00/0x10/0x20/0x30/0x40`, not 0–4.
4. **Loaders chain** VRAM and CRAM. Recomputed bases have collided twice.
5. **A 256-colour bitmap palette must start on a 256-colour boundary** — only
   CRAM `0x000/0x200/0x400/0x600` are legal.
6. **Sprite index 0 is transparent.** Portraits must be fully opaque; effects
   must be keyed. Opposite requirements, same mechanism.
7. **`declared_actor` / `blocker_id` are `uint8_t`; the sentinel is `0xFF`.**
   A `>= 0` guard is vacuous.
8. **Mednafen's VDP1 VRAM dump is byte-pair-swapped.**
9. **`pipefail` around the build script** or a compile error reads as success.
10. **The Alagard font advances 8 px** despite a 16×16 cell.

---

## 6. Definition of done

- Every screen in §3 marked yes
- `verify_facelift.py`, `qa_fidelity.py`, `qa_portraits.py` green
- Host tests green, including the effect-trigger suite
- Headroom above the measured hang point with margin
- A capture per screen, committed
- No asset from the original game still shipping where official art exists
