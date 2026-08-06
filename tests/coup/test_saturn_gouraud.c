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

/*
 * RGB-code marker.
 *
 * VDP1 writes a polygon's colour straight into the frame buffer, and VDP2
 * reads each pixel as an RGB code only when bit 15 is set; with MSB=0 it
 * treats the value as a palette index into CRAM (ST-013-R3 2.1).
 *
 * MEASURED: omitting the bit rendered brass frames as pale grey and produced
 * a band of pure yellow over the portraits, because VDP2 was indexing CRAM
 * with the colour value. A savestate CRAM dump contained no such colour,
 * which is what ruled out a palette fault.
 */
CUI_TEST(polygon_colour_is_marked_as_rgb_code)
{
    saturn_vdp1_cmd_t cmd;

    saturn_vdp1_encode_polygon(&cmd, 0, 0, 16, 16, 0x2063);
    CUI_ASSERT_EQ(0xA063, cmd.colr);
    CUI_ASSERT(cmd.colr & 0x8000);

    /* Already-marked colours must not be corrupted. */
    saturn_vdp1_encode_polygon(&cmd, 0, 0, 16, 16, 0xA063);
    CUI_ASSERT_EQ(0xA063, cmd.colr);
}

/*
 * Gouraud-shaded TEXTURED SPRITE (Normal Sprite command + gouraud colour
 * calculation), as opposed to the flat-colour polygon path above.
 *
 * ST-013-R3 p.94 (VDP1_Manual.txt:4091-4094): "In parts (sprites) with an
 * original graphic, color calculation other than shadow is enabled only in
 * the RGB mode (lookup table is RGB code in color mode 1 and color mode 5).
 * If not in the RGB mode (characters in color bank mode), the results
 * cannot be guaranteed."
 *
 * The existing 4bpp font/sprite path (SATURN_VDP1_SPR_PMOD) uses colour
 * mode 0 (16-colour bank) - explicitly the mode the manual says is NOT
 * guaranteed under gouraud. A gouraud sprite command must therefore select
 * colour mode 5 (32,768 RGB), matching the precedent already set by
 * SATURN_VDP1_RECT_GRD_PMOD for polygons (see comment above that macro).
 */

CUI_TEST(sprite_gouraud_uses_normal_sprite_command)
{
    saturn_vdp1_cmd_t cmd;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 10, 20, 16, 16, 0x1000, 0);
    CUI_ASSERT_EQ(VDP1_CMD_NORMAL_SPRITE, cmd.ctrl);
}

CUI_TEST(sprite_gouraud_pmod_sets_gouraud_colour_calc_bit)
{
    /* Colour calculation bits are CMDPMOD 2-0; 100b = gouraud
     * (ST-013-R3 p.93 / VDP1_Manual.txt:3935-3942). */
    saturn_vdp1_cmd_t cmd;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 0, 0, 16, 16, 0x1000, 0);
    CUI_ASSERT_EQ(VDP1_CCALC_GOURAUD, cmd.pmod & 0x7);
}

CUI_TEST(sprite_gouraud_pmod_selects_rgb_colour_mode_not_bank)
{
    /* Colour mode is CMDPMOD 5-3. Gouraud on a textured sprite is only
     * guaranteed in mode 5 (RGB) or mode 1 (LUT-with-RGB-entries); this
     * path picks mode 5, matching VDP1_CMOD_RGB, and must NOT be mode 0
     * (colour bank) which is what the ungouraud-shaded sprite path uses. */
    saturn_vdp1_cmd_t cmd;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 0, 0, 16, 16, 0x1000, 0);
    CUI_ASSERT_EQ((unsigned)VDP1_CMOD_RGB, (unsigned)(cmd.pmod & (0x7u << 3)));
    CUI_ASSERT(((cmd.pmod & (0x7u << 3)) >> 3) != 0);
}

CUI_TEST(sprite_gouraud_grda_matches_pool_slot_same_as_polygon_path)
{
    /* Same slot pool, same address/8 encoding as saturn_vdp1_draw_rect_gouraud
     * (ST-013-R3 6.8 / VDP1_Manual.txt:4464-4480). */
    saturn_vdp1_cmd_t cmd;
    int slot = 3;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 0, 0, 16, 16, 0x1000, slot);
    CUI_ASSERT_EQ((uint16_t)(saturn_vdp1_gouraud_slot_addr(slot) / 8), cmd.grda);
}

CUI_TEST(sprite_gouraud_colr_is_ignored_field_and_left_zero)
{
    /* ST-013-R3 6.4 Table 6.3 (VDP1_Manual.txt:4201-4211): "When it is a
     * textured part in the RGB mode, this word [CMDCOLR] is ignored."
     * There is no CRAM bank in RGB mode, so it is deterministically zeroed
     * rather than carrying a stale bank value. */
    saturn_vdp1_cmd_t cmd;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 0, 0, 16, 16, 0x1000, 0);
    CUI_ASSERT_EQ(0, cmd.colr);
}

CUI_TEST(sprite_gouraud_srca_and_size_match_texture_and_dimensions)
{
    saturn_vdp1_cmd_t cmd;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 0, 0, 32, 24, 0x2000, 0);
    CUI_ASSERT_EQ((uint16_t)(0x2000 / 8), cmd.srca);
    CUI_ASSERT_EQ((uint16_t)(((32 / 8) << 8) | 24), cmd.size);
}

CUI_TEST(sprite_gouraud_position_sets_vertex_a)
{
    saturn_vdp1_cmd_t cmd;
    saturn_vdp1_encode_sprite_gouraud(&cmd, 42, -7, 16, 16, 0x1000, 0);
    CUI_ASSERT_EQ(42, cmd.xa);
    CUI_ASSERT_EQ(-7, cmd.ya);
}
