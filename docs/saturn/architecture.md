# Saturn PAL Architecture

Hardware-level notes for the Saturn frontend. The PAL talks to **bare
SGL** — no Jo Engine runtime is linked. The application owns the main
loop (no callback inversion).

For the wiring sequence, see [quickstart.md](quickstart.md). For the
canonical entry point, see
[`examples/coup/saturn/main_saturn.c`](../../examples/coup/saturn/main_saturn.c).

## Memory layout

```
0x00200000 ┌─────────────────────┐
           │ Low Work RAM (1MB)  │
           │  Code, static data, │
           │  stack              │
0x002FFFFF └─────────────────────┘

0x06000000 ┌─────────────────────┐
           │ High Work RAM (1MB) │
           │  Dynamic data,      │
           │  buffers            │
0x060FFFFF └─────────────────────┘

0x05C00000 ┌─────────────────────┐
           │ VDP1 VRAM (512KB)   │
           │  Sprites,           │
           │  framebuffer        │
0x05C7FFFF └─────────────────────┘

0x05E00000 ┌─────────────────────┐
           │ VDP2 VRAM (512KB)   │
           │  Backgrounds,       │
           │  character data     │
0x05E7FFFF └─────────────────────┘
```

The cui core is static-allocation only — every component is a
caller-provided struct, no malloc. That maps cleanly onto Saturn's
fixed memory map.

## Rendering pipeline

### VDP1 (sprites + UI quads)

Coup buffers VDP1 commands in RAM during `coup_render()`, then
flushes them to VDP1 VRAM after `slSynch()` returns. This works around
SGL's behavior of overwriting VDP1 VRAM offset `0x40` with its own END
command on every `slSynch()` — direct writes during render would be
clobbered.

```
coup_render() ──> RAM-buffered VDP1 cmds
slSynch()     ──> VBLANK; SGL writes its END cmd at 0x40
flush_cmds()  ──> our cmds copied to VDP1 VRAM, takes effect next frame
```

Constraints:
- ~1200–1300 quads per frame before dropping to 30 Hz
- All primitives are 4-vertex quads (no native filled rect)
- No Z-buffer — manual draw order
- No overdraw culling

### VDP2 (backgrounds)

`coup_init` configures NBG0 as a solid-color background plane via
`slBack1ColSet()`. Solid backgrounds on VDP2 cost zero VDP1 quads,
which is a meaningful saving when budget is tight.

## Color: RGB888 → RGB555

Source colors come from cui as 32-bit RGBA; Saturn wants 15-bit RGB555:

```
cui (32-bit RGBA):
  0xRRGGBBAA   8 bits per channel

Saturn RGB555:
  0bXRRRRRGGGGGBBBBB   5 bits per channel, alpha discarded
```

Conversion is right-shift-3 per channel via `pal/saturn/saturn_color_mapper.c`:

```c
uint8_t r = (rgba >> 24) & 0xFF;
uint8_t g = (rgba >> 16) & 0xFF;
uint8_t b = (rgba >>  8) & 0xFF;
return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
```

Loss: 256→32 levels per channel; 16.7M→32K total colors; alpha
discarded entirely. Expect mild banding on gradients.

## Input

Reading the pad goes through SGL/SMPC, not Jo Engine. The PAL exposes
edge-detected input via `cui_saturn_poll_input()`:

```c
cui_input_action_t action = cui_saturn_poll_input();
```

For raw pad data (e.g. multi-button combos like the A+B+C+START soft
reset), read `Smpc_Peripheral[0].data` directly and check
`Per_Connect1` for controller presence. Bits are active-low — see
`main_saturn.c`'s reset handler for the pattern.

| `cui_input_action_t` | Saturn button |
|---|---|
| `CUI_INPUT_UP / DOWN / LEFT / RIGHT` | D-Pad |
| `CUI_INPUT_CONFIRM` | A |
| `CUI_INPUT_CANCEL` | B |
| `CUI_INPUT_PAGE_UP / PAGE_DOWN` | L / R shoulder |
| `CUI_INPUT_QUIT` | Start (no-op in coup) |

## Frame budget @ 60 Hz

```
16.67 ms per frame
├─ VDP1 rasterization   ~8 ms
├─ VDP2 background      ~2 ms
├─ App / SH-2           ~5 ms
└─ Headroom            ~1.7 ms
```

Bottlenecks, in practice:
1. VDP1 quad count (>1300 = 30 Hz)
2. SH-2 master @ 28 MHz (slow by modern standards)
3. Memory bandwidth (shared bus contention, especially during NetLink TX)

## Dual SH-2

```
Master SH-2              Slave SH-2
    │                        │
    └──────┬─────────────────┘
           V
   Shared Work RAM (no hardware protection)
```

Coup currently runs entirely on the Master SH-2. The Slave is idle.
TODO.md lists planned Slave-SH-2 work for the NetLink ring-buffer
transport, but nothing in the shipped game uses it yet.

If/when Slave code is added: use cache-through memory regions
(`0x20XXXXXX` mirror of work RAM) for any data shared between SH-2s,
since neither cache is coherent with the other.

## Audio

`coup_audio_init()` loads the M68K sound driver into Sound RAM and
starts the 68K. Important: do **not** send `SNDOFF` (SMPC 0x07) after
this — the 68K must keep running to manage SCSP slot allocation, CD-DA
routing, and PCM mixing. The startup sequence and rationale is
documented inline in `main_saturn.c`.

## NetLink

NetLink modem I/O goes through the 16550 UART exposed via cart-port
addresses. `pal/saturn/saturn_uart16550.h` is the driver; `modem.h`
handles AT-command dial. Two cart-port base addresses are probed at
boot (`0x25895001` and `0x04895001`) to handle different NetLink cart
revisions.

TX is currently fully blocking — `saturn_uart_putc()` busy-waits per
byte, so a 68-byte chat message stalls ~24 ms (1–2 dropped frames).
Non-blocking transport (Slave SH-2 ring buffer or SCU External
Interrupt 12 ISR) is in TODO.md.
