/**
 * @file cui_layout.c
 * @brief Layout system implementation
 */

#include "cui_layout.h"
#include <string.h>

/* Global layout state */
static cui_layout_t g_layout;
static bool g_layout_set = false;

/* Default layout (used if no platform sets one) */
static const cui_layout_t g_default_layout = {
    .screen_width = 640,
    .screen_height = 480,
    .char_width = 8,
    .char_height = 16,
    .safe_top = 16,
    .safe_bottom = 16,
    .safe_left = 8,
    .safe_right = 8,
    .grid_cols = 80,
    .grid_rows = 30,
    .safe_col = 1,
    .safe_row = 1,
    .safe_cols = 78,
    .safe_rows = 28,
    .platform_name = "default"
};

void cui_layout_calculate(cui_layout_t* layout)
{
    if (!layout) return;

    /* Avoid division by zero */
    if (layout->char_width <= 0) layout->char_width = 8;
    if (layout->char_height <= 0) layout->char_height = 8;

    /* Calculate total grid dimensions */
    layout->grid_cols = layout->screen_width / layout->char_width;
    layout->grid_rows = layout->screen_height / layout->char_height;

    /* Calculate safe area in grid coordinates */
    layout->safe_col = layout->safe_left / layout->char_width;
    layout->safe_row = layout->safe_top / layout->char_height;

    /* Calculate usable columns and rows within safe area */
    int safe_right_col = (layout->screen_width - layout->safe_right) / layout->char_width;
    int safe_bottom_row = (layout->screen_height - layout->safe_bottom) / layout->char_height;

    layout->safe_cols = safe_right_col - layout->safe_col;
    layout->safe_rows = safe_bottom_row - layout->safe_row;

    /* Clamp to valid values */
    if (layout->safe_cols < 0) layout->safe_cols = 0;
    if (layout->safe_rows < 0) layout->safe_rows = 0;
}

void cui_layout_set(const cui_layout_t* layout)
{
    if (!layout) return;

    /* Copy layout */
    memcpy(&g_layout, layout, sizeof(cui_layout_t));

    /* Calculate derived values */
    cui_layout_calculate(&g_layout);

    g_layout_set = true;
}

const cui_layout_t* cui_layout_get(void)
{
    if (!g_layout_set) {
        return &g_default_layout;
    }
    return &g_layout;
}

bool cui_layout_is_set(void)
{
    return g_layout_set;
}

/* ============================================================================
 * Pixel-based queries
 * ============================================================================ */

int cui_layout_safe_x(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_left;
}

int cui_layout_safe_y(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_top;
}

int cui_layout_safe_width(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->screen_width - layout->safe_left - layout->safe_right;
}

int cui_layout_safe_height(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->screen_height - layout->safe_top - layout->safe_bottom;
}

/* ============================================================================
 * Grid-based queries
 * ============================================================================ */

int cui_layout_safe_col(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_col;
}

int cui_layout_safe_row(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_row;
}

int cui_layout_safe_cols(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_cols;
}

int cui_layout_safe_rows(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_rows;
}

int cui_layout_grid_cols(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->grid_cols;
}

int cui_layout_grid_rows(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->grid_rows;
}

/* ============================================================================
 * Semantic region queries
 * ============================================================================ */

int cui_layout_header_row(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_row;
}

int cui_layout_footer_row(void)
{
    const cui_layout_t* layout = cui_layout_get();
    return layout->safe_row + layout->safe_rows - 1;
}

int cui_layout_content_row(void)
{
    /* Content starts 2 rows below header (title + spacing) */
    return cui_layout_header_row() + 2;
}

int cui_layout_content_rows(void)
{
    /* Rows between content start and footer */
    int content_start = cui_layout_content_row();
    int footer = cui_layout_footer_row();
    int rows = footer - content_start;
    return rows > 0 ? rows : 0;
}

int cui_layout_content_col(void)
{
    return cui_layout_safe_col();
}

int cui_layout_content_cols(void)
{
    return cui_layout_safe_cols();
}

/* ============================================================================
 * Coordinate conversion utilities
 * ============================================================================ */

int cui_layout_col_to_x(int col)
{
    const cui_layout_t* layout = cui_layout_get();
    return col * layout->char_width;
}

int cui_layout_row_to_y(int row)
{
    const cui_layout_t* layout = cui_layout_get();
    return row * layout->char_height;
}

int cui_layout_x_to_col(int x)
{
    const cui_layout_t* layout = cui_layout_get();
    if (layout->char_width <= 0) return 0;
    return x / layout->char_width;
}

int cui_layout_y_to_row(int y)
{
    const cui_layout_t* layout = cui_layout_get();
    if (layout->char_height <= 0) return 0;
    return y / layout->char_height;
}
