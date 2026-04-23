# Saturn PAL Architecture

Technical documentation for the Saturn platform abstraction layer.

## Overview

The Saturn PAL bridges cui's poll-based architecture with Jo Engine's callback-based main loop through inversion of control.

## Control Flow Inversion

### Challenge

```
cui library:                    Jo Engine:
  poll-based loop                 callback-based
  ┌─────────────┐                ┌──────────────┐
  │ while(1) {  │                │ jo_main() {  │
  │   poll()    │                │   init()     │
  │   update()  │                │   run()      │
  │   render()  │                │ } // never   │
  │ }           │                │   returns    │
  └─────────────┘                └──────────────┘
```

### Solution

Saturn PAL inverts control by making Jo Engine drive cui:

```
┌─────────────────────────────────────────────────────┐
│ jo_main()                                           │
│  ├─ jo_core_init()                                  │
│  ├─ cui_pal_register(cui_saturn_platform())         │
│  ├─ cui_pal_init()                                  │
│  ├─ cui_saturn_set_frame_callback(app_callback)     │
│  └─ cui_saturn_run() ────┐                          │
│                          │                          │
│         ┌────────────────┘                          │
│         │                                           │
│         V                                           │
│  ┌─────────────────────────────────────┐            │
│  │ jo_core_run() @ 60Hz                │            │
│  │  └─> app_callback()                 │            │
│  │       ├─ action = poll()            │            │
│  │       ├─ handle(action)             │            │
│  │       └─ render()                   │            │
│  └─────────────────────────────────────┘            │
│           (never returns)                           │
└─────────────────────────────────────────────────────┘
```

## Memory Layout

### Saturn RAM Organization

```
0x00200000 ┌─────────────────────┐
           │ Low Work RAM (1MB)  │
           │  - Program code     │
           │  - Static data      │
           │  - Stack            │
0x002FFFFF └─────────────────────┘

0x06000000 ┌─────────────────────┐
           │ High Work RAM (1MB) │
           │  - Dynamic data     │
           │  - Buffers          │
0x060FFFFF └─────────────────────┘

0x05C00000 ┌─────────────────────┐
           │ VDP1 VRAM (512KB)   │
           │  - Sprites          │
           │  - Framebuffer      │
0x05C7FFFF └─────────────────────┘

0x05E00000 ┌─────────────────────┐
           │ VDP2 VRAM (512KB)   │
           │  - Backgrounds      │
           │  - Character data   │
0x05E7FFFF └─────────────────────┘
```

### cui Static Allocation

cui uses static allocation, which is ideal for Saturn's limited RAM:

```c
/* No malloc - everything is stack or static */
typedef struct cui_button {
    char label[CUI_BUTTON_MAX_LABEL];  /* 32 bytes */
    cui_rect_t bounds;                /* 16 bytes */
    cui_state_t state;                  /* 4 bytes  */
} cui_button_t;                         /* Total: ~52 bytes */

/* Application declares on stack or static */
static cui_button_t button;  /* Known size, no heap */
```

## Display Implementation

### Text Rendering

```c
void saturn_display_draw_text(int x, int y,
                              const char* text,
                              uint32_t color)
{
    /* LIMITATION: jo_printf ignores color, always white */
    jo_printf(col, row, "%s", text);
}
```

**Why color is ignored:**
- `jo_printf()` uses Jo Engine's built-in 8x8 font
- Font is 1-bit (on/off), rendered as white on transparent
- Colored text requires custom font system or sprite-based text

**Alternatives:**
1. Use `jo_font_print()` with TGA-based fonts (supports color)
2. Create sprite font system (more VRAM usage)
3. Accept white-only text for menus (simplest)

### Rectangle Rendering

```c
void saturn_display_draw_rect(int x, int y, int w, int h,
                              uint32_t color)
{
    /* Convert to character grid */
    int start_col = x / 8;
    int start_row = y / 8;
    int char_width = (w + 7) / 8;
    int char_height = (h + 7) / 8;

    /* Draw using block characters */
    for (int dy = 0; dy < char_height; dy++) {
        for (int dx = 0; dx < char_width; dx++) {
            jo_printf(start_col + dx, start_row + dy, "#");
        }
    }
}
```

