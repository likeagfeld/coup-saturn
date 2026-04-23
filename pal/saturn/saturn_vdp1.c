/**
 * saturn_vdp1.c - VDP1 Pixel-Accurate Rectangle Implementation
 *
 * Renders pixel-accurate rectangles using VDP1 polygon commands.
 * No texture needed - uses RGB555 direct color mode.
 */

#include "saturn_vdp1.h"
#include "saturn_font.h"
#include "sgl_defs.h"
#include <string.h>
#include <stdint.h>

/*============================================================================
 * State
 *============================================================================*/

static saturn_vdp1_state_t g_vdp1_state = {
    .cmd_count = 0,
    .initialized = false
};

/* RAM-side command buffer.
 * Commands are written here during rendering, then flushed to VDP1 VRAM
 * after slSynch() returns. This prevents SGL from overwriting our
 * commands with its own END command at offset 0x40. */
static saturn_vdp1_cmd_t g_cmd_buffer[SATURN_VDP1_MAX_CMDS + 1];
static int g_cmd_buffer_count = 0;

/*============================================================================
 * Testable Functions (Software Logic)
 *============================================================================*/

void saturn_vdp1_encode_polygon(saturn_vdp1_cmd_t* cmd, int x, int y, int w, int h, uint16_t rgb555)
{
    if (!cmd) return;

    /* Zero the structure */
    memset(cmd, 0, sizeof(saturn_vdp1_cmd_t));

    /* Command type: POLYGON */
    cmd->ctrl = VDP1_CMD_POLYGON;

    /* Link: unused for draw mode */
    cmd->link = 0x0000;

    /* Draw mode: ECD disable + SPD opaque + RGB direct color */
    cmd->pmod = SATURN_VDP1_RECT_PMOD;

    /* Color: RGB555 direct color */
    cmd->colr = rgb555;

    /* Polygon doesn't use texture, so srca and size are 0 */
    cmd->srca = 0x0000;
    cmd->size = 0x0000;

    /* Vertices: clockwise from top-left (exclusive far edge, matching SGL convention)
     * A = top-left (x, y)
     * B = top-right (x+w, y)
     * C = bottom-right (x+w, y+h)
     * D = bottom-left (x, y+h) */
    cmd->xa = (int16_t)x;
    cmd->ya = (int16_t)y;
    cmd->xb = (int16_t)(x + w);
    cmd->yb = (int16_t)y;
    cmd->xc = (int16_t)(x + w);
    cmd->yc = (int16_t)(y + h);
    cmd->xd = (int16_t)x;
    cmd->yd = (int16_t)(y + h);

    /* Gouraud shading: unused */
    cmd->grda = 0x0000;
}

void saturn_vdp1_encode_end(saturn_vdp1_cmd_t* cmd)
{
    if (!cmd) return;

    /* Zero the structure */
    memset(cmd, 0, sizeof(saturn_vdp1_cmd_t));

    /* END command */
    cmd->ctrl = VDP1_CMD_END;
}

void saturn_vdp1_begin_frame_internal(saturn_vdp1_state_t* state)
{
    if (!state) return;
    state->cmd_count = 0;
}

bool saturn_vdp1_check_budget(const saturn_vdp1_state_t* state)
{
    if (!state) return false;
    return state->cmd_count < SATURN_VDP1_MAX_CMDS;
}

int saturn_vdp1_get_count(const saturn_vdp1_state_t* state)
{
    if (!state) return 0;
    return state->cmd_count;
}

/*============================================================================
 * Hardware Functions (VRAM Writes)
 *============================================================================*/

/**
 * Buffer a command for later flush to VDP1 VRAM.
 * Commands are stored in RAM during rendering, then flushed to VRAM
 * via saturn_vdp1_flush() after slSynch() completes.
 */
