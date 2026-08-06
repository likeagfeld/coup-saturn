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

/* One bit per queue slot: set means the slot's VRAM contents were written by
 * an external owner (see saturn_vdp1_reserve_cmd_slot) and the flush must not
 * publish the empty RAM entry over them. A bitmap rather than a list because
 * the flush has to answer "is this slot external?" once per slot per frame,
 * and a linear scan of a list would make that O(slots * reservations).
 * 257 bytes of BSS, cleared once per frame. */
static uint8_t g_cmd_external[(SATURN_VDP1_MAX_CMDS + 8) / 8];

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

    /* Colour: RGB555 direct colour.
     *
     * Bit 15 MUST be set. VDP1 writes this value straight into the frame
     * buffer, and VDP2 decides per pixel how to read it: MSB=1 means RGB
     * code, MSB=0 means a palette code to be looked up in CRAM
     * (ST-013-R3 section 2.1; VDP2 sprite-data handling).
     *
     * MEASURED: without the MSB, the brass frames rendered as pale grey and
     * a band of pure yellow appeared over the portraits - VDP2 was indexing
     * CRAM with our colour value rather than displaying it. A CRAM dump
     * from a savestate contained no 0x03FF entry anywhere, which is what
     * ruled out a palette bug and pointed here. */
    cmd->colr = (uint16_t)(rgb555 | 0x8000u);

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
    memset(g_cmd_external, 0, sizeof(g_cmd_external));
}

/*============================================================================
 * Externally-written command slots
 *============================================================================*/

uint32_t saturn_vdp1_cmd_slot_addr(int index)
{
    if (index < 0 || index > SATURN_VDP1_MAX_CMDS) {
        return 0;
    }
    return (uint32_t)SATURN_VDP1_CMD_OFFSET
           + (uint32_t)index * SATURN_VDP1_CMD_SIZE;
}

bool saturn_vdp1_slot_is_external(int index)
{
    if (index < 0 || index > SATURN_VDP1_MAX_CMDS) {
        return false;
    }
    return (g_cmd_external[index >> 3] & (uint8_t)(1u << (index & 7))) != 0;
}

#ifdef __SATURN__
/**
 * Spin until VDP1 reports the current frame's drawing complete.
 *
 * CEF (EDSR bit 1) = 1 means the command list has been walked to its END, so
 * VRAM command slots are safe to rewrite until VDP1 auto-starts again at the
 * beginning of active display (PTMR=0x02). Shared by the flush and by slot
 * reservation so both write under the same guarantee.
 */
static void saturn_vdp1_wait_draw_end(void)
{
    while (!(*(volatile uint16_t*)(uintptr_t)SATURN_VDP1_EDSR
             & SATURN_VDP1_EDSR_CEF)) {
        /* VDP1 still drawing - wait */
    }
}
#endif

uint32_t saturn_vdp1_reserve_cmd_slot(void)
{
    int index;

    if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
        return 0;
    }

    index = g_vdp1_state.cmd_count++;
    g_cmd_external[index >> 3] |= (uint8_t)(1u << (index & 7));

    /* Keep the RAM queue's high-water mark in step. The entry itself is never
     * published (the flush skips external slots) but the mark is what puts
     * saturn_vdp1_end_frame()'s END command AFTER this slot rather than on
     * top of it. */
    memset(&g_cmd_buffer[index], 0, sizeof(g_cmd_buffer[index]));
    if (index >= g_cmd_buffer_count) {
        g_cmd_buffer_count = index + 1;
    }

#ifdef __SATURN__
    {
        volatile saturn_vdp1_cmd_t* slot;

        saturn_vdp1_wait_draw_end();

        /* Pre-fill with a no-op so an unwritten reservation cannot leave last
         * frame's command on a live chain. LOCAL_COORD(0,0) only re-states the
         * origin saturn_vdp1_activate() already set at slot 2. */
        slot = (volatile saturn_vdp1_cmd_t*)(uintptr_t)(
            SATURN_VDP1_VRAM + saturn_vdp1_cmd_slot_addr(index));
        slot->ctrl = VDP1_CMD_LOCAL_COORD;
        slot->link = 0;
        slot->xa = 0;
        slot->ya = 0;
    }
