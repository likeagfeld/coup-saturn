/**
 * coup_rules_bridge.c - Flat C API for FFI (Python ctypes)
 *
 * Wraps a single static coup_rules_t instance with accessor functions
 * that use only basic types. No structs cross the FFI boundary.
 */

#include "coup_rules_bridge.h"
#include "coup_rules.h"
#include <string.h>

/*============================================================================
 * Static State
 *============================================================================*/

/* Non-static so coup_bot_bridge.c can access via extern */
coup_rules_t g_rules;
static coup_event_t g_events[COUP_RULES_MAX_EVENTS];
static int g_event_count = 0;

/*============================================================================
 * Helpers
 *============================================================================*/

static const coup_event_t* get_event(int idx)
{
    if (idx >= 0 && idx < g_event_count) return &g_events[idx];
    return NULL;
}

static int submit(const coup_input_t* input)
{
    int result = coup_rules_submit(&g_rules, input, g_events, COUP_RULES_MAX_EVENTS);
    g_event_count = (result >= 0) ? result : 0;
    return result;
}

/*============================================================================
 * Lifecycle
 *============================================================================*/

void bridge_init(uint32_t seed)
{
    coup_rules_init(&g_rules, seed);
    g_event_count = 0;
    memset(g_events, 0, sizeof(g_events));
}

/*============================================================================
 * Submit Functions
 *============================================================================*/

int bridge_submit_start(void)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_START_GAME;
    return submit(&input);
}

int bridge_submit_action(uint8_t player_id, uint8_t action, uint8_t target)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_ACTION;
    input.player_id = player_id;
    input.data.action.action = action;
    input.data.action.target_id = target;
    return submit(&input);
}

int bridge_submit_response(uint8_t player_id, uint8_t response)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_RESPONSE;
    input.player_id = player_id;
    input.data.response.response = response;
    return submit(&input);
}

int bridge_submit_block_claim(uint8_t player_id, uint8_t character)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_BLOCK_CLAIM;
    input.player_id = player_id;
    input.data.block_claim.character = character;
    return submit(&input);
}

int bridge_submit_lose_influence(uint8_t player_id, uint8_t card_idx)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_LOSE_INFLUENCE;
    input.player_id = player_id;
    input.data.lose_influence.card_idx = card_idx;
    return submit(&input);
}

int bridge_submit_exchange(uint8_t player_id, uint8_t keep0, uint8_t keep1)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_EXCHANGE_CHOICE;
    input.player_id = player_id;
    input.data.exchange_choice.keep[0] = keep0;
    input.data.exchange_choice.keep[1] = keep1;
    return submit(&input);
}

int bridge_submit_timeout(void)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_TIMEOUT;
    return submit(&input);
}

int bridge_submit_add_player(void)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_ADD_PLAYER;
    return submit(&input);
}

int bridge_submit_add_bot(void)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_ADD_BOT;
    return submit(&input);
}

int bridge_submit_remove_player(uint8_t player_id)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_REMOVE_PLAYER;
    input.player_id = player_id;
    return submit(&input);
}

int bridge_submit_set_ready(uint8_t player_id, uint8_t ready)
{
    coup_input_t input;
    memset(&input, 0, sizeof(input));
    input.type = COUP_INPUT_SET_READY;
    input.player_id = player_id;
    input.data.set_ready.ready = ready;
    return submit(&input);
}

/*============================================================================
 * Event Access
 *============================================================================*/

int bridge_event_count(void) { return g_event_count; }

int bridge_event_type(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? (int)e->type : -1;
}

/*============================================================================
 * Event Field Accessors
 *============================================================================*/

/* GAME_STARTED */
uint8_t bridge_evt_game_started_count(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.game_started.player_count : 0;
}

/* TURN_STARTED */
uint8_t bridge_evt_turn_started_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.turn_started.player_id : 0;
}

uint8_t bridge_evt_turn_started_actions(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.turn_started.valid_actions : 0;
}

/* ACTION_DECLARED */
uint8_t bridge_evt_action_declared_actor(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_declared.actor_id : 0;
}

uint8_t bridge_evt_action_declared_action(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_declared.action : 0;
}

uint8_t bridge_evt_action_declared_target(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_declared.target_id : 0xFF;
}

