/**
 * saturn_vdp2.h - Saturn VDP2 utilities for cui
 *
 * Provides testable logic for VDP2-based rendering:
 * - CRAM palette management (colored text)
 * - NBG1 rectangle layer (grid-aligned rectangles)
 * - Efficient screen clearing
 *
 * All logic in this module is hardware-independent and testable.
 * The actual VDP2 register writes happen in saturn_pal.c.
 */

#ifndef CUI_SATURN_VDP2_H
#define CUI_SATURN_VDP2_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

#define SATURN_VDP2_MAX_COLS     80  /* Max columns (640x224 hi-res) */
#define SATURN_VDP2_MAX_ROWS     28  /* Max rows (all modes) */

/*============================================================================
 * Palette Management (Phase 1: Colored Text)
 *============================================================================*/

/**
 * Palette slot indices for UI color roles.
 * Each maps to a 16-color palette in VDP2 CRAM.
 *
 * Banks 0-7: Foreground-only (text colors)
 * Banks 8-15: Combined background + foreground
 */
typedef enum saturn_palette_slot {
    /* Foreground-only palettes (black background) */
    SATURN_PAL_TEXT      = 0,   /* White text (default) */
    SATURN_PAL_ACCENT    = 1,   /* Blue text (focused elements) */
    SATURN_PAL_SELECTED  = 2,   /* Yellow text (selected items) */
    SATURN_PAL_ERROR     = 3,   /* Red text (error state) */
    SATURN_PAL_SUCCESS   = 4,   /* Green text (success state) */
    SATURN_PAL_WARNING   = 5,   /* Yellow/orange text (warning) */
    SATURN_PAL_DISABLED  = 6,   /* Gray/purple text (disabled) */
    SATURN_PAL_HIGHLIGHT = 7,   /* Inverted (dark on light bg) */

    /* Combined background + foreground palettes */
    SATURN_PAL_BG_BLUE_FG_WHITE    = 8,   /* Blue bg, white text */
    SATURN_PAL_BG_SURFACE_FG_WHITE = 9,   /* Dark gray bg, white text */
    SATURN_PAL_BG_SURFACE_FG_ACCENT = 10, /* Dark gray bg, blue text */
    SATURN_PAL_BG_BLUE_FG_YELLOW   = 11,  /* Blue bg, yellow text */
    SATURN_PAL_BG_SURFACE_FG_RED   = 12,  /* Dark gray bg, red text */
    SATURN_PAL_BG_SURFACE_FG_GREEN = 13,  /* Dark gray bg, green text */
    SATURN_PAL_BG_SURFACE_FG_GRAY  = 14,  /* Dark gray bg, gray text */
    SATURN_PAL_BG_SURFACE_FG_YELLOW = 15, /* Dark gray bg, yellow text */

    SATURN_PAL_COUNT     = 16
} saturn_palette_slot_t;

/**
 * RGB555 color format used by Saturn VDP2 CRAM.
 * Bits: 0BBBBBGGGGGRRRRR
 */
typedef uint16_t saturn_rgb555_t;

/**
 * Convert RGBA (0xRRGGBBAA) to Saturn RGB555.
 */
saturn_rgb555_t saturn_rgba_to_rgb555(uint32_t rgba);

/**
 * Convert Saturn RGB555 back to RGBA (for testing/simulation).
 */
uint32_t saturn_rgb555_to_rgba(saturn_rgb555_t rgb555);

/**
 * Map an RGBA color to the nearest palette slot.
 * Uses heuristic matching against the defined UI palette.
 */
saturn_palette_slot_t saturn_rgba_to_palette_slot(uint32_t rgba);

/**
 * Map a cui color role to a palette slot.
 */
saturn_palette_slot_t saturn_role_to_palette_slot(int color_role);

/**
 * Find the combined palette bank for a given bg and fg combination.
 * @param bg_slot Background palette slot (8-15 range expected)
 * @param fg_slot Foreground palette slot (0-7)
 * @return Combined palette bank index (8-15), or 8 as default
 */
