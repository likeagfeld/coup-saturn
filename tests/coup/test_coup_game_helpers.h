/**
 * test_coup_game_helpers.h - Shared helpers for coup game client tests
 *
 * Provides setup/teardown and convenience wrappers around the
 * coup_game.c public API for concise test assertions.
 */

#ifndef TEST_COUP_GAME_HELPERS_H
#define TEST_COUP_GAME_HELPERS_H

#include "cui_test_framework.h"
#include "coup.h"
#include "coup_rules.h"
#include "../framework/mocks/mock_pal.h"
#include "cui_pal.h"
#include <stdbool.h>
#include <stdint.h>

/* Stub API (defined in test_coup_game_stubs.c) */
extern void stub_sfx_reset(void);
extern int  stub_sfx_last(void);
extern int  stub_sfx_count(void);
extern int  stub_sfx_at(int index);

/*============================================================================
 * Setup Helpers
 *============================================================================*/

/**
 * Full game setup: register mock PAL, reset SFX log, call coup_init().
 */
static inline void game_setup(void)
{
    cui_pal_register(cui_mock_platform());
    stub_sfx_reset();
    coup_init();
}

/**
 * Start a local game from a clean state.
 * Since frame_count=0 after coup_init(), seed = 0*7919+31337 = 31337.
 */
static inline void start_local_game(void)
{
    game_setup();
    coup_start_local_game();
}

/*============================================================================
 * Convenience Accessors
 *============================================================================*/

/** Get current game state. */
static inline const coup_state_t* st(void)
{
    return coup_get_state();
}

/** Get mutable game state (for test setup only). */
static inline coup_state_t* st_mut(void)
{
    return (coup_state_t*)(uintptr_t)coup_get_state();
}

/** Send an input action. */
static inline void press(cui_input_action_t a)
{
    coup_update(a);
}

/** Advance N frames (calls coup_tick N times). */
static inline void tick_frames(int n)
{
    int i;
    for (i = 0; i < n; i++)
        coup_tick();
}

/**
 * Wait until the game reaches a specific phase, ticking each frame.
 * Returns true if the phase was reached within max_frames.
 */
static inline bool wait_for_phase(coup_phase_t phase, int max_frames)
{
    int i;
    for (i = 0; i < max_frames; i++) {
        if (st()->phase == phase)
            return true;
        coup_tick();
    }
    return st()->phase == phase;
}

/**
 * Wait until the game reaches a specific screen, ticking each frame.
 * Returns true if the screen was reached within max_frames.
 */
static inline bool wait_for_screen(coup_screen_t screen, int max_frames)
{
    int i;
    for (i = 0; i < max_frames; i++) {
        if (st()->screen == screen)
            return true;
        coup_tick();
    }
    return st()->screen == screen;
}

#endif /* TEST_COUP_GAME_HELPERS_H */
