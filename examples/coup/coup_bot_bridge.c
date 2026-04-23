/**
 * coup_bot_bridge.c - Flat C API for bot FFI (Python ctypes)
 *
 * Reads the shared g_rules from coup_rules_bridge.c, builds a table view,
 * calls coup_bot_decide(), and stores the result for accessor queries.
 */

#include "coup_bot_bridge.h"
#include "coup_bot.h"
#include "coup_table_view.h"
#include "coup_rules.h"
#include <string.h>

/*============================================================================
 * Shared State (from coup_rules_bridge.c)
 *============================================================================*/

extern coup_rules_t g_rules;

/*============================================================================
 * Static Result Storage
 *============================================================================*/

static coup_bot_decision_t g_bot_result;

/*============================================================================
 * Main Decision Function
 *============================================================================*/

uint32_t bot_bridge_decide(uint8_t bot_id, uint8_t difficulty,
                           uint32_t rng_state)
{
    coup_table_view_t view;

    coup_table_view_from_rules(&view, &g_rules, bot_id);
    g_bot_result = coup_bot_decide(&view, difficulty, rng_state);

    return g_bot_result.rng_state;
}

/*============================================================================
 * Result Accessors
 *============================================================================*/

int bot_bridge_result_valid(void)
{
    return g_bot_result.valid ? 1 : 0;
}

uint8_t bot_bridge_result_input_type(void)
{
    return (uint8_t)g_bot_result.input.type;
}

uint8_t bot_bridge_result_player_id(void)
{
    return g_bot_result.input.player_id;
}

uint8_t bot_bridge_result_action(void)
{
    return g_bot_result.input.data.action.action;
}

uint8_t bot_bridge_result_target(void)
{
    return g_bot_result.input.data.action.target_id;
}

uint8_t bot_bridge_result_response(void)
{
    return g_bot_result.input.data.response.response;
}

uint8_t bot_bridge_result_card_idx(void)
{
    return g_bot_result.input.data.lose_influence.card_idx;
}

uint8_t bot_bridge_result_keep0(void)
{
    return g_bot_result.input.data.exchange_choice.keep[0];
}

uint8_t bot_bridge_result_keep1(void)
{
    return g_bot_result.input.data.exchange_choice.keep[1];
}

int bot_bridge_result_has_block_claim(void)
{
    return g_bot_result.has_block_claim ? 1 : 0;
}

uint8_t bot_bridge_result_block_claim_char(void)
{
    return g_bot_result.block_claim_char;
}