uint8_t saturn_find_combined_palette(uint8_t bg_slot, uint8_t fg_slot);

/**
 * Map an RGBA color to a background palette slot (banks 8-15).
 * Uses similar heuristic to rgba_to_palette_slot but returns combined banks.
 */
uint8_t saturn_rgba_to_bg_palette(uint32_t rgba);

/**
 * Palette entry definition for initializing CRAM.
 * Each slot has a foreground (text) and background color.
 */
typedef struct saturn_palette_entry {
    saturn_rgb555_t fg;     /* Foreground (text) color */
    saturn_rgb555_t bg;     /* Background color (usually transparent/black) */
} saturn_palette_entry_t;

/**
 * Get the default UI palette definition.
 * Returns an array of SATURN_PAL_COUNT entries.
 */
const saturn_palette_entry_t* saturn_get_default_palette(void);

/*============================================================================
 * Rectangle Layer (Phase 2: Grid-Aligned Rectangles)
 *============================================================================*/

/**
 * Rectangle layer state.
 * Each cell holds a palette slot index, or 0 for transparent.
 */
typedef struct saturn_rect_layer {
    uint8_t cells[SATURN_VDP2_MAX_ROWS][SATURN_VDP2_MAX_COLS];
    int cols;       /* Active column count */
    int rows;       /* Active row count */
    bool dirty;     /* True if layer needs flushing to VDP2 */
} saturn_rect_layer_t;

/**
 * Initialize the rectangle layer for the given grid dimensions.
 */
void saturn_rect_layer_init(saturn_rect_layer_t* layer, int cols, int rows);

/**
 * Clear the entire rectangle layer (all cells transparent).
 */
void saturn_rect_layer_clear(saturn_rect_layer_t* layer);

/**
 * Fill a rectangular region with a palette slot color.
 * Coordinates are in pixels; they are converted to grid cells internally.
 * Clips to layer bounds.
 *
 * @param layer     Rectangle layer
 * @param x         X position in pixels
 * @param y         Y position in pixels
 * @param w         Width in pixels
 * @param h         Height in pixels
 * @param slot      Palette slot to fill with
 */
void saturn_rect_layer_fill(saturn_rect_layer_t* layer,
                            int x, int y, int w, int h,
                            saturn_palette_slot_t slot);

/**
 * Fill a rectangular region using grid coordinates directly.
 */
void saturn_rect_layer_fill_grid(saturn_rect_layer_t* layer,
                                 int col, int row, int cols, int rows,
                                 saturn_palette_slot_t slot);

/**
 * Get the palette slot at a specific grid cell.
 * Returns 0 (transparent) if out of bounds.
 */
uint8_t saturn_rect_layer_get(const saturn_rect_layer_t* layer, int col, int row);

/**
 * Check if the rectangle layer has been modified since last flush.
 */
bool saturn_rect_layer_is_dirty(const saturn_rect_layer_t* layer);

/**
 * Mark the layer as clean (called after flushing to VDP2).
 */
void saturn_rect_layer_mark_clean(saturn_rect_layer_t* layer);

/*============================================================================
 * Pattern Name Table Entry Construction (Phase 1+2)
 *============================================================================*/

/**
 * Build a VDP2 pattern name table entry.
 * Encodes a character code + palette number into a 16-bit word.
 *
 * @param char_code  Character/tile index (0-4095)
 * @param palette    Palette number (0-15)
 * @return 16-bit PNT entry
 */
uint16_t saturn_pnt_entry(uint16_t char_code, uint8_t palette);

/**
 * Extract the character code from a PNT entry.
 */
uint16_t saturn_pnt_char_code(uint16_t entry);

/**
 * Extract the palette number from a PNT entry.
 */
uint8_t saturn_pnt_palette(uint16_t entry);

#ifdef __cplusplus
}
#endif

#endif /* CUI_SATURN_VDP2_H */
