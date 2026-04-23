/**
 * cui_pal.h - Platform Abstraction Layer
 *
 * Defines the interface that platform implementations must provide.
 * Components use these abstractions to remain platform-independent.
 */

#ifndef CUI_PAL_H
#define CUI_PAL_H

#include "cui_types.h"

/*============================================================================
 * Display Interface
 *============================================================================*/

typedef struct cui_pal_display {
    /**
     * Initialize the display subsystem.
     * @return CUI_OK on success, error code on failure
     */
    cui_result_t (*init)(void);

    /**
     * Shutdown the display subsystem and release resources.
     */
    void (*shutdown)(void);

    /**
     * Begin a new frame. Clears screen with background color.
     * @param bg_color Background color in RGBA format
     */
    void (*begin_frame)(uint32_t bg_color);

    /**
     * End the current frame. Presents rendered content to screen.
     */
    void (*end_frame)(void);

    /**
     * Draw text at the specified pixel position.
     * @param x X position in pixels
     * @param y Y position in pixels
     * @param text Null-terminated string to draw
     * @param color Text color in RGBA format
     */
    void (*draw_text)(int x, int y, const char* text, uint32_t color);

    /**
     * Draw text as sprites at exact pixel positions.
     * Renders character glyphs from the font sprite sheet without
     * using any tile/grid layer. On Saturn this means VDP1 only
     * (no VDP2 PNT), on other platforms this is identical to draw_text.
     * @param x X position in pixels
     * @param y Y position in pixels
     * @param text Null-terminated string to draw
     * @param color Text color in RGBA format
     */
    void (*draw_text_sprite)(int x, int y, const char* text, uint32_t color);

    /**
     * Draw a filled rectangle.
     * @param x X position in pixels
     * @param y Y position in pixels
     * @param w Width in pixels
     * @param h Height in pixels
     * @param color Fill color in RGBA format
     */
    void (*draw_rect)(int x, int y, int w, int h, uint32_t color);

} cui_pal_display_t;

/*============================================================================
 * Input Interface
 *============================================================================*/

typedef struct cui_pal_input {
    /**
     * Initialize the input subsystem.
     * @return CUI_OK on success, error code on failure
     */
    cui_result_t (*init)(void);

    /**
     * Shutdown the input subsystem and release resources.
     */
    void (*shutdown)(void);

    /**
     * Poll for input action.
     * @return The current input action, or CUI_INPUT_NONE if no input
     */
    cui_input_action_t (*poll)(void);

    /**
     * Get display label for an action.
     * Optional: may be NULL if platform doesn't provide labels.
     *
     * @param action The input action
     * @return Display label string, or NULL if unknown
     */
    const char* (*get_action_label)(cui_input_action_t action);

} cui_pal_input_t;

/*============================================================================
 * Storage Interface
 *============================================================================*/

typedef struct cui_pal_storage {
    cui_result_t (*init)(void);
    void (*shutdown)(void);
    bool (*save)(const char* filename, const void* data, uint32_t size);
    bool (*load)(const char* filename, void* buffer, uint32_t buffer_size,
                 uint32_t* out_size);
    bool (*exists)(const char* filename);
    bool (*delete_file)(const char* filename);
    uint32_t (*get_free_space)(void);
} cui_pal_storage_t;

/*============================================================================
 * Platform Structure
 *============================================================================*/

typedef struct cui_platform {
    const char* name;           /* Platform identifier (e.g., "sdl", "n64") */
    cui_pal_display_t display;  /* Display operations */
    cui_pal_input_t input;      /* Input operations */
    cui_pal_storage_t* storage; /* Storage operations (optional, NULL if unsupported) */
} cui_platform_t;

/*============================================================================
 * Platform Registration
 *============================================================================*/

/**
 * Register a platform implementation.
 * Must be called before cui_init().
 *
 * @param platform Pointer to platform structure (must remain valid)
 * @return CUI_OK on success
 */
cui_result_t cui_pal_register(const cui_platform_t* platform);

/**
 * Get the currently registered platform.
 * @return Pointer to platform, or NULL if none registered
 */
const cui_platform_t* cui_pal_get(void);

/**
 * Initialize the registered platform.
 * Calls init() on both display and input subsystems.
 *
 * @return CUI_OK on success
 */
cui_result_t cui_pal_init(void);

/**
 * Shutdown the registered platform.
 * Calls shutdown() on both display and input subsystems.
 */
void cui_pal_shutdown(void);

/*============================================================================
 * Action Label Helpers
 *============================================================================*/

/**
 * Get display label for an action from the current platform.
 *
 * @param action The input action
 * @return Display label string (e.g., "Enter", "A"), or "?" if not found
 */
const char* cui_get_action_label(cui_input_action_t action);

/**
 * Format an instruction string with action label.
 * Example: cui_format_instruction(buf, size, CUI_INPUT_CONFIRM, "Select")
 *          produces "Enter: Select" on SDL platform.
 *
 * @param buf Output buffer
 * @param size Buffer size
 * @param action The input action
 * @param desc Description text
 * @return Number of characters written (excluding null terminator), or
 *         negative on error
 */
int cui_format_instruction(char* buf, size_t size,
                           cui_input_action_t action, const char* desc);

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/* Get display interface from registered platform */
#define CUI_DISPLAY() (&cui_pal_get()->display)

/* Get input interface from registered platform */
#define CUI_INPUT() (&cui_pal_get()->input)

/* Get storage interface from registered platform (may be NULL) */
#define CUI_STORAGE() (cui_pal_get()->storage)

#endif /* CUI_PAL_H */
