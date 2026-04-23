/**
 * cui_pal.c - Platform Abstraction Layer implementation
 */

#include "../include/cui_pal.h"
#include <stddef.h>
#include <stdio.h>

/*============================================================================
 * Static State
 *============================================================================*/

static const cui_platform_t* s_platform = NULL;

/*============================================================================
 * Platform Registration
 *============================================================================*/

cui_result_t cui_pal_register(const cui_platform_t* platform)
{
    if (platform == NULL) {
        return CUI_ERROR_INVALID_PARAM;
    }

    /* Validate required functions */
    if (platform->display.init == NULL ||
        platform->display.shutdown == NULL ||
        platform->display.begin_frame == NULL ||
        platform->display.end_frame == NULL ||
        platform->display.draw_text == NULL ||
        platform->display.draw_rect == NULL) {
        return CUI_ERROR_INVALID_PARAM;
    }

    if (platform->input.init == NULL ||
        platform->input.shutdown == NULL ||
        platform->input.poll == NULL) {
        return CUI_ERROR_INVALID_PARAM;
    }

    s_platform = platform;
    return CUI_OK;
}

const cui_platform_t* cui_pal_get(void)
{
    return s_platform;
}

cui_result_t cui_pal_init(void)
{
    if (s_platform == NULL) {
        return CUI_ERROR_PAL_MISSING;
    }

    cui_result_t result;

    result = s_platform->display.init();
    if (result != CUI_OK) {
        return result;
    }

    result = s_platform->input.init();
    if (result != CUI_OK) {
        s_platform->display.shutdown();
        return result;
    }

    if (s_platform->storage && s_platform->storage->init) {
        result = s_platform->storage->init();
        if (result != CUI_OK) {
            s_platform->input.shutdown();
            s_platform->display.shutdown();
            return result;
        }
    }

    return CUI_OK;
}

void cui_pal_shutdown(void)
{
    if (s_platform == NULL) {
        return;
    }

    if (s_platform->storage && s_platform->storage->shutdown) {
        s_platform->storage->shutdown();
    }

    s_platform->input.shutdown();
    s_platform->display.shutdown();
}

/*============================================================================
 * Action Label Helpers
 *============================================================================*/

const char* cui_get_action_label(cui_input_action_t action)
{
    if (s_platform == NULL || s_platform->input.get_action_label == NULL) {
        return "?";
    }

    const char* label = s_platform->input.get_action_label(action);
    return label ? label : "?";
}

int cui_format_instruction(char* buf, size_t size,
                           cui_input_action_t action, const char* desc)
{
    if (buf == NULL || size == 0) {
        return -1;
    }

    return snprintf(buf, size, "%s: %s", cui_get_action_label(action), desc);
}
