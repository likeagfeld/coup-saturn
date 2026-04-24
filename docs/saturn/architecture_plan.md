# Saturn Architecture Plan

## Comprehensive Strategy for Leveraging Saturn Hardware in cui

### Document Status
- **Author**: pal-saturn-agent (architecture analysis)
- **Scope**: Full Saturn PAL evolution from text-only to hardware-accelerated UI
- **Dependencies**: SGL documentation (see separate Saturn reference repo)

---

## 1. Current State Assessment

### 1.1 What Exists Today

The Saturn PAL (`pal/saturn/`) is a **text-only implementation** using `slPrint()`:

| Feature | SDL | N64 | Saturn |
|---------|-----|-----|--------|
| Text rendering | 5x7 bitmap font (2x scaled) | libdragon rdpq_text | SGL `slPrint()` (8x8 fixed) |
| Colored text | Full 32-bit RGBA | Full 32-bit RGBA | White only (color ignored) |
| Rectangle fill | `SDL_RenderFillRect` | `rdpq_fill_rectangle` | **No-op** |
| Color palette | Unlimited (true color) | Unlimited (RGBA5551/8888) | 7-color semantic mapping |
| Background clear | `SDL_RenderClear` | `rdpq_clear` | Blank line overwrite (40 spaces x 28 rows) |
| Frame sync | `SDL_RenderPresent` | `rdpq_detach_show` | App-owned `slSynch()` |
| Controller input | SDL keyboard events | libdragon joypad API | SMPC `Smpc_Peripheral[0].push` |

### 1.2 Key Limitations of Current Saturn PAL

1. **`draw_rect()` is a no-op** - Components that rely on rectangles for selection highlighting, progress bars, sliders, etc. render incorrectly
2. **Color is completely ignored** - All text is white on black, making it impossible to distinguish states (focused, disabled, error, success)
3. **Screen clear is expensive** - Printing 40 spaces across 28 rows (1,120 characters) per frame via `slPrint()` is wasteful
4. **No custom font support** - Locked to SGL's built-in 8x8 ROM font
5. **`sgl_defs.h` is minimal** - Only declares `slPrint`, `slLocate`, `slSynch`, `slInitSystem` - missing all VDP1/VDP2/color functions

### 1.3 What N64 Does That Saturn Doesn't

The N64 PAL is a good reference for a "complete" retro platform implementation:
- Full color text via `rdpq_font_style()` color changes per draw call
- Rectangle rendering via `rdpq_set_mode_fill()` + `rdpq_fill_rectangle()`
- Proper background clear via `rdpq_clear()`
- Font loading via `rdpq_font_load_builtin()`

---

## 2. Saturn Hardware Capabilities (from SGL Documentation)

### 2.1 VDP2 - The Background/Scroll Processor

VDP2 is the Saturn's background processor. It manages scroll planes and is **ideal for UI rendering** because:

- **4 normal scroll planes (NBG0-NBG3)** - Each independently scrollable
- **Character pattern mode** - 8x8 or 16x16 tiles, same grid concept as `slPrint()`
- **Per-character palette selection** - Each tile can reference a different palette slot
- **Color modes**: 16, 256, 2048, 32768 colors per plane
- **512 KB VRAM** organized in 4x128KB banks
- **4 KB Color RAM (CRAM)** shared between VDP1 and VDP2

**Key insight**: SGL's `slPrint()` already uses VDP2 NBG0 in character mode. We can expand this to use **multiple scroll planes** and **palette manipulation** without abandoning the existing text infrastructure.

#### VDP2 Color RAM Modes

| Mode | Colors | Format | Best For |
|------|--------|--------|----------|
| Mode 0 | 1024 | RGB555 (1 word) | General UI (plenty of colors) |
| Mode 1 | 2048 | RGB555 (1 word) | Extended palettes |
| Mode 2 | 1024 | RGB888 (2 words) | True-color accuracy |

For UI purposes, **Mode 0 with 1024 colors** is more than sufficient - we only need ~16-32 distinct colors for the theme system.

### 2.2 VDP1 - The Sprite Processor

VDP1 handles sprites and polygons via a display list (command table):

- **512 KB VRAM** for sprite data and command tables
- **Textured sprites**: Rectangular bitmaps with color lookup
- **Non-textured sprites**: Solid color rectangles, polygons, polylines
- **Scaled sprites**: Hardware scaling support
- **Gouraud shading**: Per-vertex color interpolation
- **Color calculation**: Shadow, half-transparency, half-luminance

**Key for cui**: VDP1 can draw **solid-color rectangles** as non-textured sprites. This is the path to implementing `draw_rect()`.

#### VDP1 Drawing Commands Relevant to UI

