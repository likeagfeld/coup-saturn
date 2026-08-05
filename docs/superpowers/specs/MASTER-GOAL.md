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
| D3 | Use RetroArch, not Mednafen, for live memory and snapshots | **WORKING** — see §7 |
| D4 | Replace ALL original artwork with the official/generated art | in progress |
| D5 | Animations and effects for every action | done, unit-tested |
| D6 | Studio-grade visual quality; nothing pixelated or over-reduced | measured: 36–38 dB backgrounds |
| D7 | Assets cleanly placed; text centred on buttons | padding-centred labels eliminated; 7 screens still position labels literally |
| D8 | Complete the whole game end to end, every screen | **IN PROGRESS** |
| D9 | Server must stay turnkey — no protocol or rules changes | held; §8 unbroken |
| D10 | Slight compression acceptable to fit more assets | done losslessly (45% saved) |

---

## 2. Measured state (facts, not claims)

```
host tests            278 / 278 green
verify_facelift       10 / 10 gates green
fidelity              bg    game 36.4 dB, title 38.3 dB, rules 33.9 dB
                      portraits 29.9–34.8 dB, 13–14/15 palette, detail 97–104%
wordmark on screen    correlation peak 0.354 at offset (0,0), control 0.097
WRAM-H headroom       197,344 B  (measured hang point 181,312 — G8 lies)
                      ONLY 1,032 B above the 15,000 B safety margin
VDP1 textures         288,544 B, ends 0x057720 of 0x80000
backgrounds resident  3 of 7 delivered (game, title, rules)
labels                0 padding-centred; 1 computed, 140 literal of 141 draws
```

**HEADROOM IS NEARLY EXHAUSTED.** The wordmark cost 8,016 B and left 1,032 B
of slack over the safety margin. Nothing further of consequence fits in WRAM
without removing something or moving assets to CD streaming. Treat P3 and P5
as blocked on that decision, not merely unstarted.

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

### P1 — Title wordmark — **DONE**, verified on a captured frame
Fixed in 484a3ca. The sprite is drawn at (32, 2), keyed on its dark backing by
a border-connected flood fill, with no plate behind it. Proven on hardware
output by `qa_title_wordmark.py`: masked correlation peaks at 0.354 exactly at
the placed position, against 0.097 for the same template on the logo-less
backdrop, which the gate runs as a negative control every time.

### P2 — Layout audit, every screen — **PARTLY DONE**
`qa_centring.py` audits every render function and classifies each text draw by
how its x position is derived.

Done: the worst class is gone. Six labels were centred by padding a string
literal with spaces (`"     GAME  OVER"`, `"      ^  [%c]  v"`, and others) —
centring correct only for that exact string at that exact advance. They now use
`draw_centered()`, which measures the string. The gate fires RED on the
pre-fix source (4 hits from git) and is clean now.

Remaining: 140 of 141 text draws still take a literal position, across 7
screens. **Not all of these are defects** — the rules screen's 84 draws are
left-aligned body lines and must stay that way. The real work is the subset
where a plate frames a label: those two must stay concentric. Convert those,
then run `qa_centring.py --strict` to hold the line.

### P3 — Remaining backgrounds
Four delivered scenes do not fit: lobby, connecting, victory, defeat. Options,
in order of preference:
1. CD streaming — load the active scene from disc on transition. New subsystem.
2. Reduce to 4bpp (16 colours) for the less detailed scenes — halves to 35,840 B.
3. Accept the title fallback for minor screens.

### P4 — RetroArch harness — **DONE**, see §7

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


---

## 7. RetroArch on this machine (D3) — working

MEASURED 2026-08-05.

```
retroarch  D:\sonicmaniasaturn\tools\retroarch\RetroArch-Win64\retroarch.exe
core       cores\mednafen_saturn_libretro.dll
cart       beetle_saturn_cart = "Extended RAM (4MB)"   (already set)
BIOS       sega_101.bin, mpr-17933.bin, mpr-18811-mx.ic1  (present)
```

It was reported "not installed" for most of this project because the harness
matched only the name `beetle_saturn`, and the installed file uses upstream's
`mednafen_saturn` name. Same emulator. Both are accepted now.

**This machine is shared — another agent runs RetroArch here.** Two rules, both
enforced in `qa_retroarch.py`, neither optional:

1. Never enumerate-and-kill retroarch processes. The Mednafen harness in the
   Saturn skill does a blanket taskkill; copying that here would destroy the
   other session. We record the pid we spawn and terminate only that pid.
2. Bind port 55366, not the default 55355. A read on the default could be
   answered by their emulator running a different game — not a wrong answer we
   would notice, but a plausible one. Verified in practice: a capture ran while
   one of their instances was live, and both survived.