**Why character-based:**
- Saturn VDP1 doesn't have a "draw filled rect" primitive
- VDP1 works with textured quads
- Character-based is simplest for UI, acceptable performance

**Better alternatives:**
1. Pre-create 1x1 colored sprite, stretch to size
2. Use VDP2 colored plane for backgrounds
3. Software renderer for per-pixel drawing

## Input Implementation

### Button Mapping

```c
static cui_input_action_t saturn_input_poll(void) {
    /* Edge detection - button just pressed */
    if (jo_is_pad1_key_down(JO_KEY_UP))   return CUI_INPUT_UP;
    if (jo_is_pad1_key_down(JO_KEY_A))    return CUI_INPUT_CONFIRM;
    // ...

    /* State check - button currently held */
    if (jo_is_pad1_key_pressed(JO_KEY_UP)) { /* held */ }

    return CUI_INPUT_NONE;
}
```

**Key choice: `key_down` vs `key_pressed`**

- `jo_is_pad1_key_down()`: True only on **first frame** button pressed (edge)
- `jo_is_pad1_key_pressed()`: True **every frame** button is held (state)

We use `key_down()` for menu navigation to prevent unwanted repeat.

### Controller Layout

```
         L                            R
      ┌─────────────────────────────────┐
      │                                 │
      │      ┌───┐                      │
      │    ┌─┴───┴─┐                    │
      │    │  D-Pad│              X     │
      │    └───┬───┘          Y       A │
      │        │                B       │
      │                                 │
      │     START                       │
      └─────────────────────────────────┘
```

**Mapping rationale:**
- D-Pad: Natural for menu navigation
- A: Confirm (right-hand thumb position)
- B: Cancel (to the left of A)
- L/R: Page up/down (less common, shoulder buttons)
- Start: Quit/pause (standard pause button)

## Color System

### Color Conversion

cui uses 32-bit RGBA, Saturn uses 15-bit RGB555:

```
cui (32-bit RGBA):
  0xRRGGBBAA
  ├─────────── R: 8 bits (0-255)
  ├───────── G: 8 bits (0-255)
  ├───── B: 8 bits (0-255)
  └─ A: 8 bits (0-255)

Saturn RGB555:
  0bXRRRRRGGGGGBBBBB
  │└────┴────┴──── 15 bits color
  └─────────────── X: unused (always 0)
    R: 5 bits (0-31)
    G: 5 bits (0-31)
    B: 5 bits (0-31)
```

**Conversion algorithm:**

```c
static inline jo_color cui_to_saturn_color(uint32_t rgba) {
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >>  8) & 0xFF;
    /* Alpha ignored */

    /* Right-shift 3 bits: 8-bit -> 5-bit */
    return JO_COLOR_RGB(r >> 3, g >> 3, b >> 3);
}
```

**Color loss:**
- Precision: 256 levels -> 32 levels per channel
- Total colors: 16.7M -> 32K
- Alpha: Lost completely

**Example:**
```
cui: 0xFF8040FF (255, 128, 64, 255)
  R: 255 >> 3 = 31  (11111)
  G: 128 >> 3 = 16  (10000)
  B:  64 >> 3 =  8  (01000)
Saturn: 0x7F08 (RGB555)
```

## VDP1 Architecture

### Polygon Rendering

```
VDP1 Command Table (VRAM)
┌──────────────────────┐
│ CMD 0: Quad (sprite) │
│ CMD 1: Quad (sprite) │
│ CMD 2: Quad (UI box) │
│ ...                  │
│ CMD N: End list      │
└──────────────────────┘
         │
         V
   Rasterization
         │
         V
    Framebuffer
   (double-buffered)
```

**Constraints:**
- ~1200-1300 quads per frame (depends on size/complexity)
- No Z-buffer (manual draw order)
- No pixel overdraw culling
- All primitives are 4-vertex quads

