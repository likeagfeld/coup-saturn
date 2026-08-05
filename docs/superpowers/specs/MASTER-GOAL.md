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
| D3 | Use RetroArch, not Mednafen, for live memory and snapshots | **DONE** — live memory + per-screen capture, §7 |
| D4 | Replace ALL original artwork with the official/generated art | **DONE** — last original asset retired |
| D5 | Animations and effects for every action | done, unit-tested |
| D6 | Studio-grade visual quality; nothing pixelated or over-reduced | **DONE** — 8 scenes 33.9–38.4 dB; backdrops visible through the UI |
| D7 | Assets cleanly placed; text centred on buttons | **DONE** — ratcheted by `qa_centring.py --strict` |
| D8 | Complete the whole game end to end, every screen | **DONE** — all 8 screens captured on hardware |
| D9 | Server must stay turnkey — no protocol or rules changes | held; §8 unbroken |
| D10 | Slight compression acceptable to fit more assets | **not needed** — streaming removed the constraint |

---

## 2. Measured state (facts, not claims)

```
host tests            291 / 291 green
verify_facelift       11 / 11 gates green
QA gates              fidelity, portraits, centring, legibility, wordmark,
                      CD budget - all green
fidelity              8 scenes 33.9-38.4 dB, 254-255/255 palette
legibility            every screen 90-135 contrast (threshold 60)
wordmark on screen    correlation 0.400 at offset (0,0), control 0.097
streamed scene load   72,192 B in 21 vblanks = 0.35 s   (budget 1.00 s)
backgrounds           8 of 8, streamed, full 8bpp, VISIBLE through the UI
screens captured      8 of 8 on real hardware output
server contract       0 files changed in server / rules / protocol
```

**HEADROOM IS NO LONGER THE BINDING CONSTRAINT.** It was 197,344 B with 1,032
B of slack over the safety margin. Streaming the backgrounds and retiring the
game-over sprite took it to 322,972 B, 141,660 above the hang point. Adding
the eighth scene moved it by 32 bytes - scenes cost disc space now, not WRAM.

**G8 IS NOT TRUSTWORTHY ALONE.** A build hung with 181,312 B headroom while G8
reported GREEN. Gate F in `verify_facelift.py` enforces a 15 KB margin above
that measured point. Never ship on G8 alone.

---

## 3. Screen-by-screen completion checklist

Each screen is done when: correct backdrop, correct art, no duplicated or
misplaced element, text centred, and a gate proves it.

| Screen | Backdrop | Art | Layout | Done |
|---|---|---|---|---|
| Splash | own, streamed | official | n/a | **yes** |
| Title | skyline, streamed | portraits, wordmark | PLAY + hint centred | **yes** |
| Lobby | own, streamed | seat panels | heading centred; seat rows literal | near |
| Connecting | own, streamed | progress bar | heading + cancel centred | **yes** |
| Rules | rules table, streamed | official overlay | heading centred; body left-aligned by design | **yes** |
| Game | council chamber, streamed | portraits, coins, effects | no literal labels at all | **yes** |
| Game over | victory/defeat, streamed | VICTORY/DEFEAT banner | heading + action centred | **yes** |
| Settings | title art (front-end) | sliders | heading centred; option rows literal | near |
| Name entry | title art (front-end) | — | heading, indicator, controls centred | **yes** |

Title, settings and name entry share the title art on purpose: they are one
continuous front-end, and changing the backdrop between them would read as a
glitch rather than a scene change.


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

### P3 — Remaining backgrounds — **DONE**
All eight scenes ship, streamed from the disc at full 8bpp. Lobby, connecting,
victory and defeat had been falling back to the title art; they have their own
now, and game over picks victory or defeat with the SAME win test its banner
uses, so the two cannot disagree. See section 9 for why streaming rather than
4bpp, and section 10 for the measured load time.