static void saturn_vdp1_write_cmd(int index, const saturn_vdp1_cmd_t* cmd)
{
    if (!cmd || index < 0 || index > SATURN_VDP1_MAX_CMDS) return;

    g_cmd_buffer[index] = *cmd;
    if (index >= g_cmd_buffer_count) {
        g_cmd_buffer_count = index + 1;
    }
}

/*============================================================================
 * Sprite Support (4bpp Textured)
 *============================================================================*/

void saturn_vdp1_upload_texture(uint32_t offset, const uint8_t* data, uint32_t size)
{
#ifdef __SATURN__
    volatile uint8_t* dst = (volatile uint8_t*)(uintptr_t)(SATURN_VDP1_TEX_BASE + offset);
    uint32_t i;
    for (i = 0; i < size; i++) {
        dst[i] = data[i];
    }
#else
    (void)offset; (void)data; (void)size;
#endif
}

void saturn_vdp1_upload_palette(int bank, const uint16_t* colors)
{
#ifdef __SATURN__
    /* VDP2 CRAM: each bank = 16 colors * 2 bytes = 32 bytes */
    volatile uint16_t* cram = (volatile uint16_t*)(uintptr_t)(0x25F00000 + bank * 32);
    int i;
    for (i = 0; i < 16; i++) {
        cram[i] = colors[i];
    }
#else
    (void)bank; (void)colors;
#endif
}

static void saturn_vdp1_encode_sprite(saturn_vdp1_cmd_t* cmd,
                                       int x, int y, int w, int h,
                                       uint32_t tex_offset, int cram_bank)
{
    if (!cmd) return;

    memset(cmd, 0, sizeof(saturn_vdp1_cmd_t));

    /* Normal Sprite command */
    cmd->ctrl = VDP1_CMD_NORMAL_SPRITE;

    /* 4bpp, color index 0 = transparent */
    cmd->pmod = SATURN_VDP1_SPR_PMOD;

    /* Color bank: bank number in bits [10:4] */
    cmd->colr = (uint16_t)(cram_bank << 4);

    /* Texture address in VDP1 VRAM, divided by 8 */
    cmd->srca = (uint16_t)(tex_offset / 8);

    /* Size: high byte = width/8, low byte = height */
    cmd->size = (uint16_t)(((w / 8) << 8) | h);

    /* Position (top-left corner for normal sprites) */
    cmd->xa = (int16_t)x;
    cmd->ya = (int16_t)y;
}

bool saturn_vdp1_draw_sprite(int x, int y, int w, int h,
                              uint32_t tex_offset, int cram_bank)
{
    saturn_vdp1_cmd_t cmd;
    if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
        return false;
    }

    saturn_vdp1_encode_sprite(&cmd, x, y, w, h, tex_offset, cram_bank);
    saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);
    g_vdp1_state.cmd_count++;

    return true;
}

bool saturn_vdp1_draw_sprite_scaled(int x, int y,
                                     int src_w, int src_h,
                                     int dst_w, int dst_h,
                                     uint32_t tex_offset, int cram_bank)
{
    saturn_vdp1_cmd_t cmd;
    if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
        return false;
    }

    memset(&cmd, 0, sizeof(cmd));

    /* Distorted Sprite: 4 explicit corners, widely supported */
    cmd.ctrl = VDP1_CMD_DISTORTED_SPRITE;
    cmd.pmod = SATURN_VDP1_SPR_PMOD;
    cmd.colr = (uint16_t)(cram_bank << 4);
    cmd.srca = (uint16_t)(tex_offset / 8);
    cmd.size = (uint16_t)(((src_w / 8) << 8) | src_h);

    /* Four corners: A=top-left, B=top-right, C=bottom-right, D=bottom-left */
    cmd.xa = (int16_t)x;
    cmd.ya = (int16_t)y;
    cmd.xb = (int16_t)(x + dst_w - 1);
    cmd.yb = (int16_t)y;
    cmd.xc = (int16_t)(x + dst_w - 1);
    cmd.yc = (int16_t)(y + dst_h - 1);
    cmd.xd = (int16_t)x;
    cmd.yd = (int16_t)(y + dst_h - 1);

    saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);
    g_vdp1_state.cmd_count++;

    return true;
}

