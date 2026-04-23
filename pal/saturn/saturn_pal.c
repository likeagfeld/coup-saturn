/**
 * saturn_pal.c - Saturn Platform Abstraction Layer Implementation
 *
 * Implementation of cui PAL for Sega Saturn using bare SGL.
 * No Jo Engine dependency - works with any Saturn application.
 *
 * Color support: Uses VDP2 CRAM palettes for colored text.
 *
 * SGL initializes NBG0 with the ASCII font in 256-color mode
 * (COL_TYPE_256). In 256-color mode, the PNT palette field
 * interpretation is complex -- SGL's PNCN0 register config
 * controls how palette bits are decoded, and changing the
 * palette per-cell is unreliable.
 *
 * Our approach: switch NBG0 to 16-color mode (COL_TYPE_16)
 * after SGL init, then upload our own font in 4bpp format.
 * In 16-color mode, the PNT entry's bits 15-12 directly
 * select one of 16 palette banks (each 16 colors in CRAM).
 * This gives us 8+ distinct text colors by writing simple
 * 16-color palettes to CRAM and encoding the palette number
 * in each PNT entry.
 */

#include "saturn_pal.h"
#include "saturn_input.h"
#include "saturn_storage.h"
#include "saturn_vdp2.h"
#include "saturn_vdp1.h"
#include "saturn_font.h"
#include "../../core/include/cui_color_mapper.h"

/* SGL types and functions */
#include "sgl_defs.h"

/* memcpy for VRAM uploads */
#include <string.h>

/*============================================================================
 * Configuration
 *============================================================================*/

/* Character size for converting pixel to grid coordinates */
#define CHAR_WIDTH  8
#define CHAR_HEIGHT 8

/*
 * VDP2 PNT page boundary (hardware constant).
 * Each scroll page is 64 columns in 1-word PNT mode with 8x8 characters.
 * At 640x224 (80 cols), columns 0-63 are on Map A, 64-79 on Map B.
 * In 640 mode, slLocate() cannot be used (see saturn_pnt_row comment).
 */
#define PNT_PAGE_COLS 64

/*
 * PNT layout discovered at runtime by probing slLocate().
 * This avoids hardcoded addresses that may not match what SGL
 * actually configures after slPlaneNbg0() and slMapNbg0().
 */
typedef struct {
    volatile uint16_t* page0_base;  /* slLocate(0, 0) */
    uint32_t row_stride;            /* bytes between slLocate(0, 1) and slLocate(0, 0) */
    volatile uint16_t* page1_base;  /* Map B base for 640 mode (NULL in 320 mode) */
} pnt_layout_t;

static pnt_layout_t g_pnt = { 0 };

/*
 * VDP2 CRAM palette layout for colored text (16-color mode).
 *
 * We reconfigure NBG0 from SGL's default 256-color mode to 16-color
 * mode (COL_TYPE_16). In 16-color mode:
 *   - Each palette bank = 16 colors * 2 bytes = 32 bytes in CRAM
 *   - Palette N starts at CRAM base + N * 32 bytes
 *   - PNT entry bits 15-12 directly select palette bank 0-15
 *   - Font characters use color index 0 (bg) and index 1 (fg)
 *
 * This is more reliable than 256-color mode because the VDP2
 * hardware directly reads the palette number from the PNT entry
 * without PNCN register complications.
 */
#define CRAM_16COLOR_BANK_SIZE  0x20   /* 32 bytes = 16 colors per bank */
#define CRAM_BASE               VDP2_COLRAM

/*
 * SGL's ASCII character pattern and map locations in VRAM-B1.
 * From SGL docs (Chapter 8, "ASCII Scrolls"):
 *   CEL_DATA (character patterns): 0x25e60000 - 0x25e61FFF (0x2000 bytes)
 *   MAP_DATA (PNT / scroll map):   0x25e76000 - 0x25e76FFF (0x1000 bytes)
 *
 * We overwrite the character data with our 4bpp font (half the size).
 */
#define ASCII_CEL_VRAM_ADDR     0x25e60000  /* VRAM-B1 base — CEL_DATA */

/* Space character for screen clearing */
#define ASCII_SPACE             0x20

