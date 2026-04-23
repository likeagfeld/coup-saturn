# Saturn PAL Quick Start

Minimal guide to get cui running on Sega Saturn.

## 30-Second Setup

```c
#include <jo/jo.h>
#include "saturn_pal.h"

void my_frame(void) {
    cui_input_action_t action = CUI_INPUT()->poll();
    /* handle input, render UI */
}

void jo_main(void) {
    jo_core_init(JO_COLOR_Black);
    cui_pal_register(cui_saturn_platform());
    cui_pal_init();

    /* init your cui components here */

    cui_saturn_set_frame_callback(my_frame);
    cui_saturn_run();  /* never returns */
}
```

## Key Differences from PC

| Feature | PC (SDL) | Saturn |
|---------|----------|--------|
| Main loop | `while(running)` | Callback @ 60Hz |
| Exit | `return 0;` | Can't exit |
| Colors | 32-bit RGBA | 15-bit RGB |
| Text color | Any color | White only |
| Display | Variable | Fixed 40x28 |

## Common Mistakes

### 1. Forgot to call jo_core_init

```c
/* WRONG */
void jo_main(void) {
    cui_pal_init();  /* Will fail! */
    // ...
}

/* CORRECT */
void jo_main(void) {
    jo_core_init(JO_COLOR_Black);  /* First! */
    cui_pal_init();
}
```

### 2. Using poll loop instead of callback

```c
/* WRONG - won't work on Saturn */
void jo_main(void) {
    jo_core_init(JO_COLOR_Black);
    while (1) {  /* Saturn uses callbacks, not loops */
        cui_input_action_t action = poll();
        render();
    }
}

/* CORRECT - use callback */
void my_frame(void) {
    cui_input_action_t action = CUI_INPUT()->poll();
    render();
}

void jo_main(void) {
    jo_core_init(JO_COLOR_Black);
    cui_saturn_set_frame_callback(my_frame);
    cui_saturn_run();
}
```

### 3. Expecting color in text

```c
/* This draws WHITE text, color is ignored */
CUI_DISPLAY()->draw_text(0, 0, "Hello", CUI_RGB(255, 0, 0));
```

### 4. Too many rectangles

```c
/* SLOW - draws 1000 quads per frame */
for (int i = 0; i < 1000; i++) {
    draw_rect(i * 2, 0, 1, 1, color);
}
```

## Input Cheat Sheet

```c
cui_input_action_t action = CUI_INPUT()->poll();

switch (action) {
    case CUI_INPUT_UP:        /* D-Pad Up */
    case CUI_INPUT_DOWN:      /* D-Pad Down */
    case CUI_INPUT_LEFT:      /* D-Pad Left */
    case CUI_INPUT_RIGHT:     /* D-Pad Right */
    case CUI_INPUT_CONFIRM:   /* A button */
    case CUI_INPUT_CANCEL:    /* B button */
    case CUI_INPUT_PAGE_UP:   /* L shoulder */
    case CUI_INPUT_PAGE_DOWN: /* R shoulder */
    case CUI_INPUT_QUIT:      /* Start (won't exit) */
}
```

## Build & Run

```bash
# Install Jo Engine first
# Download from https://jo-engine.org/

# Build
cd pal/saturn
make

# Run in emulator
mednafen -force_module ss cui_saturn.cue
```

## Minimal Project Structure

```
my_project/
├── src/
│   └── main.c           # Your jo_main()
├── pal/
│   └── saturn/
│       ├── saturn_pal.c
│       └── saturn_pal.h
├── core/
│   └── include/
│       ├── cui_pal.h
│       └── cui_types.h
└── Makefile
```

## Display Dimensions

- **Pixels**: 320 x 224
- **Characters**: 40 x 28 (8x8 font)
- **Refresh**: 60 Hz

## Color Palette

cui provides default colors (Catppuccin theme):

```c
CUI_COLOR_BG          /* Dark background */
CUI_COLOR_TEXT        /* Light text */
CUI_COLOR_HIGHLIGHT   /* Blue */
CUI_COLOR_ACCENT      /* Pink */
CUI_COLOR_SUCCESS     /* Green */
CUI_COLOR_WARNING     /* Yellow */
CUI_COLOR_ERROR       /* Red */
```

**Note**: Converted to 15-bit on Saturn (slight color loss).

## Performance Tips

1. **Limit draw calls per frame** (<100 rectangles)
2. **Update text only when changed** (not every frame)
3. **Use static allocation** (no malloc)
4. **Keep component count reasonable** (<50 UI elements)

## Debugging

### Black Screen

1. Check `jo_core_init()` called first
2. Verify frame callback is set
3. Ensure emulator uses SS module: `mednafen -force_module ss`

### Input Not Working

1. Test with example_main.c
2. Check controller in emulator settings
3. Verify using `jo_is_pad1_key_down()` not `pressed()`

### Slow Rendering

1. Count rectangles per frame (use `jo_printf` to show count)
2. Reduce draw calls
3. Pre-create sprites for repeated elements

## Example Output

Running example_main.c should show:

```
cui Saturn PAL Demo
====================

Frame: 1234
Display: 40x28 chars
Last Input: A

Rectangle Test:
[RED] [GREEN] [BLUE]

Controls:
  D-Pad: Navigation
  A: Confirm
  B: Cancel
  L/R: Page Up/Down
  Start: Quit
```

## Need Help?

- **README.md** - Full documentation
- **ARCHITECTURE.md** - Technical deep dive
- **example_main.c** - Working example
- **Jo Engine Docs** - https://jo-engine.org/doxygen/

## Resources

- [Jo Engine](https://jo-engine.org/)
- [Saturn Emulator (Mednafen)](https://mednafen.github.io/)
- [Saturn Hardware](https://www.copetti.org/writings/consoles/sega-saturn/)
