/**
 * saturn_vdp2.c - Saturn VDP2 utilities implementation
 *
 * Hardware-independent logic for VDP2-based rendering.
 * All functions here are pure C with no SGL dependency,
 * making them fully testable on any platform.
 */

#include "saturn_vdp2.h"
#include "../../core/include/cui_types.h"
#include "../../core/include/cui_color_mapper.h"
#include <string.h>

/*============================================================================
 * Configuration
 *============================================================================*/

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 8

/*============================================================================
 * Phase 1: RGBA <-> RGB555 Conversion
 *============================================================================*/

saturn_rgb555_t saturn_rgba_to_rgb555(uint32_t rgba)
{
    uint8_t r = CUI_COLOR_R(rgba);
    uint8_t g = CUI_COLOR_G(rgba);
    uint8_t b = CUI_COLOR_B(rgba);

    /* Saturn RGB555 format: 0BBBBBGGGGGRRRRR */
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g5 = (g >> 3) & 0x1F;
    uint16_t b5 = (b >> 3) & 0x1F;

    return (b5 << 10) | (g5 << 5) | r5;
}

uint32_t saturn_rgb555_to_rgba(saturn_rgb555_t rgb555)
{
    uint8_t r5 = rgb555 & 0x1F;
    uint8_t g5 = (rgb555 >> 5) & 0x1F;
    uint8_t b5 = (rgb555 >> 10) & 0x1F;

    /* Expand 5-bit to 8-bit: shift left 3, preserving precision */
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g5 << 3) | (g5 >> 2);
    uint8_t b = (b5 << 3) | (b5 >> 2);

    return CUI_RGBA(r, g, b, 0xFF);
}

/*============================================================================
 * Phase 1: RGBA to Palette Slot Mapping
 *============================================================================*/

saturn_palette_slot_t saturn_rgba_to_palette_slot(uint32_t rgba)
{
    int r = (int)CUI_COLOR_R(rgba);
    int g = (int)CUI_COLOR_G(rgba);
    int b = (int)CUI_COLOR_B(rgba);
    int brightness = (r + g + b) / 3;

    /* Compute saturation (max - min of channels) */
    int max_ch = r;
    if (g > max_ch) max_ch = g;
    if (b > max_ch) max_ch = b;
    int min_ch = r;
    if (g < min_ch) min_ch = g;
    if (b < min_ch) min_ch = b;
    int saturation = max_ch - min_ch;

    /* Black -> highlight (inverted text context) */
    if (brightness < 32) {
        return SATURN_PAL_HIGHLIGHT;
    }

    /* Low-saturation colors: grays */
    if (saturation < 50) {
        if (brightness < 128) {
            return SATURN_PAL_DISABLED;  /* Dark gray / muted */
        }
        return SATURN_PAL_TEXT;  /* Light gray / white */
    }

    /* Chromatic colors: find dominant channel(s) */

    /* Purple/magenta: R and B high relative to G */
    if (r >= 100 && b >= 100 && g < r - 30 && g < b - 30) {
        return SATURN_PAL_DISABLED;
    }

    /* Green dominant: G is the highest channel by a meaningful margin */
    if (g > r && g > b && (g - r > 20 || g - b > 20)) {
        return SATURN_PAL_SUCCESS;
    }

    /* Yellow/warm: R and G both high, B significantly lower, R near or above G */
    if (r > 150 && g > 150 && b < g - 30 && r >= g - 40) {
        return SATURN_PAL_WARNING;
    }

    /* Blue dominant: B leads both R and G */
    if (b > r && b > g) {
        return SATURN_PAL_ACCENT;
    }

    /* Red dominant */
    if (r > g && r > b) {
        /* If green is close to red, it's warm/yellow */
        if (g > r - 40 && g > 150) {
            return SATURN_PAL_WARNING;
        }
        return SATURN_PAL_ERROR;
    }

    /* Default to text (white) */
    return SATURN_PAL_TEXT;
}

saturn_palette_slot_t saturn_role_to_palette_slot(int color_role)
{
    switch ((cui_color_role_t)color_role) {
        case CUI_ROLE_TEXT:        return SATURN_PAL_TEXT;
        case CUI_ROLE_TEXT_MUTED:  return SATURN_PAL_DISABLED;
        case CUI_ROLE_ACCENT:      return SATURN_PAL_ACCENT;
        case CUI_ROLE_SELECTED:    return SATURN_PAL_SELECTED;
        case CUI_ROLE_DISABLED:    return SATURN_PAL_DISABLED;
        case CUI_ROLE_SUCCESS:     return SATURN_PAL_SUCCESS;
        case CUI_ROLE_WARNING:     return SATURN_PAL_WARNING;
        case CUI_ROLE_ERROR:       return SATURN_PAL_ERROR;
        case CUI_ROLE_BACKGROUND:  return SATURN_PAL_HIGHLIGHT;
        case CUI_ROLE_SURFACE:     return SATURN_PAL_HIGHLIGHT;
        case CUI_ROLE_BORDER:      return SATURN_PAL_TEXT;
        default:                   return SATURN_PAL_TEXT;
    }
}

