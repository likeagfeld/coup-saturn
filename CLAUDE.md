# cui - C UI Component Library

A platform-agnostic C component library for building UIs on retro/embedded platforms.

## Quick Reference

- **Build**: `./build.sh lib` - compile library
- **Test**: `./build.sh tests` - run unit tests
- **Storybook**: `./build.sh storybook && ./run.sh storybook` - launch component gallery

## Agent System

This project uses specialized agents for development. See `.claude/agents/` for individual agent definitions and `.claude/agents.yaml` for machine-readable configuration.

### Component Workflow

```
1. component-lead → proposal
2. User approval
3. design-agent → spec
4. test-agent → tests FIRST (TDD)
5. spec-agent → interface
6. core-agent → implementation
7. pal-*-agent → platform specifics
8. storybook-agent → gallery entry
```

## Architecture

### Directory Structure

```
cui/
├── build.sh / run.sh      # Unified build and run scripts
├── core/include/           # Public headers
├── core/src/               # Implementation
├── pal/sdl/                # SDL platform
├── pal/n64/                # N64 platform
├── pal/saturn/             # Saturn platform
├── storybook/              # Component gallery app
│   ├── sdl/                # SDL entry point
│   ├── saturn/             # Saturn entry point + Makefile
│   └── n64/                # N64 entry point + Makefile
├── tests/                  # Test suite
│   ├── core/               # Platform-agnostic unit tests
│   ├── sdl/                # SDL integration tests
│   ├── saturn/             # Saturn-specific tests + ROM
│   └── framework/          # Shared test infrastructure
├── examples/saturn/        # Saturn example projects
├── docs/                   # Documentation
│   ├── saturn/             # Saturn platform docs
│   ├── n64/                # N64 platform docs
│   └── testing/            # Test harness docs
├── components/             # Component designs and proposals
└── scripts/                # Internal build scripts
```

### Memory Model

- **Static-first**: All components work with stack/static allocation
- **No hidden malloc**: Caller provides all memory
- **Fixed-size buffers**: Compile-time limits via defines

### Component Pattern

```c
// 1. Statically allocatable struct
typedef struct cui_button {
    char label[CUI_BUTTON_MAX_LABEL];
    cui_rect_t bounds;
    cui_state_t state;
} cui_button_t;

// 2. init() - initialize with caller memory
void cui_button_init(cui_button_t* btn, const char* label, int x, int y);

// 3. render() - draw using theme
void cui_button_render(const cui_button_t* btn, const cui_theme_t* theme);

// 4. handle() - process input, return events
cui_handle_result_t cui_button_handle(cui_button_t* btn, cui_input_action_t action, cui_event_t* out);
```

## Configuration

```c
#define CUI_MAX_LIST_ITEMS     64
#define CUI_MAX_LABEL_LEN      32
#define CUI_BUTTON_MAX_LABEL   32
```

## Platform Support

- SDL (primary development)
- Saturn (SGL-based, see `docs/saturn/`)
- N64 (libdragon-based, see `docs/n64/`)
- Wii U (planned)
