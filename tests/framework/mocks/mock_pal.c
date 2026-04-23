/**
 * mock_pal.c - Mock PAL implementation for unit testing
 *
 * Provides a minimal PAL implementation that records calls
 * and allows verification in tests.
 */

#include "mock_pal.h"
#include "../../core/include/cui_pal.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Mock State
 *============================================================================*/

#define MOCK_MAX_DRAW_CALLS 100
#define MOCK_MAX_TEXT_LEN 128

typedef struct mock_draw_text_call {
    int x;
    int y;
    char text[MOCK_MAX_TEXT_LEN];
    uint32_t color;
} mock_draw_text_call_t;

typedef struct mock_draw_rect_call {
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
} mock_draw_rect_call_t;

static struct {
    bool initialized;
    int frame_count;

    /* draw_text tracking */
    mock_draw_text_call_t text_calls[MOCK_MAX_DRAW_CALLS];
    int text_call_count;

    /* draw_text_sprite tracking */
    mock_draw_text_call_t sprite_text_calls[MOCK_MAX_DRAW_CALLS];
    int sprite_text_call_count;

    /* draw_rect tracking */
    mock_draw_rect_call_t rect_calls[MOCK_MAX_DRAW_CALLS];
    int rect_call_count;

    /* Input queue */
    cui_input_action_t input_queue[32];
    int input_queue_head;
    int input_queue_tail;

    /* Display dimensions */
    int cols;
    int rows;
} mock_state = {
    .initialized = false,
    .cols = 71,   /* Default: 854 / 12 */
    .rows = 26    /* Default: 480 / 18 */
};

/*============================================================================
 * Mock Control API
 *============================================================================*/

void mock_pal_reset(void)
{
    mock_state.initialized = false;
    mock_state.frame_count = 0;
    mock_state.text_call_count = 0;
    mock_state.sprite_text_call_count = 0;
    mock_state.rect_call_count = 0;
    mock_state.input_queue_head = 0;
    mock_state.input_queue_tail = 0;
}

void mock_pal_set_dimensions(int cols, int rows)
{
    mock_state.cols = cols;
    mock_state.rows = rows;
}

void mock_pal_queue_input(cui_input_action_t action)
{
    if (mock_state.input_queue_tail < 32) {
        mock_state.input_queue[mock_state.input_queue_tail++] = action;
    }
}

int mock_pal_get_frame_count(void)
{
    return mock_state.frame_count;
}

int mock_pal_get_text_call_count(void)
{
    return mock_state.text_call_count;
}

const mock_draw_text_call_t* mock_pal_get_text_call(int index)
{
    if (index < 0 || index >= mock_state.text_call_count) {
        return NULL;
    }
    return &mock_state.text_calls[index];
}

int mock_pal_get_rect_call_count(void)
{
    return mock_state.rect_call_count;
}

const mock_draw_rect_call_t* mock_pal_get_rect_call(int index)
{
    if (index < 0 || index >= mock_state.rect_call_count) {
        return NULL;
    }
    return &mock_state.rect_calls[index];
}

mock_text_call_t mock_pal_get_last_text_call(void)
{
    mock_text_call_t result = {0};
    if (mock_state.text_call_count > 0) {
        mock_draw_text_call_t* call = &mock_state.text_calls[mock_state.text_call_count - 1];
        result.x = call->x;
        result.y = call->y;
        result.color = call->color;
        strncpy(result.text, call->text, MOCK_MAX_TEXT_LEN - 1);
        result.text[MOCK_MAX_TEXT_LEN - 1] = '\0';
    }
    return result;
}

mock_rect_call_t mock_pal_get_last_rect_call(void)
{
    mock_rect_call_t result = {0};
    if (mock_state.rect_call_count > 0) {
        mock_draw_rect_call_t* call = &mock_state.rect_calls[mock_state.rect_call_count - 1];
        result.x = call->x;
        result.y = call->y;
        result.w = call->w;
        result.h = call->h;
        result.color = call->color;
    }
    return result;
}

int mock_pal_get_sprite_text_call_count(void)
{
    return mock_state.sprite_text_call_count;
}

const mock_text_call_t* mock_pal_get_sprite_text_call(int index)
{
    if (index < 0 || index >= mock_state.sprite_text_call_count) {
        return NULL;
    }
    /* Return pointer to the internal struct — layout matches mock_text_call_t */
    return (const mock_text_call_t*)&mock_state.sprite_text_calls[index];
}

