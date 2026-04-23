/**
 * coup_bot.h - Shared Bot AI for Coup Card Game
 *
 * Pure C bot decision library. Takes a table view snapshot (what the bot
 * can see) and returns the input it wants to submit. No malloc, no I/O,
 * no side effects — same pattern as coup_rules.h.
 *
 * Used by both the local client (coup_game.c) and the network server
 * (via coup_bot_bridge for Python FFI).
 */

#ifndef COUP_BOT_H
#define COUP_BOT_H

#include "coup_table_view.h"

/*============================================================================
 * Constants
 *============================================================================*/

#define COUP_BOT_DIFFICULTY_EASY   0
#define COUP_BOT_DIFFICULTY_MEDIUM 1
#define COUP_BOT_DIFFICULTY_HARD   2

/*============================================================================
 * Decision Result
 *============================================================================*/

/**
 * Decision returned by coup_bot_decide().
 */
typedef struct {
    coup_input_t input;         /* The chosen input */
    bool         valid;         /* false if bot has nothing to do */
    uint32_t     rng_state;     /* Updated RNG state (caller stores this) */
    bool         has_block_claim;   /* If response was BLOCK, includes claim */
    uint8_t      block_claim_char;  /* Character for the block claim */
} coup_bot_decision_t;

/*============================================================================
 * API
 *============================================================================*/

/**
 * Pure function: given a table view of the game, decide what input to submit.
 *
 * Same view + same RNG state = same decision (deterministic).
 * Caller is responsible for submitting the input and managing timing.
 */
coup_bot_decision_t coup_bot_decide(const coup_table_view_t* view,
                                    uint8_t difficulty,
                                    uint32_t rng_state);

#endif /* COUP_BOT_H */
