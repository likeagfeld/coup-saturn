/**
 * coup_table_view.c - Shared table view builder
 *
 * Builds a complete game snapshot from coup_rules_t for a given viewer.
 * Used by both bot AI and human rendering.
 */

#include "coup_table_view.h"
#include <string.h>

void coup_table_view_from_rules(coup_table_view_t* view,
                                 const coup_rules_t* rules,
                                 uint8_t viewer_id)
{
    int i, j;

    memset(view, 0, sizeof(*view));

    view->viewer_id = viewer_id;
    view->seat_count = rules->player_count;

    /* Per-seat state with fog of war */
    for (i = 0; i < rules->player_count; i++) {
        const coup_rules_player_t* p = &rules->players[i];
        coup_seat_view_t* s = &view->seats[i];

        s->coins = p->coins;
        s->alive = p->alive;
        s->is_self = ((uint8_t)i == viewer_id);

        for (j = 0; j < COUP_RULES_CARDS_PER_PLAYER; j++) {
            s->revealed[j] = p->revealed[j];
            s->cards[j] = coup_rules_visible_card(rules, viewer_id,
                                                    (uint8_t)i, j);
        }
    }

    /* Phase and turn */
    view->phase = rules->phase;
    view->current_turn_player = coup_rules_current_player(rules);
    view->valid_actions = coup_rules_valid_actions(rules);

    /* Pending response for this viewer */
    if (viewer_id < COUP_RULES_MAX_PLAYERS) {
        view->pending_response = rules->pending_responses[viewer_id];
    }

    /* Action context */
    view->action_actor = rules->action_actor;
    view->action_type = rules->action_type;
    view->action_target = rules->action_target;
    {
        int claim = coup_rules_action_claim[rules->action_type];
        view->action_claim = (claim >= 0) ? (uint8_t)claim : 0xFF;
    }
    /* When no action is in progress, action_target is already 0xFF from rules */

    /* Block context */
    view->blocker_id = rules->blocker_id;
    view->block_char = rules->block_char;
    view->blockable_by = rules->current_blockable_by;

    /* Influence loss */
    view->influence_loser = rules->influence_loser;

    /* Exchange: only visible to the exchange player */
    if (rules->phase == COUP_TURN_WAITING_FOR_EXCHANGE &&
        rules->exchange_player == viewer_id) {
        view->exchange_count = rules->exchange_count;
        for (i = 0; i < rules->exchange_count && i < 4; i++) {
            view->exchange_cards[i] = rules->exchange_cards[i];
        }
    } else {
        view->exchange_count = 0;
    }

    /* Game metadata */
    view->round_number = rules->round_number;
    view->game_active = rules->game_active;

    /* Winner: scan for last alive if game is over */
    if (!rules->game_active && rules->phase != COUP_TURN_LOBBY) {
        int pi;
        for (pi = 0; pi < rules->player_count; pi++) {
            if (rules->players[pi].alive) {
                view->winner_id = (uint8_t)pi;
                break;
            }
        }
    } else {
        view->winner_id = 0xFF;
    }
}
