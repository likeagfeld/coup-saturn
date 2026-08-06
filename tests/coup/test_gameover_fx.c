/**
 * test_gameover_fx.c - The game-over entrance dissolve.
 *
 * WHY THIS EXISTS
 *   Design doc 2026-08-04-saturn-visual-facelift-design.md section 4.2
 *   "Game over": "mesh + colour-offset dissolve into it." The colour-offset
 *   half is the SAME whole-screen fade every screen transition already gets
 *   (coup_render_screen()'s own s_last_screen diff + saturn_fade_start());
 *   this module supplies the missing mesh half's TIMING - which frame of
 *   entering GAME_OVER should the winner portrait draw through
 *   saturn_distort_draw_mesh_dissolve() instead of its normal solid draw.
 *
 *   Driven purely by OBSERVING coup_state_t transitions, exactly like
 *   coup_reveal_observe() (spec D9 - the server stays turnkey): entering
 *   GAME_OVER is read the identical way coup_render_screen()'s own
 *   s_last_screen static already reads it, just made host-testable and
 *   given a frame counter instead of a one-shot flag.
 */

#include "cui_test_framework.h"
#include "coup.h"

#include <string.h>

static void set_screen(coup_state_t* st, coup_screen_t screen)
{
    memset(st, 0, sizeof(*st));
    st->screen = screen;
}

/*============================================================================
 * Null safety
 *============================================================================*/

CUI_TEST(gameover_fx_null_arguments_are_safe)
{
    coup_state_t st;
    set_screen(&st, COUP_SCREEN_TITLE);

    coup_gameover_fx_init(NULL);
    coup_gameover_fx_observe(NULL, &st);
    coup_gameover_fx_observe((coup_gameover_fx_t*)1, NULL);
    coup_gameover_fx_tick(NULL);
    CUI_ASSERT(!coup_gameover_fx_dissolving(NULL));
    CUI_ASSERT_EQ(COUP_GAMEOVER_DISSOLVE_FRAMES, coup_gameover_fx_step(NULL));
}

/*============================================================================
 * Never dissolving until GAME_OVER is actually observed
 *============================================================================*/

CUI_TEST(gameover_fx_not_dissolving_before_any_observation)
{
    coup_gameover_fx_t gd;
    coup_gameover_fx_init(&gd);
    CUI_ASSERT(!coup_gameover_fx_dissolving(&gd));
}

CUI_TEST(gameover_fx_first_observation_never_arms_it)
{
    /* The very first frame this client ever sees must not look like a
     * transition INTO GAME_OVER just because there was no earlier screen to
     * compare against - the same "seeded" gate coup_reveal_observe() uses
     * for its first look. */
    coup_gameover_fx_t gd;
    coup_state_t st;

    coup_gameover_fx_init(&gd);
    set_screen(&st, COUP_SCREEN_GAME_OVER);

    coup_gameover_fx_observe(&gd, &st);
    CUI_ASSERT(!coup_gameover_fx_dissolving(&gd));
}

CUI_TEST(gameover_fx_staying_on_game_over_does_not_rearm)
{
    coup_gameover_fx_t gd;
    coup_state_t st;
    int i;

    coup_gameover_fx_init(&gd);
    set_screen(&st, COUP_SCREEN_GAME);
    coup_gameover_fx_observe(&gd, &st);   /* seed */

    set_screen(&st, COUP_SCREEN_GAME_OVER);
    coup_gameover_fx_observe(&gd, &st);
    CUI_ASSERT(coup_gameover_fx_dissolving(&gd));

    /* Run the dissolve out. */
    for (i = 0; i < COUP_GAMEOVER_DISSOLVE_FRAMES; i++) {
        coup_gameover_fx_tick(&gd);
    }
    CUI_ASSERT(!coup_gameover_fx_dissolving(&gd));

    /* Still on the same screen, next frame: must not restart. */
    coup_gameover_fx_observe(&gd, &st);
    CUI_ASSERT(!coup_gameover_fx_dissolving(&gd));
}

