# Coup — Saturn + browser crossplay card game

2–6 player Coup. Sega Saturn clients connect via NetLink modem +
DreamPi to a Python authoritative server; browser clients connect via
WebSocket. Bots fill empty seats.

## Repo layout

```
coup-saturn/
├── core/                       # cui core (static-allocation UI primitives)
├── examples/coup/              # Shared game logic
│   ├── coup_rules.c            #   authoritative rule engine
│   ├── coup_bot.c              #   bot AI
│   ├── coup_game.c             #   game state machine
│   ├── coup_render.c           #   rendering (platform-agnostic)
│   ├── coup_audio.c            #   audio
│   └── saturn/                 #   Saturn entry point + Makefile
│       ├── main_saturn.c
│       └── Makefile
├── pal/saturn/                 # Saturn PAL (bare SGL — no Jo Engine runtime)
├── tools/coup_server/          # Python server: TCP :4821, WebSocket :4823
├── web/                        # Browser client (HTML/JS, talks to WS port)
├── scripts/                    # Build scripts (Docker Saturn build)
├── docs/saturn/                # Saturn-specific docs
└── Makefile                    # Top-level: coup-lib, coup-server, coup-saturn
```

## Build

```bash
make coup-all         # build everything (host coup-lib + python server + Saturn disc)
make coup-saturn      # Saturn disc image only (Docker, hermetic)
make coup-lib         # host shared lib (libcoup_rules.{so,dylib}) for server use
make coup-server      # python server package
make serve            # run server + web client locally for browser play
make test-coup        # host-toolchain unit tests
```

The Saturn build runs in a hermetic Docker image
(`scripts/saturn-build.Dockerfile`); see [docs/saturn/](docs/saturn/)
for what's actually pulled from joengine and why. The host portions
(coup-lib, coup-server) need a host gcc and Python 3 with
`websockets`.

## Architecture, briefly

- **Authoritative server** (`tools/coup_server/server.py`): owns game
  state. Loads `libcoup_rules.so` (built from `examples/coup/coup_rules.c`)
  so server and client run *identical* rule logic. Bots also live
  here and use the same C bot via FFI.
- **Saturn client** (`pal/saturn/` + `examples/coup/saturn/`): SGL
  rendering, NetLink modem TCP transport via DreamPi, app-owned main
  loop. Game logic shared with server via `examples/coup/`.
- **Web client** (`web/`): WebSocket client to server. Currently
  hardcodes `ws://${host}:4823`.

The Saturn PAL talks to **bare SGL only** — Jo Engine the engine is
not linked. The joengine repo is just the bundle that ships SGL +
SH-2 toolchain + IP.BIN.

## Layer note (from workspace coordinator)

This repo sits at L3 (application) in the retro/ workspace's
L1/L2/L3 layering. cui in `core/` is L2-ish but has been
coup-specialized over time. Do not propose adding Saturn-specific
code into cross-platform cui core — that's a layer violation.

## Worker scope

This is the `coup-saturn` repo, separate from `retro/saturn/games/coup/`
(which is likeagfeld's `cui_sandbox` fork). Distinct from the workspace
`coup-worker`'s scope; needs its own worker definition or coordinator
direct-edits if/when active work resumes.