/* Total ASCII characters in SGL font set (0-127) */
#define ASCII_CHAR_COUNT        128

/*============================================================================
 * State
 *============================================================================*/

static cui_saturn_resolution_t g_resolution = CUI_SATURN_RES_320x224;
static bool g_initialized = false;
static bool g_palettes_initialized = false;

/* Input processor with queue and D-pad repeat */
static saturn_input_t g_input;
static bool g_input_initialized = false;
static bool g_input_needs_update = true;

/* Rectangle layer for colored backgrounds */
static saturn_rect_layer_t g_rect_layer;
static bool g_rect_layer_initialized = false;

/* Multi-font registry */
static saturn_font_registry_t g_font_registry;

/*============================================================================
 * PNT Row Pointer Helper
 *============================================================================*/

/*
 * Get PNT row pointer using the discovered layout.
 *
 * In 640 mode, slLocate() uses 128-col stride after slPlaneNbg0(PL_SIZE_2x1),
 * but VDP2 reads each page with 64-col stride. Using the probed base address
 * and stride avoids this mismatch.
 *
 * In 320/352 modes, the discovered values match slLocate() exactly.
 */
static inline volatile uint16_t* saturn_pnt_row(int start_col, int row) {
    if (start_col >= PNT_PAGE_COLS && g_pnt.page1_base) {
        return (volatile uint16_t*)((uint8_t*)g_pnt.page1_base
            + (uint32_t)row * g_pnt.row_stride);
    }
    return (volatile uint16_t*)((uint8_t*)g_pnt.page0_base
        + (uint32_t)row * g_pnt.row_stride);
}

/*============================================================================
 * NBG0 16-Color Mode Setup
 *============================================================================*/

/**
 * Convert and upload the built-in 1bpp font as 4bpp character patterns.
 *
 * SGL stores 128 ASCII characters (0-127) at ASCII_CEL_VRAM_ADDR in
 * 256-color (8bpp) format. We overwrite this with 4bpp data for
 * 16-color mode.
 *
 * The 4bpp font uses color index 1 for foreground pixels and index 0
 * for background. The actual colors come from the CRAM palette bank
 * selected by the PNT entry's palette number field.
 *
 * Character layout in VRAM:
 *   - Characters 0-31: blank (non-printable)
 *   - Characters 32-126: printable ASCII from built-in font
 *   - Character 127: blank
 *
 * Each 4bpp 8x8 character = 32 bytes.
 * Total: 128 * 32 = 4096 bytes (0x1000) -- fits in original 0x2000 area.
 */
static void saturn_upload_4bpp_font(void)
{
    volatile uint8_t* vram = (volatile uint8_t*)ASCII_CEL_VRAM_ADDR;

    /* Clear entire character area first (128 chars * 32 bytes = 4096) */
    for (int i = 0; i < ASCII_CHAR_COUNT * SATURN_FONT_4BPP_CHAR_SIZE; i++) {
        vram[i] = 0;
    }

    /* Get the built-in 1bpp font data (95 printable characters, 32-126) */
    const uint8_t* font_1bpp = saturn_font_get_builtin();

    /*
     * Convert each printable character from 1bpp to 4bpp and write
     * directly to VRAM at the correct character position.
     *
     * Character N is stored at VRAM offset N * 32 bytes.
     * Foreground uses palette color index 1.
     */
    for (int ch = 0; ch < SATURN_FONT_CHAR_COUNT; ch++) {
        int ascii_code = SATURN_FONT_FIRST_CHAR + ch;  /* 32 + ch */
        int vram_offset = ascii_code * SATURN_FONT_4BPP_CHAR_SIZE;

        /* Convert 8 rows of this character */
        for (int row = 0; row < SATURN_FONT_CHAR_HEIGHT; row++) {
            uint8_t src_byte = font_1bpp[ch * SATURN_FONT_CHAR_HEIGHT + row];
            uint8_t dst[4];
            saturn_font_convert_row(src_byte, dst, 1);  /* fg = index 1 */

            /* Write 4 bytes (one row in 4bpp) to VRAM */
            vram[vram_offset + row * 4 + 0] = dst[0];
            vram[vram_offset + row * 4 + 1] = dst[1];
            vram[vram_offset + row * 4 + 2] = dst[2];
            vram[vram_offset + row * 4 + 3] = dst[3];
        }
    }
}