| Command | SGL Function | Use Case |
|---------|-------------|----------|
| Normal Sprite | `slPutSprite()` | Icons, custom glyphs |
| Scaled Sprite | `slDispSprite()` | Resizable elements |
| Polygon | `slPutPolygon()` | Colored rectangles, fills |
| Polyline | N/A (direct VDP1) | Borders, outlines |
| Line | N/A (direct VDP1) | Dividers |

### 2.3 SGL Function Reference (from SGL.txt)

#### Display/System
```
slInitSystem(tv_mode, tex_table, framerate)  - System initialization
slSynch()                                     - VBlank sync
slTVOn() / slTVOff()                         - Display enable/disable
```

#### Scroll/Background (VDP2)
```
slCharNbg0(size, color_mode)     - Configure NBG0 character format
slCharNbg1(size, color_mode)     - Configure NBG1 character format
slPageNbg0(page_ptr, page_num)   - Set NBG0 page data pointer
slPageNbg1(page_ptr, page_num)   - Set NBG1 page data pointer
slScrPosNbg0(x, y)               - Set NBG0 scroll position
slScrPosNbg1(x, y)               - Set NBG1 scroll position
slPriorityNbg0(priority)         - Set NBG0 draw priority
slPriorityNbg1(priority)         - Set NBG1 draw priority
slColorCalc(mode)                - Enable color calculation (transparency)
slColorCalcOn(target)            - Enable per-plane transparency
slColRateNbg0(rate)              - Set NBG0 transparency rate
slBack1ColSet(addr, color)       - Set back screen color
slScrAutoDisp(flags)             - Enable/disable scroll planes
```

#### Color RAM
```
slColRAMMode(mode)               - Set CRAM mode (0=1024, 1=2048, 2=1024 RGB888)
slSetColRAM(addr, data, count)   - Write palette entries to CRAM
```

#### Sprite/Polygon (VDP1)
```
slPutSprite(x, y, attr, angle)   - Draw sprite at position
slDispSprite(pos, attr, angle)   - Draw positioned sprite
slSetSprite(data, nb)            - Define sprite data
slPutPolygon(def)                - Draw 3D polygon model
```

#### Text
```
slPrint(string, pos)             - Print text (VDP2 NBG text layer)
slLocate(x, y)                   - Get text position
slPrintFX(x, y, string)         - Print with effects (if available)
```

#### Matrix/Transform (for potential future 3D UI effects)
```
slPushMatrix() / slPopMatrix()   - Matrix stack
slTranslate(x, y, z)            - Translation
slRotX/Y/Z(angle)               - Rotation
slScale(sx, sy, sz)             - Scaling
```

### 2.4 SMPC Controller Interface

The current implementation correctly uses SGL's edge detection:
```c
Smpc_Peripheral[0].push  // Newly pressed (edge-triggered)
Smpc_Peripheral[0].data  // Currently held
Smpc_Peripheral[0].pull  // Newly released
```

**Potential enhancement**: The `data` field enables **key repeat** detection (hold D-pad to scroll continuously), which is important for list navigation.

### 2.5 Memory Budget

| Region | Size | Current Usage | Available |
|--------|------|---------------|-----------|
| Work RAM High | 1 MB | Application code + data | ~800 KB |
| Work RAM Low | 512 KB | SGL system | ~256 KB |
| VDP1 VRAM | 512 KB | Unused (no sprites) | ~480 KB |
| VDP2 VRAM | 512 KB | NBG0 text layer only | ~384 KB |
| Color RAM | 4 KB | Default palette (7 colors) | ~3.9 KB |

The Saturn has substantial unused graphics memory for UI rendering.

---

## 3. Phased Implementation Plan

### Phase 1: Colored Text via VDP2 Palette Manipulation
**Priority: HIGH | Effort: Medium | Impact: Transforms usability**

This is the single most impactful change. Currently all text is white - making focused/disabled/error states indistinguishable.

#### 3.1.1 Strategy

SGL's `slPrint()` uses VDP2 NBG0 in character pattern mode. Each character cell in the pattern name table contains:
- Character code (which 8x8 tile to display)
- **Palette number** (which color palette to use for that character)

The key insight is that we don't need to replace `slPrint()` - we need to **manipulate the VDP2 palette** to define our UI colors, then **select the correct palette per text draw call**.

#### 3.1.2 Implementation

**Step 1: Define a UI Color Palette in CRAM**

