/**
 * coup_table_view.h - Shared table view for both bot AI and human rendering
 *
 * Builds a complete snapshot of what a given player can see at the table,
 * directly from the authoritative coup_rules_t state. This eliminates the
 * divergence between incremental event-driven UI state and snapshot-based
 * bot state — both paths now use the same builder.
 *
 * Pure function: no side effects, no I/O, no timers.
 */

#ifndef COUP_TABLE_VIEW_H
#define COUP_TABLE_VIEW_H

#include "coup_rules.h"

/*============================================================================
 * Per-Seat View (what you see looking at one player's area)
 *============================================================================*/

typedef struct {
    uint8_t coins;
    uint8_t cards[COUP_RULES_CARDS_PER_PLAYER];
    bool    revealed[COUP_RULES_CARDS_PER_PLAYER];
    bool    alive;
    bool    is_self;
} coup_seat_view_t;

/*============================================================================
 * Table View (complete snapshot for one viewer)
 *============================================================================*/

typedef struct {
    /* Viewer identity */
    uint8_t viewer_id;

    /* Seats */
    coup_seat_view_t seats[COUP_RULES_MAX_PLAYERS];
    int seat_count;

    /* Phase & turn */
    coup_turn_phase_t phase;
    uint8_t current_turn_player;

    /* Valid actions (only meaningful when it's viewer's turn) */
    uint8_t valid_actions;

    /* Pending response — does the viewer need to respond? */
    bool pending_response;

    /* Action context */
    uint8_t action_actor;
    uint8_t action_type;
    uint8_t action_target;      /* 0xFF if no target */
    uint8_t action_claim;       /* character being claimed, 0xFF if none */

    /* Block context */
    uint8_t blocker_id;         /* 0xFF if no blocker */
    uint8_t block_char;
    uint8_t blockable_by;       /* bitmask of characters that can block */

    /* Influence loss */
    uint8_t influence_loser;    /* 0xFF if none */

    /* Exchange (only populated for the exchange player) */
    uint8_t exchange_cards[4];
    int exchange_count;

    /* Game metadata */
    int round_number;
    bool game_active;
    uint8_t winner_id;          /* valid only when !game_active */
} coup_table_view_t;

/*============================================================================
 * API
 *============================================================================*/

/**
 * Build a complete table view from the authoritative rules state.
 *
 * Applies fog of war: the viewer can see their own cards and any revealed
 * cards, but opponents' unrevealed cards appear as COUP_RULES_CHAR_NONE.
 *
 * Exchange cards are only visible to the exchange player.
 *
 * This is a pure function with no side effects.
 */
void coup_table_view_from_rules(coup_table_view_t* view,
                                 const coup_rules_t* rules,
                                 uint8_t viewer_id);

#endif /* COUP_TABLE_VIEW_H */
