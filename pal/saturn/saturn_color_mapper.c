/**
 * saturn_color_mapper.c - Saturn Color Mapper Implementation
 *
 * Maps semantic color roles to Saturn's 8-slot VDP2 palette.
 * Delegates to saturn_vdp2.h for palette slot logic, providing
 * the cui_color_mapper_t interface used by the core library.
 *
 * Palette slots (see saturn_vdp2.h):
 *   0 = Text (white), 1 = Accent (blue), 2 = Selected (yellow),
 *   3 = Error (red), 4 = Success (green), 5 = Warning (yellow),
 *   6 = Disabled (gray), 7 = Highlight (inverted)
 */

#include "../../core/include/cui_color_mapper.h"
#include "../../core/include/cui_types.h"
#include "saturn_vdp2.h"

/**
 * Map RGBA to nearest Saturn palette slot index.
 * Delegates to the heuristic in saturn_vdp2.c.
 */
static uint32_t saturn_map_rgba(uint32_t rgba)
{
    return (uint32_t)saturn_rgba_to_palette_slot(rgba);
}

/**
 * Map semantic color role to RGBA color value.
 *
 * Returns RGBA colors that correspond to Saturn's 8-slot palette.
 * The PAL's draw_text will then convert these RGBA values to
 * palette indices via saturn_map_rgba().
 */
static uint32_t saturn_map_role(cui_color_role_t role)
{
    /* Get the default palette to return the actual RGB555 color as RGBA */
    const saturn_palette_entry_t* pal = saturn_get_default_palette();
    saturn_palette_slot_t slot = saturn_role_to_palette_slot((int)role);

    return saturn_rgb555_to_rgba(pal[slot].fg);
}

/* Saturn color mapper instance */
static cui_color_mapper_t saturn_color_mapper = {
    .map_rgba = saturn_map_rgba,
    .map_role = saturn_map_role,
    .full_color = 0,  /* false */
    .palette_size = SATURN_PAL_COUNT,  /* 8 semantic palette slots */
    .platform_name = "saturn"
};

void cui_saturn_init_color_mapper(void)
{
    cui_color_mapper_set(&saturn_color_mapper);
}

const cui_color_mapper_t* cui_saturn_get_color_mapper(void)
{
    return &saturn_color_mapper;
}