```c
// Saturn CRAM layout for cui (16-color palettes within CRAM)
// CRAM address space: 0x05F00000 - 0x05F00FFF (4KB)
//
// Palette 0: Default text (white on transparent)  - used by slPrint default
// Palette 1: Accent text (blue on transparent)
// Palette 2: Selected text (yellow on transparent)
// Palette 3: Error text (red on transparent)
// Palette 4: Success text (green on transparent)
// Palette 5: Warning text (yellow on transparent)
// Palette 6: Disabled text (gray/purple on transparent)
// Palette 7: Highlight background (yellow bg, black text)

#define SATURN_PAL_TEXT        0   // White text
#define SATURN_PAL_ACCENT      1   // Blue text
#define SATURN_PAL_SELECTED    2   // Yellow text
#define SATURN_PAL_ERROR       3   // Red text
#define SATURN_PAL_SUCCESS     4   // Green text
#define SATURN_PAL_WARNING     5   // Yellow text (shared with selected)
#define SATURN_PAL_DISABLED    6   // Purple/gray text
#define SATURN_PAL_HIGHLIGHT   7   // Inverted (dark text on light bg)

static void saturn_init_ui_palettes(void) {
    // Each 16-color palette: index 0 = transparent (bg), index 1 = foreground
    // Using RGB555 format: 0bBBBBBGGGGGRRRRR

    uint16_t palette_data[8][16];   // 8 palettes x 16 colors each

    // Palette 0: White text
    palette_data[0][0] = 0x0000;    // Transparent
    palette_data[0][1] = 0x7FFF;    // White (R=31, G=31, B=31)

    // Palette 1: Blue accent
    palette_data[1][0] = 0x0000;
    palette_data[1][1] = 0x7C00;    // Blue (B=31)

    // Palette 2: Yellow selected
    palette_data[2][0] = 0x0000;
    palette_data[2][1] = 0x03FF;    // Yellow (R=31, G=31)

    // ... etc for each palette

    // Write to CRAM via SGL
    for (int i = 0; i < 8; i++) {
        slSetColRAM(i * 16, (void*)palette_data[i], 16);
    }
}
```

**Step 2: Modify draw_text to use colored palettes**

Instead of plain `slPrint()`, we write directly to the VDP2 pattern name table with palette bits set:

```c
// VDP2 Pattern Name Table entry format (16-bit):
//   Bits 15-12: Palette number
//   Bits 11-0:  Character code (tile index)

static void saturn_draw_text_colored(int x, int y, const char* text, uint32_t color) {
    uint8_t palette = saturn_color_to_palette_index(color);

    // Get pointer to VDP2 NBG0 pattern name table
    volatile uint16_t* pnt = (volatile uint16_t*)VDP2_NBG0_PNT_ADDR;

    for (int i = 0; text[i] != '\0' && (col + i) < SATURN_COLS; i++) {
        uint16_t char_code = (uint16_t)text[i];
        uint16_t entry = (palette << 12) | char_code;
        pnt[(row * SATURN_COLS) + col + i] = entry;
    }
}
```

**Step 3: Map cui colors to palette indices**

```c
static uint8_t saturn_color_to_palette_index(uint32_t rgba) {
    // Use the color mapper's semantic role mapping
    // Quick heuristic based on RGB values:
    uint8_t r = CUI_COLOR_R(rgba);
    uint8_t g = CUI_COLOR_G(rgba);
    uint8_t b = CUI_COLOR_B(rgba);

    // Match against known theme colors -> palette index
    if (r > 200 && g > 200 && b > 200) return SATURN_PAL_TEXT;      // White
    if (r > 200 && g < 100 && b < 100) return SATURN_PAL_ERROR;     // Red
    if (r < 100 && g > 200 && b < 100) return SATURN_PAL_SUCCESS;   // Green
    if (r > 200 && g > 200 && b < 100) return SATURN_PAL_SELECTED;  // Yellow
    if (r < 100 && g < 100 && b > 200) return SATURN_PAL_ACCENT;    // Blue
    if (r > 128 && g < 100 && b > 128) return SATURN_PAL_DISABLED;  // Purple
    return SATURN_PAL_TEXT;  // Default to white
}
```

#### 3.1.3 New sgl_defs.h Additions Required

```c
// Color RAM
extern void slColRAMMode(Uint16 mode);
extern void slSetColRAM(Uint32 addr, void* data, Uint32 count);

// Scroll plane configuration
extern void slCharNbg0(Uint16 size, Uint16 color_mode);
extern void slPageNbg0(void* page_ptr, Uint16 page_num);
extern void slPriorityNbg0(Uint16 priority);
extern void slScrAutoDisp(Uint32 flags);
extern void slBack1ColSet(void* addr, Uint16 color);

// VDP2 VRAM access
#define VDP2_VRAM_BASE   0x05E00000
#define VDP2_CRAM_BASE   0x05F00000
```

#### 3.1.4 Color Mapper Updates

The existing `saturn_color_mapper.c` already defines a 7-color semantic mapping. This phase upgrades it to actually produce visible color differences:

