/**
 * saturn_vdp1.h - VDP1 Pixel-Accurate Rectangle Rendering
 *
 * Provides pixel-accurate rectangle drawing using VDP1's polygon engine.
 * Uses RGB555 direct color mode for any color without palette constraints.
 *
 * Rendering approach:
 * - VDP1 polygon commands (4-vertex filled quads)
 * - RGB555 direct color (no texture needed)
 * - 2048 command budget per frame (rects + text sprites)
 * - Priority 4 (below NBG0 text layer at priority 5)
 *
 * Fallback strategy:
 * - When budget exceeded, saturn_vdp1_draw_rect() returns false
 * - Caller falls back to VDP2 character-cell rendering
 */

#ifndef SATURN_VDP1_H
#define SATURN_VDP1_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * VDP1 Memory Layout
 *============================================================================*/

/* VDP1 VRAM base address */
#define SATURN_VDP1_VRAM        0x25C00000

/* VDP1 register addresses */
#define SATURN_VDP1_EWDR        0x25D00006  /* Erase/Write Data Register */
#define SATURN_VDP1_EWLR        0x25D00008  /* Erase/Write Upper-Left */
#define SATURN_VDP1_EWRR        0x25D0000A  /* Erase/Write Lower-Right */

/* Command table layout:
 * SGL owns slots 0-3 (4 commands x 32 bytes = 128 bytes):
 *   Slot 0 (0x00): System Clipping
 *   Slot 1 (0x20): User Clipping
 *   Slot 2 (0x40): Local Coordinate (set to screen center by SGL)
 *   Slot 3 (0x60): END (we patch this to LOCAL_COORD(0,0) after slSynch)
 * Our commands go to slot 4+ (offset 0x80). */
#define SATURN_VDP1_CMD_OFFSET  0x80    /* Our first command at slot 4 (after SGL's 4 system commands) */
#define SATURN_VDP1_CMD_SIZE    0x20    /* Each command is 32 bytes */
#define SATURN_VDP1_MAX_CMDS    2048    /* Max commands per frame (rects + text sprites) */
#define SATURN_VDP1_MAX_RECTS   SATURN_VDP1_MAX_CMDS  /* backward compat */

/*============================================================================
 * VDP1 Command Types
 *============================================================================*/

#define VDP1_CMD_NORMAL_SPRITE  0x0000
#define VDP1_CMD_SCALED_SPRITE  0x0001
#define VDP1_CMD_DISTORTED_SPRITE 0x0002
#define VDP1_CMD_POLYGON        0x0004
#define VDP1_CMD_POLYLINE       0x0005
#define VDP1_CMD_LINE           0x0006
#define VDP1_CMD_LOCAL_COORD    0x000A
#define VDP1_CMD_END            0x8000

/* Jump select bits (CMDCTRL bits 14-12).
 * These modify ANY command: after processing the command, VDP1
 * follows the jump instead of advancing sequentially. */
#define VDP1_JP_NEXT            0x0000  /* Process next command (default) */
#define VDP1_JP_ASSIGN          0x1000  /* Jump to CMDLINK address */

/* VDP1 status registers (read-only) */
#define SATURN_VDP1_EDSR        0x25D00010  /* Draw End Status Register */
#define SATURN_VDP1_EDSR_CEF    0x0002      /* Current frame End Flag */

/*============================================================================
 * VDP1 Draw Mode Bits (CMDPMOD)
 *============================================================================*/

/* MSB shadow */
#define VDP1_PMOD_MSB_ON        0x8000

/* High-speed shrink */
#define VDP1_PMOD_HSS_OFF       0x0000

/* Pre-clipping */
#define VDP1_PMOD_PCLP_INSIDE   0x0000

/* Mesh */
#define VDP1_PMOD_MESH_OFF      0x0000

/* End code disable */
#define VDP1_PMOD_ECD_DISABLE   0x0080

/* Sprite priority disable (all pixels opaque) */
#define VDP1_PMOD_SPD_OPAQUE    0x0040

/* Color modes (bits 5-3) */
#define VDP1_CMOD_BANK_16       (0 << 3)  /* 16-color bank mode */
#define VDP1_CMOD_LUTABLE       (1 << 3)  /* Lookup table */
#define VDP1_CMOD_BANK_64       (2 << 3)  /* 64-color bank */
#define VDP1_CMOD_BANK_128      (3 << 3)  /* 128-color bank */
#define VDP1_CMOD_BANK_256      (4 << 3)  /* 256-color bank */
#define VDP1_CMOD_RGB           (5 << 3)  /* RGB555 direct color */

