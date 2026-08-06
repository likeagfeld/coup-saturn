/**
 * test_challenge_flash.c - The challenge-result flash-white trigger.
 *
 * WHY THIS EXISTS
 *   Design doc 2026-08-04-saturn-visual-facelift-design.md section 4.2 "All
 *   transitions": "Flash-white for challenge results (+k ramp)." The +k ramp
 *   itself (saturn_fade_start(..., white=true)) already existed and is
 *   already gated (tests/coup/test_saturn_fade.c) - what was missing was the
 *   TRIGGER: something that fires exactly once, on the frame a challenge
 *   window is decided.
 *
 *   coup_challenge_resolved() reuses the same coup_fx_prev_t snapshot
 *   coup_fx_on_transition() already keeps (it already carries `phase`), so
 *   it is driven purely by OBSERVING coup_state_t transitions, exactly like
 *   every other trigger in coup_render.c (spec D9 - the server stays
 *   turnkey: no wire message, message format or rule-engine change).
 */

#include "cui_test_framework.h"
#include "coup.h"

#include <string.h>

static void reset(coup_fx_prev_t* prev, coup_state_t* st)
{
    memset(st, 0, sizeof(*st));
    st->phase = COUP_PHASE_IDLE;

    prev->action = -1;
    prev->phase = -1;
    prev->blocker_id = -1;
}

/*============================================================================
 * Null safety
 *============================================================================*/

CUI_TEST(challenge_resolved_null_arguments_are_safe)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    CUI_ASSERT(!coup_challenge_resolved(NULL, &st));
    CUI_ASSERT(!coup_challenge_resolved(&prev, NULL));
}

/*============================================================================
 * The window opening never fires it - only the window CLOSING does
 *============================================================================*/

CUI_TEST(challenge_flash_does_not_fire_when_the_window_opens)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    st.phase = COUP_PHASE_CHALLENGE_WAIT;
    CUI_ASSERT(!coup_challenge_resolved(&prev, &st));
}

CUI_TEST(challenge_flash_does_not_fire_when_the_block_challenge_window_opens)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    st.phase = COUP_PHASE_BLOCK_CHALLENGE;
    CUI_ASSERT(!coup_challenge_resolved(&prev, &st));
}

/*============================================================================
 * The window closing fires it, whichever way it resolved
 *============================================================================*/

CUI_TEST(challenge_flash_fires_when_the_challenge_window_closes)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    st.phase = COUP_PHASE_CHALLENGE_WAIT;
    coup_fx_remember(&prev, &st);

    /* Resolved - the server moved everyone on to RESOLVING. */
    st.phase = COUP_PHASE_RESOLVING;
    CUI_ASSERT(coup_challenge_resolved(&prev, &st));
}

CUI_TEST(challenge_flash_fires_when_the_block_challenge_window_closes)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    st.phase = COUP_PHASE_BLOCK_CHALLENGE;
    coup_fx_remember(&prev, &st);

    st.phase = COUP_PHASE_RESOLVING;
    CUI_ASSERT(coup_challenge_resolved(&prev, &st));
}

CUI_TEST(challenge_flash_fires_the_same_way_whether_the_challenge_won_or_lost)
{
    /* The doc asks for a flash on the RESULT, not a colour keyed to which
     * side won it - so a challenge that catches a bluff and one that does
     * not must trigger identically. This client cannot even tell the two
     * apart from coup_state_t alone (the win/lose detail lives in a
     * COUP_EVT_CHALLENGE_RESULT event, not in persistent state), so the
     * trigger MUST be outcome-blind - this pins that down instead of
     * leaving it to be true by accident. */
    coup_fx_prev_t prev_a, prev_b;
    coup_state_t st_a, st_b;

    reset(&prev_a, &st_a);
    st_a.phase = COUP_PHASE_CHALLENGE_WAIT;
    coup_fx_remember(&prev_a, &st_a);
    st_a.phase = COUP_PHASE_LOSE_INFLUENCE;   /* challenger caught the bluff */

    reset(&prev_b, &st_b);
    st_b.phase = COUP_PHASE_CHALLENGE_WAIT;
    coup_fx_remember(&prev_b, &st_b);
    st_b.phase = COUP_PHASE_BLOCK_WAIT;       /* challenge failed, action stands */

    CUI_ASSERT(coup_challenge_resolved(&prev_a, &st_a));
    CUI_ASSERT(coup_challenge_resolved(&prev_b, &st_b));
}

/*============================================================================
 * It fires exactly once, not every frame the state holds still
 *============================================================================*/

CUI_TEST(challenge_flash_fires_once_then_stays_quiet)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    st.phase = COUP_PHASE_CHALLENGE_WAIT;
    coup_fx_remember(&prev, &st);

    st.phase = COUP_PHASE_RESOLVING;
    CUI_ASSERT(coup_challenge_resolved(&prev, &st));
    coup_fx_remember(&prev, &st);

    /* Same phase, next frame: must not re-fire. */
    CUI_ASSERT(!coup_challenge_resolved(&prev, &st));
}

/*============================================================================
 * A phase that was never a challenge window closing fires nothing
 *============================================================================*/

CUI_TEST(challenge_flash_ignores_ordinary_phase_changes)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    st.phase = COUP_PHASE_SELECT_ACTION;
    coup_fx_remember(&prev, &st);

    st.phase = COUP_PHASE_SELECT_TARGET;
    CUI_ASSERT(!coup_challenge_resolved(&prev, &st));
}

CUI_TEST(challenge_flash_idle_state_fires_nothing)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    CUI_ASSERT(!coup_challenge_resolved(&prev, &st));
}