```c
// Updated palette_size from 7 to 8 (add highlight/inverted)
static cui_color_mapper_t saturn_color_mapper = {
    .map_rgba = saturn_map_rgba,
    .map_role = saturn_map_role,
    .full_color = 0,
    .palette_size = 8,
    .platform_name = "saturn"
};
```

#### 3.1.5 Impact on Components

| Component | Before | After |
|-----------|--------|-------|
| Button | White text, no focus indicator | Blue text when focused, gray when disabled |
| List | White text, ">" for selection | Yellow highlighted selected item |
| Checkbox | White [x] or [ ] | Green when checked, white when unchecked |
| Progressbar | White text percentage | Color changes at warning/critical thresholds |
| Tabs | All white, ">" for active | Active tab in accent color |
| Slider | White bar | Colored fill portion |

---

### Phase 2: Rectangle Rendering via VDP1 Sprites
**Priority: HIGH | Effort: Medium | Impact: Enables progress bars, selection highlights, sliders**

#### 3.2.1 Strategy

Use VDP1 non-textured polygon commands to draw solid-color rectangles. SGL provides the `slDispSprite()` function but for simple rectangles, we can use the polygon command directly.

Alternatively, use a **second VDP2 scroll plane (NBG1)** dedicated to colored rectangles via tile fills. This is simpler and avoids VDP1 command list management.

**Recommended approach: VDP2 NBG1 as a "rectangle layer"**

- NBG1 sits behind NBG0 (text layer)
- Fill rectangular regions with solid-color tiles
- Transparent elsewhere so NBG0 text shows through
- VDP2 handles compositing automatically via priority system

#### 3.2.2 NBG1 Rectangle Layer Implementation

```c
// NBG1 configured as a simple 40x28 color grid
// Each cell can be one of our palette colors or transparent
static uint8_t g_rect_layer[SATURN_ROWS][SATURN_COLS];  // Color index per cell

static void saturn_init_rect_layer(void) {
    // Configure NBG1 as a character plane with 16-color palette
    slCharNbg1(CHAR_SIZE_1x1, COL_TYPE_16);

    // Load simple solid-color tile patterns into VRAM:
    //   Tile 0 = fully transparent
    //   Tile 1 = fully opaque (fg color = palette index 1)
    //   ... etc per palette

    // Set NBG1 priority below NBG0 (text on top of rects)
    slPriorityNbg1(5);  // Lower than NBG0
    slPriorityNbg0(6);  // Text on top

    // Enable NBG1
    slScrAutoDisp(NBG0ON | NBG1ON);

    // Clear rectangle layer
    memset(g_rect_layer, 0, sizeof(g_rect_layer));
}

static void saturn_draw_rect(int x, int y, int w, int h, uint32_t color) {
    // Convert pixel coordinates to grid coordinates
    int col_start = x / CHAR_WIDTH;
    int row_start = y / CHAR_HEIGHT;
    int col_end = (x + w + CHAR_WIDTH - 1) / CHAR_WIDTH;
    int row_end = (y + h + CHAR_HEIGHT - 1) / CHAR_HEIGHT;

    uint8_t palette_idx = saturn_color_to_palette_index(color);

    // Fill the rectangle region in the NBG1 pattern name table
    for (int row = row_start; row < row_end && row < SATURN_ROWS; row++) {
        for (int col = col_start; col < col_end && col < SATURN_COLS; col++) {
            g_rect_layer[row][col] = palette_idx;
        }
    }

    // Flush to VDP2 NBG1 PNT (done in end_frame or via DMA)
    saturn_flush_rect_layer();
}
```

#### 3.2.3 Alternative: VDP1 Approach

For pixel-precise rectangles (not grid-aligned):

```c
// VDP1 command table entry for a non-textured polygon
typedef struct {
    uint16_t ctrl;      // Command control word
    uint16_t link;      // Link to next command
    uint16_t pmod;      // Draw mode (color mode, etc.)
    uint16_t colr;      // Color data (RGB555 or palette)
    int16_t  xa, ya;    // Vertex A
    int16_t  xb, yb;    // Vertex B
    int16_t  xc, yc;    // Vertex C
    int16_t  xd, yd;    // Vertex D
    uint16_t grda;      // Gouraud shading table address
    uint16_t dummy;     // Padding
} vdp1_cmd_t;

static void saturn_draw_rect_vdp1(int x, int y, int w, int h, uint32_t color) {
    uint16_t rgb555 = saturn_rgba_to_rgb555(color);

    // Issue VDP1 polygon command via SGL sprite interface
    // or write directly to VDP1 command table
    FIXED pos[4] = { toFIXED(x), toFIXED(y), 0 };
    // ... construct sprite attribute with non-textured polygon mode
}
```