**UI implications:**
- Each rectangle = 1 quad
- Each character = 1 quad (if sprite-based)
- 40x28 grid of chars = 1120 quads (max)
- Must leave budget for UI elements

## Performance Profile

### Frame Budget @ 60Hz

```
16.67ms per frame
├─ VDP1 rendering:     ~8ms   (sprite processing)
├─ VDP2 rendering:     ~2ms   (background layers)
├─ CPU processing:     ~5ms   (your code)
└─ Overhead:           ~1.67ms
```

**Bottlenecks:**
1. VDP1 quad count (>1300 quads drops to 30Hz)
2. SH-2 CPU @ 28MHz (slow by modern standards)
3. Memory bandwidth (shared bus contention)

### Optimization Strategies

**1. Minimize quads per frame**
```c
/* Bad: Every cell is a separate draw */
for (int i = 0; i < 1000; i++) {
    draw_rect(...);  /* 1000 quads! */
}

/* Good: Batch similar elements */
create_sprite_sheet();  /* 1 sprite with all UI elements */
draw_sprite_instances();  /* Much faster */
```

**2. Use VDP2 for static content**
```c
/* Bad: VDP1 sprite for background */
draw_rect(0, 0, 320, 224, bg_color);  /* Wastes quad */

/* Good: VDP2 background plane */
jo_set_background_color(bg_color);  /* No VDP1 usage */
```

**3. Limit text rendering**
```c
/* Bad: Update every frame */
void frame() {
    jo_printf(0, 0, "Frame: %d", frame_count++);  /* Slow */
}

/* Good: Only update when changed */
void frame() {
    if (frame_count % 60 == 0) {  /* Once per second */
        jo_printf(0, 0, "Frame: %d", frame_count);
    }
    frame_count++;
}
```

## Thread Safety

### Dual SH-2 Considerations

```
Master SH-2              Slave SH-2
    │                        │
    V                        V
┌────────────────────────────────┐
│      Shared Work RAM           │
│  (No hardware protection!)     │
└────────────────────────────────┘
```

**cui PAL approach:**
- Run all cui code on Master SH-2 only
- Don't use Slave SH-2 unless explicitly needed
- If using Slave, use cache-through memory regions

**Jo Engine default:**
- Slave SH-2 is idle by default
- Safe to ignore unless you explicitly start slave code

## Future Improvements

### 1. Sprite-based Rectangles

Replace character-based rects with 1x1 colored sprite:

```c
static int rect_sprite = -1;

void init() {
    /* Create 1x1 white sprite */
    rect_sprite = create_1x1_sprite();
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    jo_sprite_draw_scaled_rotated(rect_sprite, x, y, w, h,
                                   color, 0);
}
```

### 2. Custom Font System

Implement colored text using TGA fonts:

```c
jo_font* font = jo_font_load("font.tga", "!\"#$%...", 8, 8, 0);
jo_font_print(font, x, y, scale, "Colored text");
```

### 3. VDP2 Background Integration

Use VDP2 for solid color backgrounds:

```c
void begin_frame(uint32_t bg_color) {
    jo_color saturn_bg = cui_to_saturn_color(bg_color);
    jo_set_background_color(saturn_bg);
    /* Saves 1 VDP1 quad */
}
```

### 4. Palette-based Rendering

Pre-generate palette for common UI colors:

```c
/* Define palette with 256 colors */
jo_color palette[256] = {
    cui_to_saturn_color(CUI_COLOR_BG),
    cui_to_saturn_color(CUI_COLOR_TEXT),
    /* ... */
};

/* Use indexed color sprites (less VRAM) */
```

## References

- [Jo Engine Documentation](https://jo-engine.org/doxygen/)
- [Saturn Hardware Guide](https://www.copetti.org/writings/consoles/sega-saturn/)
- [VDP1 Programming](http://antime.kapsi.fi/sega/docs.html)
- [SH-2 Manual](https://www.st.com/resource/en/user_manual/cd00147165.pdf)
