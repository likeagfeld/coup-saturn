/**
 * test_vdp1_cmd_slot.c - Reserving a VDP1 command slot for an external writer.
 *
 * WHY THIS EXISTS
 *   saturn_distort_draw_flip() and saturn_distort_draw_mesh_dissolve() take an
 *   explicit `cmd_vram_offset` and publish their 32-byte command straight to
 *   VDP1 VRAM, because saturn_vdp1.c's queue (g_cmd_buffer / g_cmd_buffer_count)
 *   is static to that file. Handing them an arbitrary offset is not enough:
 *   the offset has to be a slot VDP1 will actually WALK this frame, and one
 *   that saturn_vdp1_flush_cmds() will not then overwrite from the RAM buffer.
 *
 * THE CONTRACT UNDER TEST
 *   - the returned offset is a real command-table slot, 32-byte aligned
 *     (ST-013-R3 section 6.2: command tables sit on 20H boundaries and the
 *     lower 2 bits of CMDLINK are 00H - VDP1_Manual.txt:3295-3297)
 *   - reserving CONSUMES a queue position, so the next ordinary draw cannot
 *     land on top of the reserved command
 *   - the reserved slot is flagged external, which is what makes the flush
 *     skip it instead of publishing the (empty) RAM entry over it
 *   - the flag is per frame: begin_frame clears it
 *   - the budget is respected, and exhaustion is reported rather than
 *     scribbling past the command table into the gouraud pool
 */

#include "cui_test_framework.h"
#include "saturn_vdp1.h"

/*============================================================================
 * Address arithmetic (pure)
 *============================================================================*/

CUI_TEST(vdp1_slot_addr_is_the_documented_32_byte_grid)
{
    CUI_ASSERT_EQ(SATURN_VDP1_CMD_OFFSET, (int)saturn_vdp1_cmd_slot_addr(0));
    CUI_ASSERT_EQ(SATURN_VDP1_CMD_OFFSET + SATURN_VDP1_CMD_SIZE,
                  (int)saturn_vdp1_cmd_slot_addr(1));
    CUI_ASSERT_EQ(SATURN_VDP1_CMD_OFFSET + 7 * SATURN_VDP1_CMD_SIZE,
                  (int)saturn_vdp1_cmd_slot_addr(7));
}

CUI_TEST(vdp1_every_slot_address_is_32_byte_aligned)
{
    int i;
    for (i = 0; i < SATURN_VDP1_MAX_CMDS; i++) {
        CUI_ASSERT_EQ(0, (int)(saturn_vdp1_cmd_slot_addr(i) & 0x1F));
    }
}

CUI_TEST(vdp1_slot_addr_rejects_indices_outside_the_command_table)
{
    CUI_ASSERT_EQ(0, (int)saturn_vdp1_cmd_slot_addr(-1));
    CUI_ASSERT_EQ(0, (int)saturn_vdp1_cmd_slot_addr(SATURN_VDP1_MAX_CMDS + 1));
}

CUI_TEST(vdp1_the_last_slot_stays_clear_of_the_gouraud_pool)
{
    /* The gouraud pool lives in the gap between the command table and the
     * texture area (saturn_vdp1.h:128-150). A slot that ran past the end of
     * the table would corrupt a gradient rather than fail loudly. */
    uint32_t last = saturn_vdp1_cmd_slot_addr(SATURN_VDP1_MAX_CMDS);
    CUI_ASSERT_LE(last + SATURN_VDP1_CMD_SIZE, SATURN_VDP1_GRD_POOL);
}

/*============================================================================
 * Reservation
 *============================================================================*/

CUI_TEST(vdp1_a_reservation_returns_the_next_free_slot)
{
    uint32_t off;

    saturn_vdp1_begin_frame();
    off = saturn_vdp1_reserve_cmd_slot();

    CUI_ASSERT_EQ((int)saturn_vdp1_cmd_slot_addr(0), (int)off);
    CUI_ASSERT_EQ(1, saturn_vdp1_get_command_count());
}

CUI_TEST(vdp1_a_reservation_is_not_reused_by_the_next_draw)
{
    /* The whole point: an ordinary rect issued after a reservation must go to
     * the slot AFTER it, or the distorted sprite is silently overwritten. */
    uint32_t reserved;

    saturn_vdp1_begin_frame();
    saturn_vdp1_draw_rect(0, 0, 8, 8, 0x1234);
    reserved = saturn_vdp1_reserve_cmd_slot();
    saturn_vdp1_draw_rect(0, 0, 8, 8, 0x1234);

    CUI_ASSERT_EQ((int)saturn_vdp1_cmd_slot_addr(1), (int)reserved);
    CUI_ASSERT_EQ(3, saturn_vdp1_get_command_count());
}

CUI_TEST(vdp1_reservations_are_distinct)
{
    uint32_t a, b, c;

    saturn_vdp1_begin_frame();
    a = saturn_vdp1_reserve_cmd_slot();
    b = saturn_vdp1_reserve_cmd_slot();
    c = saturn_vdp1_reserve_cmd_slot();

    CUI_ASSERT_NEQ((int)a, (int)b);
    CUI_ASSERT_NEQ((int)b, (int)c);
    CUI_ASSERT_EQ((int)(b - a), SATURN_VDP1_CMD_SIZE);
    CUI_ASSERT_EQ((int)(c - b), SATURN_VDP1_CMD_SIZE);
}

CUI_TEST(vdp1_only_reserved_slots_are_flagged_external)
{
    saturn_vdp1_begin_frame();
    saturn_vdp1_draw_rect(0, 0, 8, 8, 0x1234);   /* slot 0 - ordinary */
    saturn_vdp1_reserve_cmd_slot();              /* slot 1 - external */
    saturn_vdp1_draw_rect(0, 0, 8, 8, 0x1234);   /* slot 2 - ordinary */

    CUI_ASSERT_FALSE(saturn_vdp1_slot_is_external(0));
    CUI_ASSERT_TRUE(saturn_vdp1_slot_is_external(1));
    CUI_ASSERT_FALSE(saturn_vdp1_slot_is_external(2));
}

CUI_TEST(vdp1_external_flags_do_not_survive_the_frame)
{
    saturn_vdp1_begin_frame();
    saturn_vdp1_reserve_cmd_slot();
    CUI_ASSERT_TRUE(saturn_vdp1_slot_is_external(0));

    saturn_vdp1_begin_frame();
    CUI_ASSERT_FALSE(saturn_vdp1_slot_is_external(0));
}

CUI_TEST(vdp1_slot_is_external_is_safe_outside_the_table)
{
    saturn_vdp1_begin_frame();
    CUI_ASSERT_FALSE(saturn_vdp1_slot_is_external(-1));
    CUI_ASSERT_FALSE(saturn_vdp1_slot_is_external(SATURN_VDP1_MAX_CMDS + 99));
}

CUI_TEST(vdp1_reservation_fails_when_the_command_budget_is_gone)
{
    int i;

    saturn_vdp1_begin_frame();
    for (i = 0; i < SATURN_VDP1_MAX_CMDS; i++) {
        CUI_ASSERT_TRUE(saturn_vdp1_draw_rect(0, 0, 8, 8, 0x1234));
    }

    /* Zero, not a wild offset - the caller skips the effect for this frame. */
    CUI_ASSERT_EQ(0, (int)saturn_vdp1_reserve_cmd_slot());
    CUI_ASSERT_EQ(SATURN_VDP1_MAX_CMDS, saturn_vdp1_get_command_count());

    saturn_vdp1_begin_frame();
}