The VDP2 approach is recommended for Phase 2 because it integrates cleanly with the existing text layer and avoids VDP1 command list complexity.

#### 3.2.4 Impact

- **Progressbar**: Can render actual colored fill bars
- **Slider**: Can render the track and filled portion
- **List**: Selection highlight background becomes visible
- **Button**: Can have a visible background when focused
- **Divider**: Can render colored horizontal/vertical lines

---

### Phase 3: Efficient Screen Clear via VDP2 Back Screen
**Priority: MEDIUM | Effort: Low | Impact: Performance improvement**

#### 3.3.1 Problem

Current `begin_frame()` prints 40 spaces across 28 rows = 1,120 `slPrint()` calls worth of characters per frame. This is extremely wasteful.

#### 3.3.2 Solution

SGL provides `slBack1ColSet()` to set the VDP2 back screen color. Combined with simply clearing the pattern name table (or using DMA), we can achieve a clean frame start:

```c
static void saturn_begin_frame(uint32_t bg_color) {
    uint16_t rgb555 = saturn_rgba_to_rgb555(bg_color);

    // Set back screen color (displayed where no scroll plane pixel is opaque)
    slBack1ColSet((void*)BACK_CRAM_ADDR, rgb555);

    // Clear NBG0 pattern name table via DMA or memset
    // (much faster than printing 1120 space characters)
    memset((void*)VDP2_NBG0_PNT_ADDR, 0, SATURN_COLS * SATURN_ROWS * 2);

    // Also clear the rectangle layer
    memset(g_rect_layer, 0, sizeof(g_rect_layer));
}
```

This replaces 1,120 character writes with a single register write + one DMA/memset operation.

---

### Phase 4: Custom Font Support
**Priority: MEDIUM | Effort: Medium | Impact: Visual consistency across platforms**

#### 3.4.1 Strategy

The SDL simulation already has a Saturn 8x8 font defined in `sdl_sim_saturn.c`. We should use the same font data on real hardware by loading it into VDP2 VRAM as character patterns.

#### 3.4.2 Implementation

```c
// Load custom 8x8 font into VDP2 VRAM character pattern area
// Each character = 8x8 pixels at 4bpp = 32 bytes per character
// 96 printable ASCII characters = 3,072 bytes

static void saturn_load_custom_font(const uint8_t* font_data, int char_count) {
    // Convert 1bpp font data to 4bpp VDP2 character format
    uint8_t vdp2_chars[96 * 32];  // 4bpp, 8x8, 96 chars

    for (int c = 0; c < char_count; c++) {
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font_data[c * 8 + row];  // 1bpp source row
            // Convert each pixel to 4-bit palette index
            for (int px = 0; px < 8; px += 2) {
                uint8_t hi = (bits & (0x80 >> px)) ? 1 : 0;
                uint8_t lo = (bits & (0x80 >> (px + 1))) ? 1 : 0;
                vdp2_chars[c * 32 + row * 4 + px / 2] = (hi << 4) | lo;
            }
        }
    }

    // Copy to VDP2 character pattern area
    memcpy((void*)(VDP2_VRAM_BASE + FONT_PATTERN_OFFSET), vdp2_chars, sizeof(vdp2_chars));

    // Update NBG0 to use our custom character patterns
    slCharNbg0(CHAR_SIZE_1x1, COL_TYPE_16);
}
```

#### 3.4.3 Benefits

- Consistent font appearance between SDL Saturn simulation and real hardware
- Ability to add custom glyphs (box-drawing characters, icons) to the font
- Foundation for the Icon component on Saturn

---

### Phase 5: Color Calculation Effects (Half-Transparency)
**Priority: LOW | Effort: Medium | Impact: Visual polish**

#### 3.5.1 Capability

VDP2 supports **color calculation** between scroll planes:
- Half-transparency between NBG layers
- Color offset (global RGB adjustment)
- Shadow effects

#### 3.5.2 UI Applications

- **Modal overlays**: Darken background when a dialog is shown (color offset on NBG0, dialog on NBG1)
- **Disabled state**: Half-luminance on disabled component regions
- **Focus pulse**: Subtle brightness variation on focused elements via color offset animation

```c
// Enable half-transparency on NBG1 (rectangle layer)
slColorCalc(CC_RATE | CC_2ND);
slColorCalcOn(CC_NBG1);
slColRateNbg1(16);  // 50% transparency (0-31 range)
```

---

### Phase 6: Key Repeat / Hold Input
**Priority: MEDIUM | Effort: Low | Impact: Better list/slider navigation**

#### 3.6.1 Problem