/**
 * Write our 16 UI palettes to VDP2 CRAM in 16-color bank format.
 *
 * In 16-color mode, each palette bank is 16 colors (32 bytes).
 * Font characters use only indices 0 and 1:
 *   Index 0 = background (black/transparent or colored bg)
 *   Index 1 = foreground (the text color)
 *
 * Banks 0-7: Foreground-only (black background)
 * Banks 8-15: Combined background + foreground colors
 *
 * We write these two entries for each of the 16 palette banks.
 * The PNT entry's palette number (bits 15-12) selects the bank.
 */
static void saturn_init_cram_palettes(void)
{
    const saturn_palette_entry_t* pal = saturn_get_default_palette();
    volatile uint16_t* cram = (volatile uint16_t*)CRAM_BASE;

    for (int i = 0; i < SATURN_PAL_COUNT; i++) {
        /*
         * In 16-color mode, each bank is 16 colors = 16 words.
         * Bank i word offset = i * 16.
         * Bank i byte offset = i * 32 = i * 0x20.
         */
        int bank_offset = i * (CRAM_16COLOR_BANK_SIZE / sizeof(uint16_t));

        /* Index 0: background color */
        cram[bank_offset + 0] = pal[i].bg;

        /* Index 1: foreground color (the text color for this slot) */
        cram[bank_offset + 1] = pal[i].fg;
    }

    g_palettes_initialized = true;
}

/**
 * Probe slLocate() to discover the actual PNT base address and row stride.
 *
 * Called after plane configuration (slPlaneNbg0, slMapNbg0) but before
 * font upload, so the addresses reflect SGL's final layout. This avoids
 * hardcoding addresses that may differ from what SGL actually sets.
 *
 * In 640 mode, page1_base is set to our Map B address (0x25E78000) since
 * slLocate() doesn't support columns >= 64.
 */
static void saturn_discover_pnt_layout(void) {
    g_pnt.page0_base = (volatile uint16_t*)slLocate(0, 0);
    volatile uint16_t* row1 = (volatile uint16_t*)slLocate(0, 1);
    g_pnt.row_stride = (uint8_t*)row1 - (uint8_t*)g_pnt.page0_base;
    g_pnt.page1_base = (g_resolution == CUI_SATURN_RES_640x224)
        ? (volatile uint16_t*)0x25E78000
        : NULL;
}

/**
 * Switch NBG0 from SGL's default 256-color mode to 16-color mode.
 *
 * This must be called after slInitSystem() and before rendering.
 * Steps:
 *   1. Switch NBG0 to COL_TYPE_16 (16-color / 4bpp)
 *   2. Reconfigure the page to point at our character patterns
 *   3. Configure plane size and maps for 640 mode
 *   4. Discover PNT layout by probing slLocate()
 *   5. Upload our 4bpp font to VRAM
 *   6. Write 16-color palette banks to CRAM
 *
 * After this, PNT entry bits 15-12 directly control the palette
 * bank per character cell, enabling colored text.
 */
