/**
 * saturn_font.h - Custom font utilities for Saturn
 *
 * Handles conversion of 1bpp bitmap fonts to VDP2 4bpp character patterns.
 * Pure logic module - no hardware dependency. Fully testable.
 *
 * The Saturn's VDP2 stores character patterns (tiles) in 4bpp format.
 * Our source fonts are 1bpp (one bit per pixel, 8 pixels per byte).
 * This module handles the conversion.
 */

#ifndef CUI_SATURN_FONT_H
#define CUI_SATURN_FONT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

#define SATURN_FONT_CHAR_WIDTH    8   /* Pixels per character row */
#define SATURN_FONT_CHAR_HEIGHT   8   /* Rows per character */
#define SATURN_FONT_FIRST_CHAR   32   /* ASCII space */
#define SATURN_FONT_CHAR_COUNT   95   /* Space (32) through tilde (126) */

/**
 * Bytes per character in 4bpp format.
 * 8x8 pixels at 4bpp = 8*8/2 = 32 bytes per character.
 */
#define SATURN_FONT_4BPP_CHAR_SIZE  32

/**
 * Total size of converted font data in bytes.
 */
#define SATURN_FONT_4BPP_TOTAL_SIZE (SATURN_FONT_CHAR_COUNT * SATURN_FONT_4BPP_CHAR_SIZE)

/*============================================================================
 * Multi-Font System
 *============================================================================*/

#define SATURN_FONT_MAX_FONTS  12   /* Maximum registered fonts */
#define SATURN_FONT_MAX_CELL   32   /* Maximum cell width or height in pixels */

/**
 * Font descriptor — describes a 1bpp bitmap font for VDP1 rendering.
 *
 * cell_width must be a multiple of 8 (VDP1 sprite width constraint).
 * advance_x can be less than cell_width for narrow fonts padded to 8px cells.
 */
typedef struct saturn_font_desc {
    const char*    name;
    const uint8_t* data_1bpp;
    uint16_t cell_width;          /* VDP1 sprite width, must be multiple of 8 */
    uint16_t cell_height;
    uint16_t advance_x;           /* Horizontal advance per char (can be < cell_width) */
    uint16_t first_char;          /* Usually 32 */
    uint16_t char_count;          /* Usually 95 */
    uint16_t bytes_per_row_1bpp;  /* cell_width / 8 */
} saturn_font_desc_t;

/**
 * Font entry — a registered font with computed VRAM layout info.
 */
typedef struct saturn_font_entry {
    saturn_font_desc_t desc;
    uint32_t vram_offset;         /* Byte offset in VDP1 texture area */
    uint32_t char_4bpp_size;      /* (cell_width * cell_height) / 2 */
    uint32_t total_4bpp_size;     /* char_count * char_4bpp_size */
} saturn_font_entry_t;

/**
 * Font registry — manages multiple registered fonts and tracks VRAM usage.
 */
typedef struct saturn_font_registry {
    saturn_font_entry_t fonts[SATURN_FONT_MAX_FONTS];
    int count;
    int active;                   /* Index of active font (-1 if none) */
    uint32_t vram_cursor;         /* Next free byte offset in VDP1 texture area */
} saturn_font_registry_t;

/*============================================================================
 * Font Conversion
 *============================================================================*/

/**
 * Convert a single 1bpp font row to 4bpp format.
 * Input: 1 byte (8 pixels, MSB = leftmost)
 * Output: 4 bytes (8 pixels at 4bpp, packed big-endian nibbles)
 *
 * The fg_index parameter specifies which palette index to use for
 * foreground pixels. Background pixels are always index 0 (transparent).
 *
 * @param src_row   Source byte (1bpp, MSB first)
 * @param dst       Output buffer (must be at least 4 bytes)
 * @param fg_index  Foreground palette index (1-15)
 */
void saturn_font_convert_row(uint8_t src_row, uint8_t* dst, uint8_t fg_index);

/**
 * Convert an entire 1bpp font to 4bpp character patterns.
 *
 * @param src_1bpp      Source font data (char_count * 8 bytes)
 * @param dst_4bpp      Output buffer (char_count * 32 bytes)
 * @param char_count    Number of characters to convert
 * @param fg_index      Foreground palette index (1-15)
 * @return Number of characters converted
 */
int saturn_font_convert(const uint8_t* src_1bpp, uint8_t* dst_4bpp,
                        int char_count, uint8_t fg_index);

/**
 * Get a pointer to the built-in Saturn 8x8 font (1bpp format).
 * This is the same font data used by the SDL Saturn simulation.
 *
 * @return Pointer to font data (95 chars * 8 bytes = 760 bytes)
 */
const uint8_t* saturn_font_get_builtin(void);

/**
 * Get the size in bytes of the built-in font data.
 */
int saturn_font_get_builtin_size(void);

/*============================================================================
 * Multi-Font Registry
 *============================================================================*/

/**
 * Get a font descriptor for the built-in 8x8 font.
 * Convenience for registering the built-in font with the registry.
 *
 * @param desc  Output descriptor (caller provides storage)
 */
void saturn_font_builtin_desc(saturn_font_desc_t* desc);

/**
 * Initialize a font registry.
 *
 * @param reg  Registry to initialize
 */
void saturn_font_registry_init(saturn_font_registry_t* reg);

/**
 * Register a font in the registry.
 * Computes VRAM offsets (8-byte aligned) and stores the entry.
 *
 * @param reg   Registry
 * @param desc  Font descriptor (copied into registry)
 * @return Font index (0-based), or -1 on error (full, invalid desc)
 */
int saturn_font_register(saturn_font_registry_t* reg, const saturn_font_desc_t* desc);

/**
 * Set the active font by index.
 *
 * @param reg    Registry
 * @param index  Font index (0 to count-1)
 */
void saturn_font_set_active(saturn_font_registry_t* reg, int index);

/**
 * Get the active font entry.
 *
 * @param reg  Registry
 * @return Pointer to active font entry, or NULL if none active
 */
const saturn_font_entry_t* saturn_font_get_active_entry(const saturn_font_registry_t* reg);

/**
 * Convert a wide 1bpp font row to 4bpp format.
 *
 * Generalizes saturn_font_convert_row for rows wider than 8 pixels.
 * Processes the row in 8-pixel chunks, calling convert_row per chunk.
 *
 * @param src_row         Source bytes (bytes_per_row bytes, MSB first per byte)
 * @param dst             Output buffer (must be at least bytes_per_row * 4 bytes)
 * @param bytes_per_row   Number of source bytes (cell_width / 8)
 * @param fg_index        Foreground palette index (1-15)
 */
void saturn_font_convert_row_wide(const uint8_t* src_row, uint8_t* dst,
                                   int bytes_per_row, uint8_t fg_index);

#ifdef __cplusplus
}
#endif

#endif /* CUI_SATURN_FONT_H */
