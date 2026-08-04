/**
 * saturn_bg.h - VDP2 NBG1 painted background layer.
 *
 * A 512x256 256-colour bitmap living in VDP2 VRAM bank A0 (0x25E00000), which
 * is exactly 0x20000 bytes - one whole bank, and the 0x20000 alignment that
 * bitmap mode requires (ST-058-R2 section 4.9).
 *
 * Bank A0 is used rather than B1 because SGL's auto-arbiter
 * non-deterministically DROPS a standalone NBG whose data sits in bank B1,
 * where the text layer's character data already lives
 * (sega-saturn-developer gotcha #7, MEASURED).
 *
 * PALETTE PLACEMENT - a 256-colour bitmap palette must start on a 256-colour
 * boundary, so the only legal homes are CRAM byte 0x000, 0x200, 0x400 or
 * 0x600 (256-colour banks 0-3). The existing CRAM map fills the first three:
 *
 *   16-col banks  0-15  0x000-0x1FF  text palettes      (saturn_pal.c)
 *   16-col banks 16-24  0x200-0x31F  sprite palettes    (coup_sprite_loader.c)
 *   16-col banks 25-31  0x3E0-0x3FF  game-over strips   (coup_gameover_loader.c)
 *   16-col banks 32-36  0x400-0x49F  animated portraits (coup_anim_loader.c)
 *
 * So this layer takes 256-colour bank 3 at 0x600 (16-colour banks 48-63).
 *
 * MEASURED: bank 2 (0x400) was tried first and rendered the background with
 * wrong colours, because coup_anim_load() runs AFTER saturn_bg_init() and
 * overwrote the first 80 entries with character palettes. The bitmap geometry
 * was correct throughout - only the colours were wrong.
 */

#ifndef SATURN_BG_H
#define SATURN_BG_H

#include <stdint.h>
#include <stdbool.h>

/* VDP2 CRAM base and the background palette's home within it. */
#define SATURN_BG_CRAM_BASE     0x25F00000u
#define SATURN_BG_CRAM_OFFSET   0x600u   /* colour index 768 = 256-col bank 3 */
#define SATURN_BG_PALETTE_BANK  3

/* First CRAM byte already claimed by the sprite loaders (16-colour bank 37).
 * The background palette must start at or above this. */
#define SATURN_BG_CRAM_FIRST_FREE  0x4A0u

/* VDP2 VRAM bank A0. Bitmap base must be 0x20000-aligned. */
#define SATURN_BG_VRAM          0x25E00000u

/**
 * CRAM byte address of background palette entry `index`.
 * Index is clamped to 0..255. Pure function; host-testable.
 */
uint32_t saturn_bg_cram_addr(int index);

/**
 * Upload the bitmap and palette to VDP2, then arm NBG1.
 * Call once, after cui_saturn_init().
 */
void saturn_bg_init(void);

/** True once saturn_bg_init() has run. */
bool saturn_bg_is_armed(void);

#endif /* SATURN_BG_H */
