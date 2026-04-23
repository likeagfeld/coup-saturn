/**
 * saturn_key_repeat.c - Key repeat state machine implementation
 *
 * Pure logic module, no hardware dependency.
 */

#include "saturn_key_repeat.h"

/*============================================================================
 * Single Button Key Repeat
 *============================================================================*/

void saturn_key_repeat_init(saturn_key_repeat_t* kr, int delay_frames, int rate_frames)
{
    if (!kr) return;
    kr->delay_frames = delay_frames;
    kr->rate_frames = rate_frames;
    kr->counter = 0;
    kr->was_held = false;
    kr->active = false;
}

bool saturn_key_repeat_update(saturn_key_repeat_t* kr, bool held)
{
    if (!kr) return false;

    if (!held) {
        /* Button released - reset state */
        kr->was_held = false;
        kr->active = false;
        kr->counter = 0;
        return false;
    }

    /* Button is held */
    if (!kr->was_held) {
        /* First frame of hold - start counting toward initial delay */
        kr->was_held = true;
        kr->active = false;
        kr->counter = 1;
        /* Check if delay is already met (delay_frames <= 1) */
        if (kr->counter >= kr->delay_frames) {
            kr->active = true;
            kr->counter = 0;
            return true;
        }
        return false;
    }

    /* Continuing to hold */
    kr->counter++;

    if (!kr->active) {
        /* Waiting for initial delay */
        if (kr->counter >= kr->delay_frames) {
            kr->active = true;
            kr->counter = 0;
            return true;
        }
        return false;
    }

    /* In repeat mode - fire at rate interval */
    if (kr->counter >= kr->rate_frames) {
        kr->counter = 0;
        return true;
    }

    return false;
}

void saturn_key_repeat_reset(saturn_key_repeat_t* kr)
{
    if (!kr) return;
    kr->counter = 0;
    kr->was_held = false;
    kr->active = false;
}

/*============================================================================
 * D-Pad Multi-Direction Repeat
 *============================================================================*/

void saturn_dpad_repeat_init(saturn_dpad_repeat_t* dpad, int delay_frames, int rate_frames)
{
    if (!dpad) return;
    for (int i = 0; i < SATURN_KEY_REPEAT_DIRECTIONS; i++) {
        saturn_key_repeat_init(&dpad->directions[i], delay_frames, rate_frames);
    }
}

int saturn_dpad_repeat_update(saturn_dpad_repeat_t* dpad, uint8_t held_mask)
{
    if (!dpad) return -1;

    int result = -1;

    for (int i = 0; i < SATURN_KEY_REPEAT_DIRECTIONS; i++) {
        bool held = (held_mask & (1 << i)) != 0;
        if (saturn_key_repeat_update(&dpad->directions[i], held)) {
            if (result == -1) {
                result = i;
            }
        }
    }

    return result;
}
