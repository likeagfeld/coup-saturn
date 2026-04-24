# Saturn Documentation

Saturn-specific docs for Coup. The Saturn frontend lives in
[`pal/saturn/`](../../pal/saturn/) and [`examples/coup/saturn/`](../../examples/coup/saturn/);
shared game logic is in `examples/coup/`.

## Documents

| Document | Purpose |
|----------|---------|
| [quickstart.md](quickstart.md) | How the Saturn entry point is wired together |
| [architecture.md](architecture.md) | Hardware notes: memory map, VDP1 budget, color, dual SH-2 |
| [backup-ram.md](backup-ram.md) | BUP storage layout and API |
| [visual_safe_area.md](visual_safe_area.md) | Safe-area test spec |

## Build

```bash
make coup-saturn          # build Saturn disc image (game.cue / track01.bin)
```

Requires Docker. The build runs in a hermetic image
(`scripts/saturn-build.Dockerfile`); no host Saturn SDK install is
needed. `JOENGINE_LOCAL=/path/to/joengine` overrides the baked SGL +
SH-2 toolchain with a local checkout when iterating on the engine
itself.

## What the PAL provides

The Saturn PAL talks to **bare SGL** — no Jo Engine runtime is linked.
The joengine repo is used purely as a delivery vehicle for SGL, IP.BIN,
the SH-2 toolchain, and the SGL linker script. See
[architecture.md](architecture.md) for details.

Public surface (`pal/saturn/saturn_pal.h`):

```c
void cui_saturn_init(void);                              /* call after slInitSystem/slInitSynch */
void cui_saturn_set_resolution(cui_saturn_resolution_t); /* before init; default 320x224 */
cui_input_action_t cui_saturn_poll_input(void);          /* edge-detected pad action */
void cui_saturn_vdp1_flush_cmds(void);                   /* flush buffered VDP1 cmds after slSynch() */
void cui_saturn_vdp1_activate(void);
```

The application owns the main loop — see
[`examples/coup/saturn/main_saturn.c`](../../examples/coup/saturn/main_saturn.c).

## Key constraints

- Resolution: 320×224 default (352×224 / 640×224 also supported)
- Color: RGB555 (15-bit, no alpha)
- Static allocation only (no malloc)
- VDP1 budget: ~1200–1300 quads/frame before dropping to 30Hz
