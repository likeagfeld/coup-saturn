/**
 * saturn_layout.c - Saturn Layout Configuration
 *
 * Defines the layout configuration for Sega Saturn platform.
 * The Saturn supports multiple resolutions with 8x8 character cells.
 */

#include "../../core/include/cui_layout.h"
#include "saturn_pal.h"

/*
 * Saturn Display Specifications:
 *
 * 320x224 mode (Standard):
 * - Resolution: 320x224 pixels
 * - Character grid: 40 columns x 28 rows
 * - Character size: 8x8 pixels
 * - Safe area: 1 col left/right, 1 row top for overscan
 *
 * 352x224 mode (Wide):
 * - Resolution: 352x224 pixels
 * - Character grid: 44 columns x 28 rows
 * - Character size: 8x8 pixels
 * - Safe area: 2 cols left/right, 1 row top for overscan
 *
 * 640x224 mode (Hi-Res):
 * - Resolution: 640x224 pixels
 * - Character grid: 80 columns x 28 rows
 * - Character size: 8x8 pixels
 * - Safe area: 4 cols left/right, 1 row top for overscan
 */

/* Layout for 320x224 resolution (Standard) */
static cui_layout_t saturn_layout_320 = {
    /* Screen dimensions (pixels) */
    .screen_width = 320,
    .screen_height = 224,

    /* Character dimensions (pixels) - Jo Engine default font */
    .char_width = 8,
    .char_height = 8,

    /* Safe area margins (pixels from edge) - for CRT overscan */
    .safe_top = 8,       /* 1 row — netlink_test renders at row 1 */
    .safe_bottom = 0,
    .safe_left = 8,      /* 1 col */
    .safe_right = 8,

    /* Derived values are calculated by cui_layout_calculate() */
    .grid_cols = 0,      /* Will be 40 */
    .grid_rows = 0,      /* Will be 28 */
    .safe_col = 0,       /* Will be 1 */
    .safe_row = 0,       /* Will be 1 */
    .safe_cols = 0,      /* Will be 38 */
    .safe_rows = 0,      /* Will be 27 */

    .platform_name = "saturn"
};

/* Layout for 352x224 resolution (Wide) */
static cui_layout_t saturn_layout_352 = {
    /* Screen dimensions (pixels) */
    .screen_width = 352,
    .screen_height = 224,

    /* Character dimensions (pixels) - Jo Engine default font */
    .char_width = 8,
    .char_height = 8,

    /* Safe area margins (pixels from edge) - for CRT overscan */
    .safe_top = 8,       /* 1 row — netlink_test renders at row 1 */
    .safe_bottom = 0,
    .safe_left = 16,     /* 2 cols */
    .safe_right = 16,

    /* Derived values are calculated by cui_layout_calculate() */
    .grid_cols = 0,      /* Will be 44 */
    .grid_rows = 0,      /* Will be 28 */
    .safe_col = 0,       /* Will be 2 */
    .safe_row = 0,       /* Will be 1 */
    .safe_cols = 0,      /* Will be 40 */
    .safe_rows = 0,      /* Will be 27 */

    .platform_name = "saturn"
};

/* Layout for 640x224 resolution (Hi-Res) */
static cui_layout_t saturn_layout_640 = {
    /* Screen dimensions (pixels) */
    .screen_width = 640,
    .screen_height = 224,

    /* Character dimensions (pixels) - Jo Engine default font */
    .char_width = 8,
    .char_height = 8,

    /* Safe area margins (pixels from edge) - for CRT overscan */
    .safe_top = 8,       /* 1 row — netlink_test renders at row 1 */
    .safe_bottom = 0,
    .safe_left = 32,     /* 4 cols */
    .safe_right = 32,

    /* Derived values are calculated by cui_layout_calculate() */
    .grid_cols = 0,      /* Will be 80 */
    .grid_rows = 0,      /* Will be 28 */
    .safe_col = 0,       /* Will be 4 */
    .safe_row = 0,       /* Will be 1 */
    .safe_cols = 0,      /* Will be 72 */
    .safe_rows = 0,      /* Will be 27 */

    .platform_name = "saturn"
};

/* Current active layout (based on resolution setting) */
static cui_layout_t* g_current_layout = &saturn_layout_320;

void cui_saturn_init_layout(void)
{
    /* Select layout based on current resolution setting */
    switch (cui_saturn_get_resolution()) {
        case CUI_SATURN_RES_352x224:
            g_current_layout = &saturn_layout_352;
            break;
        case CUI_SATURN_RES_640x224:
            g_current_layout = &saturn_layout_640;
            break;
        case CUI_SATURN_RES_320x224:
        default:
            g_current_layout = &saturn_layout_320;
            break;
    }

    cui_layout_set(g_current_layout);
}

const cui_layout_t* cui_saturn_get_layout(void)
{
    return g_current_layout;
}