/*============================================================================
 * Phase 1: Default Palette Definition
 *============================================================================*/

/*
 * Default UI palette in RGB555 format.
 * Uses Saturn's 0BBBBBGGGGGRRRRR bit layout.
 * Colors chosen to be visible and distinguishable on CRT displays.
 *
 * Banks 0-7: Foreground-only (bg = black)
 * Banks 8-15: Combined background + foreground
 */
static const saturn_palette_entry_t s_default_palette[SATURN_PAL_COUNT] = {
    /* SATURN_PAL_TEXT: White */
    { .fg = 0x7FFF, .bg = 0x0000 },

    /* SATURN_PAL_ACCENT: Blue (Catppuccin Blue 89B4FA -> R=17,G=22,B=31) */
    { .fg = 0x7ED1, .bg = 0x0000 },

    /* SATURN_PAL_SELECTED: Yellow (bright: R=31,G=31,B=0) */
    { .fg = 0x03FF, .bg = 0x0000 },

    /* SATURN_PAL_ERROR: Red (Catppuccin Red F38BA8 -> R=30,G=17,B=21) */
    { .fg = 0x563E, .bg = 0x0000 },

    /* SATURN_PAL_SUCCESS: Green (Catppuccin Green A6E3A1 -> R=20,G=28,B=20) */
    { .fg = 0x5394, .bg = 0x0000 },

    /* SATURN_PAL_WARNING: Yellow (Catppuccin Yellow F9E2AF -> R=31,G=28,B=21) */
    { .fg = 0x579F, .bg = 0x0000 },

    /* SATURN_PAL_DISABLED: Gray (Catppuccin Overlay0 6C7086 -> R=13,G=14,B=16) */
    { .fg = 0x41CD, .bg = 0x0000 },

    /* SATURN_PAL_HIGHLIGHT: Inverted (dark text on light bg) */
    { .fg = 0x0C63, .bg = 0x7FFF },

    /* SATURN_PAL_BG_BLUE_FG_WHITE (8): Blue bg, white text */
    { .fg = 0x7FFF, .bg = 0x7ED1 },

    /* SATURN_PAL_BG_SURFACE_FG_WHITE (9): Dark gray bg (Catppuccin Surface0 313244 -> RGB555 0x20C6), white text */
    { .fg = 0x7FFF, .bg = 0x20C6 },

    /* SATURN_PAL_BG_SURFACE_FG_ACCENT (10): Dark gray bg, blue text */
    { .fg = 0x7ED1, .bg = 0x20C6 },

    /* SATURN_PAL_BG_BLUE_FG_YELLOW (11): Blue bg, yellow text */
    { .fg = 0x03FF, .bg = 0x7ED1 },

    /* SATURN_PAL_BG_SURFACE_FG_RED (12): Dark gray bg, red text */
    { .fg = 0x563E, .bg = 0x20C6 },

    /* SATURN_PAL_BG_SURFACE_FG_GREEN (13): Dark gray bg, green text */
    { .fg = 0x5394, .bg = 0x20C6 },

    /* SATURN_PAL_BG_SURFACE_FG_GRAY (14): Dark gray bg, gray text */
    { .fg = 0x41CD, .bg = 0x20C6 },

    /* SATURN_PAL_BG_SURFACE_FG_YELLOW (15): Dark gray bg, yellow text */
    { .fg = 0x03FF, .bg = 0x20C6 },
};

const saturn_palette_entry_t* saturn_get_default_palette(void)
{
    return s_default_palette;
}

/*============================================================================
 * Phase 1: Combined Palette Helpers
 *============================================================================*/

uint8_t saturn_find_combined_palette(uint8_t bg_slot, uint8_t fg_slot)
{
    /* Map (bg_slot, fg_slot) pairs to combined palette banks.
     * bg_slot is expected to be 8-15 (background color from rgba_to_bg_palette).
     * fg_slot is 0-7 (foreground text color). */

    /* Blue bg combinations */
    if (bg_slot == 8) {
        if (fg_slot == 0) return 8;   /* Blue + white */
        if (fg_slot == 2) return 11;  /* Blue + yellow */
    }

    /* Surface (dark gray) bg combinations */
    if (bg_slot == 9) {
        if (fg_slot == 0) return 9;   /* Surface + white */
        if (fg_slot == 1) return 10;  /* Surface + accent/blue */
        if (fg_slot == 2) return 15;  /* Surface + selected/yellow */
        if (fg_slot == 3) return 12;  /* Surface + error/red */
        if (fg_slot == 4) return 13;  /* Surface + success/green */
        if (fg_slot == 5) return 15;  /* Surface + warning/yellow */
        if (fg_slot == 6) return 14;  /* Surface + disabled/gray */
    }

    /* Fallback: preserve text color, lose background.
     * fg-only palette (0-7) has black bg — better than wrong color. */
    return fg_slot;
}