/* CHALLENGE_OPENED */
uint8_t bridge_evt_challenge_opened_defender(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.challenge_opened.defender_id : 0;
}

uint8_t bridge_evt_challenge_opened_char(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.challenge_opened.claimed_char : 0;
}

/* CHALLENGE_RESULT */
uint8_t bridge_evt_challenge_result_challenger(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.challenge_result.challenger_id : 0;
}

uint8_t bridge_evt_challenge_result_defender(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.challenge_result.defender_id : 0;
}

int bridge_evt_challenge_result_had_card(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? (e->data.challenge_result.defender_had_card ? 1 : 0) : 0;
}

uint8_t bridge_evt_challenge_result_revealed(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.challenge_result.revealed_char : 0;
}

/* BLOCK_OPENED */
uint8_t bridge_evt_block_opened_blockable_by(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_opened.blockable_by : 0;
}

uint8_t bridge_evt_block_opened_target_only(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_opened.target_only : 0;
}

/* BLOCK_DECLARED */
uint8_t bridge_evt_block_declared_blocker(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_declared.blocker_id : 0;
}

uint8_t bridge_evt_block_declared_char(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_declared.character : 0;
}

/* BLOCK_CHALLENGE_OPENED */
uint8_t bridge_evt_block_challenge_opened_blocker(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_challenge_opened.blocker_id : 0;
}

uint8_t bridge_evt_block_challenge_opened_char(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_challenge_opened.claimed_char : 0;
}

/* BLOCK_CHALLENGE_RESULT */
uint8_t bridge_evt_block_challenge_result_challenger(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_challenge_result.challenger_id : 0;
}

uint8_t bridge_evt_block_challenge_result_blocker(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_challenge_result.blocker_id : 0;
}

int bridge_evt_block_challenge_result_had_card(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? (e->data.block_challenge_result.blocker_had_card ? 1 : 0) : 0;
}

uint8_t bridge_evt_block_challenge_result_revealed(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.block_challenge_result.revealed_char : 0;
}

/* INFLUENCE_LOSS_REQUESTED */
uint8_t bridge_evt_influence_loss_requested_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.influence_loss_requested.player_id : 0;
}

/* INFLUENCE_LOST */
uint8_t bridge_evt_influence_lost_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.influence_lost.player_id : 0;
}

uint8_t bridge_evt_influence_lost_card_idx(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.influence_lost.card_idx : 0;
}

uint8_t bridge_evt_influence_lost_char(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.influence_lost.revealed_char : 0;
}

/* EXCHANGE_OFFERED */
uint8_t bridge_evt_exchange_offered_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.exchange_offered.player_id : 0;
}

uint8_t bridge_evt_exchange_offered_card(int idx, int card_idx)
{
    const coup_event_t* e = get_event(idx);
    if (e && card_idx >= 0 && card_idx < 4)
        return e->data.exchange_offered.cards[card_idx];
    return 0;
}

uint8_t bridge_evt_exchange_offered_count(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.exchange_offered.count : 0;
}

/* EXCHANGE_RESOLVED */
uint8_t bridge_evt_exchange_resolved_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.exchange_resolved.player_id : 0;
}

/* COINS_CHANGED */
uint8_t bridge_evt_coins_changed_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.coins_changed.player_id : 0;
}

uint8_t bridge_evt_coins_changed_old(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.coins_changed.old_coins : 0;
}

uint8_t bridge_evt_coins_changed_new(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.coins_changed.new_coins : 0;
}

/* PLAYER_ELIMINATED */
uint8_t bridge_evt_player_eliminated_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.player_eliminated.player_id : 0;
}

/* ACTION_RESOLVED */
uint8_t bridge_evt_action_resolved_action(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_resolved.action : 0;
}

uint8_t bridge_evt_action_resolved_actor(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_resolved.actor_id : 0;
}

uint8_t bridge_evt_action_resolved_target(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_resolved.target_id : 0xFF;
}

/* ACTION_CANCELLED */
uint8_t bridge_evt_action_cancelled_action(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_cancelled.action : 0;
}

uint8_t bridge_evt_action_cancelled_actor(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_cancelled.actor_id : 0;
}