/* Composite draw mode for our rectangles:
 * ECD disable + SPD opaque + RGB direct color mode */
#define SATURN_VDP1_RECT_PMOD   (VDP1_PMOD_ECD_DISABLE | VDP1_PMOD_SPD_OPAQUE | VDP1_CMOD_RGB)

/* Draw mode for 4bpp textured sprites:
 * ECD disable + color index 0 transparent + 16-color bank mode */
#define SATURN_VDP1_SPR_PMOD    (VDP1_PMOD_ECD_DISABLE | VDP1_CMOD_BANK_16)

/*============================================================================
 * VDP1 Texture VRAM Layout
 *============================================================================*/

/* Texture data starts after the command table.
 * Command table: OFFSET + MAX_CMDS * 32 bytes, rounded up to 4KB boundary. */
#define SATURN_VDP1_CMD_TABLE_END  (SATURN_VDP1_CMD_OFFSET + SATURN_VDP1_MAX_CMDS * SATURN_VDP1_CMD_SIZE)
#define SATURN_VDP1_TEX_OFFSET     (((SATURN_VDP1_CMD_TABLE_END) + 0xFFF) & ~0xFFF)
#define SATURN_VDP1_TEX_BASE    (SATURN_VDP1_VRAM + SATURN_VDP1_TEX_OFFSET)

/* Font texture layout in VDP1 VRAM texture area */
#define SATURN_VDP1_FONT_CHAR_SIZE    32   /* 8x8 @ 4bpp = 32 bytes per char */
#define SATURN_VDP1_FONT_CHAR_COUNT   95   /* ASCII 32-126 */
#define SATURN_VDP1_FONT_DATA_SIZE    (SATURN_VDP1_FONT_CHAR_COUNT * SATURN_VDP1_FONT_CHAR_SIZE)
#define SATURN_VDP1_APP_TEX_START     SATURN_VDP1_FONT_DATA_SIZE  /* app textures start after font */

/*============================================================================
 * VDP1 Command Structure
 *============================================================================*/

/**
 * VDP1 command structure (32 bytes).
 * Aligned to hardware layout for direct VRAM write.
 */
typedef struct saturn_vdp1_cmd {
    uint16_t ctrl;      /* +0x00: Command control word */
    uint16_t link;      /* +0x02: Link specification */
    uint16_t pmod;      /* +0x04: Draw mode word */
    uint16_t colr;      /* +0x06: Color control word */
    uint16_t srca;      /* +0x08: Character address (÷8) */
    uint16_t size;      /* +0x0A: Character size ((w/8)<<8 | h) */
    int16_t  xa;        /* +0x0C: Vertex A x */
    int16_t  ya;        /* +0x0E: Vertex A y */
    int16_t  xb;        /* +0x10: Vertex B x */
    int16_t  yb;        /* +0x12: Vertex B y */
    int16_t  xc;        /* +0x14: Vertex C x */
    int16_t  yc;        /* +0x16: Vertex C y */
    int16_t  xd;        /* +0x18: Vertex D x */
    int16_t  yd;        /* +0x1A: Vertex D y */
    uint16_t grda;      /* +0x1C: Gouraud shading table */
    uint16_t reserved;  /* +0x1E: Unused */
} saturn_vdp1_cmd_t;

/*============================================================================
 * VDP1 State
 *============================================================================*/

/**
 * Rectangle drawing state.
 */
typedef struct saturn_vdp1_state {
    int cmd_count;          /* Number of rects queued this frame */
    bool initialized;       /* VDP1 priority configured? */
} saturn_vdp1_state_t;

/*============================================================================
 * Public API (Hardware Path)
 *============================================================================*/

/**
 * Initialize VDP1 rectangle system.
 * Sets sprite priority to 4 (below NBG0 text at priority 5).
 * Call once during PAL initialization.
 */
void saturn_vdp1_init(void);

/**
 * Begin a new frame.
 * Resets rectangle command counter.
 * Call at start of each frame (before any draw_rect calls).
 */
void saturn_vdp1_begin_frame(void);

/**
 * Draw a filled rectangle using VDP1 polygon command.
 *
 * Uses 4-vertex polygon (no texture needed) with RGB555 direct color.
 * Vertices are ordered: top-left, top-right, bottom-right, bottom-left.
 *
 * @param x      X position (pixels)
 * @param y      Y position (pixels)
 * @param w      Width (pixels)
 * @param h      Height (pixels)
 * @param rgb555 Color in Saturn RGB555 format (0BBBBBGGGGGRRRRR)
 * @return true if drawn, false if budget exceeded (use VDP2 fallback)
 */