The current input implementation only reports edge-triggered presses (`push` field). Holding the D-pad doesn't generate repeated events, making long list scrolling tedious.

#### 3.6.2 Solution

Implement a software key-repeat timer using the `data` field (held buttons):

```c
#define REPEAT_DELAY_FRAMES  15  // ~250ms initial delay
#define REPEAT_RATE_FRAMES    3  // ~50ms repeat rate

static uint16_t g_last_held = 0;
static int g_repeat_counter = 0;
static uint16_t g_repeat_button = 0;

static cui_input_action_t saturn_input_poll(void) {
    if (!Per_Connect1) return CUI_INPUT_NONE;

    uint16_t pressed = Smpc_Peripheral[0].push;
    uint16_t held = ~Smpc_Peripheral[0].data;  // Invert: active-low -> active-high

    // Edge-triggered press takes priority
    if (pressed) {
        g_repeat_button = pressed;
        g_repeat_counter = REPEAT_DELAY_FRAMES;
        return saturn_map_button(pressed);
    }

    // Key repeat for held buttons
    if (held & g_repeat_button) {
        g_repeat_counter--;
        if (g_repeat_counter <= 0) {
            g_repeat_counter = REPEAT_RATE_FRAMES;
            return saturn_map_button(g_repeat_button);
        }
    } else {
        g_repeat_button = 0;
        g_repeat_counter = 0;
    }

    return CUI_INPUT_NONE;
}
```

This should also be considered for the N64 PAL and potentially promoted to a core utility.

---

### Phase 7: DMA-Accelerated VRAM Updates
**Priority: LOW | Effort: High | Impact: Performance at scale**

#### 3.7.1 Opportunity

The Saturn's SCU (System Control Unit) provides DMA channels for high-speed data transfer between Work RAM and VDP VRAM. For large UI updates (full-screen redraws, scroll transitions), DMA can significantly outperform CPU memcpy.

```c
// SCU DMA transfer from Work RAM to VDP2 VRAM
void saturn_dma_to_vdp2(void* src, uint32_t vdp2_offset, uint32_t size) {
    // Configure SCU DMA channel 0
    // Source: Work RAM address
    // Destination: VDP2 VRAM base + offset
    // Size: byte count
    // This is fire-and-forget; DMA runs in background
}
```

---

### Phase 8: Sound Integration Stub
**Priority: LOW | Effort: Low | Impact: Foundation for UI audio feedback**

#### 3.8.1 Capability

The Saturn has a dedicated 68EC000 sound CPU with 32-channel SCSP:
- PCM playback for sound effects
- FM synthesis for tones
- 128-step DSP for effects

#### 3.8.2 UI Applications

- Button click sound
- Navigation tick
- Error buzz
- Success chime

This would require a new PAL interface extension:

```c
// Potential future PAL audio interface
typedef struct cui_pal_audio {
    cui_result_t (*init)(void);
    void (*shutdown)(void);
    void (*play_sfx)(cui_sfx_t sfx);  // CUI_SFX_CLICK, CUI_SFX_NAV, etc.
} cui_pal_audio_t;
```

This is out of scope for the current PAL interface but worth noting as a future consideration.

---

## 4. Updated `sgl_defs.h` Requirements

The current `sgl_defs.h` only declares 6 functions. The full Saturn PAL needs significantly more:

```c
// === PHASE 1: Color ===
extern void slColRAMMode(Uint16 mode);
extern Uint16* slSetColRAM(Uint32 addr, void* data, Uint32 count);

// === PHASE 2: Scroll Planes ===
extern void slCharNbg0(Uint16 size, Uint16 color_mode);
extern void slCharNbg1(Uint16 size, Uint16 color_mode);
extern void slPageNbg0(void* ptr, Uint16 page);
extern void slPageNbg1(void* ptr, Uint16 page);
extern void slPriorityNbg0(Uint16 pri);
extern void slPriorityNbg1(Uint16 pri);
extern void slScrAutoDisp(Uint32 flags);

// === PHASE 3: Background ===
extern void slBack1ColSet(void* addr, Uint16 color);

// === PHASE 5: Color Calculation ===
extern void slColorCalc(Uint16 mode);
extern void slColorCalcOn(Uint16 target);
extern void slColRateNbg0(Uint16 rate);
extern void slColRateNbg1(Uint16 rate);
extern void slColorOffset(Uint16 mode);
extern void slColorOffsetA(Sint16 r, Sint16 g, Sint16 b);

// === Constants ===
#define CHAR_SIZE_1x1   0
#define CHAR_SIZE_2x2   1
#define COL_TYPE_16     0
#define COL_TYPE_256    1
#define COL_TYPE_2048   2
#define COL_TYPE_32768  3
#define COL_TYPE_16M    4

#define NBG0ON          (1 << 0)
#define NBG1ON          (1 << 1)
#define NBG2ON          (1 << 2)
#define NBG3ON          (1 << 3)
#define RBG0ON          (1 << 4)

// Priority/color calculation constants
#define CC_RATE         (1 << 0)
#define CC_2ND          (1 << 1)
#define CC_NBG0         (1 << 4)
#define CC_NBG1         (1 << 5)
```

