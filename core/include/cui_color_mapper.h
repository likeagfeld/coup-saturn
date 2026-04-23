/**
 * @file cui_color_mapper.h
 * @brief Color mapping interface for platform-specific color handling
 *
 * Bridges theme colors (RGBA) to platform-specific color values.
 * Platforms with limited palettes (e.g., Saturn's 8 colors) implement
 * mapping functions to translate semantic color roles to available colors.
 *
 * Usage:
 *   1. Platform provides a cui_color_mapper_t at initialization
 *   2. Components use cui_color_map_role() for semantic colors
 *   3. Components use cui_color_map_rgba() for direct color conversion
 */

#ifndef CUI_COLOR_MAPPER_H
#define CUI_COLOR_MAPPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Semantic color roles
 *
 * Components should use these roles instead of direct colors
 * to enable proper theming across platforms.
 */
typedef enum cui_color_role {
    CUI_ROLE_TEXT,         /**< Primary text color */
    CUI_ROLE_TEXT_MUTED,   /**< Secondary/dimmed text */
    CUI_ROLE_ACCENT,       /**< Accent/highlight color */
    CUI_ROLE_SELECTED,     /**< Selected item highlight */
    CUI_ROLE_DISABLED,     /**< Disabled element color */
    CUI_ROLE_SUCCESS,      /**< Success/positive feedback */
    CUI_ROLE_WARNING,      /**< Warning/caution feedback */
    CUI_ROLE_ERROR,        /**< Error/negative feedback */
    CUI_ROLE_BACKGROUND,   /**< Background color */
    CUI_ROLE_SURFACE,      /**< Surface/card background */
    CUI_ROLE_BORDER,       /**< Border/outline color */

    CUI_ROLE_COUNT         /**< Number of color roles */
} cui_color_role_t;

/**
 * @brief Function pointer type for RGBA to platform color conversion
 *
 * @param rgba 32-bit RGBA color (0xRRGGBBAA)
 * @return Platform-specific color value
 */
typedef uint32_t (*cui_color_map_rgba_fn)(uint32_t rgba);

/**
 * @brief Function pointer type for semantic role to platform color conversion
 *
 * @param role Semantic color role
 * @return Platform-specific color value
 */
typedef uint32_t (*cui_color_map_role_fn)(cui_color_role_t role);

/**
 * @brief Color mapper interface
 *
 * Platforms implement this to provide color translation.
 */
typedef struct cui_color_mapper {
    /**
     * @brief Map RGBA to platform-specific value
     *
     * For full-color platforms, this may be a passthrough.
     * For palette-based platforms, this finds the nearest match.
     * Can be NULL if platform doesn't support direct RGBA mapping.
     */
    cui_color_map_rgba_fn map_rgba;

    /**
     * @brief Map semantic role to platform-specific value
     *
     * Maps semantic color roles to available platform colors.
     * Should never be NULL.
     */
    cui_color_map_role_fn map_role;

    /**
     * @brief Whether platform supports full RGB color
     *
     * true = 24-bit RGB or better
     * false = palette-based (indexed colors)
     */
    bool full_color;

    /**
     * @brief Maximum palette size (0 = unlimited)
     *
     * For full_color platforms, this is 0.
     * For palette platforms, this is the max number of colors (e.g., 8, 16, 256).
     */
    int palette_size;

    /**
     * @brief Platform name for debugging
     */
    const char* platform_name;
} cui_color_mapper_t;

/**
 * @brief Set the global color mapper
 *
 * Platforms should call this at initialization.
 *
 * @param mapper Color mapper (copied internally)
 */
void cui_color_mapper_set(const cui_color_mapper_t* mapper);

/**
 * @brief Get the current color mapper
 *
 * @return Pointer to current color mapper (never NULL)
 */
const cui_color_mapper_t* cui_color_mapper_get(void);

/**
 * @brief Check if a color mapper has been set
 *
 * @return true if cui_color_mapper_set has been called
 */
bool cui_color_mapper_is_set(void);

/**
 * @brief Map an RGBA color to platform-specific value
 *
 * Convenience function that uses the current mapper.
 *
 * @param rgba 32-bit RGBA color (0xRRGGBBAA)
 * @return Platform-specific color value
 */
uint32_t cui_color_map_rgba(uint32_t rgba);

/**
 * @brief Map a semantic color role to platform-specific value
 *
 * Convenience function that uses the current mapper.
 *
 * @param role Semantic color role
 * @return Platform-specific color value
 */
uint32_t cui_color_map_role(cui_color_role_t role);

/**
 * @brief Check if the current platform supports full color
 *
 * @return true if platform has 24-bit RGB or better
 */
bool cui_color_is_full_color(void);

/**
 * @brief Get the current platform's palette size
 *
 * @return Palette size (0 = unlimited for full-color platforms)
 */
int cui_color_palette_size(void);

#ifdef __cplusplus
}
#endif

#endif /* CUI_COLOR_MAPPER_H */
