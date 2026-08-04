/**
 * test_saturn_gouraud.c - Host tests for VDP1 gouraud table encoding.
 *
 * The encoding and pool addressing are pure arithmetic and are pinned here.
 * The VRAM write itself is Saturn-only.
 */

#include "cui_test_framework.h"
#include "saturn_vdp1.h"

CUI_TEST(gouraud_neutral_word_is_no_change)
{
    /* The hardware subtracts 0x10, so 0x10 in every channel means "add 0". */
    uint16_t w = saturn_vdp1_gouraud_word(0, 0, 0);
    CUI_ASSERT_EQ(0x10, w & 0x1F);          /* R */
    CUI_ASSERT_EQ(0x10, (w >> 5) & 0x1F);   /* G */
    CUI_ASSERT_EQ(0x10, (w >> 10) & 0x1F);  /* B */
}

CUI_TEST(gouraud_word_packs_rgb555_channel_order)
{
    /* Saturn packs 0BBBBBGGGGGRRRRR, so a red-only correction must not
     * disturb the green or blue fields. */
    uint16_t w = saturn_vdp1_gouraud_word(+4, 0, 0);
    CUI_ASSERT_EQ(0x14, w & 0x1F);
    CUI_ASSERT_EQ(0x10, (w >> 5) & 0x1F);
    CUI_ASSERT_EQ(0x10, (w >> 10) & 0x1F);
}

CUI_TEST(gouraud_word_clamps_to_channel_range)
{
    uint16_t lo = saturn_vdp1_gouraud_word(-99, -99, -99);
    uint16_t hi = saturn_vdp1_gouraud_word(+99, +99, +99);

    CUI_ASSERT_EQ(0x00, lo & 0x1F);
    CUI_ASSERT_EQ(0x1F, hi & 0x1F);
    /* Bit 15 must stay clear - it is not part of the correction. */
    CUI_ASSERT_EQ(0, (hi >> 15) & 1);
}

CUI_TEST(gouraud_vshade_lights_top_and_darkens_bottom)
{
    uint16_t t[4];
    saturn_vdp1_gouraud_vshade(t, +6, -6);

    /* Corners A,B are the top pair; C,D the bottom pair. */
    CUI_ASSERT_EQ(t[0], t[1]);
    CUI_ASSERT_EQ(t[2], t[3]);
    CUI_ASSERT((t[0] & 0x1F) > (t[2] & 0x1F));

    /* Hue preserved: all three channels corrected equally. */
    CUI_ASSERT_EQ(t[0] & 0x1F, (t[0] >> 5) & 0x1F);
    CUI_ASSERT_EQ(t[0] & 0x1F, (t[0] >> 10) & 0x1F);
}

CUI_TEST(gouraud_pool_is_eight_byte_aligned_and_in_a_legal_window)
{
    uint32_t first = saturn_vdp1_gouraud_slot_addr(0);
    uint32_t last = saturn_vdp1_gouraud_slot_addr(SATURN_VDP1_GRD_MAX - 1);

    /* Tables must be 8-byte aligned and above 0x1F (ST-013-R3 5.3). */
    CUI_ASSERT_EQ(0u, first % 8u);
    CUI_ASSERT(first > 0x1Fu);

    /* Must sit in the gap between the command region, which is rewritten
     * every frame, and the texture area the asset loaders fill upward.
     * MEASURED: placing the pool near the top of VRAM instead produced
     * garbage tables - SGL claims more of the top page than it documents. */
    CUI_ASSERT(first >= (uint32_t)(SATURN_VDP1_CMD_OFFSET
                                   + SATURN_VDP1_MAX_CMDS * SATURN_VDP1_CMD_SIZE));
    CUI_ASSERT(last + SATURN_VDP1_GRD_SIZE <= (uint32_t)SATURN_VDP1_TEX_OFFSET);
}

CUI_TEST(gouraud_slot_addr_clamps_out_of_range)
{
    CUI_ASSERT_EQ(saturn_vdp1_gouraud_slot_addr(0),
                  saturn_vdp1_gouraud_slot_addr(-5));
    CUI_ASSERT_EQ(saturn_vdp1_gouraud_slot_addr(SATURN_VDP1_GRD_MAX - 1),
                  saturn_vdp1_gouraud_slot_addr(SATURN_VDP1_GRD_MAX + 99));
}