---

## 5. New Files and Module Structure

### 5.1 Proposed File Structure

```
pal/saturn/
├── saturn_pal.h                # Public API (existing, expanded)
├── saturn_pal.c                # Core PAL implementation (existing, modified)
├── saturn_layout.c             # Layout configuration (existing, unchanged)
├── saturn_color_mapper.c       # Color mapping (existing, expanded)
├── saturn_vdp2.h               # NEW: VDP2 scroll/background utilities
├── saturn_vdp2.c               # NEW: VDP2 plane management, palette loading
├── saturn_font.h               # NEW: Custom font loading utilities
├── saturn_font.c               # NEW: Font data and VDP2 character patterns
├── saturn_netlink.h            # NetLink networking (existing)
├── saturn_netlink.c            # NetLink implementation (existing)
├── sgl_defs.h                  # SGL declarations (existing, expanded)
├── example_main.c              # Example app (existing, updated)
├── saturn_cxx_stubs.c          # C++ stubs (existing)
└── saturn_xmp_stubs.c          # XMP stubs (existing)
```

### 5.2 Module Responsibilities

| Module | Responsibility |
|--------|---------------|
| `saturn_pal.c` | PAL interface implementation, delegates to VDP2 module |
| `saturn_vdp2.c` | VDP2 plane setup, CRAM management, rectangle layer |
| `saturn_font.c` | Custom font loading, character pattern management |
| `saturn_color_mapper.c` | Semantic role to palette index mapping |
| `sgl_defs.h` | SGL function/type declarations for compilation |

---

## 6. SDL Simulation Updates

The SDL Saturn simulation (`pal/sdl/sdl_sim_saturn.c`) should be updated in parallel to match new Saturn capabilities:

### 6.1 Current Simulation

- 320x224 at 8x8 character cells
- 7-color palette
- Custom Saturn font bitmap

### 6.2 Needed Updates

1. **Rectangle rendering**: Simulate the grid-aligned rectangle behavior (NBG1 layer)
2. **Color palette**: Expand from 7 to 8+ colors matching the real CRAM palettes
3. **Transparency simulation**: Visualize half-transparency effects

---

## 7. Component-Specific Saturn Adaptations

### 7.1 Components That Benefit Most

| Component | Current Issue | Phase Fix | Saturn-Specific Adaptation |
|-----------|--------------|-----------|---------------------------|
| **Progressbar** | No visible bar | Phase 2 | NBG1 rectangle fill for progress region |
| **Slider** | No visible track/fill | Phase 2 | NBG1 rectangle for track, colored for fill |
| **List** | No selection highlight | Phase 1+2 | Yellow text + background rect for selected |
| **Button** | No focus indicator | Phase 1 | Blue text when focused, accent color |
| **Tabs** | No active indicator | Phase 1 | Active tab in accent color text |
| **Checkbox** | No color difference | Phase 1 | Green "[x]", white "[ ]" |
| **Divider** | Works (text-based) | -- | Already functional |
| **Label** | No custom colors | Phase 1 | Color text via palette selection |
| **Icon** | No glyph rendering | Phase 4 | Custom font glyphs for icon patterns |
| **Spinner** | Works (text-based) | Phase 1 | Arrow indicators in accent color |

### 7.2 Components That Need No Change

- **Label** (basic): Already works with `slPrint()`
- **Divider**: Text-based, works today
- **Spinner**: Text-based, works today

---

## 8. Testing Strategy

### 8.1 SDL Simulation Testing

All Saturn-specific rendering behavior should be testable via the SDL simulation mode:

```c
// Test that Saturn color mapping produces correct palette indices
void test_saturn_color_mapping(void) {
    CUI_ASSERT_EQ(saturn_map_rgba(0xFF0000FF), SATURN_PAL_ERROR);   // Red -> Error palette
    CUI_ASSERT_EQ(saturn_map_rgba(0x00FF00FF), SATURN_PAL_SUCCESS); // Green -> Success palette
    CUI_ASSERT_EQ(saturn_map_rgba(0xFFFF00FF), SATURN_PAL_SELECTED);// Yellow -> Selected palette
}

// Test that rectangle layer fills correct grid cells
void test_saturn_rect_layer(void) {
    saturn_draw_rect(0, 0, 16, 8, 0xFF0000FF);
    CUI_ASSERT_EQ(g_rect_layer[0][0], SATURN_PAL_ERROR);
    CUI_ASSERT_EQ(g_rect_layer[0][1], SATURN_PAL_ERROR);
    CUI_ASSERT_EQ(g_rect_layer[0][2], 0);  // Transparent
}
```

