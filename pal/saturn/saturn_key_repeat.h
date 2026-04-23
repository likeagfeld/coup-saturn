/**
 * saturn_key_repeat.h - Key repeat state machine
 *
 * Implements software key-repeat for held buttons on Saturn.
 * Pure logic module - no hardware dependency. Fully testable.
 *
 * Usage:
 *   saturn_key_repeat_t kr;
 *   saturn_key_repeat_init(&kr, 15, 3);  // 15 frame delay, 3 frame repeat rate
 *
 *   // Each frame:
 *   bool held = (button_state & BUTTON_UP) != 0;
 *   if (saturn_key_repeat_update(&kr, held)) {
 *       // Fire repeat event
 *   }
 */

#ifndef CUI_SATURN_KEY_REPEAT_H
#define CUI_SATURN_KEY_REPEAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Key repeat state for a single button/direction.
 */
typedef struct saturn_key_repeat {
    int delay_frames;   /* Frames to wait before first repeat */
    int rate_frames;    /* Frames between subsequent repeats */
    int counter;        /* Current frame counter */
    bool was_held;      /* Was the button held last frame? */
    bool active;        /* Is repeat sequence active? */
} saturn_key_repeat_t;

/**
 * Initialize key repeat state.
 *
 * @param kr            Key repeat state
 * @param delay_frames  Frames to wait before first repeat (e.g., 15 = ~250ms at 60fps)
 * @param rate_frames   Frames between repeats (e.g., 3 = ~50ms at 60fps)
 */
void saturn_key_repeat_init(saturn_key_repeat_t* kr, int delay_frames, int rate_frames);

/**
 * Update key repeat state for one frame.
 * Returns true if a repeat event should fire this frame.
 *
 * @param kr    Key repeat state
 * @param held  Whether the button is currently held
 * @return true if a repeat event should fire
 */
bool saturn_key_repeat_update(saturn_key_repeat_t* kr, bool held);

/**
 * Reset the key repeat state (e.g., when switching contexts).
 */
void saturn_key_repeat_reset(saturn_key_repeat_t* kr);

/**
 * Multi-button key repeat tracker for the full D-pad.
 */
#define SATURN_KEY_REPEAT_DIRECTIONS 4

typedef struct saturn_dpad_repeat {
    saturn_key_repeat_t directions[SATURN_KEY_REPEAT_DIRECTIONS];
} saturn_dpad_repeat_t;

/**
 * Index constants for dpad_repeat directions.
 */
#define SATURN_DPAD_UP    0
#define SATURN_DPAD_DOWN  1
#define SATURN_DPAD_LEFT  2
#define SATURN_DPAD_RIGHT 3

/**
 * Initialize D-pad repeat tracker with uniform timing.
 */
void saturn_dpad_repeat_init(saturn_dpad_repeat_t* dpad, int delay_frames, int rate_frames);

/**
 * Update D-pad repeat state and return the direction that should fire, or -1.
 * Takes a bitmask of held directions (bit 0=up, 1=down, 2=left, 3=right).
 *
 * @param dpad  D-pad repeat state
 * @param held_mask  Bitmask of held directions
 * @return Direction index (0-3) that should fire, or -1 if none
 */
int saturn_dpad_repeat_update(saturn_dpad_repeat_t* dpad, uint8_t held_mask);

#ifdef __cplusplus
}
#endif

#endif /* CUI_SATURN_KEY_REPEAT_H */