#endif

    return saturn_vdp1_cmd_slot_addr(index);
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
    saturn_vdp1_wait_draw_end();

    /* Write all buffered commands to VDP1 VRAM at slot 4+ (offset 0x80). */
    for (i = 0; i < g_cmd_buffer_count; i++) {
        volatile uint16_t* dst;
        const uint16_t* src;

        /* A reserved slot already holds its owner's command, written direct
         * to VRAM. Publishing the (empty) RAM entry here would erase it -
         * which is the whole reason the reservation is tracked. */
        if (saturn_vdp1_slot_is_external(i)) {
            continue;
        }

        dst = (volatile uint16_t*)(uintptr_t)(
            SATURN_VDP1_VRAM + SATURN_VDP1_CMD_OFFSET
            + (uint32_t)i * SATURN_VDP1_CMD_SIZE);
        src = (const uint16_t*)&g_cmd_buffer[i];
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

/*============================================================================
 * Gouraud Shading
 *============================================================================*/

uint16_t saturn_vdp1_gouraud_word(int dr, int dg, int db)
{
    int r = SATURN_VDP1_GRD_NEUTRAL + dr;
    int g = SATURN_VDP1_GRD_NEUTRAL + dg;
    int b = SATURN_VDP1_GRD_NEUTRAL + db;

    if (r < 0) {
        r = 0;
    } else if (r > 0x1F) {
        r = 0x1F;
    }
    if (g < 0) {
        g = 0;
    } else if (g > 0x1F) {
        g = 0x1F;
    }
    if (b < 0) {
        b = 0;
    } else if (b > 0x1F) {
        b = 0x1F;
    }

    /* Saturn RGB555: 0BBBBBGGGGGRRRRR. The MSB is ignored for gouraud
     * entries (ST-013-R3 section 5.3). */
    return (uint16_t)(((uint16_t)b << 10) | ((uint16_t)g << 5) | (uint16_t)r);
}

void saturn_vdp1_gouraud_vshade(uint16_t out[4], int top, int bottom)
{
    uint16_t hi, lo;

    if (!out) return;

    /* Equal correction on all three channels preserves hue; an uneven one
     * would tint the panel as it shades. */
    hi = saturn_vdp1_gouraud_word(top, top, top);
    lo = saturn_vdp1_gouraud_word(bottom, bottom, bottom);

    out[0] = hi;  /* A - upper left  */
    out[1] = hi;  /* B - upper right */
    out[2] = lo;  /* C - lower right */
    out[3] = lo;  /* D - lower left  */
}

uint32_t saturn_vdp1_gouraud_slot_addr(int slot)
{
    if (slot < 0) slot = 0;
    if (slot >= SATURN_VDP1_GRD_MAX) slot = SATURN_VDP1_GRD_MAX - 1;
    return SATURN_VDP1_GRD_POOL + (uint32_t)slot * SATURN_VDP1_GRD_SIZE;
}

void saturn_vdp1_set_gouraud_table(int slot, const uint16_t colors[4])
{
#ifdef __SATURN__
    volatile uint16_t* dst;
    int i;

    if (!colors || slot < 0 || slot >= SATURN_VDP1_GRD_MAX) return;

    dst = (volatile uint16_t*)(uintptr_t)(SATURN_VDP1_VRAM
                                          + saturn_vdp1_gouraud_slot_addr(slot));
    for (i = 0; i < 4; i++) {
        dst[i] = colors[i];
    }
#else
    (void)slot; (void)colors;
#endif
}

bool saturn_vdp1_draw_rect_gouraud(int x, int y, int w, int h,
                                   uint16_t rgb555, int slot)
{
    saturn_vdp1_cmd_t cmd;

    if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
        return false;
    }

    saturn_vdp1_encode_polygon(&cmd, x, y, w, h, rgb555);

    /* Gouraud is write-only, so this costs no extra fill time. CMDGRDA is a
     * VRAM byte address divided by 8 (ST-013-R3 section 6.8). */
    cmd.pmod = SATURN_VDP1_RECT_GRD_PMOD;
    cmd.grda = (uint16_t)(saturn_vdp1_gouraud_slot_addr(slot) / 8);

    saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);
    g_vdp1_state.cmd_count++;
    return true;
}

/*============================================================================
 * Gouraud-Shaded Textured Sprite
 *
 * Same colour-calculation bits as saturn_vdp1_draw_rect_gouraud, but on a
 * Normal Sprite (textured) command instead of a flat-colour polygon. The
 * texture MUST be RGB555 (colour mode 5) - see SATURN_VDP1_SPR_GRD_PMOD in
 * saturn_vdp1.h for the ST-013-R3 citation on why the existing 4bpp
 * colour-bank sprite path (SATURN_VDP1_SPR_PMOD) cannot be reused here.
 *============================================================================*/

void saturn_vdp1_encode_sprite_gouraud(saturn_vdp1_cmd_t* cmd,
                                        int x, int y, int w, int h,
                                        uint32_t tex_offset, int slot)
{
    if (!cmd) return;

    memset(cmd, 0, sizeof(saturn_vdp1_cmd_t));

    /* Normal Sprite command, same as saturn_vdp1_encode_sprite */
    cmd->ctrl = VDP1_CMD_NORMAL_SPRITE;

    /* Draw mode: ECD disable + RGB colour mode (5) + gouraud colour calc */
    cmd->pmod = SATURN_VDP1_SPR_GRD_PMOD;

    /* CMDCOLR is ignored for a textured part in RGB mode
     * (ST-013-R3 6.4 Table 6.3 / VDP1_Manual.txt:4201-4211). There is no
     * CRAM bank in RGB mode, so leave it deterministically zero rather
     * than carry a stale/meaningless value. */
    cmd->colr = 0x0000;

    /* Texture address in VDP1 VRAM, divided by 8 - identical encoding to
     * saturn_vdp1_encode_sprite. The texture data itself must be RGB555
     * words, not 4bpp bank indices. */
    cmd->srca = (uint16_t)(tex_offset / 8);

    /* Size: high byte = width/8, low byte = height */
    cmd->size = (uint16_t)(((w / 8) << 8) | h);

    /* Position (top-left corner for normal sprites) */
    cmd->xa = (int16_t)x;
    cmd->ya = (int16_t)y;

    /* Gouraud table address, divided by 8 - same pool, same encoding as
     * saturn_vdp1_draw_rect_gouraud (ST-013-R3 6.8). Gouraud is
     * write-only, so this costs no extra fill time over the plain sprite
     * path (the framebuffer is never read back for this colour-calc mode). */
    cmd->grda = (uint16_t)(saturn_vdp1_gouraud_slot_addr(slot) / 8);
}

bool saturn_vdp1_draw_sprite_gouraud(int x, int y, int w, int h,
                                      uint32_t tex_offset, int slot)
{
    saturn_vdp1_cmd_t cmd;

    if (!saturn_vdp1_check_budget(&g_vdp1_state)) {
        return false;
    }

    saturn_vdp1_encode_sprite_gouraud(&cmd, x, y, w, h, tex_offset, slot);
    saturn_vdp1_write_cmd(g_vdp1_state.cmd_count, &cmd);
    g_vdp1_state.cmd_count++;
    return true;
}
