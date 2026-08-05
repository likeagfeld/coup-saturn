# Adversarial hardware-validation plan

Goal: data-driven confidence that the facelift renders and functions correctly
on a **real Saturn**, first attempt. Everything to date is verified on Beetle
Saturn (Mednafen's core). Emulator agreement is necessary, not sufficient.

Written before any code change, per the sega-saturn-developer binding
methodology steps 1-5.

## Docs to be read (step 2/3 — every applicable row, not the first match)

| Subsystem | Docs and samples |
|---|---|
| VDP2 colour calculation | `ST-058-R2-060194.pdf` + `sega_saturn_docs/VDP2_Manual.txt` + `Saturn Documentation HTML Files/ST-058-R2-060194.html` + `vdp2-reference.md` |
| VDP1 colour codes / budget | `ST-013-R3-061694.pdf` + `VDP1_Manual.txt` + HTML + `SEGASMP/SPR/SMPSPR2/SMPSPR20.C` + `vdp1-reference.md` |
| SGL colour-calc + CD API | `ST-237-R1` (tutorial) + `ST-238-R1` (reference) + `SGL302/INC/SL_DEF.H` + `SGL_CD.H` + `SGL302/SAMPLE/*` |
| CD file system | `ST-39-R2` + `ST-38-R1` + `ST-098` + `NOV96_DTS/EXAMPLES/GFSDEMO/` + `SEGASMP/GFS/` + `cd-backup-reference.md` |
| SCU DMA (CD transfer path) | `ST-097-R5` + `ST-210` (precautions) + `SEGASMP/DMA/` |
| Memory / BSS | `mednafen-debug-qa-reference.md` + linker map |

## The claims under test

Each is a specific, falsifiable statement about the shipped build. A claim is
only "confirmed" with a doc citation or a measurement, never by reasoning that
it ought to hold.

| # | Claim | Risk if false |
|---|---|---|
| C1 | `slSpriteCCalcCond(CC_MSB)` + `slColorCalcOn(SPRON)` + `slColRateSpr0()` + `slColorCalc(CC_RATE\|CC_TOP)` is the COMPLETE and CORRECT arming sequence | panels opaque again, or ALL sprites blend incl. text |
| C2 | For an RGB-code VDP1 colour word, bit 15 is what the MSB colour-calc condition tests | text blends and becomes unreadable, or nothing blends |
| C3 | The sprite TYPE in use exposes a usable colour-calc path for RGB-code data | blending silently does nothing on hardware |
| C4 | Colour calculation does not conflict with the colour-offset fade (`slColOffsetAUse`) | fades corrupt colours mid-transition |
| C5 | `slCdInit / slCdOpen / slCdLoadFile / slCdGetStatus / slCdAbort` is the correct lifecycle, and `slCdAbort` is the right release | handle leak after ~N loads, then load failure mid-game |
| C6 | The CD work area satisfies any alignment the library requires (CDERR_ALIGN exists, so one is documented) | `slCdInit` fails, every backdrop black |
| C7 | `slCdEvent()` polled from a busy loop is sufficient; it does not require a vblank/interrupt context | load never completes on hardware, 8.5 s timeout then black |
| C8 | Worst-case VDP1 command count stays inside `SATURN_VDP1_MAX_CMDS` (2048) | dropped draws on the busiest frame |
| C9 | CRAM banks do not collide across sprite / gameover / anim / fx / background loaders | wrong palettes, as already happened once at 0x400 |
| C10 | VDP2 VRAM: the NBG1 bitmap in bank A0 and NBG0 text cells coexist in a legal cycle pattern | gotcha #7 - an NBG silently dropped |
| C11 | WRAM-H headroom is real, and the 72,192 B CD staging buffer does not collide with the stack | boot hang, the class G8 already lied about once |
| C12 | A 72,192 B read is inside the real drive's capability at 2x, with seek | scene load exceeds budget on hardware |

## Acceptance criteria (measurable, not "looks right")

- Every claim above resolved to CONFIRMED (with citation or measurement) or
  REFUTED (with the defect written up and a RED-firing gate added).
- Any defect found gets a gate that fires RED on the current build BEFORE the
  fix, per the QA-iterative rule. No fix is claimed on a compile or a capture
  that "looks better".
- No claim resolved by reasoning alone.

## Method

Sub-agents are dispatched per subsystem, each bound to this skill's
methodology (the dispatch preamble is mandatory and includes the literal
strings `complete-doc-index.md` and `RED-firing gate`). Their findings are
cross-checked against the local code and the linker map before acceptance.

---

## Measured while the doc review ran

### C5 — file handles do NOT leak (CONFIRMED, empirically)

`-DCOUP_QA_SCREEN=100` selects a CD stress mode that cycles every scene off the
disc 64 times at boot, then leaves the outcome in `g_saturn_cd_stats` to be
read live over `READ_CORE_RAM`.

```
loads completed   66          (64 stress + 2 boot)
last result       0 (ok)
typical load      21 vblanks  (0.35 s)
worst load        23 vblanks  (0.38 s)
```

This is the decisive test for the `slCdAbort()`-as-release question.
`SLCD_MAX_OPEN` is 24 (SGL_CD.H:109), so a leaked handle would have exhausted
the pool and failed `slCdOpen` by roughly the 24th load. 66 consecutive loads
succeeded. The handle is genuinely released.

It also bounds the timing variance that a single measurement could not: across
66 reads with a seek between each, the spread is 21-23 vblanks. The earlier
0.35 s figure was not a lucky sample.

Still an EMULATOR measurement. It settles the API-lifecycle question, which is
emulator-faithful (it is library state, not drive physics). It does NOT settle
real-drive seek timing - see C12.

### C12 — the 0.35 s figure is transfer-dominated (supporting analysis)

The whole disc payload is tiny, and that changes how much the emulator figure
can be trusted.

```
executable 0.bin        546,576 B =  266 sectors
8 scene files           577,536 B =  282 sectors
total user data       1,124,112 B =  548 sectors

one scene              36 sectors -> 0.23 s of pure transfer at 2x (150 sect/s)
measured on emulator                 0.35 s
```

So of the measured 0.35 s, roughly 0.23 s is transfer the drive cannot beat,
and ~0.12 s is seek plus library overhead.

Seek is bounded by the payload SPAN, not the disc. 548 sectors is 0.16% of a
650 MB disc's sector count, so every scene file sits within a very short stroke
of every other. Worst-case seek here is near the drive's minimum, not its
average - a full-stroke seek does not arise, because there is nowhere on this
disc to stroke to.

This matters because transfer rate is the part an emulator models most
faithfully (it is a fixed 2x constant), while seek is the part it approximates.
The measurement is dominated by the faithful component, and the remaining
budget headroom is 1.00 s against 0.35 s - roughly 3x - which absorbs a seek
several times worse than modelled.

Files are laid out alphabetically and contiguously by mkisofs, which is why the
span is this tight. Adding a large asset later would widen it; that is worth
remembering, not fixing now.