static void saturn_init_nbg0_16color(void)
{
    /*
     * Switch NBG0 to 16-color mode, 1x1 character size.
     * This tells the VDP2 to interpret character patterns as 4bpp
     * and PNT palette fields as 16-color bank selectors.
     *
     * slCharNbg0 parameters: (color_type, char_size)
     * Matches SGL convention: slCharNbg1(COL_TYPE_256, CHAR_SIZE_1x1)
     */
    slCharNbg0(COL_TYPE_16, CHAR_SIZE_1x1);

    /*
     * Reconfigure the NBG0 page to use our character pattern location
     * and 1-word PNT with 10-bit character numbers.
     *
     * Parameters:
     *   cell_adr  = character pattern VRAM address
     *   col_adr   = color palette offset (0 = CRAM base)
     *   data_type = PNB_1WORD | CN_10BIT
     *
     * With 10-bit character numbers, we can address characters 0-1023.
     * We only need 0-127, so 10 bits is sufficient.
     *
     * PNT entry format (1-word, 10-bit CN):
     *   Bits 15-12: Palette number (0-15)
     *   Bits 11-10: H/V flip
     *   Bits 9-0:   Character number (0-1023)
     */
    slPageNbg0((void*)ASCII_CEL_VRAM_ADDR, (void*)0,
               PNB_1WORD | CN_10BIT);

    /*
     * In 640 mode (80 cols), the scroll plane must be 2 pages wide.
     * VDP2 PNT pages are 64 columns; without this, columns 64-79
     * wrap page 0 and duplicate the left side of the screen.
     *
     * Map A (Page 0): 0x25E76000 (8KB page: 0x25E76000–0x25E77FFF)
     * Map B (Page 1): 0x25E78000 (next full page boundary — avoids
     *   overlapping Map A which spans 0x2000 bytes in 64×64 1-word mode)
     *
     * Reset scroll position — slInitSystem may set a non-zero default
     * X scroll offset for hi-res modes.
     */
    if (g_resolution == CUI_SATURN_RES_640x224) {
        slPlaneNbg0(PL_SIZE_2x1);
        slMapNbg0((void*)0x25E76000, (void*)0x25E78000,
                  (void*)0x25E76000, (void*)0x25E78000);
        slScrPosNbg0(toFIXED(0), toFIXED(0));
    }

    /* Discover PNT layout by probing slLocate() after plane config */
    saturn_discover_pnt_layout();

    /* Upload 4bpp font to VRAM (overwrites SGL's 8bpp ASCII data) */
    saturn_upload_4bpp_font();

    /* Write 16-color palettes to CRAM */
    saturn_init_cram_palettes();
}

/*============================================================================
 * Display Implementation
 *============================================================================*/

static cui_result_t saturn_display_init(void) {
    /*
     * Display is initialized by slInitSystem() which must be
     * called from main() before any cui functions.
     * We just validate that it was called.
     */
    if (!g_initialized) {
        return CUI_ERROR_INIT_FAILED;
    }

    /*
     * Switch NBG0 to 16-color mode with our custom font.
     * This replaces SGL's default 256-color ASCII setup with
     * a 16-color setup that supports per-character palette selection.
     */
    saturn_init_nbg0_16color();

    /*
     * Grid dimensions are derived from the layout system.
     * cui_saturn_init_layout() must be called before cui_pal_init()
     * so the layout is already set by the time we get here.
     */

    /*
     * Initialize VDP1 rectangle system.
     * Sets sprite priority to 4 (below NBG0 text at priority 5).
     */
    saturn_vdp1_init();

    /*
     * Initialize font registry and register the built-in 8x8 font.
     * The multi-font system manages VRAM layout for all registered fonts.
     * Additional fonts can be registered via cui_saturn_font_register().
     */
    saturn_font_registry_init(&g_font_registry);
    {
        saturn_font_desc_t builtin;
        saturn_font_builtin_desc(&builtin);
        saturn_font_register(&g_font_registry, &builtin);
    }

    /*
     * Upload all registered fonts to VDP1 VRAM.
     * Converts 1bpp font data to 4bpp and uploads at computed offsets.
     * Also upload via legacy path for backward compatibility.
     */
    saturn_vdp1_upload_fonts(&g_font_registry);
    saturn_vdp1_upload_font();

    /*
     * Initialize the rectangle layer for background colors.
     */
    {
        const cui_layout_t* layout = cui_layout_get();
        saturn_rect_layer_init(&g_rect_layer, layout->grid_cols, layout->grid_rows);
    }
    g_rect_layer_initialized = true;

    return CUI_OK;
}

static void saturn_display_shutdown(void) {
    /* No shutdown on console - this is a no-op */
}