/*============================================================================
 * Mock Display Implementation
 *============================================================================*/

static cui_result_t mock_display_init(void)
{
    mock_state.initialized = true;
    return CUI_OK;
}

static void mock_display_shutdown(void)
{
    mock_state.initialized = false;
}

static void mock_display_begin_frame(uint32_t bg_color)
{
    (void)bg_color;
    /* Clear call tracking for new frame */
    mock_state.text_call_count = 0;
    mock_state.sprite_text_call_count = 0;
    mock_state.rect_call_count = 0;
}

static void mock_display_end_frame(void)
{
    mock_state.frame_count++;
}

static void mock_display_draw_text(int x, int y, const char* text, uint32_t color)
{
    if (mock_state.text_call_count >= MOCK_MAX_DRAW_CALLS) {
        return;
    }

    mock_draw_text_call_t* call = &mock_state.text_calls[mock_state.text_call_count++];
    call->x = x;
    call->y = y;
    call->color = color;

    if (text != NULL) {
        strncpy(call->text, text, MOCK_MAX_TEXT_LEN - 1);
        call->text[MOCK_MAX_TEXT_LEN - 1] = '\0';
    } else {
        call->text[0] = '\0';
    }
}

static void mock_display_draw_text_sprite(int x, int y, const char* text, uint32_t color)
{
    if (mock_state.sprite_text_call_count >= MOCK_MAX_DRAW_CALLS) {
        return;
    }

    mock_draw_text_call_t* call = &mock_state.sprite_text_calls[mock_state.sprite_text_call_count++];
    call->x = x;
    call->y = y;
    call->color = color;

    if (text != NULL) {
        strncpy(call->text, text, MOCK_MAX_TEXT_LEN - 1);
        call->text[MOCK_MAX_TEXT_LEN - 1] = '\0';
    } else {
        call->text[0] = '\0';
    }
}

static void mock_display_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (mock_state.rect_call_count >= MOCK_MAX_DRAW_CALLS) {
        return;
    }

    mock_draw_rect_call_t* call = &mock_state.rect_calls[mock_state.rect_call_count++];
    call->x = x;
    call->y = y;
    call->w = w;
    call->h = h;
    call->color = color;
}

/*============================================================================
 * Mock Input Implementation
 *============================================================================*/

static const char* mock_action_labels[] = {
    [CUI_INPUT_NONE]      = NULL,
    [CUI_INPUT_UP]        = "Up",
    [CUI_INPUT_DOWN]      = "Down",
    [CUI_INPUT_LEFT]      = "Left",
    [CUI_INPUT_RIGHT]     = "Right",
    [CUI_INPUT_CONFIRM]   = "Confirm",
    [CUI_INPUT_CANCEL]    = "Cancel",
    [CUI_INPUT_PAGE_UP]   = "PageUp",
    [CUI_INPUT_PAGE_DOWN] = "PageDown",
    [CUI_INPUT_QUIT]      = "Quit",
};

static const char* mock_input_get_action_label(cui_input_action_t action)
{
    if (action < 0 || (size_t)action >= sizeof(mock_action_labels)/sizeof(mock_action_labels[0])) {
        return NULL;
    }
    return mock_action_labels[action];
}

static cui_result_t mock_input_init(void)
{
    return CUI_OK;
}

static void mock_input_shutdown(void)
{
}

static cui_input_action_t mock_input_poll(void)
{
    if (mock_state.input_queue_head < mock_state.input_queue_tail) {
        return mock_state.input_queue[mock_state.input_queue_head++];
    }
    return CUI_INPUT_NONE;
}

/*============================================================================
 * Platform Definition
 *============================================================================*/

static const cui_platform_t mock_platform = {
    .name = "mock",
    .display = {
        .init = mock_display_init,
        .shutdown = mock_display_shutdown,
        .begin_frame = mock_display_begin_frame,
        .end_frame = mock_display_end_frame,
        .draw_text = mock_display_draw_text,
        .draw_text_sprite = mock_display_draw_text_sprite,
        .draw_rect = mock_display_draw_rect,
    },
    .input = {
        .init = mock_input_init,
        .shutdown = mock_input_shutdown,
        .poll = mock_input_poll,
        .get_action_label = mock_input_get_action_label,
    },
};

const cui_platform_t* cui_mock_platform(void)
{
    return &mock_platform;
}