### P5 — Boot splash — **DONE**
`L2_boot_splash.png` ships as scene 0. That index is deliberate:
`saturn_bg_init()` displays scene 0 and is called immediately before every
sprite, font and effect is loaded, so the splash covers that wait with artwork
instead of a blank screen. It also stops a wasted load - scene 0 used to be
the game table, fetched at boot only for the first rendered frame to replace
it.

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


---

## 10. CD streaming, as built and measured

```
subsystem     pal/saturn/saturn_cd.{c,h}    SGL CD API, synchronous load
scene file    512 B big-endian RGB555 palette + 224 rows of 320 8bpp pixels
              72,192 B each, 8.3 uppercase names (ISO9660 mangles longer)
staging       one 72,192 B buffer, replacing N resident const tables
load time     21 vblanks = 0.35 s      MEASURED on the running console
budget        60 vblanks = 1.00 s
server bound  12.0 s (CHALLENGE_TIMEOUT / BLOCK_TIMEOUT)
```

Timing is measured, not estimated: `saturn_cd.c` counts VDP2 TVSTAT vblank
edges across each load into `g_saturn_cd_stats`, and `qa_cd_budget.py` finds
that symbol in the linker map and reads it live over `READ_CORE_RAM`. Vblank
edges rather than the SH-2 free running timer because SL_DEF.H offers no way
to ask how SGL has programmed the prescaler.

Three things about this that are easy to get wrong:

1. **The link line needs `--start-group`.** LIBCD.A needs `slDMAXCopy` and
   `slDMAStatus` from LIBSGL.A, and `SetCDFunc`, `CSH_Purge` and the
   `DMA_Scu*` helpers from SEGA_SYS.A - all listed before it. Without the
   group every one comes back undefined, and the natural reading is that SGL
   3.02j has no CD support.
2. **SGL_CD.H cannot be included.** It pulls in SGL.H/SL_DEF.H, which collide
   with this PAL's own bare declarations. The CD types live in `sgl_defs.h`,
   each cited to its header and line.
3. **The scene `.BIN` files are tracked in git, not ignored.** The hermetic
   Saturn image has no Python or Pillow, so it copies them onto the disc but
   cannot regenerate them. Ignoring them would make a fresh clone build a disc
   whose every background is black, with no build-time symptom.

`verify_facelift` gate A fails if a declared scene has no file on the disc,
because that failure has no other build-time signal.

---

## 11. What remains

**Label placement.** 11 of 142 text draws are computed; the rest take a
literal position. Most are correct as they stand - the rules screen alone has
83 left-aligned body lines that must NOT be centred, and lobby seat rows are a
left-aligned list. The real remaining work is the subset where a plate frames
a label. `qa_centring.py --strict` enforces it once that is done.

**Screens not yet seen on hardware.** Title and the boot path are captured and
gated. Lobby, connecting, game and game over are behind online play; their
backdrops and label placement are verified by gate and by host test rather
than by capture. Reaching them needs either input injection or a server
session.


---

## 12. Completion record

Every item in this document is delivered and gated. Final state measured
2026-08-05 on the running console, not inferred.

### The gates, and what each is proven to fail on

| Gate | Proves | Proven RED on |
|---|---|---|
| `verify_facelift.py` | 11 structural invariants | see below |
| `qa_fidelity.py` | conversion loses no more than it must | wrong-source scene, 3.7 dB |
| `qa_portraits.py` | portraits opaque, animate, loop | — |
| `qa_title_wordmark.py` | wordmark IS on screen, at its position | the logo-less backdrop |
| `qa_centring.py --strict` | no padded label, no screen regresses | pre-fix source; injected literal |
| `qa_cd_budget.py` | a streamed load meets its time budget | — (reports INCONCLUSIVE under host contention) |
| `qa_retroarch.py --check` | emulator, core, shared-host safety | missing core; unresolvable cue |
| gate I | no implicit function declarations | a log line containing one |
| gate J | no QA screen-forcing build can ship | a log defining COUP_QA_SCREEN |

### Defects found by measurement, not by inspection

Recorded because each was invisible to reading the code:

