# Facelift Hardware Reference (condensed doc-sweep findings)

Condensed from two full citation sweeps (2026-08-04) of ST-013-R3 (VDP1),
ST-058-R2 (VDP2), SGL SCROLL.TXT, SL_DEF.H, SGLFAQ_F.TXT, and NOV96 DTS
samples. Backs `docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md`.
Line refs are into `D:\Claude Saturn Skill Documentation\sega_saturn_docs\{VDP1,VDP2}_Manual.txt`.

## VDP1 raw-command encodings (this project's buffered path)

CMDCTRL = `END(15)|JP(14-12)|ZP(11-8)|Dir(5-4)|Comm(3-0)`;
CMDPMOD = `MON(15)|HSS(12)|Pclp(11)|Clip(10)|Cmod(9)|Mesh(8)|ECD(7)|SPD(6)|ColorMode(5-3)|ColorCalc(2-0)`.
All VRAM pointers (CMDLINK/CMDSRCA/CMDGRDA/LUT) = byte_address/8.
NOTE: `vdp1-reference.md` has CMDPMOD bits 11-8 and color-mode 000/001 wrong; the
manual (txt:3313-3351, 3765-3783) is authoritative.

| Effect | Encoding | Cost | Cite |
|---|---|---|---|
| Gouraud | ccalc bits2-0=`100` (+gouraud `110`=+half-lum, `111`=+half-trans); CMDGRDA=table/8. Table = 4×RGB555 (A,B,C,D corners), 8 B, 8-byte aligned, range 0x20–0x7FFFF. Channel value −0x10..+0x0F encoded as 0x00..0x1F (subtract 0x10), ADDED to source, clamped | write-only, no FB read — free vs flat | txt:2699-2793, 4070-4074; SMPSPR20.C:177-231 |
| Half-transparency | ccalc=`011`; blends 50/50 ONLY where FB pixel MSB=1 (RGB code); source must be RGB (mode 5 or RGB-entry LUT) | **6×** fill on covered px | txt:4058-4068 |
| Half-luminance | ccalc=`010`; halves part's RGB; no FB read | cheap | txt:4053-4056 |
| Shadow | ccalc=`001`; draws nothing, halves luminance of FB RGB-code px under footprint; stack 2× → 1/4 | **6×** | txt:4026-4051; SMPSPR20.C:164-175 |
| Mesh | CMDPMOD bit 8; checkerboard (X+Y even); works with ALL color modes incl. palette text | free | txt:3538-3554 |
| Distorted sprite (flip) | Comm=`0010`, 4 free vertices A(UL)B(UR)C(LR)D(LL); coincident verts → triangle/line ok | ~area | txt:5163-5261; S_4_3_1/2 |
| Scaled sprite | Comm=`0001`; ZP=0 two-corner, or ZP∈{5,6,7,9,A,B,D,E,F} anchor + CMDXB/YB=display w/h; w=0 draws 1 px; negative w prohibited | ~area | txt:3068-3218, 5018-5159, 6847-6848 |
| Polyline/Line | Comm=`0101`/`0110`; non-textured CMDCOLR; gouraud ok (gradient borders); ECD=1,SPD=1 required, colormode 000; shadow/half-trans double-hit at corners | ~perimeter | txt:5341-5506 |
| End codes | ECD=0 needs SPD=0 (never ECD=0+SPD=1); HSS=1 ignores end codes | — | txt:3676-3678, 3384-3385 |
| Font sprites TODAY | color-bank-16 (`SATURN_VDP1_SPR_PMOD`) → FB MSB=0 → half-trans/shadow REPLACE glyphs, never blend. LUT mode w/ RGB entries (32 B, 32-byte aligned) is the upgrade path | — | txt:2646-2649, 3956-3959 |

Constraints: draw rate 1 px/28.6 MHz clock → ~477k px-clocks/frame @60fps
(txt:1114-1115). BEF=0 after swap = transfer-over (frame dropped) (txt:2359-2380).
Draw order = blend order (txt:2894-2898). END must stay reachable or EDSR CEF
never sets → flush spin-wait deadlock (txt:2335-2350). Gouraud table home: fixed
pool inside texture area; NOT 0x80–0x10080 (per-frame rewritten), NOT
0x7FF00–0x7FFFF (SGL GouraudRAM reserve, SL_DEF.H:225). Animated tables:
double-buffer by frame parity; write in same CEF=1 window as command flush.
CMDGRDA patched at buffer time like CMDSRCA.