static void saturn_display_begin_frame(uint32_t bg_color) {
    /*
     * Clear the text layer by writing space PNT entries directly.
     * Each entry uses palette 0 (black bg, white fg) and the
     * space character code (0x20). This is faster than slPrint
     * with a 40-character string per row, and also ensures the
     * palette bits are reset to 0 for the clear.
     */
    (void)bg_color;  /* Background color set via back screen at init */

    const cui_layout_t* layout = cui_layout_get();
    int cols = layout->grid_cols;
    int rows = layout->grid_rows;
    uint16_t clear_entry = saturn_pnt_entry(ASCII_SPACE, 0);
    int page0_cols = (cols <= PNT_PAGE_COLS) ? cols : PNT_PAGE_COLS;

    for (int row = 0; row < rows; row++) {
        volatile uint16_t* pnt = saturn_pnt_row(0, row);
        for (int col = 0; col < page0_cols; col++) {
            pnt[col] = clear_entry;
        }
        if (cols > PNT_PAGE_COLS) {
            pnt = saturn_pnt_row(PNT_PAGE_COLS, row);
            for (int col = 0; col < cols - PNT_PAGE_COLS; col++) {
                pnt[col] = clear_entry;
            }
        }
    }

    /* Reset VDP1 rectangle counter for new frame */
    saturn_vdp1_begin_frame();

    /* Clear the rectangle layer for this frame */
    if (g_rect_layer_initialized) {
        saturn_rect_layer_clear(&g_rect_layer);
    }
}

static void saturn_display_end_frame(void) {
    /*
     * Flush remaining rect layer cells (those not overwritten by draw_text)
     * as space characters with the background palette.
     */
    if (g_rect_layer_initialized && saturn_rect_layer_is_dirty(&g_rect_layer)) {
        const cui_layout_t* layout = cui_layout_get();
        int cols = layout->grid_cols;
        int rows = layout->grid_rows;
        uint16_t clear_entry = saturn_pnt_entry(ASCII_SPACE, 0);
        int page0_cols = (cols <= PNT_PAGE_COLS) ? cols : PNT_PAGE_COLS;

        for (int r = 0; r < rows; r++) {
            volatile uint16_t* pnt = saturn_pnt_row(0, r);
            for (int c = 0; c < page0_cols; c++) {
                uint8_t bg = saturn_rect_layer_get(&g_rect_layer, c, r);
                if (bg != 0) {
                    if (pnt[c] == clear_entry) {
                        pnt[c] = saturn_pnt_entry(ASCII_SPACE, bg);
                    }
                }
            }
            if (cols > PNT_PAGE_COLS) {
                pnt = saturn_pnt_row(PNT_PAGE_COLS, r);
                for (int c = PNT_PAGE_COLS; c < cols; c++) {
                    uint8_t bg = saturn_rect_layer_get(&g_rect_layer, c, r);
                    if (bg != 0) {
                        if (pnt[c - PNT_PAGE_COLS] == clear_entry) {
                            pnt[c - PNT_PAGE_COLS] = saturn_pnt_entry(ASCII_SPACE, bg);
                        }
                    }
                }
            }
        }
        saturn_rect_layer_mark_clean(&g_rect_layer);
    }

    /*
     * Write VDP1 END command to terminate command list.
     * VDP1 will process commands during V-BLANK (triggered by slSynch).
     */
    saturn_vdp1_end_frame();

    /*
     * Frame synchronization is handled by slSynch() which should
     * be called by the application after all rendering is done.
     * Mark input as needing update for the next frame.
     */
    g_input_needs_update = true;
}

/**
 * VDP2 PNT text rendering fallback.
 *
 * Renders text by writing PNT entries directly to VDP2 VRAM.
 * Grid-aligned (8x8 cells) — less precise than VDP1 but unlimited budget.
 *
 * @param start_col  Starting grid column
 * @param row        Grid row
 * @param text       Text string to render
 * @param len        Number of characters
 * @param fg_slot    Foreground palette slot (0-7)
 */