bool saturn_vdp1_draw_rect(int x, int y, int w, int h, uint16_t rgb555);

/**
 * End the current frame.
 * Writes END command to terminate VDP1 command list.
 * Call after all draw_rect calls, before slSynch().
 */
void saturn_vdp1_end_frame(void);

/**
 * Flush buffered VDP1 commands to VRAM.
 * Call BEFORE slSynch() — writes bulk draw commands to slots 4+
 * while VDP1 is idle (finished drawing current frame).
 * Waits for VDP1 draw completion (EDSR CEF) before writing.
 */
void saturn_vdp1_flush_cmds(void);

/**
 * Activate the flushed commands by patching SGL's command table.
 * Call AFTER slSynch() returns (during vblank).
 *
 * Overwrites SGL's slot 2 with LOCAL_COORD(0,0) + JP=ASSIGN to
 * jump to slot 4, skipping SGL's END at slot 3 entirely.
 * Only 4 register writes (~200ns), guaranteed to complete before
 * VDP1 auto-starts at the beginning of active display (PTMR=0x02).
 */
void saturn_vdp1_activate(void);

/**
 * Upload sprite texture data to VDP1 VRAM.
 *
 * Copies 4bpp pixel data from work RAM to the VDP1 texture area.
 *
 * @param offset   Byte offset within texture area (from SATURN_VDP1_TEX_OFFSET)
 * @param data     Source pixel data (4bpp packed)
 * @param size     Number of bytes to copy
 */
void saturn_vdp1_upload_texture(uint32_t offset, const uint8_t* data, uint32_t size);

/**
 * Upload a 16-color palette to VDP2 CRAM for VDP1 sprite use.
 *
 * @param bank     CRAM bank number (0-127). Use banks >= 16 to avoid
 *                 conflicting with VDP2 text palettes (banks 0-15).
 * @param colors   16 RGB555 color values
 */
void saturn_vdp1_upload_palette(int bank, const uint16_t* colors);

/**
 * Draw a textured sprite using VDP1 Normal Sprite command.
 *
 * Uses 4bpp texture from VDP1 VRAM with 16-color CRAM bank.
 * Color index 0 = transparent.
 *
 * @param x            X position (pixels, top-left)
 * @param y            Y position (pixels, top-left)
 * @param w            Width (must be multiple of 8)
 * @param h            Height
 * @param tex_offset   Byte offset of texture in VDP1 VRAM (from base 0x25C00000)
 * @param cram_bank    CRAM palette bank number
 * @return true if drawn, false if budget exceeded
 */
bool saturn_vdp1_draw_sprite(int x, int y, int w, int h,
                             uint32_t tex_offset, int cram_bank);

/**
 * Draw a scaled sprite using VDP1 Scaled Sprite command.
 *
 * @param x            X position (pixels, top-left)
 * @param y            Y position (pixels, top-left)
 * @param src_w        Source texture width (must be multiple of 8)
 * @param src_h        Source texture height
 * @param dst_w        Display width (scaled)
 * @param dst_h        Display height (scaled)
 * @param tex_offset   Byte offset of texture in VDP1 VRAM
 * @param cram_bank    CRAM palette bank number
 * @return true if drawn, false if budget exceeded
 */
bool saturn_vdp1_draw_sprite_scaled(int x, int y,
                                     int src_w, int src_h,
                                     int dst_w, int dst_h,
                                     uint32_t tex_offset, int cram_bank);

/**
 * Upload the built-in font to VDP1 VRAM as 4bpp textures.
 * Converts 1bpp font data to 4bpp and uploads 95 characters (ASCII 32-126).
 * Call once during initialization, after saturn_vdp1_init().
 */
void saturn_vdp1_upload_font(void);

/**
 * Draw a text string using VDP1 Normal Sprite commands.
 *
 * Each character is rendered as an 8x8 sprite at pixel-accurate coordinates.
 * Space characters are skipped (transparent VDP2 background shows through).
 * Returns the number of characters processed; if budget is exhausted mid-string,
 * the caller can fall back to VDP2 for remaining characters.
 *
 * @param x          X position (pixels, top-left of first character)
 * @param y          Y position (pixels)
 * @param text       Text string to render
 * @param len        Number of characters to render
 * @param cram_bank  CRAM palette bank for text color
 * @return Number of characters processed (may be < len if budget exhausted)
 */
