/**
 * cui_theme.c - Theming system implementation
 */

#include "../include/cui_theme.h"

/*============================================================================
 * Default Theme (Catppuccin Mocha)
 *============================================================================*/

static const cui_theme_t s_default_theme = {
    /* Background colors */
    .bg_color           = CUI_COLOR_BG,
    .surface_color      = CUI_COLOR_SURFACE,
    .overlay_color      = CUI_COLOR_OVERLAY,

    /* Text colors */
    .text_color         = CUI_COLOR_TEXT,
    .text_muted_color   = CUI_COLOR_MUTED,
    .text_accent_color  = CUI_COLOR_ACCENT,

    /* Interactive colors */
    .highlight_color    = CUI_COLOR_HIGHLIGHT,
    .pressed_color      = 0x7BA4E8FF,   /* Slightly darker blue */
    .disabled_color     = CUI_COLOR_SURFACE,

    /* Semantic colors */
    .success_color      = CUI_COLOR_SUCCESS,
    .warning_color      = CUI_COLOR_WARNING,
    .error_color        = CUI_COLOR_ERROR,

    /* Component-specific */
    .button_bg_color    = CUI_COLOR_SURFACE,
    .button_text_color  = CUI_COLOR_TEXT,
    .list_item_color    = CUI_COLOR_SURFACE,
    .checkbox_color     = CUI_COLOR_HIGHLIGHT
};

/*============================================================================
 * Theme Functions
 *============================================================================*/

const cui_theme_t* cui_theme_default(void)
{
    return &s_default_theme;
}

void cui_theme_init(cui_theme_t* theme)
{
    if (theme == NULL) {
        return;
    }

    *theme = s_default_theme;
}

uint32_t cui_theme_state_bg(const cui_theme_t* theme, cui_state_t state)
{
    if (theme == NULL) {
        theme = &s_default_theme;
    }

    switch (state) {
        case CUI_STATE_FOCUSED:
            return theme->highlight_color;
        case CUI_STATE_PRESSED:
            return theme->pressed_color;
        case CUI_STATE_DISABLED:
            return theme->disabled_color;
        case CUI_STATE_NORMAL:
        default:
            return theme->surface_color;
    }
}

uint32_t cui_theme_state_text(const cui_theme_t* theme, cui_state_t state)
{
    if (theme == NULL) {
        theme = &s_default_theme;
    }

    switch (state) {
        case CUI_STATE_DISABLED:
            return theme->text_muted_color;
        case CUI_STATE_FOCUSED:
        case CUI_STATE_PRESSED:
        case CUI_STATE_NORMAL:
        default:
            return theme->text_color;
    }
}