/*============================================================================
 * Font Texture Support
 *============================================================================*/

uint32_t saturn_vdp1_font_tex_offset(int char_index)
{
    return SATURN_VDP1_TEX_OFFSET + (uint32_t)char_index * SATURN_VDP1_FONT_CHAR_SIZE;
}

void saturn_vdp1_encode_font_sprite(saturn_vdp1_cmd_t* cmd,
                                     int x, int y,
                                     int char_index, int cram_bank)
{
    if (!cmd) return;
    if (char_index < 0 || char_index >= SATURN_VDP1_FONT_CHAR_COUNT) return;

    uint32_t tex_offset = saturn_vdp1_font_tex_offset(char_index);
    saturn_vdp1_encode_sprite(cmd, x, y, 8, 8, tex_offset, cram_bank);
}

void saturn_vdp1_upload_font(void)
{
    const uint8_t* font_1bpp = saturn_font_get_builtin();
    int ch;

    for (ch = 0; ch < SATURN_VDP1_FONT_CHAR_COUNT; ch++) {
        uint8_t char_data[SATURN_VDP1_FONT_CHAR_SIZE];
        int row;

        for (row = 0; row < 8; row++) {
            uint8_t src_byte = font_1bpp[ch * 8 + row];
            saturn_font_convert_row(src_byte, &char_data[row * 4], 1);
        }

        saturn_vdp1_upload_texture(
            (uint32_t)ch * SATURN_VDP1_FONT_CHAR_SIZE,
            char_data,
            SATURN_VDP1_FONT_CHAR_SIZE
        );
    }
}

int saturn_vdp1_draw_text(int x, int y, const char* text, int len, int cram_bank)
{
    int i;

    for (i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)text[i];

        /* Skip spaces — transparent VDP2 background shows through */
        if (ch == ' ') continue;

        /* Clamp non-printable to space (skip) */
        if (ch < 0x20 || ch > 0x7E) continue;

        /* Check budget before encoding */
        if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
            return i;  /* Budget exhausted mid-string */
        }

        int char_index = ch - 0x20;  /* ASCII 32 = index 0 */
        saturn_vdp1_cmd_t cmd;
        saturn_vdp1_encode_font_sprite(&cmd, x + i * 8, y, char_index, cram_bank);
        saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);
        g_vdp1_state.cmd_count++;
    }

    return len;
}

/*============================================================================
 * Public API
 *============================================================================*/

void saturn_vdp1_init(void)
{
#ifdef __SATURN__
    /* Configure VDP1/VDP2 sprite system via SGL.
     * slSpriteColMode(SPR_PAL_RGB) sets SPCLMD=1 in VDP2 SPCTL,
     * enabling RGB555 direct color interpretation for VDP1 output.
     * Without this, RGB555 polygon colors would be misinterpreted. */
    slSpriteColMode(SPR_PAL_RGB);
    slSpriteType(0);
    slPrioritySpr0(4);  /* VDP1 priority 4 (below NBG0 text at 5) */
#endif

    g_vdp1_state.initialized = true;
    g_vdp1_state.cmd_count = 0;
}

void saturn_vdp1_begin_frame(void)
{
    saturn_vdp1_begin_frame_internal(&g_vdp1_state);
    g_cmd_buffer_count = 0;
}

