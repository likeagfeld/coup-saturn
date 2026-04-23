/**
 * saturn_input.h - Saturn input processing with queue and key repeat
 *
 * Testable input processing layer for Saturn controllers.
 * Takes raw peripheral state as input (hardware-independent),
 * manages an input queue for simultaneous presses, and
 * integrates D-pad key repeat.
 *
 * This solves two problems with raw SGL peripheral reading:
 * 1. Simultaneous presses: all buttons pressed on the same frame
 *    are queued and returned across consecutive dequeue calls
 * 2. Key repeat: holding a D-pad direction generates repeat events
 *    after a configurable delay and at a configurable rate
 *
 * Usage on hardware (saturn_pal.c):
 *   saturn_input_update(&inp, &state);   // once per frame after slSynch()
 *   action = saturn_input_dequeue(&inp);  // returns next queued action
 *
 * Usage in tests (simulate button press via active-low .data):
 *   saturn_input_state_t released = { .data = 0xFFFF };  // all released
 *   saturn_input_update(&inp, &released);                 // establish baseline
 *   saturn_input_state_t pressed = { .data = ~(PER_DGT_KD | PER_DGT_TA) };
 *   saturn_input_update(&inp, &pressed);
 *   assert(saturn_input_dequeue(&inp) == CUI_INPUT_DOWN);
 *   assert(saturn_input_dequeue(&inp) == CUI_INPUT_CONFIRM);
 */

#ifndef CUI_SATURN_INPUT_H
#define CUI_SATURN_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "saturn_key_repeat.h"
#include "../../core/include/cui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

#define SATURN_INPUT_QUEUE_SIZE 16

/*============================================================================
 * Types
 *============================================================================*/

/**
 * Raw peripheral state from Smpc_Peripheral[0].
 * Only .data is used — edge detection is done manually using the
 * proven AICTRL.C pattern (do NOT rely on SGL's .push field).
 */
typedef struct saturn_input_state {
    uint16_t data;   /* Current button state (active-low: 0 = pressed) */
} saturn_input_state_t;

/**
 * Input processor configuration.
 */
typedef struct saturn_input_config {
    int repeat_delay;   /* Frames before D-pad repeat starts (0 = no repeat) */
    int repeat_rate;    /* Frames between D-pad repeats */
} saturn_input_config_t;

/**
 * Input processor state.
 * All fields are managed internally - treat as opaque.
 */
typedef struct saturn_input {
    saturn_input_config_t config;
    cui_input_action_t queue[SATURN_INPUT_QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_count;
    uint16_t prev_data;  /* Previous frame's .data for edge detection */
    saturn_dpad_repeat_t dpad_repeat;
} saturn_input_t;

/*============================================================================
 * API
 *============================================================================*/

/**
 * Initialize input processor.
 * @param inp     Input processor state (caller-owned)
 * @param config  Configuration (copied, may be NULL for defaults)
 */
void saturn_input_init(saturn_input_t* inp, const saturn_input_config_t* config);

/**
 * Process one frame of peripheral state.
 * Queues all newly pressed buttons and any D-pad repeat events.
 * Call exactly once per frame, after slSynch() on hardware.
 *
 * @param inp    Input processor state
 * @param state  Raw peripheral state for this frame
 */
void saturn_input_update(saturn_input_t* inp, const saturn_input_state_t* state);

/**
 * Dequeue one input action.
 * Returns CUI_INPUT_NONE if queue is empty.
 * Can be called multiple times per frame to drain simultaneous presses.
 *
 * @param inp  Input processor state
 * @return Next queued action, or CUI_INPUT_NONE
 */
cui_input_action_t saturn_input_dequeue(saturn_input_t* inp);

/**
 * Get number of pending actions in queue.
 *
 * @param inp  Input processor state
 * @return Number of queued actions
 */
int saturn_input_pending(const saturn_input_t* inp);

/**
 * Clear all pending input actions and reset repeat state.
 *
 * @param inp  Input processor state
 */
void saturn_input_clear(saturn_input_t* inp);

#ifdef __cplusplus
}
#endif

#endif /* CUI_SATURN_INPUT_H */
