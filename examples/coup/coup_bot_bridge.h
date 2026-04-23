/**
 * coup_bot_bridge.h - Flat C API for bot FFI (Python ctypes)
 *
 * Wraps the coup_bot library with functions that use only basic types
 * (uint8_t, int, uint32_t) — no structs cross the FFI boundary.
 *
 * Reads game state from the shared g_rules in the rules bridge,
 * builds a bot view, calls coup_bot_decide(), and stores the result
 * for accessor queries.
 */

#ifndef COUP_BOT_BRIDGE_H
#define COUP_BOT_BRIDGE_H

#include <stdint.h>

/**
 * Compute a bot decision for the given player.
 *
 * Reads the current engine state from the rules bridge's g_rules,
 * builds a coup_bot_view_t, and calls coup_bot_decide().
 *
 * Parameters:
 *   bot_id      - which player to decide for
 *   difficulty  - 0=easy, 1=medium, 2=hard
 *   rng_state   - current RNG state for this bot
 *
 * Returns: updated rng_state (caller must store it)
 *
 * After calling, use bot_bridge_result_* accessors to read the decision.
 */
uint32_t bot_bridge_decide(uint8_t bot_id, uint8_t difficulty,
                           uint32_t rng_state);

/* Result accessors (valid after bot_bridge_decide) */
int     bot_bridge_result_valid(void);
uint8_t bot_bridge_result_input_type(void);
uint8_t bot_bridge_result_player_id(void);
uint8_t bot_bridge_result_action(void);
uint8_t bot_bridge_result_target(void);
uint8_t bot_bridge_result_response(void);
uint8_t bot_bridge_result_card_idx(void);
uint8_t bot_bridge_result_keep0(void);
uint8_t bot_bridge_result_keep1(void);
int     bot_bridge_result_has_block_claim(void);
uint8_t bot_bridge_result_block_claim_char(void);

#endif /* COUP_BOT_BRIDGE_H */