void saturn_vdp1_flush_cmds(void)
{
#ifdef __SATURN__
    int i, w;

    if (g_cmd_buffer_count == 0) return;

    /* === Phase 1: Bulk-write commands to VDP1 VRAM (before slSynch) ===
     *
     * Safe to call during active display because VDP1 has already
     * finished drawing the current frame by the time game logic
     * completes.  We verify this via EDSR (Draw End Status Register):
     * CEF (bit 1) = 1 means current frame drawing is complete.
     *
     * After this call, slots 4+ contain our draw commands + END.
     * Slot 3 still has SGL's END from the previous slSynch —
     * VDP1 won't reach our commands until we activate them.
     */

    /* Wait for VDP1 to finish drawing current frame.
     * Spin-read EDSR until CEF=1.  Typically VDP1 finishes well
     * before our game logic completes, so this rarely spins. */
    while (!(*(volatile uint16_t*)(uintptr_t)SATURN_VDP1_EDSR
             & SATURN_VDP1_EDSR_CEF)) {
        /* VDP1 still drawing — wait */
    }

    /* Write all buffered commands to VDP1 VRAM at slot 4+ (offset 0x80). */
    for (i = 0; i < g_cmd_buffer_count; i++) {
        volatile uint16_t* dst = (volatile uint16_t*)(uintptr_t)(
            SATURN_VDP1_VRAM + SATURN_VDP1_CMD_OFFSET
            + (uint32_t)i * SATURN_VDP1_CMD_SIZE);
        const uint16_t* src = (const uint16_t*)&g_cmd_buffer[i];
        for (w = 0; w < SATURN_VDP1_CMD_SIZE / 2; w++) {
            dst[w] = src[w];
        }
    }

    /* Configure VDP1 erase registers. */
    *(volatile uint16_t*)(uintptr_t)SATURN_VDP1_EWDR = 0x0000;
    *(volatile uint16_t*)(uintptr_t)SATURN_VDP1_EWLR = 0x0000;
    *(volatile uint16_t*)(uintptr_t)SATURN_VDP1_EWRR =
        (uint16_t)(((319 >> 3) << 9) | 223);
#endif
}

void saturn_vdp1_activate(void)
{
#ifdef __SATURN__
    /* === Phase 2: Patch slot 2 with JUMP to skip slot 3 (after slSynch) ===
     *
     * SGL's slSynch() writes END to slot 3 every frame.  Instead of
     * patching slot 3 (race with VDP1 auto-start), we overwrite slot 2
     * to include a JUMP that skips slot 3 entirely:
     *
     *   Slot 0: SGL SysClip  → next
     *   Slot 1: SGL UserClip → next
     *   Slot 2: LOCAL_COORD(0,0) + JP=ASSIGN → jump to slot 4
     *   Slot 3: SGL END      → SKIPPED (VDP1 never reads this)
     *   Slot 4: Our first draw command
     *   ...
     *   Slot N: Our END command
     *
     * Only 4 writes (~200ns total), guaranteed to complete within
     * vblank before VDP1 auto-starts at active display (PTMR=0x02).
     *
     * If no commands were buffered, skip activation — VDP1 will hit
     * SGL's END at slot 3 and draw nothing (correct for empty frames).
     */
    volatile saturn_vdp1_cmd_t* slot2;

    if (g_cmd_buffer_count == 0) return;

    slot2 = (volatile saturn_vdp1_cmd_t*)(uintptr_t)(
        SATURN_VDP1_VRAM + 2 * SATURN_VDP1_CMD_SIZE);

    /* Write link target FIRST (data-before-trigger pattern).
     * Slot 4 byte address = 0x80, CMDLINK = byte_addr / 8 = 0x10.
     * Then write LOCAL_COORD + JP=ASSIGN to ctrl — this is the
     * "trigger" that enables the jump. */
    slot2->link = (uint16_t)(SATURN_VDP1_CMD_OFFSET / 8);
    slot2->xa = 0;
    slot2->ya = 0;
    slot2->ctrl = VDP1_CMD_LOCAL_COORD | VDP1_JP_ASSIGN;
#endif
}

bool saturn_vdp1_draw_rect(int x, int y, int w, int h, uint16_t rgb555)
{
    saturn_vdp1_cmd_t cmd;

    /* Check budget */
    if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
        return false;  /* Budget exceeded, caller should use VDP2 fallback */
    }

    /* Encode the polygon command */
    saturn_vdp1_encode_polygon(&cmd, x, y, w, h, rgb555);

    /* Write to VDP1 VRAM */
    saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);

    /* Increment counter */
    g_vdp1_state.cmd_count++;

    return true;
}

