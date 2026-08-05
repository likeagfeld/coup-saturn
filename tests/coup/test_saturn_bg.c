/**
 * test_saturn_bg.c - Host tests for the NBG1 background layer's pure logic.
 *
 * Only the CRAM address arithmetic is host-testable; the VRAM and CRAM writes
 * are hardware-only and are covered by the on-screen visibility check (G2/G9).
 */

#include "cui_test_framework.h"
#include "saturn_bg.h"

CUI_TEST(bg_palette_starts_on_a_legal_256_colour_boundary)
{
    /* A 256-colour bitmap palette number is 3 bits, so only banks 0-7 exist,
     * each 512 bytes: 0x000, 0x200, 0x400, 0x600, 0x800, 0xA00, 0xC00, 0xE00.
     * Anything else is not addressable.
     *
     * This asserts the PROPERTY, not a particular bank. The previous version
     * pinned bank 3 at 0x600 - and bank 3 turned out to be wrong, because the
     * 16-colour chain grew into it and eight sprite palettes ended up wearing
     * the backdrop's colours. A test that pins a value cannot notice that the
     * value became wrong. */
    uint32_t base = saturn_bg_cram_addr(0);
    uint32_t off = base - SATURN_BG_CRAM_BASE;

    CUI_ASSERT_EQ(0u, off % 512u);          /* on a 256-colour boundary */
    CUI_ASSERT(off / 512u <= 7u);           /* within the 3-bit selector */
    CUI_ASSERT_EQ((uint32_t)SATURN_BG_PALETTE_BANK, off / 512u);

    /* 256 entries, 2 bytes each, contiguous and inside CRAM's 4 KB. */
    CUI_ASSERT_EQ(base + 2u, saturn_bg_cram_addr(1));
    CUI_ASSERT_EQ(base + 510u, saturn_bg_cram_addr(255));
    CUI_ASSERT(off + 512u <= 0x1000u);
}

CUI_TEST(bg_cram_addr_clamps_out_of_range)
{
    CUI_ASSERT_EQ(saturn_bg_cram_addr(0), saturn_bg_cram_addr(-1));
    CUI_ASSERT_EQ(saturn_bg_cram_addr(255), saturn_bg_cram_addr(999));
}

CUI_TEST(bg_palette_clears_the_whole_sprite_palette_chain)
{
    /* THE REGRESSION THIS REPLACES. The 16-colour palette chain grows upward
     * from bank 0 as loaders are added; the background sits at a fixed
     * 256-colour boundary and is NOT part of that chain. At bank 3 (0x600) the
     * chain had grown to 0x700, so the last eight sprite palettes shared CRAM
     * with the first 128 background colours - and the background won, because
     * it is rewritten on every scene change.
     *
     * MEASURED on a captured title screen: 99.9% of solid wordmark pixels
     * matched the BACKGROUND palette, colour error 18.3 vs 167.2 for its own.
     *
     * The chain's own end is checked by scripts/qa/qa_cram_map.py, which reads
     * the real per-loader counts. Here we assert the structural margin: the
     * background must start in the upper half of CRAM, so a chain growing from
     * zero has to more than double before it can reach it. */
    uint32_t off = saturn_bg_cram_addr(0) - SATURN_BG_CRAM_BASE;

    CUI_ASSERT(off >= 0x800u);
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
