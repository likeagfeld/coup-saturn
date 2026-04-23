/**
 * mock_pal.h - Mock PAL header for unit testing
 */

#ifndef CUI_MOCK_PAL_H
#define CUI_MOCK_PAL_H

#include "../../core/include/cui_pal.h"

/*============================================================================
 * Mock Call Recording Types
 *============================================================================*/

#define MOCK_MAX_TEXT_LEN 128

typedef struct mock_text_call {
    int x;
    int y;
    char text[MOCK_MAX_TEXT_LEN];
    uint32_t color;
} mock_text_call_t;

typedef struct mock_rect_call {
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
} mock_rect_call_t;

/*============================================================================
 * Mock Control API
 *============================================================================*/

/**
 * Get the mock platform implementation.
 */
const cui_platform_t* cui_mock_platform(void);

/**
 * Reset mock state. Call before each test.
 */
void mock_pal_reset(void);

/**
 * Set mock display dimensions.
 */
void mock_pal_set_dimensions(int cols, int rows);

/**
 * Queue an input action to be returned by poll().
 */
void mock_pal_queue_input(cui_input_action_t action);

/**
 * Get the number of frames rendered.
 */
int mock_pal_get_frame_count(void);

/**
 * Get the number of draw_text calls in current frame.
 */
int mock_pal_get_text_call_count(void);

/**
 * Get the number of draw_rect calls in current frame.
 */
int mock_pal_get_rect_call_count(void);

/**
 * Get the last draw_text call made.
 */
mock_text_call_t mock_pal_get_last_text_call(void);

/**
 * Get the last draw_rect call made.
 */
mock_rect_call_t mock_pal_get_last_rect_call(void);

/**
 * Get the number of draw_text_sprite calls in current frame.
 */
int mock_pal_get_sprite_text_call_count(void);

/**
 * Get a specific draw_text_sprite call by index.
 */
const mock_text_call_t* mock_pal_get_sprite_text_call(int index);

#endif /* CUI_MOCK_PAL_H */