uint8_t bridge_evt_action_cancelled_reason(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.action_cancelled.reason : 0;
}

/* CARD_REPLACED */
uint8_t bridge_evt_card_replaced_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.card_replaced.player_id : 0;
}

uint8_t bridge_evt_card_replaced_card_idx(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.card_replaced.card_idx : 0;
}

uint8_t bridge_evt_card_replaced_new_char(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.card_replaced.new_char : 0;
}

/* ROUND_ADVANCED */
uint8_t bridge_evt_round_advanced_number(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.round_advanced.round_number : 0;
}

/* GAME_OVER */
uint8_t bridge_evt_game_over_winner(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.game_over.winner_id : 0;
}

/* PLAYER_JOINED */
uint8_t bridge_evt_player_joined_id(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.player_joined.player_id : 0;
}

uint8_t bridge_evt_player_joined_is_bot(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.player_joined.is_bot : 0;
}

/* PLAYER_LEFT */
uint8_t bridge_evt_player_left_id(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.player_left.player_id : 0;
}

/* READY_CHANGED */
uint8_t bridge_evt_ready_changed_player(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.ready_changed.player_id : 0;
}

uint8_t bridge_evt_ready_changed_ready(int idx)
{
    const coup_event_t* e = get_event(idx);
    return e ? e->data.ready_changed.ready : 0;
}

/*============================================================================
 * State Queries
 *============================================================================*/

uint8_t bridge_phase(void)
{
    return (uint8_t)g_rules.phase;
}

uint8_t bridge_current_player(void)
{
    return coup_rules_current_player(&g_rules);
}

uint8_t bridge_valid_actions(void)
{
    return coup_rules_valid_actions(&g_rules);
}

int bridge_game_active(void)
{
    return g_rules.game_active ? 1 : 0;
}

/*============================================================================
 * Player State
 *============================================================================*/

uint8_t bridge_player_card(int pid, int slot)
{
    if (pid < 0 || pid >= g_rules.player_count) return 0;
    if (slot < 0 || slot >= COUP_RULES_CARDS_PER_PLAYER) return 0;
    return g_rules.players[pid].cards[slot];
}

int bridge_player_revealed(int pid, int slot)
{
    if (pid < 0 || pid >= g_rules.player_count) return 0;
    if (slot < 0 || slot >= COUP_RULES_CARDS_PER_PLAYER) return 0;
    return g_rules.players[pid].revealed[slot] ? 1 : 0;
}

uint8_t bridge_player_coins(int pid)
{
    if (pid < 0 || pid >= g_rules.player_count) return 0;
    return g_rules.players[pid].coins;
}

int bridge_player_alive(int pid)
{
    if (pid < 0 || pid >= g_rules.player_count) return 0;
    return g_rules.players[pid].alive ? 1 : 0;
}

int bridge_player_ready(int pid)
{
    if (pid < 0 || pid >= g_rules.player_count) return 0;
    return g_rules.players[pid].ready ? 1 : 0;
}

int bridge_player_is_bot(int pid)
{
    if (pid < 0 || pid >= g_rules.player_count) return 0;
    return g_rules.players[pid].is_bot ? 1 : 0;
}

int bridge_player_count(void)
{
    return g_rules.player_count;
}

/*============================================================================
 * Timeout Helpers
 *============================================================================*/

uint8_t bridge_influence_loser(void)
{
    return g_rules.influence_loser;
}

uint8_t bridge_exchange_player(void)
{
    return g_rules.exchange_player;
}

uint8_t bridge_blocker_id(void)
{
    return g_rules.blocker_id;
}

/*============================================================================
 * Exchange Cards
 *============================================================================*/

uint8_t bridge_exchange_card(int idx)
{
    if (idx < 0 || idx >= g_rules.exchange_count) return 0;
    return g_rules.exchange_cards[idx];
}

int bridge_exchange_count(void)
{
    return g_rules.exchange_count;
}

/*============================================================================
 * Pending Response Tracking
 *============================================================================*/

int bridge_pending_response(int pid)
{
    if (pid < 0 || pid >= COUP_RULES_MAX_PLAYERS) return 0;
    return g_rules.pending_responses[pid] ? 1 : 0;
}

int bridge_pending_count(void)
{
    return g_rules.pending_count;
}
