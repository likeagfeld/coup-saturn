/**
 * coup_gameover_loader.c - RETIRED. Kept only to hold the allocation chain.
 *
 * This used to upload a full-screen game-over image to VDP1 as seven 320x32
 * strips, each with its own 15-colour palette, because one 15-colour palette
 * could not carry a detailed full-screen picture.
 *
 * It is gone for two reasons:
 *
 *   1. The screen now has real backdrops. VICTORY and DEFEAT are separate
 *      256-colour scenes streamed from the disc and chosen by the same test
 *      the VICTORY/DEFEAT banner uses, so the outcome cannot disagree with
 *      the art. A 15-colours-per-strip VDP1 sprite drawn on top would cover
 *      that with a markedly worse image.
 *   2. It was the last piece of original artwork still shipping where
 *      official art exists.
 *
 * Retiring it hands back 71,680 bytes of VDP1 texture VRAM, roughly 227 KB of
 * generated source, and seven CRAM banks.
 *
 * WHY THE MODULE REMAINS
 *   The loaders form a chain: each asks the previous one where its VRAM and
 *   CRAM allocation ended, so nothing has to recompute a base address. Two
 *   separate corruption bugs came from recomputing those bases by hand. This
 *   file keeps that link intact by passing the sprite loader's end straight
 *   through. Deleting it outright would make coup_anim_loader.c chain from
 *   nothing.
 */

#include "coup_gameover_loader.h"
#include "coup_sprite_loader.h"

static uint32_t s_vram_end = 0;
static int s_cram_base = 0;

void coup_gameover_load(void)
{
    /* Consume nothing; simply forward the sprite loader's endpoints so the
     * next loader in the chain allocates from the right place. */
    s_vram_end = (coup_sprites_vram_end() + 7) & ~(uint32_t)7;
    s_cram_base = coup_sprites_cram_end_bank();
}

bool coup_gameover_draw(void)
{
    /* Nothing to draw. The caller falls through to the VDP2 backdrop, which
     * is where the game-over art now lives. */
    return false;
}

bool coup_gameover_loaded(void)
{
    return false;
}

uint32_t coup_gameover_vram_end(void)
{
    return s_vram_end;
}

int coup_gameover_cram_end_bank(void)
{
    return s_cram_base;   /* claims no banks of its own */
}