static void saturn_vdp2_draw_text_fallback(int start_col, int row,
                                            const char* text, int len,
                                            uint8_t fg_slot) {
    const cui_layout_t* layout = cui_layout_get();
    int cols = layout->grid_cols;

    int pnt_base;
    volatile uint16_t* pnt;

    if (start_col >= PNT_PAGE_COLS) {
        pnt = saturn_pnt_row(PNT_PAGE_COLS, row);
        pnt_base = PNT_PAGE_COLS;
    } else {
        pnt = saturn_pnt_row(0, row);
        pnt_base = 0;
    }

    for (int i = 0; i < len; i++) {
        int c = start_col + i;
        if (c >= cols) break;

        /* Re-anchor PNT pointer at page boundary */
        if (c == PNT_PAGE_COLS && pnt_base == 0) {
            pnt = saturn_pnt_row(PNT_PAGE_COLS, row);
            pnt_base = PNT_PAGE_COLS;
        }

        if (c >= 0) {
            uint8_t ch = (uint8_t)text[i];
            if (ch < 0x20 || ch > 0x7E) {
                ch = ASCII_SPACE;
            }

            uint8_t pal;
            if (g_rect_layer_initialized) {
                uint8_t bg = saturn_rect_layer_get(&g_rect_layer, c, row);
                if (bg != 0) {
                    pal = saturn_find_combined_palette(bg, fg_slot);
                    g_rect_layer.cells[row][c] = 0;
                } else {
                    pal = fg_slot;
                }
            } else {
                pal = fg_slot;
            }

            pnt[c - pnt_base] = saturn_pnt_entry((uint16_t)ch, pal);
        }
    }
}

static void saturn_display_draw_text(int x, int y, const char* text, uint32_t color) {
    if (!text) return;

    const cui_layout_t* layout = cui_layout_get();
    int cols = layout->grid_cols;
    int rows = layout->grid_rows;

    /* Convert pixel coordinates to grid coordinates */
    int col = x / CHAR_WIDTH;
    int row = y / CHAR_HEIGHT;

    /* Bounds check row */
    if (row < 0 || row >= rows) return;
    if (col >= cols) return;

    /* Measure string length (clipped to screen) */
    int len = 0;
    while (text[len] != '\0' && (col + len) < cols) len++;
    if (len == 0) return;

    /*
     * Map the RGBA color to one of our foreground palette slots (0-7).
     * The color mapper returns a palette slot index for the text color.
     */
    uint8_t fg_slot = (uint8_t)saturn_rgba_to_palette_slot(color);

    /*
     * VDP1 sprite-based text (pixel-accurate positioning).
     * Each printable character becomes an 8x8 Normal Sprite command.
     * This supplements the VDP2 PNT rendering below. VDP1 sprites
     * are at priority 4 (behind VDP2 NBG0 at priority 5), so they
     * act as a backup layer visible through transparent VDP2 cells.
     *
     * Note: VDP1 palette-mode sprites have not yet been verified on
     * hardware, so VDP2 PNT always renders all text as the primary
     * guaranteed-visible path.
     */
    saturn_vdp1_draw_text(x, y, text, len, fg_slot);

    /*
     * VDP2 PNT text rendering (primary path — guaranteed visible).
     * Always render all text to VDP2 PNT entries. VDP2 NBG0 at
     * priority 5 is above VDP1 sprites, so this text is always
     * visible regardless of VDP1 sprite state.
     */
    saturn_vdp2_draw_text_fallback(col, row, text, len, fg_slot);
}

static void saturn_display_draw_text_sprite(int x, int y, const char* text, uint32_t color) {
    if (!text) return;

    const cui_layout_t* layout = cui_layout_get();
    int screen_w = layout->grid_cols * CHAR_WIDTH;

    /* Use active font's advance_x for bounds checking, default to CHAR_WIDTH */
    const saturn_font_entry_t* active = saturn_font_get_active_entry(&g_font_registry);
    int advance = active ? active->desc.advance_x : CHAR_WIDTH;

    if (x >= screen_w) return;

    /* Measure string length (clipped to screen using advance width) */
    int len = 0;
    while (text[len] != '\0' && (x + len * advance) < screen_w) len++;
    if (len == 0) return;

    /* Map RGBA color to foreground palette slot */
    uint8_t fg_slot = (uint8_t)saturn_rgba_to_palette_slot(color);

    /* VDP1-only: render text as sprites at pixel-accurate positions.
     * Use multi-font path if an active font entry is available. */
    if (active) {
        saturn_vdp1_draw_text_font(x, y, text, len, fg_slot, active);
    } else {
        saturn_vdp1_draw_text(x, y, text, len, fg_slot);
    }
}