uint8_t saturn_rgba_to_bg_palette(uint32_t rgba)
{
    /* Map RGBA to background palette slot (banks 8-15).
     * Uses similar logic to rgba_to_palette_slot but returns combined banks. */

    int r = (int)CUI_COLOR_R(rgba);
    int g = (int)CUI_COLOR_G(rgba);
    int b = (int)CUI_COLOR_B(rgba);
    int brightness = (r + g + b) / 3;

    /* Compute saturation */
    int max_ch = r;
    if (g > max_ch) max_ch = g;
    if (b > max_ch) max_ch = b;
    int min_ch = r;
    if (g < min_ch) min_ch = g;
    if (b < min_ch) min_ch = b;
    int saturation = max_ch - min_ch;

    /* Low saturation or dark colors -> surface (bank 9) */
    if (saturation < 50 || brightness < 80) {
        return 9;  /* SATURN_PAL_BG_SURFACE_FG_WHITE */
    }

    /* Saturated colors map to blue highlight bg (bank 8).
     * Banks 12-15 are now surface+fg combos, not colored bg. */
    return 8;  /* SATURN_PAL_BG_BLUE_FG_WHITE */
}

/*============================================================================
 * Phase 1: PNT Entry Construction
 *============================================================================*/

uint16_t saturn_pnt_entry(uint16_t char_code, uint8_t palette)
{
    /* VDP2 Pattern Name Table entry (16-bit, 1-word mode):
     * Bits 15-12: Palette number (0-15)
     * Bits 11-0:  Character number (0-4095) */
    return ((uint16_t)(palette & 0x0F) << 12) | (char_code & 0x0FFF);
}

uint16_t saturn_pnt_char_code(uint16_t entry)
{
    return entry & 0x0FFF;
}

uint8_t saturn_pnt_palette(uint16_t entry)
{
    return (entry >> 12) & 0x0F;
}

/*============================================================================
 * Phase 2: Rectangle Layer
 *============================================================================*/

void saturn_rect_layer_init(saturn_rect_layer_t* layer, int cols, int rows)
{
    if (!layer) return;

    if (cols > SATURN_VDP2_MAX_COLS) cols = SATURN_VDP2_MAX_COLS;
    if (rows > SATURN_VDP2_MAX_ROWS) rows = SATURN_VDP2_MAX_ROWS;

    layer->cols = cols;
    layer->rows = rows;
    layer->dirty = false;
    memset(layer->cells, 0, sizeof(layer->cells));
}

void saturn_rect_layer_clear(saturn_rect_layer_t* layer)
{
    if (!layer) return;
    memset(layer->cells, 0, sizeof(layer->cells));
    layer->dirty = true;
}

void saturn_rect_layer_fill(saturn_rect_layer_t* layer,
                            int x, int y, int w, int h,
                            saturn_palette_slot_t slot)
{
    if (!layer || w <= 0 || h <= 0) return;

    /* Convert pixel coordinates to grid coordinates.
     * Use floor division for start, ceiling division for end. */
    int col_start = x / CHAR_WIDTH;
    int row_start = y / CHAR_HEIGHT;

    /* Handle negative coordinates */
    if (x < 0) col_start = x / CHAR_WIDTH - (x % CHAR_WIDTH != 0 ? 1 : 0);
    if (y < 0) row_start = y / CHAR_HEIGHT - (y % CHAR_HEIGHT != 0 ? 1 : 0);

    int col_end = (x + w + CHAR_WIDTH - 1) / CHAR_WIDTH;
    int row_end = (y + h + CHAR_HEIGHT - 1) / CHAR_HEIGHT;

    /* Clamp to layer bounds */
    if (col_start < 0) col_start = 0;
    if (row_start < 0) row_start = 0;
    if (col_end > layer->cols) col_end = layer->cols;
    if (row_end > layer->rows) row_end = layer->rows;

    for (int r = row_start; r < row_end; r++) {
        for (int c = col_start; c < col_end; c++) {
            layer->cells[r][c] = (uint8_t)slot;
        }
    }

    if (col_start < col_end && row_start < row_end) {
        layer->dirty = true;
    }
}

void saturn_rect_layer_fill_grid(saturn_rect_layer_t* layer,
                                 int col, int row, int cols, int rows,
                                 saturn_palette_slot_t slot)
{
    if (!layer || cols <= 0 || rows <= 0) return;

    int col_end = col + cols;
    int row_end = row + rows;

    /* Clamp to bounds */
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col_end > layer->cols) col_end = layer->cols;
    if (row_end > layer->rows) row_end = layer->rows;

    for (int r = row; r < row_end; r++) {
        for (int c = col; c < col_end; c++) {
            layer->cells[r][c] = (uint8_t)slot;
        }
    }

    if (col < col_end && row < row_end) {
        layer->dirty = true;
    }
}

uint8_t saturn_rect_layer_get(const saturn_rect_layer_t* layer, int col, int row)
{
    if (!layer || col < 0 || row < 0 || col >= layer->cols || row >= layer->rows) {
        return 0;
    }
    return layer->cells[row][col];
}

bool saturn_rect_layer_is_dirty(const saturn_rect_layer_t* layer)
{
    if (!layer) return false;
    return layer->dirty;
}

void saturn_rect_layer_mark_clean(saturn_rect_layer_t* layer)
{
    if (!layer) return;
    layer->dirty = false;
}
