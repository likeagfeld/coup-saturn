# Saturn Quick Start

How the Saturn frontend is initialized. The canonical entry point is
[`examples/coup/saturn/main_saturn.c`](../../examples/coup/saturn/main_saturn.c);
this doc summarizes what it does and why.

## Build & run

```bash
# Build the Saturn disc image (Docker-hermetic; no host SDK required)
make coup-saturn

# Run in Mednafen (Saturn module)
mednafen -force_module ss build/coup_game/game.cue
```

Outputs land in `build/coup_game/`: `game.cue`, `track01.bin`, plus
`rebellion.wav` if `ffmpeg` is on PATH.

## Initialization sequence

```c
#include "saturn_pal.h"
#include "../../pal/saturn/sgl_defs.h"   /* SGL declarations */

void main(void)
{
    /* 1. SGL up first — required before any sl* / VDP calls */
    slInitSystem(TV_320x224, (TEXTURE*)0, 1);
    slInitSynch();

    /* 2. Configure VDP2 background, enable display */
    slTVOff();
    slBack1ColSet((void*)(VDP2_VRAM_A1 + 0x1fffe), 0x0000);
    slScrAutoDisp(NBG0ON);
    slTVOn();

    /* 3. Bring up the cui Saturn PAL (registers platform, layout, color mapper) */
    cui_saturn_init();

    /* 4. Application init: load assets, init game state, etc. */
    coup_init();

    /* 5. App-owned main loop */
    while (1) {
        cui_input_action_t action = cui_saturn_poll_input();
        coup_update(action);
        coup_render();          /* buffers VDP1 commands in RAM */
        slSynch();              /* SGL waits for VBLANK */
        cui_saturn_vdp1_flush_cmds();  /* push buffered cmds to VDP1 VRAM */
    }
}
```

The non-obvious step is **#5's flush ordering**: `slSynch()` overwrites
VDP1 VRAM offset `0x40` with its own END command, so the PAL buffers
draw commands in RAM during `coup_render()` and flushes them to VRAM
after `slSynch()` returns. See the comment block at the top of
`main_saturn.c` for the long version.

## Input

`cui_saturn_poll_input()` returns one edge-detected `cui_input_action_t`
per frame:

| Action | Saturn button |
|---|---|
| `CUI_INPUT_UP / DOWN / LEFT / RIGHT` | D-Pad |
| `CUI_INPUT_CONFIRM` | A |
| `CUI_INPUT_CANCEL` | B |
| `CUI_INPUT_PAGE_UP / PAGE_DOWN` | L / R |
| `CUI_INPUT_QUIT` | Start |

For raw pad reads (e.g. soft-reset combos), the SMPC peripheral block
is at `Smpc_Peripheral[0].data`, with connection state in
`Per_Connect1` — see the `A+B+C+START` reset handling in
`main_saturn.c`.

## Performance budget @ 60 Hz

| Cost | Approx |
|---|---|
| VDP1 rasterization | ~8 ms |
| VDP2 background | ~2 ms |
| App / SH-2 | ~5 ms |
| Headroom | ~1.7 ms |

Hard ceiling: ~1200–1300 VDP1 quads per frame. Past that, frame rate
drops to 30 Hz. Use VDP2 for solid-color backgrounds (free vs a quad).
