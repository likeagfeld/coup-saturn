/**
 * cui_types.h - Core types and result codes for cui library
 *
 * This file defines fundamental types used throughout the cui library.
 * All components and PAL implementations depend on these types.
 */

#ifndef CUI_TYPES_H
#define CUI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*============================================================================
 * Configuration Defines
 *============================================================================*/

#ifndef CUI_MAX_LABEL_LEN
#define CUI_MAX_LABEL_LEN       32
#endif

#ifndef CUI_MAX_LIST_ITEMS
#define CUI_MAX_LIST_ITEMS      64
#endif

#ifndef CUI_BUTTON_MAX_LABEL
#define CUI_BUTTON_MAX_LABEL    32
#endif

#ifndef CUI_ACTION_LABEL_MAX
#define CUI_ACTION_LABEL_MAX    16  /* e.g., "Backspace", "L Shoulder" */
#endif

/*============================================================================
 * Result Codes
 *============================================================================*/

typedef enum cui_result {
    CUI_OK = 0,
    CUI_ERROR_INIT_FAILED,
    CUI_ERROR_NO_MEMORY,
    CUI_ERROR_PAL_MISSING,
    CUI_ERROR_DISPLAY_FAILED,
    CUI_ERROR_INPUT_FAILED,
    CUI_ERROR_INVALID_PARAM,
    CUI_ERROR_OUT_OF_BOUNDS,
    CUI_ERROR_STORAGE_FAILED
} cui_result_t;

/*============================================================================
 * Input Actions
 *============================================================================*/

typedef enum cui_input_action {
    CUI_INPUT_NONE = 0,
    CUI_INPUT_UP,
    CUI_INPUT_DOWN,
    CUI_INPUT_LEFT,
    CUI_INPUT_RIGHT,
    CUI_INPUT_CONFIRM,
    CUI_INPUT_CANCEL,
    CUI_INPUT_PAGE_UP,
    CUI_INPUT_PAGE_DOWN,
    CUI_INPUT_LOG_UP,
    CUI_INPUT_LOG_DOWN,
    CUI_INPUT_LOG_RESET,
    CUI_INPUT_QUIT
} cui_input_action_t;

/*============================================================================
 * Component States
 *============================================================================*/

typedef enum cui_state {
    CUI_STATE_NORMAL = 0,
    CUI_STATE_FOCUSED,
    CUI_STATE_PRESSED,
    CUI_STATE_DISABLED
} cui_state_t;

/*============================================================================
 * Text Alignment
 *============================================================================*/

typedef enum cui_align {
    CUI_ALIGN_LEFT = 0,
    CUI_ALIGN_CENTER,
    CUI_ALIGN_RIGHT
} cui_align_t;

/*============================================================================
 * Handle Results
 *============================================================================*/

typedef enum cui_handle_result {
    CUI_HANDLED_NONE = 0,       /* No action taken */
    CUI_HANDLED_CONSUMED,       /* Input was consumed */
    CUI_HANDLED_EVENT           /* Input produced an event */
} cui_handle_result_t;

/*============================================================================
 * Event Types
 *============================================================================*/

typedef enum cui_event_type {
    CUI_EVENT_NONE = 0,
    CUI_EVENT_CLICKED,          /* Button clicked */
    CUI_EVENT_SELECTED,         /* List item selected */
    CUI_EVENT_CHANGED,          /* Value changed (checkbox, etc) */
    CUI_EVENT_CANCELLED         /* Cancel action */
} cui_event_type_t;

typedef struct cui_event {
    cui_event_type_t type;
    int index;                  /* Item index for lists */
    int value;                  /* Generic value */
    void* user_data;            /* User-provided context */
} cui_event_t;

/*============================================================================
 * Geometry Types
 *============================================================================*/

typedef struct cui_bounds {
    int x;                      /* X position in pixels */
    int y;                      /* Y position in pixels */
    int w;                      /* Width in pixels */
    int h;                      /* Height in pixels */
} cui_rect_t;

typedef struct cui_point {
    int x;
    int y;
} cui_point_t;

typedef struct cui_size {
    int width;
    int height;
} cui_size_t;

/*============================================================================
 * Color Helpers
 *============================================================================*/

/* Colors are 32-bit RGBA: 0xRRGGBBAA */

#define CUI_RGBA(r, g, b, a) \
    (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

#define CUI_RGB(r, g, b) CUI_RGBA(r, g, b, 0xFF)

#define CUI_COLOR_R(c) (((c) >> 24) & 0xFF)
#define CUI_COLOR_G(c) (((c) >> 16) & 0xFF)
#define CUI_COLOR_B(c) (((c) >> 8) & 0xFF)
#define CUI_COLOR_A(c) ((c) & 0xFF)

/*============================================================================
 * Default Colors (Catppuccin Mocha palette)
 *============================================================================*/

#define CUI_COLOR_BG            0x1E1E2EFF  /* Base */
#define CUI_COLOR_TEXT          0xCDD6F4FF  /* Text */
#define CUI_COLOR_HIGHLIGHT     0x89B4FAFF  /* Blue */
#define CUI_COLOR_ACCENT        0xF5C2E7FF  /* Pink */
#define CUI_COLOR_SUCCESS       0xA6E3A1FF  /* Green */
#define CUI_COLOR_WARNING       0xF9E2AFFF  /* Yellow */
#define CUI_COLOR_ERROR         0xF38BA8FF  /* Red */
#define CUI_COLOR_SURFACE       0x313244FF  /* Surface0 */
#define CUI_COLOR_OVERLAY       0x45475AFF  /* Surface1 */
#define CUI_COLOR_MUTED         0x6C7086FF  /* Overlay0 */

#endif /* CUI_TYPES_H */