int saturn_vdp1_draw_text(int x, int y, const char* text, int len, int cram_bank);

/**
 * Get number of rectangles drawn this frame (diagnostic).
 * @return Number of rectangle commands written this frame
 */
int saturn_vdp1_get_command_count(void);

/*============================================================================
 * Testable API (Software Logic)
 *============================================================================*/

/**
 * Encode a polygon command into a struct (testable).
 * Builds the command structure without writing to VRAM.
 *
 * @param cmd    Output command structure
 * @param x      X position (pixels)
 * @param y      Y position (pixels)
 * @param w      Width (pixels)
 * @param h      Height (pixels)
 * @param rgb555 Color in Saturn RGB555 format
 */
void saturn_vdp1_encode_polygon(saturn_vdp1_cmd_t* cmd, int x, int y, int w, int h, uint16_t rgb555);

/**
 * Encode an END command (testable).
 */
void saturn_vdp1_encode_end(saturn_vdp1_cmd_t* cmd);

/**
 * Begin frame for state struct (testable).
 */
void saturn_vdp1_begin_frame_internal(saturn_vdp1_state_t* state);

/**
 * Check if budget allows another rectangle (testable).
 * @return true if budget available, false if exceeded
 */
bool saturn_vdp1_check_budget(const saturn_vdp1_state_t* state);

/**
 * Get command count from state (testable).
 */
int saturn_vdp1_get_count(const saturn_vdp1_state_t* state);

/**
 * Calculate VDP1 VRAM byte offset for a font character texture.
 *
 * @param char_index  Index into font (0 = space, 33 = 'A', 94 = '~')
 * @return Absolute byte offset in VDP1 VRAM (TEX_OFFSET + char_index * 32)
 */
uint32_t saturn_vdp1_font_tex_offset(int char_index);

/**
 * Encode a Normal Sprite command for a font character (testable).
 *
 * @param cmd         Output command structure
 * @param x           X position (pixels)
 * @param y           Y position (pixels)
 * @param char_index  Font character index (0-94)
 * @param cram_bank   CRAM palette bank number
 */
void saturn_vdp1_encode_font_sprite(saturn_vdp1_cmd_t* cmd,
                                     int x, int y,
                                     int char_index, int cram_bank);

/*============================================================================
 * Multi-Font Support
 *============================================================================*/

/* Forward declaration — full definition in saturn_font.h */
struct saturn_font_registry;
struct saturn_font_entry;

/**
 * Upload all registered fonts to VDP1 VRAM.
 * Converts 1bpp font data to 4bpp and uploads each font at its
 * computed VRAM offset.
 *
 * @param reg  Font registry with registered fonts
 */
void saturn_vdp1_upload_fonts(const struct saturn_font_registry* reg);

/**
 * Draw text using a specific font entry (multi-font path).
 *
 * @param x          X position (pixels)
 * @param y          Y position (pixels)
 * @param text       Text string
 * @param len        Number of characters
 * @param cram_bank  CRAM palette bank
 * @param entry      Font entry with VRAM offset and dimensions
 * @return Number of characters processed
 */
int saturn_vdp1_draw_text_font(int x, int y, const char* text, int len,
                                int cram_bank, const struct saturn_font_entry* entry);

/**
 * Encode a Normal Sprite command for a font character with extended
 * dimensions (testable). Supports any cell size, not just 8x8.
 *
 * @param cmd          Output command structure
 * @param x            X position (pixels)
 * @param y            Y position (pixels)
 * @param char_index   Character index within the font
 * @param cram_bank    CRAM palette bank number
 * @param vram_offset  Base VRAM offset of the font's texture data
 * @param cell_w       Cell width (must be multiple of 8)
 * @param cell_h       Cell height
 */
void saturn_vdp1_encode_font_sprite_ext(saturn_vdp1_cmd_t* cmd,
                                         int x, int y,
                                         int char_index, int cram_bank,
                                         uint32_t vram_offset,
                                         int cell_w, int cell_h);

/**
 * Get the VRAM offset after all registered fonts.
 * Multi-font apps should use this instead of SATURN_VDP1_APP_TEX_START
 * to find where application textures can begin.
 *
 * @param reg  Font registry
 * @return VRAM cursor (byte offset in texture area), 8-byte aligned
 */
uint32_t saturn_vdp1_get_font_end_offset(const struct saturn_font_registry* reg);

#endif /* SATURN_VDP1_H */
