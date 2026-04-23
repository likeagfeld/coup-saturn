/**
 * coup_rules_bridge.h - Flat C API for FFI (Python ctypes)
 *
 * Wraps the coup_rules engine with functions that use only basic types
 * (uint8_t, int, uint32_t) — no structs cross the FFI boundary.
 *
 * Holds a single static coup_rules_t and event buffer internally.
 */

#ifndef COUP_RULES_BRIDGE_H
#define COUP_RULES_BRIDGE_H

#include <stdint.h>

/* Lifecycle */
void bridge_init(uint32_t seed);

/* Submit inputs — each returns event count (or -1 on error) */
int bridge_submit_start(void);
int bridge_submit_action(uint8_t player_id, uint8_t action, uint8_t target);
int bridge_submit_response(uint8_t player_id, uint8_t response);
int bridge_submit_block_claim(uint8_t player_id, uint8_t character);
int bridge_submit_lose_influence(uint8_t player_id, uint8_t card_idx);
int bridge_submit_exchange(uint8_t player_id, uint8_t keep0, uint8_t keep1);
int bridge_submit_timeout(void);

/* Lobby inputs */
int bridge_submit_add_player(void);
int bridge_submit_add_bot(void);
int bridge_submit_remove_player(uint8_t player_id);
int bridge_submit_set_ready(uint8_t player_id, uint8_t ready);

/* Event access — read events from the last submit */
int     bridge_event_count(void);
int     bridge_event_type(int idx);

/* Per-event-type field accessors */
uint8_t bridge_evt_game_started_count(int idx);

uint8_t bridge_evt_turn_started_player(int idx);
uint8_t bridge_evt_turn_started_actions(int idx);

uint8_t bridge_evt_action_declared_actor(int idx);
uint8_t bridge_evt_action_declared_action(int idx);
uint8_t bridge_evt_action_declared_target(int idx);

uint8_t bridge_evt_challenge_opened_defender(int idx);
uint8_t bridge_evt_challenge_opened_char(int idx);

uint8_t bridge_evt_challenge_result_challenger(int idx);
uint8_t bridge_evt_challenge_result_defender(int idx);
int     bridge_evt_challenge_result_had_card(int idx);
uint8_t bridge_evt_challenge_result_revealed(int idx);

uint8_t bridge_evt_block_opened_blockable_by(int idx);
uint8_t bridge_evt_block_opened_target_only(int idx);

uint8_t bridge_evt_block_declared_blocker(int idx);
uint8_t bridge_evt_block_declared_char(int idx);

uint8_t bridge_evt_block_challenge_opened_blocker(int idx);
uint8_t bridge_evt_block_challenge_opened_char(int idx);

uint8_t bridge_evt_block_challenge_result_challenger(int idx);
uint8_t bridge_evt_block_challenge_result_blocker(int idx);
int     bridge_evt_block_challenge_result_had_card(int idx);
uint8_t bridge_evt_block_challenge_result_revealed(int idx);

uint8_t bridge_evt_influence_loss_requested_player(int idx);

uint8_t bridge_evt_influence_lost_player(int idx);
uint8_t bridge_evt_influence_lost_card_idx(int idx);
uint8_t bridge_evt_influence_lost_char(int idx);

uint8_t bridge_evt_exchange_offered_player(int idx);
uint8_t bridge_evt_exchange_offered_card(int idx, int card_idx);
uint8_t bridge_evt_exchange_offered_count(int idx);

uint8_t bridge_evt_exchange_resolved_player(int idx);

uint8_t bridge_evt_coins_changed_player(int idx);
uint8_t bridge_evt_coins_changed_old(int idx);
uint8_t bridge_evt_coins_changed_new(int idx);

uint8_t bridge_evt_player_eliminated_player(int idx);

uint8_t bridge_evt_action_resolved_action(int idx);
uint8_t bridge_evt_action_resolved_actor(int idx);
uint8_t bridge_evt_action_resolved_target(int idx);

uint8_t bridge_evt_action_cancelled_action(int idx);
uint8_t bridge_evt_action_cancelled_actor(int idx);
uint8_t bridge_evt_action_cancelled_reason(int idx);

uint8_t bridge_evt_card_replaced_player(int idx);
uint8_t bridge_evt_card_replaced_card_idx(int idx);
uint8_t bridge_evt_card_replaced_new_char(int idx);

uint8_t bridge_evt_round_advanced_number(int idx);

uint8_t bridge_evt_game_over_winner(int idx);

/* Lobby event accessors */
uint8_t bridge_evt_player_joined_id(int idx);
uint8_t bridge_evt_player_joined_is_bot(int idx);
uint8_t bridge_evt_player_left_id(int idx);
uint8_t bridge_evt_ready_changed_player(int idx);
uint8_t bridge_evt_ready_changed_ready(int idx);

/* State queries */
uint8_t bridge_phase(void);
uint8_t bridge_current_player(void);
uint8_t bridge_valid_actions(void);
int     bridge_game_active(void);

/* Player state */
uint8_t bridge_player_card(int pid, int slot);
int     bridge_player_revealed(int pid, int slot);
uint8_t bridge_player_coins(int pid);
int     bridge_player_alive(int pid);
int     bridge_player_ready(int pid);
int     bridge_player_is_bot(int pid);
int     bridge_player_count(void);

/* Timeout helpers */
uint8_t bridge_influence_loser(void);
uint8_t bridge_exchange_player(void);
uint8_t bridge_blocker_id(void);

/* Exchange cards (from EXCHANGE_OFFERED state) */
uint8_t bridge_exchange_card(int idx);
int     bridge_exchange_count(void);

/* Pending response tracking */
int     bridge_pending_response(int pid);
int     bridge_pending_count(void);

#endif /* COUP_RULES_BRIDGE_H */
