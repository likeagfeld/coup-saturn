/**
 * saturn_fade.h - Whole-screen fades via VDP2 colour offset.
 *
 * Fades every armed layer together - the painted NBG1 backdrop, the VDP1
 * sprites and the NBG0 text - by adding a signed offset after colour
 * calculation (ST-058-R2 section 13.1).
 *
 * This is the only mechanism that can fade the text layer. VDP1's own
 * blending acts only on framebuffer pixels whose MSB is set, and the 4bpp
 * font is colour-bank data, so half-transparency would REPLACE glyphs rather
 * than dim them (ST-013-R3). It also costs no VDP1 commands and no fill rate,
 * which makes it safe to run on the busiest frames.
 */

#ifndef SATURN_FADE_H
#define SATURN_FADE_H

#include <stdint.h>
#include <stdbool.h>

/** Fully lit. */
#define SATURN_FADE_NONE     0
/** Fully black. */
#define SATURN_FADE_BLACK  255
/** Fully white. */
#define SATURN_FADE_WHITE  255

/**
 * Set the current fade directly.
 *
 * @param level  0 = unmodified, 255 = fully black (or white).
 * @param white  true fades toward white (a flash), false toward black.
 */
void saturn_fade_set(int level, bool white);

/** Remove any fade. Equivalent to saturn_fade_set(0, false). */
void saturn_fade_clear(void);

/**
 * Start an automatic fade that advances one step per saturn_fade_tick().
 *
 * @param from_level  starting level (0-255)
 * @param to_level    target level (0-255)
 * @param frames      how many frames the ramp should take (>= 1)
 * @param white       fade toward white rather than black
 */
void saturn_fade_start(int from_level, int to_level, int frames, bool white);

/** Advance an in-progress fade. Call once per rendered frame. */
void saturn_fade_tick(void);

/** True while a fade started by saturn_fade_start() is still running. */
bool saturn_fade_active(void);

/** Current level, 0-255. */
int saturn_fade_level(void);

/*============================================================================
 * Testable helper (pure)
 *============================================================================*/

/**
 * Interpolate a fade ramp.
 *
 * @param from    start level
 * @param to      end level
 * @param step    how many frames have elapsed
 * @param frames  total frames in the ramp
 * @return the level at `step`, clamped to the 0-255 range and to `to` once
 *         the ramp completes.
 */
int saturn_fade_interp(int from, int to, int step, int frames);

#endif /* SATURN_FADE_H */