static void saturn_display_draw_rect(int x, int y, int w, int h, uint32_t color) {
    /*
     * Try VDP1 pixel-accurate rendering first.
     * If budget is exceeded, fall back to VDP2 character-cell rendering.
     */
    uint16_t rgb555 = saturn_rgba_to_rgb555(color);
    if (saturn_vdp1_draw_rect(x, y, w, h, rgb555)) {
        return;  /* VDP1 success */
    }

    /*
     * VDP2 fallback: draw a colored rectangle by filling the rect layer.
     * The rect layer is flushed at end_frame, writing space characters
     * with the appropriate background color palette.
     * This is grid-aligned (8x8 cells), so less precise than VDP1.
     */
    if (!g_rect_layer_initialized) return;

    uint8_t bg_slot = saturn_rgba_to_bg_palette(color);
    saturn_rect_layer_fill(&g_rect_layer, x, y, w, h, bg_slot);
}

/*============================================================================
 * Input Implementation
 *============================================================================*/

static cui_result_t saturn_pal_input_init(void) {
    /* SGL handles peripheral initialization via slInitSystem */
    saturn_input_config_t cfg = {
        .repeat_delay = 15,  /* ~250ms at 60fps before repeat starts */
        .repeat_rate  = 4,   /* ~67ms between repeats */
    };
    saturn_input_init(&g_input, &cfg);
    g_input_initialized = true;
    g_input_needs_update = true;
    return CUI_OK;
}

static void saturn_input_shutdown(void) {
    /* No shutdown needed */
}

static cui_input_action_t saturn_pal_input_poll(void) {
    /*
     * Queue-based input processing with D-pad key repeat.
     * saturn_input module handles:
     * - Queuing all simultaneous button presses (not just the first)
     * - D-pad key repeat for held directions
     * - Edge detection via manual AICTRL.C pattern (.data only)
     *
     * Update is called once per frame (guarded by g_input_needs_update).
     * Dequeue returns one action per call; queued events persist across calls.
     */
    if (!g_input_initialized || !Per_Connect1) {
        return CUI_INPUT_NONE;
    }

    /* Update once per frame - reads peripheral state into queue */
    if (g_input_needs_update) {
        g_input_needs_update = false;
        saturn_input_state_t state = {
            .data = Smpc_Peripheral[0].data,
        };
        saturn_input_update(&g_input, &state);
    }

    return saturn_input_dequeue(&g_input);
}

static const char* saturn_input_get_action_label(cui_input_action_t action) {
    switch (action) {
        case CUI_INPUT_UP:        return "Up";
        case CUI_INPUT_DOWN:      return "Down";
        case CUI_INPUT_LEFT:      return "Left";
        case CUI_INPUT_RIGHT:     return "Right";
        case CUI_INPUT_CONFIRM:   return "A";
        case CUI_INPUT_CANCEL:    return "B";
        case CUI_INPUT_PAGE_UP:   return "L";
        case CUI_INPUT_PAGE_DOWN: return "R";
        case CUI_INPUT_QUIT:      return "Start";
        default:                  return "?";
    }
}

/*============================================================================
 * Platform Structure
 *============================================================================*/

static cui_platform_t saturn_platform = {
    .name = "saturn",

    .display = {
        .init        = saturn_display_init,
        .shutdown    = saturn_display_shutdown,
        .begin_frame = saturn_display_begin_frame,
        .end_frame   = saturn_display_end_frame,
        .draw_text   = saturn_display_draw_text,
        .draw_text_sprite = saturn_display_draw_text_sprite,
        .draw_rect   = saturn_display_draw_rect,
    },

    .input = {
        .init             = saturn_pal_input_init,
        .shutdown         = saturn_input_shutdown,
        .poll             = saturn_pal_input_poll,
        .get_action_label = saturn_input_get_action_label,
    },

    .storage = NULL,
};

/*============================================================================
 * Public API
 *============================================================================*/

const cui_platform_t* cui_saturn_platform(void) {
    saturn_platform.storage = cui_saturn_storage();
    return &saturn_platform;
}

void cui_saturn_set_resolution(cui_saturn_resolution_t resolution) {
    /* Only allow changing resolution before initialization */
    if (!g_initialized) {
        g_resolution = resolution;
    }
}

cui_saturn_resolution_t cui_saturn_get_resolution(void) {
    return g_resolution;
}