## VDP2 SGL API (facelift set)

| Feature | Calls | Cite |
|---|---|---|
| NBG1 bitmap bg | `slBitMapNbg1(COL_TYPE_256, BM_512x256, (void*)VDP2_VRAM_A0)` (128 KB exactly, 0x20000-aligned); `slBMPaletteNbg1(pal)`; `slPriority(scnNBG1, n)`; `slScrAutoDisp(NBG0ON|NBG1ON)` — returns Bool, NG = no legal cycle pattern | SCROLL.TXT:859-906, 100-116 |
| Explicit cycles | `slScrCycleSet(a0,a1,b0,b1)` if auto NG or plane drops; SGL cycle table debug dump at 0x060FFC00+0xD0..0xDF | SGLFAQ §2-8 |
| Color offset fade | `slColOffsetA(r,g,b)` signed −256..+255; `slColOffsetAUse(NBG0ON|NBG1ON|SPRON)`; B-set likewise | ST-058-R2 §13.1 txt:10215-10381 |
| Color calc | `slColorCalc(CC_RATE|CC_TOP|NBG0ON|...)`; `slColorCalcOn(flag)`; `slColRate(scnX, CLRate16_16)`; sprites: `slColRateSpr0(rate)` + `slSpriteCCalcCond(CC_MSB)` (SPCCCS=3 → RGB sprites always blend) | txt:9622-9804, 8681-8682 |
| Line color gradient | `slLineColTable(adr)` (2 B/line, 448 B) or `slLine1ColSet(adr,col)`; `slLineColDisp(NBGxON...)`; ratio via `slColRate(scnLNCL,...)` — doc-proven, NO local sample: gate it | txt:7436-7545, 9523-9589 |
| Per-line back gradient | `slBackColTable(tbl)` (224×RGB555=448 B) | SCROLL.TXT:148-157 |
| Line scroll shimmer | `slLineScrollMode(scnNBG1, lineSZ1|lineHScroll)`; `slLineScrollTable1(addr)` re-pointed per frame; sine table `16*slSin()` — sample-proven | SEGA2D_1\MAIN.C:40-63 |
| Mosaic | `slScrMosaicOn(scrn)`; `slScrMosSize(h,v)` 1-16 | SCROLL.TXT:605-619 |
| CRAM mode | `slColRAMMode(CRM16_1024/CRM16_2048/CRM32_1024)` before CRAM writes; mode 0 required for gradation/blur | txt:2325-2436 |
| Priorities | `slPriority(scnNBG0..scnRBG0, 0-7)`; `slPrioritySpr0..7`; RGB sprites always use register 0 (no PR bits) | txt:8575-8576 |

Cycle legality @320×224: PN reads in ≤2 banks (one of A0/B0 + one of A1/B1).
Access counts ×1 zoom: PN=1; char 16col=1, 256col=2, 2048/32K=4 (Table 3.3 —
**.txt extraction garbled; HTML p.52 authoritative**). NBG0@2048/32K kills NBG2;
NBG0@16.7M kills NBG1-3; NBG1@2048/32K kills NBG3. 4×256-color NBGs legal
(proven cycle `slScrCycleSet(0x55FEEEEE,0xFFFEEEEE,0x123FEEEE,0x0467EEEE)`).
RBG0 claims whole banks via RAMCTL RDBSxx (their CYCx ignored) — why RBG0 is
deferred (Approach C).

## Field gotchas that bind this work

1. SGL auto-arbiter non-deterministically DROPS a standalone NBG with char/bitmap
   base in B1 → new plane data goes in A0/B0; gate with pixel-mass check (G2).
2. Back screen reaches only where ALL layers transparent — never a bg fill.
3. SGL VDP2 registers = per-vblank DMA rewrite transient → never diagnose from
   savestate register reads; use rendered-frame measures (VRAM/CRAM reads stay valid).
4. Visual bug ⇒ gate measures the rendered frame (screenshot pixel-mass/SSIM),
   never a code-proxy. Every gate must fire RED on a known-bad fixture first.
5. Emulator captures contaminated by concurrent host load — capture with host quiet.
6. MPABN0=0x40, MPCDN0=0x42, MPABN1=0x44... (0x42 is NBG0's C/D, not NBG1).
7. SH2 DMA ch1 is reserved by SGL for the vblank scroll transfer — never use it.
