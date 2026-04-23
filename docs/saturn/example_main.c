/**
 * example_main.c - Saturn PAL Example Application
 *
 * Demonstrates how to use cui library with Saturn platform.
 * This is a minimal example showing the integration pattern.
 *
 * Build: Place in Jo Engine project src/ directory or adapt Makefile
 */

#include <jo/jo.h>
#include "saturn_pal.h"

/*
 * NOTE: To use cui components, you would include their headers here:
 * #include "cui_button.h"
 * #include "cui_theme.h"
 * etc.
 *
 * This example shows the platform integration without requiring
 * cui components to be implemented yet.
 */

/*============================================================================
 * Application State
 *============================================================================*/

static int frame_count = 0;
static cui_input_action_t last_action = CUI_INPUT_NONE;

/*============================================================================
 * Frame Callback
 *============================================================================*/

/**
 * Frame callback - invoked by Jo Engine at 60Hz.
 * This is where you poll input, update state, and render.
 */
void app_frame_callback(void) {
    /* Poll input from Saturn controller */
    cui_input_action_t action = CUI_INPUT()->poll();

    /* Update state */
    if (action != CUI_INPUT_NONE) {
        last_action = action;
    }

    /* Begin frame with background color */
    CUI_DISPLAY()->begin_frame(CUI_RGB(30, 30, 46));  /* Dark blue-gray */

    /* Render title */
    CUI_DISPLAY()->draw_text(10, 2, "cui Saturn PAL Demo", 0xFFFFFFFF);
    CUI_DISPLAY()->draw_text(10, 3, "====================", 0xFFFFFFFF);

    /* Display info */
    char buf[64];

    /* Frame counter */
    jo_sprintf(buf, "Frame: %d", frame_count);
    CUI_DISPLAY()->draw_text(2, 6, buf, 0xCDD6F4FF);

    /* Display dimensions */
    jo_sprintf(buf, "Display: %dx%d chars",
               CUI_DISPLAY()->get_cols(),
               CUI_DISPLAY()->get_rows());
    CUI_DISPLAY()->draw_text(2, 8, buf, 0xCDD6F4FF);

    /* Last input action */
    const char* action_label = "None";
    if (last_action != CUI_INPUT_NONE) {
        action_label = CUI_INPUT()->get_action_label(last_action);
    }
    jo_sprintf(buf, "Last Input: %s", action_label);
    CUI_DISPLAY()->draw_text(2, 10, buf, 0xCDD6F4FF);

    /* Draw some rectangles to test draw_rect */
    CUI_DISPLAY()->draw_text(2, 13, "Rectangle Test:", 0xCDD6F4FF);

    /* Red rectangle */
    CUI_DISPLAY()->draw_rect(16, 112, 64, 16, CUI_RGB(243, 139, 168));

    /* Green rectangle */
    CUI_DISPLAY()->draw_rect(96, 112, 64, 16, CUI_RGB(166, 227, 161));

    /* Blue rectangle */
    CUI_DISPLAY()->draw_rect(176, 112, 64, 16, CUI_RGB(137, 180, 250));

    /* Instructions */
    CUI_DISPLAY()->draw_text(2, 20, "Controls:", 0xF5C2E7FF);
    CUI_DISPLAY()->draw_text(2, 21, "  D-Pad: Navigation", 0xFFFFFFFF);
    CUI_DISPLAY()->draw_text(2, 22, "  A: Confirm", 0xFFFFFFFF);
    CUI_DISPLAY()->draw_text(2, 23, "  B: Cancel", 0xFFFFFFFF);
    CUI_DISPLAY()->draw_text(2, 24, "  L/R: Page Up/Down", 0xFFFFFFFF);
    CUI_DISPLAY()->draw_text(2, 25, "  Start: Quit", 0xFFFFFFFF);

    /* End frame */
    CUI_DISPLAY()->end_frame();

    /* Increment frame counter */
    frame_count++;

    /* Handle quit (though it won't exit on console) */
    if (action == CUI_INPUT_QUIT) {
        /* On console, we can't really quit, but we could transition
         * to a different screen or reset the app */
        CUI_DISPLAY()->draw_text(10, 27, "QUIT pressed!", 0xF38BA8FF);
    }
}

/*============================================================================
 * Jo Engine Entry Point
 *============================================================================*/

/**
 * jo_main - Entry point for Jo Engine applications.
 * This is called by Jo Engine's startup code.
 */
void jo_main(void) {
    /* Initialize Jo Engine with black background */
    jo_core_init(JO_COLOR_Black);

    /* Register Saturn platform with cui */
    cui_pal_register(cui_saturn_platform());

    /* Initialize cui platform (validates Jo Engine is ready) */
    cui_result_t result = cui_pal_init();
    if (result != CUI_OK) {
        /* Initialization failed - display error and hang */
        jo_printf(0, 0, "ERROR: cui_pal_init failed!");
        while (1) {
            jo_core_run();  /* Can't do much without cui */
        }
    }

    /*
     * Initialize cui components here.
     * Example (when components are implemented):
     *
     * static cui_button_t btn;
     * static cui_theme_t theme;
     *
     * cui_theme_init_default(&theme);
     * cui_button_init(&btn, "Test Button", 10, 15);
     */

    /* Set our frame callback */
    cui_saturn_set_frame_callback(app_frame_callback);

    /* Start main loop - this never returns */
    cui_saturn_run();
}

/*============================================================================
 * Usage Notes
 *============================================================================*/

/*
 * BUILDING THIS EXAMPLE:
 *
 * This example requires the cui library to be built and linked.
 * In a real Jo Engine project structure:
 *
 * 1. Copy this file to your Jo Engine project's src/ directory
 * 2. Copy saturn_pal.c to src/
 * 3. Copy saturn_pal.h to src/ or include/
 * 4. Add cui core sources to your project
 * 5. Update Makefile to include all sources
 * 6. Build with: make
 *
 * MINIMAL PROJECT STRUCTURE:
 *
 * my_saturn_project/
 * ├── src/
 * │   ├── main.c              (this file)
 * │   ├── saturn_pal.c
 * │   └── saturn_pal.h
 * ├── cui/
 * │   ├── cui_types.h
 * │   └── cui_pal.h
 * └── Makefile
 *
 * EXPECTED OUTPUT:
 *
 * When run, you should see:
 * - Title "cui Saturn PAL Demo"
 * - Frame counter incrementing at 60Hz
 * - Display dimensions (40x28)
 * - Last button pressed
 * - Three colored rectangles (red, green, blue)
 * - Control instructions
 *
 * TESTING:
 *
 * - Press D-pad buttons -> "Last Input" updates
 * - Press A, B, L, R -> Action labels appear
 * - Press Start -> "QUIT pressed!" appears
 * - Verify rectangles are visible and colored
 */
