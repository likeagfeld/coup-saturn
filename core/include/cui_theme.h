/**
 * cui_theme.h - Theming system for cui components
 *
 * Themes define colors and visual styles for component rendering.
 * Components receive a theme pointer and use it for all drawing.
 */

#ifndef CUI_THEME_H
#define CUI_THEME_H

#include "cui_types.h"

/*============================================================================
 * Theme Structure
 *============================================================================*/

typedef struct cui_theme {
    /* Background colors */
    uint32_t bg_color;          /* Main background */
    uint32_t surface_color;     /* Component surface */
    uint32_t overlay_color;     /* Overlays, modals */

    /* Text colors */
    uint32_t text_color;        /* Normal text */
    uint32_t text_muted_color;  /* Disabled/secondary text */
    uint32_t text_accent_color; /* Emphasized text */

    /* Interactive colors */
    uint32_t highlight_color;   /* Focused item background */
    uint32_t pressed_color;     /* Pressed state */
    uint32_t disabled_color;    /* Disabled state */

    /* Semantic colors */
    uint32_t success_color;     /* Success indicators */
    uint32_t warning_color;     /* Warning indicators */
    uint32_t error_color;       /* Error indicators */

    /* Component-specific */
    uint32_t button_bg_color;   /* Button background */
    uint32_t button_text_color; /* Button text */
    uint32_t list_item_color;   /* List item background */
    uint32_t checkbox_color;    /* Checkbox indicator */

} cui_theme_t;

/*============================================================================
 * Theme Functions
 *============================================================================*/

/**
 * Get the default theme (Catppuccin Mocha).
 * @return Pointer to static default theme
 */
const cui_theme_t* cui_theme_default(void);

/**
 * Initialize a theme with default values.
 * @param theme Theme to initialize
 */
void cui_theme_init(cui_theme_t* theme);

/**
 * Get color for a component state.
 * @param theme Theme to query
 * @param state Component state
 * @return Appropriate background color for state
 */
uint32_t cui_theme_state_bg(const cui_theme_t* theme, cui_state_t state);

/**
 * Get text color for a component state.
 * @param theme Theme to query
 * @param state Component state
 * @return Appropriate text color for state
 */
uint32_t cui_theme_state_text(const cui_theme_t* theme, cui_state_t state);

#endif /* CUI_THEME_H */
