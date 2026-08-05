/**
 * test_pacing.c - Asserts every on-screen animation rate, in SECONDS.
 *
 * WHY THIS TEST EXISTS
 *   Effect pacing was raised from 3 to 14 frames after a "too fast" report,
 *   but the PORTRAIT idle rate was a bare `/ 8` inline in coup_render.c and
 *   was missed - it kept cycling a full idle in 1.07 s while everything
 *   around it had been slowed by 4.7x. A constant nobody can grep for is a
 *   constant nobody revisits, so both rates are now named in coup.h.
 *
 *   They are asserted in SECONDS, not frames. Frames are the unit the code
 *   uses; seconds are the unit the complaint arrives in, and a bound stated
 *   in the wrong unit is a bound nobody checks the report against.
 *
 *   Lower bounds are the reported defect: below them an animation reads as a
 *   flicker rather than an event. Upper bounds matter too - an effect that
 *   outlasts the action it illustrates stalls the table.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "saturn/coup_anim_sprites.h"

/* The shortest and longest effects in coup_fx_data.h are 6 and 8 frames. */
#define FX_SHORT_FRAMES 6
#define FX_LONG_FRAMES  8

/* Durations in hundredths of a second, so the assertions stay in integers
 * and the test needs no floating point on the host toolchain. */
#define CS(frames, hold) (((frames) * (hold) * 100) / COUP_FIELD_HZ)

CUI_TEST(an_effect_lasts_long_enough_to_read_as_an_event)
{
    /* MEASURED: at hold 3 the shortest effect ran 0.30 s and was reported as
     * a flicker; at hold 14 it ran 1.40 s and was STILL reported too fast.
     * The floor sits above the second report, not the first. */
    CUI_ASSERT(CS(FX_SHORT_FRAMES, COUP_FX_HOLD_FRAMES) >= 150);
    CUI_ASSERT(CS(FX_LONG_FRAMES, COUP_FX_HOLD_FRAMES) >= 150);
}

CUI_TEST(an_effect_does_not_outlast_the_action_it_illustrates)
{
    /* An effect holds the table while it plays, so it must stay a fraction
     * of the server's tightest response window, which is 12 s. 3.2 s is a
     * quarter of it. The bound this replaces was 2.0 s, chosen when the
     * longest effect ran 1.87 s - a ceiling set just above the value it was
     * measuring can only ratify the present state, and that one went on to
     * fail a fix rather than a defect. This one is derived from the server
     * window instead, so it does not move when the pacing does. */
    CUI_ASSERT(CS(FX_SHORT_FRAMES, COUP_FX_HOLD_FRAMES) <= 320);
    CUI_ASSERT(CS(FX_LONG_FRAMES, COUP_FX_HOLD_FRAMES) <= 320);
}

CUI_TEST(the_portrait_idle_breathes_rather_than_fidgets)
{
    /* 8 frames at hold 8 was 1.07 s - the fidget this test was written for. */
    CUI_ASSERT(CS(COUP_ANIM_FRAMES, COUP_ANIM_HOLD_FRAMES) >= 200);
    CUI_ASSERT(CS(COUP_ANIM_FRAMES, COUP_ANIM_HOLD_FRAMES) <= 400);
}

CUI_TEST(the_idle_never_cycles_faster_than_an_action_effect)
{
    /* The relationship, not just the two absolute values: an idle looping
     * behind the table faster than the effect in front of it pulls the eye
     * off the action. This is what catches a future change that raises one
     * rate and leaves the other behind - the exact failure being fixed. */
    CUI_ASSERT(CS(COUP_ANIM_FRAMES, COUP_ANIM_HOLD_FRAMES)
               > CS(FX_SHORT_FRAMES, COUP_FX_HOLD_FRAMES));
}