/*============================================================================
 * Entering GAME_OVER arms it; it runs for exactly the declared length
 *============================================================================*/

CUI_TEST(gameover_fx_entering_game_over_arms_the_dissolve)
{
    coup_gameover_fx_t gd;
    coup_state_t st;

    coup_gameover_fx_init(&gd);
    set_screen(&st, COUP_SCREEN_GAME);
    coup_gameover_fx_observe(&gd, &st);   /* seed */

    set_screen(&st, COUP_SCREEN_GAME_OVER);
    coup_gameover_fx_observe(&gd, &st);

    CUI_ASSERT(coup_gameover_fx_dissolving(&gd));
    CUI_ASSERT_EQ(0, coup_gameover_fx_step(&gd));
}

CUI_TEST(gameover_fx_runs_for_exactly_its_declared_length)
{
    coup_gameover_fx_t gd;
    coup_state_t st;
    int i;

    coup_gameover_fx_init(&gd);
    set_screen(&st, COUP_SCREEN_GAME);
    coup_gameover_fx_observe(&gd, &st);
    set_screen(&st, COUP_SCREEN_GAME_OVER);
    coup_gameover_fx_observe(&gd, &st);

    for (i = 0; i < COUP_GAMEOVER_DISSOLVE_FRAMES - 1; i++) {
        CUI_ASSERT(coup_gameover_fx_dissolving(&gd));
        coup_gameover_fx_tick(&gd);
    }
    /* One tick short of the declared length: still running. */
    CUI_ASSERT(coup_gameover_fx_dissolving(&gd));
    coup_gameover_fx_tick(&gd);
    /* Exactly at the declared length: done. */
    CUI_ASSERT(!coup_gameover_fx_dissolving(&gd));
    CUI_ASSERT_EQ(COUP_GAMEOVER_DISSOLVE_FRAMES, coup_gameover_fx_step(&gd));
}

CUI_TEST(gameover_fx_tick_never_overruns_past_the_declared_length)
{
    coup_gameover_fx_t gd;
    coup_state_t st;
    int i;

    coup_gameover_fx_init(&gd);
    set_screen(&st, COUP_SCREEN_GAME);
    coup_gameover_fx_observe(&gd, &st);
    set_screen(&st, COUP_SCREEN_GAME_OVER);
    coup_gameover_fx_observe(&gd, &st);

    for (i = 0; i < COUP_GAMEOVER_DISSOLVE_FRAMES + 50; i++) {
        coup_gameover_fx_tick(&gd);
    }
    CUI_ASSERT_EQ(COUP_GAMEOVER_DISSOLVE_FRAMES, coup_gameover_fx_step(&gd));
}

/*============================================================================
 * Re-seeding: leaving and re-entering GAME_OVER (a second match) rearms it
 *============================================================================*/

CUI_TEST(gameover_fx_a_second_match_rearms_the_dissolve)
{
    coup_gameover_fx_t gd;
    coup_state_t st;
    int i;

    coup_gameover_fx_init(&gd);
    set_screen(&st, COUP_SCREEN_GAME);
    coup_gameover_fx_observe(&gd, &st);

    set_screen(&st, COUP_SCREEN_GAME_OVER);
    coup_gameover_fx_observe(&gd, &st);
    for (i = 0; i < COUP_GAMEOVER_DISSOLVE_FRAMES; i++) {
        coup_gameover_fx_tick(&gd);
    }
    CUI_ASSERT(!coup_gameover_fx_dissolving(&gd));

    /* Back to the lobby for a rematch, then GAME_OVER again. */
    set_screen(&st, COUP_SCREEN_LOBBY);
    coup_gameover_fx_observe(&gd, &st);
    set_screen(&st, COUP_SCREEN_GAME);
    coup_gameover_fx_observe(&gd, &st);
    set_screen(&st, COUP_SCREEN_GAME_OVER);
    coup_gameover_fx_observe(&gd, &st);

    CUI_ASSERT(coup_gameover_fx_dissolving(&gd));
    CUI_ASSERT_EQ(0, coup_gameover_fx_step(&gd));
}
