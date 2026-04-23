/**
 * saturn_pal.h - Saturn Platform Abstraction Layer
 *
 * Saturn-specific platform implementation using bare SGL.
 * No Jo Engine dependency - works with any Saturn application.
 */

#ifndef CUI_SATURN_PAL_H
#define CUI_SATURN_PAL_H

#include "../../core/include/cui_pal.h"
#include "../../core/include/cui_layout.h"
#include "../../core/include/cui_color_mapper.h"

/*============================================================================
 * Resolution Configuration
 *============================================================================*/

/**
 * Saturn resolution modes.
 * The Saturn supports multiple horizontal resolutions with 224 scanlines.
 */
typedef enum cui_saturn_resolution {
    CUI_SATURN_RES_320x224 = 0,  /* Standard NTSC (default) - 40x28 grid */
    CUI_SATURN_RES_352x224 = 1,  /* Wide mode - 44x28 grid */
    CUI_SATURN_RES_640x224 = 2   /* Hi-res mode - 80x28 grid */
} cui_saturn_resolution_t;

/*============================================================================
 * Saturn-specific Interface
 *============================================================================*/

/**
 * Get the Saturn platform implementation.
 * This must be registered with cui_pal_register() before calling cui_pal_init().
 *
 * @return Pointer to Saturn platform structure
 */
const cui_platform_t* cui_saturn_platform(void);

/**
 * Set the display resolution.
 * Must be called BEFORE cui_pal_init() to take effect.
 *
 * Default is CUI_SATURN_RES_320x224 for compatibility.
 *
 * @param resolution The desired resolution mode
 */
void cui_saturn_set_resolution(cui_saturn_resolution_t resolution);

/**
 * Get the current resolution setting.
 *
 * @return Current resolution mode
 */
cui_saturn_resolution_t cui_saturn_get_resolution(void);

/**
 * Initialize the entire CUI Saturn subsystem in one call.
 *
 * Registers the platform, sets up layout, initializes the PAL,
 * and configures the color mapper. Call after slInitSystem() and
 * any optional cui_saturn_set_resolution().
 *
 * Typical usage:
 *   slInitSystem(TV_320x224, NULL, 1);
 *   slInitSynch();
 *   cui_saturn_init();
 *
 * Or with hi-res:
 *   slInitSystem(TV_640x224, NULL, 1);
 *   slInitSynch();
 *   cui_saturn_set_resolution(CUI_SATURN_RES_640x224);
 *   cui_saturn_init();
 */
void cui_saturn_init(void);

/**
 * Mark the Saturn PAL as initialized.
 * Low-level — prefer cui_saturn_init() which calls this automatically.
 */
void cui_saturn_mark_initialized(void);

/**
 * Update input state from Saturn controller.
 * Reads peripheral data and queues all pressed buttons + repeat events.
 * Call once per frame, after slSynch().
 *
 * Optional: if not called explicitly, cui_saturn_poll_input() will
 * auto-update once per frame. Use this for explicit control or to
 * allow multiple poll calls per frame (drain simultaneous presses).
 */
void cui_saturn_update_input(void);

/**
 * Poll input from Saturn controller.
 * Returns the next queued input action. Simultaneous presses are
 * queued and returned across consecutive calls. D-pad directions
 * generate key repeat events when held.
 *
 * @return Next input action (CUI_INPUT_NONE if queue is empty)
 */
cui_input_action_t cui_saturn_poll_input(void);

/**
 * Flush VDP1 command buffer to VRAM (bulk write).
 * Call BEFORE slSynch(). Waits for VDP1 draw completion, then
 * writes all buffered commands to slots 4+ in VDP1 VRAM.
 */
void cui_saturn_vdp1_flush_cmds(void);

/**
 * Activate flushed VDP1 commands by patching SGL's command table.
 * Call AFTER slSynch() during vblank. Overwrites slot 2 with a
 * JUMP to slot 4, skipping SGL's END at slot 3. Only 4 writes.
 */
void cui_saturn_vdp1_activate(void);

/*============================================================================
 * Layout and Color Mapper
 *============================================================================*/

/**
 * Initialize the Saturn layout configuration.
 *
 * Call this after cui_pal_register() to set up the layout system
 * with Saturn-specific values (320x224, 8x8 chars, overscan margins).
 */
void cui_saturn_init_layout(void);

/**
 * Get the Saturn layout configuration.
 * @return Pointer to the Saturn layout
 */
const cui_layout_t* cui_saturn_get_layout(void);

/**
 * Initialize the Saturn color mapper.
 *
 * Call this to set up the color mapper that translates semantic
 * color roles to SGL-compatible values.
 */
void cui_saturn_init_color_mapper(void);

/**
 * Get the Saturn color mapper.
 * @return Pointer to the Saturn color mapper
 */
const cui_color_mapper_t* cui_saturn_get_color_mapper(void);

/*============================================================================
 * Multi-Font API (Saturn-specific, not part of core PAL)
 *============================================================================*/

/* Forward declarations — full definitions in saturn_font.h */
struct saturn_font_desc;
struct saturn_font_registry;

/**
 * Register an additional font for VDP1 sprite-based text rendering.
 * Fonts must be registered before cui_saturn_init() or during init.
 *
 * @param desc  Font descriptor (1bpp data, cell dimensions, etc.)
 * @return Font index (0-based), or -1 on error
 */
int cui_saturn_font_register(const struct saturn_font_desc* desc);

/**
 * Set the active font by index.
 * draw_text_sprite() will use this font for rendering.
 *
 * @param font_index  Index returned by cui_saturn_font_register()
 */
void cui_saturn_font_set_active(int font_index);

/**
 * Get the currently active font index.
 *
 * @return Active font index, or -1 if none
 */
int cui_saturn_font_get_active(void);

/**
 * Get the font registry (read-only access for querying font info).
 *
 * @return Pointer to the font registry
 */
const struct saturn_font_registry* cui_saturn_font_get_registry(void);

/**
 * Upload all registered fonts to VDP1 VRAM.
 * Call after registering additional fonts via cui_saturn_font_register().
 * Re-uploads all fonts (including built-in) to ensure correct VRAM layout.
 */
void cui_saturn_font_upload_all(void);

/*============================================================================
 * Platform Limitations
 *============================================================================*/

/*
 * KNOWN LIMITATIONS:
 *
 * 1. Colored text via 8 palette slots - draw_text maps RGBA colors
 *    to one of 8 VDP2 CRAM palette banks (white, blue, yellow, red,
 *    green, yellow/orange, gray, inverted). Fine color gradations
 *    are quantized to the nearest slot.
 *
 * 2. No alpha transparency - Saturn uses RGB555 (15-bit color)
 *    Alpha channel in cui colors is ignored
 *
 * 3. QUIT action doesn't exit - consoles don't have "quit"
 *    Returns to application but can't exit main loop
 *
 * 4. Rectangle drawing is a no-op
 *    Selection highlighting uses text-based indicators (">")
 *    Future: Could use VDP2 planes for background rectangles
 *
 * 5. Application owns the main loop
 *    Unlike the old Jo Engine version, applications must:
 *    - Call slInitSystem() themselves
 *    - Run their own while(1) loop
 *    - Call slSynch() for frame sync
 *
 * 6. Multiple resolution modes available:
 *    - 320x224: 40x28 grid (default, best compatibility)
 *    - 352x224: 44x28 grid (wide mode)
 *    - 640x224: 80x28 grid (hi-res mode)
 */

#endif /* CUI_SATURN_PAL_H */