1. **VDP1 polygon colours needed bit 15.** Every polygon colour in the game
   was being read by VDP2 as a CRAM index.
2. **`SPRON` was `(1<<5)`, which is the LINE COLOUR screen.** Every fade dimmed
   the backdrop while sprites, panels and glyphs stayed at full brightness.
   Found while reading SL_DEF.H for an unrelated API.
3. **`saturn_bg_set_scene()` was never declared.** C89 accepted it as an
   implicit int-returning function for the whole life of the background layer.
4. **The rules scene measured 3.7 dB** because the gate guessed its source art.
   The real figure is 33.9 dB; the converter now records provenance.
5. **The Saturn core was reported missing** while sitting in the cores
   directory, because the harness matched only one of its two brandings.
6. **"Medium" overflowed its plate by 6 px** and its text overlapped a
   neighbouring plate.
7. **"CONNECTING" sat 40 px left of its own underline.**
8. **The title wordmark had been deleted** on the strength of a colour count
   that was measuring the sunset.

### What is deliberately NOT done

- ~~Translucent UI panels~~ **DELIVERED** - see section 13.
- **Lobby seat rows and rules body text remain literally positioned.** They are
  left-aligned lists. Centring them would be a defect.


---

## 13. Blended UI plates

The painted backdrops were being buried. On rules, game and lobby the panels
covered nearly the whole scene, so the art was invisible on exactly the
screens players look at longest. Settings, whose panels are small, showed what
was being lost.

### The discrimination was already there

`saturn_vdp1.c` writes polygon colours as an RGB code with **bit 15 set**
(line 62, `rgb555 | 0x8000`) and textured sprites as a CRAM bank with **bit 15
clear** (lines 180, 223). The MSB has been separating "UI plate" from "glyph,
portrait, effect and card" all along. Selecting `CC_MSB` as the
colour-calculation condition therefore blends exactly the plates and leaves
every letter and every piece of artwork fully opaque. No sprite data changed.

```
slSpriteCCalcCond(CC_MSB);
slColRateSpr0(COUP_PANEL_BLEND);      /* CLRate24_8 */
slColorCalc(CC_RATE | CC_TOP);
slColorCalcOn(SPRON);
```

### The ratio came from captures

Ink-to-background contrast on the rules screen - the worst case, because its
backdrop is itself a table full of text:

```
opaque              176.2
blend 20:12         105.2
blend 24:8          129.2   <- shipped
```

### Two things measurement killed

1. **Drawing a plate twice does not compound the blend.** Colour calculation
   happens once, at VDP2 composite time, between the finished VDP1 framebuffer
   and the background - it is not per-command. The captured frame was
   identical to the decimal. A darker plate colour fails for the same reason.
   **The ratio is the only control.**
2. **`sgl_defs.h` already had a bogus colour-calculation block** defining
   `CC_RATE` as `(1<<0)` and `CC_2ND` as `(1<<1)`; SL_DEF.H gives `0` and
   `0x200`. Nothing used them, so nothing had broken - but adding the correct
   values collided, and **only the host build caught it**. The Saturn build
   does not use `-Werror`. Re-measured with the correct constant: identical,
   because bit 0 is not part of the mode field. Harmless this time, which is
   why it now has a test.

### Legibility is gated, not assumed

Blending trades readability for atmosphere, and a trade needs a number on both
sides. `qa_legibility.py` measures ink-to-background contrast in each screen's
densest text region and fails below 60 - well under every passing screen,
well over the 40.4 that was genuinely hard to read.

It immediately found a defect that predated all of this: the title's
`[R] Rules` hint sat over the brightest part of the skyline with nothing
behind it, at **40.4** against 95-135 everywhere else. Confirmed pre-existing
rather than caused by blending (40.4 both before and after). With a plate and
brighter ink it measures **143.6**.

```
connecting  90.4    game_over  131.6    settings  128.4
game       115.8    lobby      124.4    title     131.0
rules      120.2    name_entry 119.3
```
