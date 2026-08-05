/**
 * coup_qa_screen.h - Force a screen at boot, for capture QA only.
 *
 * WHY THIS EXISTS
 *   Lobby, game and game over are only reachable through a live server
 *   session, so an offline capture can never see them. Their backdrops and
 *   label placement were verified by gate and host test, which is real
 *   evidence but not the same as looking at the rendered frame - and the
 *   Saturn skill is explicit that a RENDERING bug's gate is the rendered
 *   frame (gotcha #4).
 *
 *   Compiling with -DCOUP_QA_SCREEN=<n> boots straight to that screen with
 *   enough synthetic state to render it, so every screen can be captured and
 *   measured.
 *
 * THIS IS NOT IN THE SHIPPED BUILD.
 *   With COUP_QA_SCREEN undefined, coup_qa_force_screen() compiles to an
 *   empty function and nothing else here is referenced. verify_facelift gate
 *   J fails if the macro is ever defined in a disc the build script produces
 *   normally, so a QA build cannot be shipped by accident.
 *
 *   It also writes no network traffic and changes no rules - it only fills
 *   the state struct the renderer reads, so the server contract is untouched.
 */

#ifndef COUP_QA_SCREEN_H
#define COUP_QA_SCREEN_H

/**
 * Overwrite the game state so `screen` renders with plausible content.
 * A no-op unless COUP_QA_SCREEN is defined at compile time.
 */
void coup_qa_force_screen(void);

#endif /* COUP_QA_SCREEN_H */
