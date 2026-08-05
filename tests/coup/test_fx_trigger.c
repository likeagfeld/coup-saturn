/**
 * test_fx_trigger.c - Proves the action effects actually fire.
 *
 * The trigger is pure logic, so whether a coup plays its burst is a testable
 * fact rather than something to be confirmed by watching the screen. These
 * drive the same transitions the game produces and assert the effect chosen.
 */

#include "cui_test_framework.h"
#include "coup.h"

#include <string.h>

static void reset(coup_fx_prev_t* prev, coup_state_t* st)
{
    memset(st, 0, sizeof(*st));
    st->declared_actor = 0xFF;   /* no actor */
    st->declared_action = 0;
    st->phase = COUP_PHASE_IDLE;
    st->blocker_id = 0xFF;       /* no blocker */

    prev->action = -1;
    prev->phase = -1;
    prev->blocker_id = -1;
}

/*============================================================================
 * Action -> effect mapping
 *============================================================================*/

CUI_TEST(fx_every_action_maps_to_an_effect)
{
    /* The five dramatic actions each get their own sequence. */
    CUI_ASSERT_EQ(0, coup_fx_for_action(COUP_ACT_COUP));
    CUI_ASSERT_EQ(1, coup_fx_for_action(COUP_ACT_ASSASSINATE));
    CUI_ASSERT_EQ(2, coup_fx_for_action(COUP_ACT_STEAL));
    CUI_ASSERT_EQ(4, coup_fx_for_action(COUP_ACT_EXCHANGE));

    /* The three coin actions share the coin effect. */
    CUI_ASSERT_EQ(3, coup_fx_for_action(COUP_ACT_TAX));
    CUI_ASSERT_EQ(3, coup_fx_for_action(COUP_ACT_INCOME));
    CUI_ASSERT_EQ(3, coup_fx_for_action(COUP_ACT_FOREIGN_AID));
}

CUI_TEST(fx_unknown_action_fires_nothing)
{
    CUI_ASSERT_EQ(-1, coup_fx_for_action(99));
    CUI_ASSERT_EQ(-1, coup_fx_for_action(-1));
}

/*============================================================================
 * Transition detection
 *============================================================================*/

CUI_TEST(fx_declaring_a_coup_fires_the_coup_burst)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    st.declared_actor = 2;
    st.declared_action = COUP_ACT_COUP;

    CUI_ASSERT_EQ(0, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_same_action_twice_fires_once)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    st.declared_actor = 1;
    st.declared_action = COUP_ACT_STEAL;

    CUI_ASSERT_EQ(2, coup_fx_on_transition(&prev, &st));

    /* After remembering, the identical state must NOT re-fire - otherwise the
     * effect would restart every frame and never complete. */
    coup_fx_remember(&prev, &st);
    CUI_ASSERT_EQ(-1, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_entering_challenge_window_fires_challenge)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    st.phase = COUP_PHASE_CHALLENGE_WAIT;
    CUI_ASSERT_EQ(6, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_blocker_appearing_fires_block)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    st.blocker_id = 3;
    CUI_ASSERT_EQ(5, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_block_wins_over_the_action_that_provoked_it)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);

    /* Both changed in the same frame: the block is the more specific event. */
    st.declared_actor = 0;
    st.declared_action = COUP_ACT_ASSASSINATE;
    st.blocker_id = 2;

    CUI_ASSERT_EQ(5, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_no_actor_means_no_effect)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    /* An action value with no actor is stale state, not a declaration. */
    st.declared_actor = 0xFF;   /* stale action, no actor */
    st.declared_action = COUP_ACT_COUP;

    CUI_ASSERT_EQ(-1, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_idle_state_fires_nothing)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    CUI_ASSERT_EQ(-1, coup_fx_on_transition(&prev, &st));
}

CUI_TEST(fx_null_arguments_are_safe)
{
    coup_state_t st;
    coup_fx_prev_t prev;
    reset(&prev, &st);

    CUI_ASSERT_EQ(-1, coup_fx_on_transition(NULL, &st));
    CUI_ASSERT_EQ(-1, coup_fx_on_transition(&prev, NULL));
    coup_fx_remember(NULL, &st);   /* must not crash */
    coup_fx_remember(&prev, NULL);
}

/*============================================================================
 * A full round drives the sequence the game would produce
 *============================================================================*/

CUI_TEST(fx_a_full_assassinate_round_fires_the_right_sequence)
{
    coup_fx_prev_t prev;
    coup_state_t st;
    int fired[4], n = 0;

    reset(&prev, &st);
    coup_fx_remember(&prev, &st);

    /* 1. Someone declares an assassination. */
    st.declared_actor = 1;
    st.declared_action = COUP_ACT_ASSASSINATE;
    fired[n++] = coup_fx_on_transition(&prev, &st);
    coup_fx_remember(&prev, &st);

    /* 2. The table gets a challenge window. */
    st.phase = COUP_PHASE_CHALLENGE_WAIT;
    fired[n++] = coup_fx_on_transition(&prev, &st);
    coup_fx_remember(&prev, &st);

    /* 3. The target blocks with Contessa. */
    st.blocker_id = 2;
    fired[n++] = coup_fx_on_transition(&prev, &st);
    coup_fx_remember(&prev, &st);

    CUI_ASSERT_EQ(1, fired[0]);   /* assassinate */
    CUI_ASSERT_EQ(6, fired[1]);   /* challenge   */
    CUI_ASSERT_EQ(5, fired[2]);   /* block       */
}

/*============================================================================
 * Effect pacing
 *============================================================================*/

/* Effect PACING used to be asserted here as well as in test_pacing.c. It is
 * now asserted only there.
 *
 * Two files bounding the same quantity is how bounds drift, and this pair
 * drifted in the way that matters: the ceiling here was written as 2000 ms
 * when COUP_FX_HOLD_FRAMES was 14 and the longest effect ran 1867 ms - set
 * just above the value it was measuring, so it could only ever ratify the
 * present state. When the pacing was slowed again in response to a second
 * "still too fast" report, this ceiling failed the fix rather than the
 * defect. Its own justification (the server's tightest window is 12 s, so an
 * effect must be a fraction of it) never supported a bound that tight.
 *
 * This file is about the TRIGGER - which effect fires, for which action, at
 * which seat. Duration lives in test_pacing.c, in seconds. */
