/**
 * test_render_budget.c - VDP1 draw-call budget gate (G4-host)
 *
 * The Saturn renderer buffers one VDP1 command per draw_rect and one per
 * non-space character (saturn_vdp1.c: saturn_vdp1_draw_rect /
 * saturn_vdp1_draw_text). Command count and total fill area are therefore
 * pure functions of the platform-agnostic render path, and are measured
 * here on the host rather than on hardware.
 *
 * Budget context: the VDP1 draws 1 pixel per 28.6 MHz clock, giving roughly
 * 477,000 pixel-clocks per frame at 60 fps (ST-013-R3 VDP1 manual,
 * txt:1114-1115).
 *
 * NOTE: this file uses the bare CUI_TEST(name) { ... } form. Do not use
 * CUI_TEST_BEGIN/CUI_TEST_END - those macros expand `cui_current_test.name`
 * with the macro parameter substituted into the member name and cannot
 * compile. No test in this suite uses them.
 */

#include "cui_test_framework.h"
#include "mock_pal.h"
#include "cui_pal.h"

/*============================================================================
 * Mock instrumentation self-tests
 *
 * The budget measurements are only trustworthy if the mock actually records
 * per-rect geometry. These two tests pin that contract.
 *============================================================================*/

CUI_TEST(mock_records_rect_geometry)
{
    cui_pal_register(cui_mock_platform());
    mock_pal_reset();

    CUI_DISPLAY()->draw_rect(11, 22, 33, 44, 0x11223344u);

    CUI_ASSERT_EQ(1, mock_pal_get_rect_call_count());

    mock_rect_call_t r = mock_pal_get_rect_call(0);
    CUI_ASSERT_EQ(11, r.x);
    CUI_ASSERT_EQ(22, r.y);
    CUI_ASSERT_EQ(33, r.w);
    CUI_ASSERT_EQ(44, r.h);
}

CUI_TEST(mock_rect_index_out_of_range_is_zeroed)
{
    cui_pal_register(cui_mock_platform());
    mock_pal_reset();

    mock_rect_call_t r = mock_pal_get_rect_call(0);
    CUI_ASSERT_EQ(0, r.x);
    CUI_ASSERT_EQ(0, r.w);
}