Their `retroarch.cfg` and `retroarch-core-options.cfg` are never written; our
settings go through `--appendconfig`.

Gotcha #3 still applies — a second emulator steals host CPU, so a fixed
wall-clock capture can land in the wrong phase. Every gate that reads a frame
validates it first and reports INCONCLUSIVE rather than RED on a bad capture.

**A cue whose files are not all present boots the BIOS CD player, not the
game.** `examples/coup/saturn/_build/game.cue` declares an audio track that
directory does not contain; captures from it show the BIOS and look exactly
like a boot failure. Capture from `build/coup_game/`, which holds the whole
disc. `qa_retroarch.py` now preflights this.

**VRAM/CRAM are not in SYSTEM_RAM** — `READ_CORE_RAM` exposes WRAM only. For
VDP1/VDP2 memory take a savestate; the core is Mednafen's, so `mcs_extract.py`
parses it.

---

## 8. Gates and what each one actually proves

| Gate | Proves | Proven to fail on |
|---|---|---|
| `verify_facelift.py` | 10 structural invariants | see its own notes |
| `qa_fidelity.py` | conversion loses no more than it must | wrong-source scene (3.7 dB) |
| `qa_portraits.py` | portraits fully opaque, animate | — |
| `qa_title_wordmark.py` | wordmark is ON SCREEN, at its position | the logo-less backdrop |
| `qa_centring.py` | no label centred by space padding | pre-fix source, 4 hits |
| `qa_retroarch.py --check` | emulator + core + shared-host safety | missing core, unresolvable cue |

Two gates were themselves defective and were fixed rather than trusted:
`qa_fidelity` guessed each scene's source art (the converter now records it in
the header), and `qa_retroarch` matched only one of the core's two names.

---

## 9. Backgrounds: why CD streaming, not 4bpp (MEASURED 2026-08-05)

The user approved either. The arithmetic decides it, and 4bpp loses.

```
real slack available          1,032 B   (197,344 headroom - 181,312 hang - 15,000 margin)
one background  8bpp         71,680 B   (320x224 visible window)
one background  4bpp         35,840 B
3 scenes resident today     215,040 B

A  add 4 missing scenes at 4bpp   needs 143,360 B   short by 142,328 B
B  convert ALL 7 scenes to 4bpp   needs  35,840 B   short by  34,808 B
C  stream, one scene resident     FREES 143,360 B   all 7 scenes at 8bpp
D  stream chunked into VRAM       FREES 206,848 B   all 7 scenes at 8bpp
```

4bpp cannot deliver the missing scenes no matter how good it looks - even
converting every scene to 16 colours still overruns the slack by 34,808 B. So
the quality question never arises. Streaming is strictly better on all three
axes: more scenes, full 8bpp quality, and it hands back ~200 KB of WRAM.

### Viability, verified by link probe

The SGL CD API (`SGL_CD.H`: `slCdInit`, `slCdOpen`, `slCdLoadFile`,
`slCdTrans`, `slCdGetStatus`) is present in `LIBCD.A` and **links clean**.

It first appeared unusable - `SetCDFunc`, `DMA_ScuSetPrm`, `DMA_ScuStart`,
`DMA_ScuGetStatus`, `CSH_Purge`, `slDMAXCopy`, `slDMAStatus` all came back
undefined. That was link ORDER, not absence: `LIBCD.A` is listed after
`LIBSGL.A`, so the SGL objects it needs had already been passed. Wrapping the
libraries in `-Wl,--start-group ... -Wl,--end-group` resolves every one.

**The Makefile link line must use a link group.** Without it the CD objects
fail to resolve and the obvious-but-wrong conclusion is that SGL 3.02j has no
CD support.

GFS (`SEGA_GFS.H`) is declared and `GFS_TRN.O` is in `LIBCD.A`, but the SGL CD
API is the supported surface here and is sufficient.

### Timing budget

The binding constraint is the server, which must stay turnkey (D9). Measured
from `tools/coup_server/server.py`:

```
CHALLENGE_TIMEOUT / BLOCK_TIMEOUT   12.0 s   <-- tightest
INFLUENCE / EXCHANGE_TIMEOUT        30.0 s
TURN_TIMEOUT                        60.0 s
HEARTBEAT_TIMEOUT                   60.0 s
```

A scene load must never stall the client into any of those windows. Two things
make that comfortable: 71,680 B at Saturn 2x (~300 KB/s) is ~0.24 s plus seek,
and scene changes happen at phase boundaries, not inside a challenge window.
TCP buffers during a stall, so a brief block costs latency, not messages.

**Budget: a scene load must complete in under 1.0 s, measured on the real
build.** That is the "fast enough" bar. It is to be measured via a timer
written to WRAM and read live over `READ_CORE_RAM`, not estimated.
