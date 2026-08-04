/**
 * test_saturn_bg.c - Host tests for the NBG1 background layer's pure logic.
 *
 * Only the CRAM address arithmetic is host-testable; the VRAM and CRAM writes
 * are hardware-only and are covered by the on-screen visibility check (G2/G9).
 */

#include "cui_test_framework.h"
#include "saturn_bg.h"

CUI_TEST(bg_palette_lands_in_256_colour_bank_3)
{
    /* CRAM base 0x25F00000. The background owns 256-colour bank 3, which is
     * colour index 768, i.e. byte offset 768*2 = 0x600. Entry 255 is the last
     * of the bank at 0x600 + 510 = 0x7FE. */
    CUI_ASSERT_EQ(0x25F00600u, saturn_bg_cram_addr(0));
    CUI_ASSERT_EQ(0x25F00602u, saturn_bg_cram_addr(1));
    CUI_ASSERT_EQ(0x25F007FEu, saturn_bg_cram_addr(255));
}

CUI_TEST(bg_cram_addr_clamps_out_of_range)
{
    CUI_ASSERT_EQ(0x25F00600u, saturn_bg_cram_addr(-1));
    CUI_ASSERT_EQ(0x25F007FEu, saturn_bg_cram_addr(255));
}

CUI_TEST(bg_palette_clears_every_sprite_loader_bank)
{
    /* REGRESSION GUARD. The background palette was originally placed at 0x400
     * and was silently overwritten by coup_anim_load(), which claims 16-colour
     * banks 32-36 (0x400-0x49F) and runs AFTER saturn_bg_init(). The whole
     * 256-entry palette must sit above every bank the sprite loaders claim. */
    CUI_ASSERT(saturn_bg_cram_addr(0) >= 0x25F00000u + SATURN_BG_CRAM_FIRST_FREE);
}

CUI_TEST(bg_palette_starts_on_a_256_colour_boundary)
{
    /* VDP2 selects a bitmap palette in 256-colour granularity, so the palette
     * base must be a multiple of 256 entries (512 bytes). Only 0x000, 0x200,
     * 0x400 and 0x600 are legal. */
    CUI_ASSERT_EQ(0u, SATURN_BG_CRAM_OFFSET % 0x200u);
    CUI_ASSERT_EQ((int)(SATURN_BG_CRAM_OFFSET / 0x200u), SATURN_BG_PALETTE_BANK);
}