void saturn_vdp1_end_frame(void)
{
    /* Write END command after our rectangle commands */
    saturn_vdp1_cmd_t end_cmd;
    saturn_vdp1_encode_end(&end_cmd);
    saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &end_cmd);

    /* VDP1 will process commands during V-BLANK (triggered by slSynch) */
}

int saturn_vdp1_get_command_count(void)
{
    return saturn_vdp1_get_count(&g_vdp1_state);
}

/*============================================================================
 * Multi-Font Support
 *============================================================================*/

void saturn_vdp1_encode_font_sprite_ext(saturn_vdp1_cmd_t* cmd,
                                         int x, int y,
                                         int char_index, int cram_bank,
                                         uint32_t vram_offset,
                                         int cell_w, int cell_h)
{
    if (!cmd) return;
    if (char_index < 0) return;

    /* Compute per-character 4bpp size: (cell_w * cell_h) / 2 */
    uint32_t char_4bpp = ((uint32_t)cell_w * (uint32_t)cell_h) / 2;
    uint32_t tex_offset = SATURN_VDP1_TEX_OFFSET + vram_offset
                          + (uint32_t)char_index * char_4bpp;

    saturn_vdp1_encode_sprite(cmd, x, y, cell_w, cell_h, tex_offset, cram_bank);
}

void saturn_vdp1_upload_fonts(const saturn_font_registry_t* reg)
{
    if (!reg) return;

    for (int f = 0; f < reg->count; f++) {
        const saturn_font_entry_t* entry = &reg->fonts[f];
        const saturn_font_desc_t* desc = &entry->desc;

        if (!desc->data_1bpp) continue;

        int bytes_per_row = desc->bytes_per_row_1bpp;
        if (bytes_per_row <= 0) bytes_per_row = desc->cell_width / 8;

        for (int ch = 0; ch < desc->char_count; ch++) {
            /* Temporary buffer for one character in 4bpp */
            uint8_t char_data[SATURN_FONT_MAX_CELL * SATURN_FONT_MAX_CELL / 2];
            int row;

            for (row = 0; row < desc->cell_height; row++) {
                const uint8_t* src = &desc->data_1bpp[
                    (ch * desc->cell_height + row) * bytes_per_row];
                uint8_t* dst = &char_data[row * bytes_per_row * 4];

                saturn_font_convert_row_wide(src, dst, bytes_per_row, 1);
            }

            saturn_vdp1_upload_texture(
                entry->vram_offset + (uint32_t)ch * entry->char_4bpp_size,
                char_data,
                entry->char_4bpp_size
            );
        }
    }
}

int saturn_vdp1_draw_text_font(int x, int y, const char* text, int len,
                                int cram_bank, const saturn_font_entry_t* entry)
{
    int i;

    if (!entry) return 0;

    int advance = entry->desc.advance_x;
    int first = entry->desc.first_char;
    int count = entry->desc.char_count;

    for (i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)text[i];

        /* Skip spaces */
        if (ch == ' ') continue;

        /* Clamp non-printable to skip */
        if (ch < first || ch >= first + count) continue;

        if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
            return i;
        }

        int char_index = ch - first;
        saturn_vdp1_cmd_t cmd;
        saturn_vdp1_encode_font_sprite_ext(&cmd, x + i * advance, y,
                                            char_index, cram_bank,
                                            entry->vram_offset,
                                            entry->desc.cell_width,
                                            entry->desc.cell_height);
        saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);
        g_vdp1_state.cmd_count++;
    }

    return len;
}

uint32_t saturn_vdp1_get_font_end_offset(const saturn_font_registry_t* reg)
{
    if (!reg) return 0;

    /* Return cursor aligned to 8 bytes */
    return (reg->vram_cursor + 7) & ~(uint32_t)7;
}
