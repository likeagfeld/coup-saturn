/**
 * @file cui_layout.h
 * @brief Platform-agnostic layout system for cui
 *
 * Provides pixel-based layout configuration with derived grid values.
 * Platforms define screen dimensions in pixels, and grid coordinates
 * are automatically calculated from character dimensions.
 *
 * Usage:
 *   1. Platform calls cui_layout_set() at startup with platform config
 *   2. Components query layout via cui_layout_get() or convenience functions
 *   3. Layout values automatically propagate to all components
 */

#ifndef CUI_LAYOUT_H
#define CUI_LAYOUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Layout configuration structure
 *
 * Pixels are the source of truth. Grid values are derived from
 * screen dimensions and character size.
 */
typedef struct cui_layout {
    /* Screen dimensions (pixels) */
    int screen_width;       /**< Total screen width in pixels (e.g., 320 for Saturn) */
    int screen_height;      /**< Total screen height in pixels (e.g., 224 for Saturn) */

    /* Character dimensions (pixels) - for text-mode rendering */
    int char_width;         /**< Width of one character cell (e.g., 8 for Saturn) */
    int char_height;        /**< Height of one character cell (e.g., 8 for Saturn) */

    /* Safe area margins (pixels from edge) */
    int safe_top;           /**< Overscan margin from top edge */
    int safe_bottom;        /**< Overscan margin from bottom edge */
    int safe_left;          /**< Overscan margin from left edge */
    int safe_right;         /**< Overscan margin from right edge */

    /* Derived grid dimensions (auto-calculated by cui_layout_calculate) */
    int grid_cols;          /**< Total columns: screen_width / char_width */
    int grid_rows;          /**< Total rows: screen_height / char_height */

    /* Safe area in grid coordinates (auto-calculated) */
    int safe_col;           /**< First safe column: safe_left / char_width */
    int safe_row;           /**< First safe row: safe_top / char_height */
    int safe_cols;          /**< Usable columns within safe area */
    int safe_rows;          /**< Usable rows within safe area */

    /* Platform identifier */
    const char* platform_name;  /**< Platform name for debugging (e.g., "saturn", "sdl") */
} cui_layout_t;

/**
 * @brief Calculate derived grid values from pixel values
 *
 * Call this after setting pixel values to populate grid_cols, grid_rows,
 * safe_col, safe_row, safe_cols, and safe_rows.
 *
 * @param layout Layout to calculate derived values for
 */
void cui_layout_calculate(cui_layout_t* layout);

/**
 * @brief Set the global layout configuration
 *
 * Platforms should call this at initialization with their layout config.
 * The layout is copied internally, so the caller can free their copy.
 *
 * @param layout Layout configuration (derived values will be calculated)
 */
void cui_layout_set(const cui_layout_t* layout);

/**
 * @brief Get the current layout configuration
 *
 * @return Pointer to the current layout (never NULL after cui_layout_set)
 */
const cui_layout_t* cui_layout_get(void);

/**
 * @brief Check if a layout has been set
 *
 * @return true if cui_layout_set has been called
 */
bool cui_layout_is_set(void);

/* ============================================================================
 * Pixel-based queries
 * ============================================================================ */

/**
 * @brief Get safe area left edge in pixels
 */
int cui_layout_safe_x(void);

/**
 * @brief Get safe area top edge in pixels
 */
int cui_layout_safe_y(void);

/**
 * @brief Get safe area width in pixels
 */
int cui_layout_safe_width(void);

/**
 * @brief Get safe area height in pixels
 */
int cui_layout_safe_height(void);

/* ============================================================================
 * Grid-based queries (for text-mode platforms)
 * ============================================================================ */

/**
 * @brief Get first safe column (grid coordinates)
 */
int cui_layout_safe_col(void);

/**
 * @brief Get first safe row (grid coordinates)
 */
int cui_layout_safe_row(void);

/**
 * @brief Get number of usable columns in safe area
 */
int cui_layout_safe_cols(void);

/**
 * @brief Get number of usable rows in safe area
 */
int cui_layout_safe_rows(void);

/**
 * @brief Get total grid columns
 */
int cui_layout_grid_cols(void);

/**
 * @brief Get total grid rows
 */
int cui_layout_grid_rows(void);

/* ============================================================================
 * Semantic region queries
 * ============================================================================ */

/**
 * @brief Get header row (first content row in safe area)
 *
 * Typically used for screen titles.
 */
int cui_layout_header_row(void);

/**
 * @brief Get footer row (last row in safe area)
 *
 * Typically used for help text / button hints.
 */
int cui_layout_footer_row(void);

/**
 * @brief Get main content start row
 *
 * Typically header_row + 2 (title + spacing).
 */
int cui_layout_content_row(void);

/**
 * @brief Get number of rows available for main content
 *
 * Rows between content_row and footer_row.
 */
int cui_layout_content_rows(void);

/**
 * @brief Get content start column (same as safe_col)
 */
int cui_layout_content_col(void);

/**
 * @brief Get content width in columns (same as safe_cols)
 */
int cui_layout_content_cols(void);

/* ============================================================================
 * Coordinate conversion utilities
 * ============================================================================ */

/**
 * @brief Convert grid column to pixel X coordinate
 *
 * @param col Grid column
 * @return Pixel X coordinate
 */
int cui_layout_col_to_x(int col);

/**
 * @brief Convert grid row to pixel Y coordinate
 *
 * @param row Grid row
 * @return Pixel Y coordinate
 */
int cui_layout_row_to_y(int row);

/**
 * @brief Convert pixel X to grid column
 *
 * @param x Pixel X coordinate
 * @return Grid column (floored)
 */
int cui_layout_x_to_col(int x);

/**
 * @brief Convert pixel Y to grid row
 *
 * @param y Pixel Y coordinate
 * @return Grid row (floored)
 */
int cui_layout_y_to_row(int y);

#ifdef __cplusplus
}
#endif

#endif /* CUI_LAYOUT_H */
