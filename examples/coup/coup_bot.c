/**
 * coup_bot.c - Shared Bot AI for Coup Card Game
 *
 * Pure C bot decision logic extracted from coup_game.c.
 * No malloc, no I/O, no platform dependencies.
 * Three difficulty tiers: easy, medium, hard.
 */

#include "coup_bot.h"
#include <string.h>

/*============================================================================
 * Internal RNG (xorshift32)
 *============================================================================*/

static uint32_t bot_rand(uint32_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

/*============================================================================
 * Target Selection
 *============================================================================*/

/**
 * Pick a target for a targeted action.
 * - Steal: pick richest opponent
 * - Assassinate/Coup: pick opponent with most unrevealed cards,
 *   with a bias toward the human player on hard difficulty
 */
static int pick_target(const coup_table_view_t* view, int action,
                       uint8_t difficulty)
{
    int i, best = -1;
    int best_score = -1;

    for (i = 0; i < view->seat_count; i++) {
        if (i == (int)view->viewer_id) continue;
        if (!view->seats[i].alive) continue;

        if (action == COUP_RULES_ACT_STEAL) {
            /* Steal from richest */
            if ((int)view->seats[i].coins > best_score) {
                best_score = (int)view->seats[i].coins;
                best = i;
            }
        } else {
            /* Assassinate/Coup: target with most cards */
            int cards = 0, j;
            for (j = 0; j < COUP_RULES_CARDS_PER_PLAYER; j++) {
                if (!view->seats[i].revealed[j]) cards++;
            }
            if (cards > best_score) {
                best_score = cards;
                best = i;
            } else if (cards == best_score &&
                       difficulty >= COUP_BOT_DIFFICULTY_HARD &&
                       !view->seats[i].is_self) {
                /* Hard: among ties, prefer non-self (i.e. other bots),
                 * but the original logic biased toward the human player.
                 * Since we don't know who is human, we keep first found. */
            }
        }
    }
    return best;
}

/*============================================================================
 * Action Decision (bot's turn to act)
 *============================================================================*/

static void decide_action(const coup_table_view_t* view,
                           uint8_t difficulty,
                           coup_bot_decision_t* result)
{
    int action, target_id;
    int r, diff;
    uint32_t rng = result->rng_state;
    uint8_t coins = view->seats[view->viewer_id].coins;

    diff = difficulty;
    r = (int)(bot_rand(&rng) % 100);

    /* Forced coup at 10+ coins */
    if (coins >= COUP_RULES_FORCE_COUP_COINS) {
        action = COUP_RULES_ACT_COUP;
    } else if (diff == COUP_BOT_DIFFICULTY_EASY) {
        if (coins >= COUP_RULES_COUP_COST && r < 15)
            action = COUP_RULES_ACT_COUP;
        else if (r < 40) action = COUP_RULES_ACT_INCOME;
        else if (r < 70) action = COUP_RULES_ACT_FOREIGN_AID;
        else if (r < 85) action = COUP_RULES_ACT_TAX;
        else action = COUP_RULES_ACT_EXCHANGE;
    } else if (diff == COUP_BOT_DIFFICULTY_MEDIUM) {
        if (coins >= COUP_RULES_COUP_COST && r < 40)
            action = COUP_RULES_ACT_COUP;
        else if (coins >= COUP_RULES_ASSASSINATE_COST && r < 20)
            action = COUP_RULES_ACT_ASSASSINATE;
        else if (r < 45) action = COUP_RULES_ACT_TAX;
        else if (r < 65) action = COUP_RULES_ACT_STEAL;
        else if (r < 85) action = COUP_RULES_ACT_FOREIGN_AID;
        else action = COUP_RULES_ACT_INCOME;
    } else {
        /* Hard difficulty */
        if (coins >= COUP_RULES_COUP_COST) {
            if (r < 70) action = COUP_RULES_ACT_COUP;
            else if (r < 85) action = COUP_RULES_ACT_TAX;
            else action = COUP_RULES_ACT_STEAL;
        } else if (coins >= COUP_RULES_ASSASSINATE_COST) {
            if (r < 60) action = COUP_RULES_ACT_ASSASSINATE;
            else if (r < 85) action = COUP_RULES_ACT_TAX;
            else action = COUP_RULES_ACT_STEAL;
        } else {
            if (r < 50) action = COUP_RULES_ACT_TAX;
            else if (r < 75) action = COUP_RULES_ACT_STEAL;
            else if (r < 90) action = COUP_RULES_ACT_FOREIGN_AID;
            else action = COUP_RULES_ACT_INCOME;
        }
    }

    /* Resolve target for targeted actions */
    target_id = -1;
    if (coup_rules_action_needs_target[action]) {
        target_id = pick_target(view, action, difficulty);

        /* If stealing from someone with 0 coins, fallback to tax */
        if (action == COUP_RULES_ACT_STEAL && target_id >= 0 &&
            view->seats[target_id].coins == 0) {
            action = COUP_RULES_ACT_TAX;
            target_id = -1;
        }

        /* If still no valid target, fallback to tax */
        if (target_id < 0 && coup_rules_action_needs_target[action]) {
            action = COUP_RULES_ACT_TAX;
            target_id = -1;
        }
    }

    result->input.type = COUP_INPUT_ACTION;
    result->input.player_id = view->viewer_id;
    result->input.data.action.action = (uint8_t)action;
    result->input.data.action.target_id =
        (target_id >= 0) ? (uint8_t)target_id : 0xFF;
    result->valid = true;
    result->rng_state = rng;
}

/*============================================================================
 * Response Decision (challenge/block windows)
 *============================================================================*/

static void decide_response(const coup_table_view_t* view,
                             coup_bot_decision_t* result)
{
    uint32_t rng = result->rng_state;
    uint8_t response;

    if (view->phase == COUP_TURN_CHALLENGE_WINDOW) {
        /* ~25% chance to challenge */
        response = ((bot_rand(&rng) % 4) == 0)
            ? COUP_RULES_RESP_CHALLENGE : COUP_RULES_RESP_PASS;
    } else if (view->phase == COUP_TURN_BLOCK_WINDOW) {
        /* ~33% chance to block */
        response = ((bot_rand(&rng) % 3) == 0)
            ? COUP_RULES_RESP_BLOCK : COUP_RULES_RESP_PASS;
    } else if (view->phase == COUP_TURN_BLOCK_CHALLENGE_WINDOW) {
        /* ~25% chance to challenge a block */
        response = ((bot_rand(&rng) % 4) == 0)
            ? COUP_RULES_RESP_CHALLENGE : COUP_RULES_RESP_PASS;
    } else {
        response = COUP_RULES_RESP_PASS;
    }

    result->input.type = COUP_INPUT_RESPONSE;
    result->input.player_id = view->viewer_id;
    result->input.data.response.response = response;
    result->valid = true;
    result->rng_state = rng;

    /* If blocking, also determine block claim character */
    if (response == COUP_RULES_RESP_BLOCK) {
        int c;
        uint8_t mask = view->blockable_by;
        result->has_block_claim = true;
        result->block_claim_char = COUP_RULES_CHAR_DUKE; /* fallback */
        for (c = 0; c < COUP_RULES_NUM_CHARACTERS; c++) {
            if (mask & (1 << c)) {
                result->block_claim_char = (uint8_t)c;
                break;
            }
        }
    }
}

/*============================================================================
 * Influence Loss Decision
 *============================================================================*/

static void decide_influence_loss(const coup_table_view_t* view,
                                   coup_bot_decision_t* result)
{
    /* Simple: lose first unrevealed card (card_idx 0).
     * The engine auto-picks a valid slot if 0 is already revealed. */
    (void)view;
    result->input.type = COUP_INPUT_LOSE_INFLUENCE;
    result->input.player_id = view->viewer_id;
    result->input.data.lose_influence.card_idx = 0;
    result->valid = true;
}

/*============================================================================
 * Exchange Decision
 *============================================================================*/

static void decide_exchange(const coup_table_view_t* view,
                             coup_bot_decision_t* result)
{
    /* Simple: keep first two cards (indices 0 and 1) */
    (void)view;
    result->input.type = COUP_INPUT_EXCHANGE_CHOICE;
    result->input.player_id = view->viewer_id;
    result->input.data.exchange_choice.keep[0] = 0;
    result->input.data.exchange_choice.keep[1] = 1;
    result->valid = true;
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/

coup_bot_decision_t coup_bot_decide(const coup_table_view_t* view,
                                    uint8_t difficulty,
                                    uint32_t rng_state)
{
    coup_bot_decision_t result;
    memset(&result, 0, sizeof(result));
    result.rng_state = rng_state;
    result.valid = false;

    switch (view->phase) {
    case COUP_TURN_WAITING_FOR_ACTION:
        if (view->current_turn_player == view->viewer_id) {
            decide_action(view, difficulty, &result);
        }
        break;

    case COUP_TURN_CHALLENGE_WINDOW:
    case COUP_TURN_BLOCK_WINDOW:
    case COUP_TURN_BLOCK_CHALLENGE_WINDOW:
        if (view->pending_response) {
            decide_response(view, &result);
        }
        break;

    case COUP_TURN_WAITING_FOR_INFLUENCE_LOSS:
        if (view->influence_loser == view->viewer_id) {
            decide_influence_loss(view, &result);
        }
        break;

    case COUP_TURN_WAITING_FOR_EXCHANGE:
        if (view->exchange_count > 0) {
            decide_exchange(view, &result);
        }
        break;

    default:
        break;
    }

    return result;
}