### 8.2 Hardware Testing

- Build with SGL toolchain targeting Saturn hardware or emulator (Mednafen, Kronos)
- Verify CRAM palette entries via emulator's VDP2 debug view
- Verify NBG0/NBG1 compositing via emulator's layer viewer
- Test controller input across all button combinations

### 8.3 Visual Regression Testing

Use the SDL screenshot capture + simulation mode to create baseline images.
(Storybook tooling was removed when this repo was split off from the cui
sandbox; revisit this section if visual regression tests are reintroduced.)

---

## 9. Implementation Priority Matrix

| Phase | Feature | Effort | Impact | Dependencies | Priority |
|-------|---------|--------|--------|--------------|----------|
| 1 | Colored text (CRAM palettes) | Medium | **Critical** | sgl_defs.h expansion | P0 |
| 2 | Rectangle rendering (NBG1 layer) | Medium | **High** | Phase 1 (shared CRAM) | P0 |
| 3 | Efficient screen clear | Low | Medium | Phase 2 (NBG1 setup) | P1 |
| 6 | Key repeat input | Low | Medium | None | P1 |
| 4 | Custom font loading | Medium | Medium | Phase 1 (VRAM management) | P2 |
| 5 | Color calculation effects | Medium | Low | Phase 2 (NBG1 layer) | P3 |
| 7 | DMA acceleration | High | Low | Phase 2/3 | P3 |
| 8 | Sound integration | Low | Low | New PAL interface | P4 |

**Recommended implementation order**: Phase 1 -> Phase 2 -> Phase 6 -> Phase 3 -> Phase 4 -> Phase 5

---

## 10. Risk Assessment

### 10.1 Technical Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `slPrint()` conflicts with direct NBG0 PNT writes | Medium | High | Use separate NBG plane for custom text, or fully replace `slPrint()` |
| CRAM layout conflicts with SGL internal usage | Low | High | Map SGL's default palette usage first, use unused CRAM regions |
| VDP2 VRAM bank contention | Low | Medium | Careful VRAM bank planning per SGL docs |
| SGL function signatures don't match real headers | Medium | Medium | Cross-reference with `SGL.txt` and SGL header files in LIBRARY/ |

### 10.2 Architectural Risks

| Risk | Mitigation |
|------|------------|
| Over-engineering the Saturn PAL | Keep changes behind the existing PAL interface; no core changes |
| Breaking text rendering by modifying NBG0 | Test incrementally; keep `slPrint()` fallback |
| Memory pressure from dual-plane usage | VDP2 has 512KB; text + rect layers use ~5KB total |

---

## 11. Cross-Platform PAL Comparison (Target State)

After completing Phases 1-3, the Saturn PAL would achieve near-parity with SDL and N64:

| Feature | SDL | N64 | Saturn (Target) |
|---------|-----|-----|-----------------|
| Colored text | 32-bit RGBA | 32-bit RGBA | 8-palette VDP2 CRAM |
| Rectangles | `SDL_RenderFillRect` | `rdpq_fill_rectangle` | NBG1 tile fill |
| Screen clear | `SDL_RenderClear` | `rdpq_clear` | `slBack1ColSet` + memset |
| Color depth | Unlimited | Unlimited | 8 semantic palettes (~16 distinct colors) |
| Transparency | Full alpha | Full alpha | VDP2 color calculation (2 levels) |
| Input repeat | OS-level | Not yet | Software timer (Phase 6) |
| Font | 5x7 bitmap | libdragon built-in | SGL ROM font -> custom (Phase 4) |

---

## 12. Conclusion

The Saturn is significantly underutilized in the current PAL implementation. The hardware provides:
- **VDP2 with 4 independent scroll planes** - currently only 1 used, partially
- **4 KB Color RAM** - currently using default 7-color palette only
- **512 KB VDP1 VRAM** - completely unused
- **512 KB VDP2 VRAM** - ~3% utilized (one text plane)

Phases 1 and 2 alone (colored text + rectangles) would transform the Saturn from a "white text on black" terminal into a proper themed UI platform matching the visual intent of the component library. These changes stay within the existing PAL interface contract and require no core component modifications.

The SGL documentation (in separate Saturn reference repo) provides sufficient detail for all proposed phases, particularly `SGL_clean.txt` for function references and `PROGRAM1.txt`/`PROGRAM2.txt` for VDP2 programming patterns.
