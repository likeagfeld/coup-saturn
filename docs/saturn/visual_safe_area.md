# Saturn Safe Area Visual Test

Tracking visual regression tests for the Saturn title clipping bug.

## Problem

Title "cui Storybook" is clipped in Mednafen. The root cause may be:
- `safe_top` margin too small (currently 24px = 3 rows)
- Layout calculation bug in `cui_layout_header_row()`
- Render offset issue in Saturn PAL `draw_text`
- Jo Engine coordinate system issue

## Success Criteria

Screenshot shows:
- Title "cui Storybook" fully visible (not clipped)
- 6 menu items: Label, Button, List, Checkbox, Icon, Divider
- Footer "Arrows:Move A:Select B:Back" visible

## Current Configuration

```c
// saturn_layout.c
static cui_layout_t saturn_layout = {
    .screen_width = 320,
    .screen_height = 224,
    .char_width = 8,
    .char_height = 8,
    .safe_top = 24,      // 3 rows @ 8px
    .safe_bottom = 24,
    .safe_left = 8,
    .safe_right = 8,
};
```

Header row calculation:
- `cui_layout_header_row()` returns `safe_row`
- `safe_row = safe_top / char_height = 24 / 8 = 3`

## Test History

| Date | Config | safe_top | header_row | Result | Screenshot |
|------|--------|----------|------------|--------|------------|
| 2026-02-02 | Original | 24px (3 rows) | row 3 | **FAIL** - Title clipped | game-0000.png |
| 2026-02-02 | Test: no margin | 0px | row 0 | **FAIL** - Title+2 items clipped | game-0003.png |
| 2026-02-02 | Test: 4 rows | 32px | row 4 | **FAIL** - Title clipped, menu OK | game-0004.png |
| 2026-02-02 | Test: 5 rows | 40px | row 5 | **PASS** - Title visible | game-0005.png |
| 2026-02-02 | Calibration | 40px | row 5 | Calibration pattern captured | game-0007.png |

## Findings (2026-02-02, Jo Engine renderer)

- Mednafen clips rows 0-4 (40 pixels from top) with Jo Engine renderer
- First visible row is row 5 with Jo Engine
- `safe_top=40` (5 rows) was the setting for Saturn in Mednafen with Jo Engine
- `safe_bottom=0` works fine (no bottom clipping observed)

## Update (2026-02-06, SGL VDP2 renderer)

- Switched from Jo Engine to SGL VDP2 direct rendering
- `examples/netlink_test` (SGL) renders at row 1 successfully
- `safe_top=8` (1 row) now used — matches SGL coordinate system
- Jo Engine findings above may not apply to SGL-based renderer

## Potential Fixes

1. **Increase safe_top from 24px to 32px (4 rows)**
   - Most conservative fix
   - Reduces usable screen area

2. **Check cui_layout_header_row() calculation**
   - Verify safe_row is calculated correctly
   - Ensure layout is initialized before use

3. **Check Saturn PAL draw_text offset**
   - Jo Engine `jo_printf` may have its own offset
   - Verify coordinate system matches expectations

4. **Jo Engine coordinate investigation**
   - Row 0 may be at top edge vs. safe area
   - May need offset in Saturn-specific code

## Notes

- Saturn NTSC resolution: 320x224
- Character grid: 40 cols x 28 rows (8x8 font)
- CRT overscan typically clips 5-10% of edges
- 3 rows = ~10% of vertical resolution
