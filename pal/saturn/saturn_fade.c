/**
 * saturn_fade.c - Whole-screen fades via VDP2 colour offset.
 *
 * See saturn_fade.h for why colour offset is the right mechanism.
 */

#include "saturn_fade.h"

#ifdef __SATURN__
#include "sgl_defs.h"
#endif

static int  s_level = 0;
static bool s_white = false;

/* Ramp state */
static int  s_from = 0;
static int  s_to = 0;
static int  s_step = 0;
static int  s_frames = 0;
static bool s_running = false;

int saturn_fade_interp(int from, int to, int step, int frames)
{
    int value;

    if (frames < 1) {
        frames = 1;
    }
    if (step >= frames) {
        value = to;
    } else if (step <= 0) {
        value = from;
    } else {
        value = from + ((to - from) * step) / frames;
    }

    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    return value;
}

void saturn_fade_set(int level, bool white)
{
    if (level < 0) {
        level = 0;
    }
    if (level > 255) {
        level = 255;
    }

    s_level = level;
    s_white = white;

#ifdef __SATURN__
    {
        /* Offsets are signed, -256..+255. Negative darkens, positive
         * brightens. Applied to the backdrop, the sprite screen and the text
         * layer together so nothing stays lit while the rest fades. */
        Sint16 offset = (Sint16)(white ? level : -level);

        slColOffsetA(offset, offset, offset);
        slColOffsetAUse(NBG0ON | NBG1ON | SPRON);
    }
#endif
}

void saturn_fade_clear(void)
{
    s_running = false;
    saturn_fade_set(0, false);
}

void saturn_fade_start(int from_level, int to_level, int frames, bool white)
{
    if (frames < 1) {
        frames = 1;
    }

    s_from = from_level;
    s_to = to_level;
    s_frames = frames;
    s_step = 0;
    s_running = true;

    saturn_fade_set(from_level, white);
}

void saturn_fade_tick(void)
{
    if (!s_running) {
        return;
    }

    s_step++;
    saturn_fade_set(saturn_fade_interp(s_from, s_to, s_step, s_frames),
                    s_white);

    if (s_step >= s_frames) {
        s_running = false;
    }
}

bool saturn_fade_active(void)
{
    return s_running;
}

int saturn_fade_level(void)
{
    return s_level;
}
