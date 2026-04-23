/**
 * @file cui_color_mapper.c
 * @brief Color mapper implementation
 */

#include "cui_color_mapper.h"
#include "cui_types.h"
#include <string.h>

/* Global color mapper state */
static cui_color_mapper_t g_mapper;
static bool g_mapper_set = false;

/* Default RGBA passthrough (for full-color platforms) */
static uint32_t default_map_rgba(uint32_t rgba)
{
    return rgba;
}

/* Default role mapping using Catppuccin Mocha palette from cui_types.h */
static uint32_t default_map_role(cui_color_role_t role)
{
    switch (role) {
        case CUI_ROLE_TEXT:        return CUI_COLOR_TEXT;
        case CUI_ROLE_TEXT_MUTED:  return CUI_COLOR_MUTED;
        case CUI_ROLE_ACCENT:      return CUI_COLOR_ACCENT;
        case CUI_ROLE_SELECTED:    return CUI_COLOR_HIGHLIGHT;
        case CUI_ROLE_DISABLED:    return CUI_COLOR_MUTED;
        case CUI_ROLE_SUCCESS:     return CUI_COLOR_SUCCESS;
        case CUI_ROLE_WARNING:     return CUI_COLOR_WARNING;
        case CUI_ROLE_ERROR:       return CUI_COLOR_ERROR;
        case CUI_ROLE_BACKGROUND:  return CUI_COLOR_BG;
        case CUI_ROLE_SURFACE:     return CUI_COLOR_SURFACE;
        case CUI_ROLE_BORDER:      return CUI_COLOR_OVERLAY;
        default:                   return CUI_COLOR_TEXT;
    }
}

/* Default color mapper */
static const cui_color_mapper_t g_default_mapper = {
    .map_rgba = default_map_rgba,
    .map_role = default_map_role,
    .full_color = true,
    .palette_size = 0,
    .platform_name = "default"
};

void cui_color_mapper_set(const cui_color_mapper_t* mapper)
{
    if (!mapper) return;

    memcpy(&g_mapper, mapper, sizeof(cui_color_mapper_t));
    g_mapper_set = true;
}

const cui_color_mapper_t* cui_color_mapper_get(void)
{
    if (!g_mapper_set) {
        return &g_default_mapper;
    }
    return &g_mapper;
}

bool cui_color_mapper_is_set(void)
{
    return g_mapper_set;
}

uint32_t cui_color_map_rgba(uint32_t rgba)
{
    const cui_color_mapper_t* mapper = cui_color_mapper_get();
    if (mapper->map_rgba) {
        return mapper->map_rgba(rgba);
    }
    /* Fallback to passthrough */
    return rgba;
}

uint32_t cui_color_map_role(cui_color_role_t role)
{
    const cui_color_mapper_t* mapper = cui_color_mapper_get();
    if (mapper->map_role) {
        return mapper->map_role(role);
    }
    /* Fallback to default mapping */
    return default_map_role(role);
}

bool cui_color_is_full_color(void)
{
    const cui_color_mapper_t* mapper = cui_color_mapper_get();
    return mapper->full_color;
}

int cui_color_palette_size(void)
{
    const cui_color_mapper_t* mapper = cui_color_mapper_get();
    return mapper->palette_size;
}
