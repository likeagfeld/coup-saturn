/**
 * saturn_input.c - Saturn input processing implementation
 *
 * Queued input processing with D-pad key repeat.
 * Pure logic module - no hardware dependency, fully testable.
 */

#include "saturn_input.h"
#include "sgl_defs.h"
#include <string.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static void enqueue(saturn_input_t* inp, cui_input_action_t action)
{
    if (inp->queue_count >= SATURN_INPUT_QUEUE_SIZE) {
        return;  /* Drop if queue is full */
    }
    inp->queue[inp->queue_tail] = action;
    inp->queue_tail = (inp->queue_tail + 1) % SATURN_INPUT_QUEUE_SIZE;
    inp->queue_count++;
}

/*============================================================================
 * Public API
 *============================================================================*/

void saturn_input_init(saturn_input_t* inp, const saturn_input_config_t* config)
{
    if (!inp) return;
    memset(inp, 0, sizeof(*inp));
    inp->prev_data = 0xFFFF;  /* All released (active-low) */

    if (config) {
        inp->config = *config;
    }

    if (inp->config.repeat_delay > 0) {
        saturn_dpad_repeat_init(&inp->dpad_repeat,
                                inp->config.repeat_delay,
                                inp->config.repeat_rate);
    }
}

void saturn_input_update(saturn_input_t* inp, const saturn_input_state_t* state)
{
    if (!inp || !state) return;

    /*
     * Manual edge detection using the proven AICTRL.C pattern.
     * Do NOT use SGL's .push field — it is unreliable in SGL 3.02j.
     * data is active-low (0 = pressed), so we invert to active-high.
     */
    uint16_t current = ~state->data;
    uint16_t old     = ~inp->prev_data;
    uint16_t pressed = current & ~old;  /* Newly pressed this frame */

    inp->prev_data = state->data;

    /*
     * Step 1: Queue newly pressed buttons.
     * D-pad is checked first so directional input has priority.
     */
    if (pressed & PER_DGT_KU) enqueue(inp, CUI_INPUT_UP);
    if (pressed & PER_DGT_KD) enqueue(inp, CUI_INPUT_DOWN);
    if (pressed & PER_DGT_KL) enqueue(inp, CUI_INPUT_LEFT);
    if (pressed & PER_DGT_KR) enqueue(inp, CUI_INPUT_RIGHT);

    /* A and C both map to CONFIRM, but don't double-queue if both pressed */
    if (pressed & PER_DGT_TA) {
        enqueue(inp, CUI_INPUT_CONFIRM);
    } else if (pressed & PER_DGT_TC) {
        enqueue(inp, CUI_INPUT_CONFIRM);
    }

    if (pressed & PER_DGT_TB) enqueue(inp, CUI_INPUT_CANCEL);

    /* Shoulder buttons */
    if (pressed & PER_DGT_TL) enqueue(inp, CUI_INPUT_PAGE_UP);
    if (pressed & PER_DGT_TR) enqueue(inp, CUI_INPUT_PAGE_DOWN);

    /* X/Y/Z: log scrolling */
    if (pressed & PER_DGT_TY) enqueue(inp, CUI_INPUT_LOG_UP);
    if (pressed & PER_DGT_TX) enqueue(inp, CUI_INPUT_LOG_DOWN);
    if (pressed & PER_DGT_TZ) enqueue(inp, CUI_INPUT_LOG_RESET);

    /* Start */
    if (pressed & PER_DGT_ST) enqueue(inp, CUI_INPUT_QUIT);

    /*
     * Step 2: D-pad key repeat for held buttons.
     * Only runs if repeat is configured (repeat_delay > 0).
     * We only enqueue repeat events when the button wasn't freshly
     * pressed this frame (edge detection already handled it).
     */
    if (inp->config.repeat_delay > 0) {
        bool up_held    = current & PER_DGT_KU;
        bool down_held  = current & PER_DGT_KD;
        bool left_held  = current & PER_DGT_KL;
        bool right_held = current & PER_DGT_KR;

        if (saturn_key_repeat_update(&inp->dpad_repeat.directions[SATURN_DPAD_UP], up_held)
            && !(pressed & PER_DGT_KU)) {
            enqueue(inp, CUI_INPUT_UP);
        }
        if (saturn_key_repeat_update(&inp->dpad_repeat.directions[SATURN_DPAD_DOWN], down_held)
            && !(pressed & PER_DGT_KD)) {
            enqueue(inp, CUI_INPUT_DOWN);
        }
        if (saturn_key_repeat_update(&inp->dpad_repeat.directions[SATURN_DPAD_LEFT], left_held)
            && !(pressed & PER_DGT_KL)) {
            enqueue(inp, CUI_INPUT_LEFT);
        }
        if (saturn_key_repeat_update(&inp->dpad_repeat.directions[SATURN_DPAD_RIGHT], right_held)
            && !(pressed & PER_DGT_KR)) {
            enqueue(inp, CUI_INPUT_RIGHT);
        }
    }
}

cui_input_action_t saturn_input_dequeue(saturn_input_t* inp)
{
    if (!inp || inp->queue_count == 0) {
        return CUI_INPUT_NONE;
    }

    cui_input_action_t action = inp->queue[inp->queue_head];
    inp->queue_head = (inp->queue_head + 1) % SATURN_INPUT_QUEUE_SIZE;
    inp->queue_count--;
    return action;
}

int saturn_input_pending(const saturn_input_t* inp)
{
    if (!inp) return 0;
    return inp->queue_count;
}

void saturn_input_clear(saturn_input_t* inp)
{
    if (!inp) return;
    inp->queue_head = 0;
    inp->queue_tail = 0;
    inp->queue_count = 0;

    /* Reset repeat state too - avoids stale repeat events */
    if (inp->config.repeat_delay > 0) {
        for (int i = 0; i < SATURN_KEY_REPEAT_DIRECTIONS; i++) {
            saturn_key_repeat_reset(&inp->dpad_repeat.directions[i]);
        }
    }
}