void cui_saturn_init(void) {
    cui_pal_register(cui_saturn_platform());
    cui_saturn_mark_initialized();
    cui_saturn_init_layout();
    cui_pal_init();
    cui_saturn_init_color_mapper();
}

void cui_saturn_mark_initialized(void) {
    g_initialized = true;
}

void cui_saturn_vdp1_flush_cmds(void) {
    saturn_vdp1_flush_cmds();
}

void cui_saturn_vdp1_activate(void) {
    saturn_vdp1_activate();
}

cui_input_action_t cui_saturn_poll_input(void) {
    return saturn_pal_input_poll();
}

void cui_saturn_update_input(void) {
    if (!g_input_initialized || !Per_Connect1) return;

    saturn_input_state_t state = {
        .data = Smpc_Peripheral[0].data,
    };
    saturn_input_update(&g_input, &state);
    g_input_needs_update = false;
}

/*============================================================================
 * Multi-Font Public API
 *============================================================================*/

int cui_saturn_font_register(const saturn_font_desc_t* desc)
{
    return saturn_font_register(&g_font_registry, desc);
}

void cui_saturn_font_set_active(int font_index)
{
    saturn_font_set_active(&g_font_registry, font_index);
}

int cui_saturn_font_get_active(void)
{
    return g_font_registry.active;
}

const saturn_font_registry_t* cui_saturn_font_get_registry(void)
{
    return &g_font_registry;
}

void cui_saturn_font_upload_all(void)
{
    saturn_vdp1_upload_fonts(&g_font_registry);
}

/*============================================================================
 * Integration Notes
 *============================================================================*/

/*
 * USAGE PATTERN (bare SGL):
 *
 * void main(void) {
 *     // Initialize SGL
 *     slInitSystem(TV_320x224, NULL, 1);
 *     slInitSynch();
 *
 *     // Initialize cui (one call does register + layout + PAL + color mapper)
 *     cui_saturn_init();
 *
 *     // Initialize cui components
 *     cui_button_init(&my_button, "Click Me", 10, 10);
 *
 *     // Main loop - slSynch() at END matches official SGL examples
 *     while (1) {
 *         cui_input_action_t action = cui_saturn_poll_input();
 *
 *         CUI_DISPLAY()->begin_frame(0);
 *         cui_button_handle(&my_button, action, &event);
 *         cui_button_render(&my_button, &theme);
 *         CUI_DISPLAY()->end_frame();
 *
 *         slSynch();  // Frame sync LAST -- updates Smpc_Peripheral for next frame
 *     }
 * }
 *
 * COLOR SYSTEM:
 * After cui_pal_init(), NBG0 is in 16-color mode with 8 palette banks:
 *   Bank 0: White text (default)
 *   Bank 1: Blue text (accent/focused)
 *   Bank 2: Yellow text (selected)
 *   Bank 3: Red text (error)
 *   Bank 4: Green text (success)
 *   Bank 5: Yellow/orange text (warning)
 *   Bank 6: Gray text (disabled/muted)
 *   Bank 7: Inverted (dark text on white background)
 *
 * Components pass RGBA colors to draw_text(), which maps them to the
 * nearest palette slot via saturn_rgba_to_palette_slot().
 *
 * IMPORTANT: After calling cui_pal_init(), do NOT use slPrint()!
 * The font format has changed from 8bpp to 4bpp, so slPrint would
 * produce garbled output. Use draw_text() exclusively.
 *
 * KEY DETAIL -- Smpc_Peripheral declaration:
 * SGL's SL_DEF.H:1499 declares: extern PerDigital* Smpc_Peripheral;
 * This is a POINTER, not an array. Using [] instead of * generates
 * completely different machine code -- [] reads raw pointer bytes as
 * struct data, while * correctly follows the pointer indirection.
 *
 * KEY DETAIL -- Input uses .data with manual edge detection:
 * Do NOT use Smpc_Peripheral[0].push -- no official SGL example uses
 * it (checked FLYING/AICTRL.C, BIPLANE/MAIN.C) and it is unreliable
 * in SGL 3.02j. Instead, follow the proven AICTRL.C pattern:
 *   current = ~(Smpc_Peripheral[0].data);  // invert active-low
 *   pressed = current & ~old;               // edge detection
 */
