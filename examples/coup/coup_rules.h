/**
 * coup_rules.h - Authoritative Coup Game Rule Engine
 *
 * Pure C state machine for Coup card game logic.
 * Processes player inputs, maintains game state, emits events.
 * No platform dependencies, no timers, fully deterministic.
 *
 * Used by both the network server and local bot mode.
 */

#ifndef COUP_RULES_H
#define COUP_RULES_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Constants (mirrors coup.h)
 *============================================================================*/

#define COUP_RULES_MAX_PLAYERS      7
#define COUP_RULES_CARDS_PER_PLAYER 2
#define COUP_RULES_DECK_SIZE        15  /* 3 copies of 5 characters */
#define COUP_RULES_NUM_CHARACTERS   5
#define COUP_RULES_NUM_ACTIONS      7
#define COUP_RULES_INITIAL_COINS    2
#define COUP_RULES_COUP_COST        7
#define COUP_RULES_ASSASSINATE_COST 3
#define COUP_RULES_FORCE_COUP_COINS 10
#define COUP_RULES_MAX_EVENTS       16  /* max events from a single submit */

/* Character IDs */
#define COUP_RULES_CHAR_DUKE        0
#define COUP_RULES_CHAR_ASSASSIN    1
#define COUP_RULES_CHAR_CAPTAIN     2
#define COUP_RULES_CHAR_AMBASSADOR  3
#define COUP_RULES_CHAR_CONTESSA    4
#define COUP_RULES_CHAR_NONE        6

/* Action IDs */
#define COUP_RULES_ACT_INCOME       0
#define COUP_RULES_ACT_FOREIGN_AID  1
#define COUP_RULES_ACT_COUP         2
#define COUP_RULES_ACT_TAX          3
#define COUP_RULES_ACT_ASSASSINATE  4
#define COUP_RULES_ACT_STEAL        5
#define COUP_RULES_ACT_EXCHANGE     6

/* Response types */
#define COUP_RULES_RESP_PASS        0
#define COUP_RULES_RESP_CHALLENGE   1
#define COUP_RULES_RESP_BLOCK       2

/*============================================================================
 * Turn Phase
 *============================================================================*/

typedef enum {
    COUP_TURN_LOBBY,
    COUP_TURN_WAITING_FOR_ACTION,
    COUP_TURN_CHALLENGE_WINDOW,
    COUP_TURN_BLOCK_WINDOW,
    COUP_TURN_BLOCK_CHALLENGE_WINDOW,
    COUP_TURN_WAITING_FOR_INFLUENCE_LOSS,
    COUP_TURN_WAITING_FOR_EXCHANGE,
    COUP_TURN_RESOLVING
} coup_turn_phase_t;

/*============================================================================
 * Input Types
 *============================================================================*/

typedef enum {
    COUP_INPUT_START_GAME,
    COUP_INPUT_ACTION,
    COUP_INPUT_RESPONSE,
    COUP_INPUT_BLOCK_CLAIM,
    COUP_INPUT_LOSE_INFLUENCE,
    COUP_INPUT_EXCHANGE_CHOICE,
    COUP_INPUT_TIMEOUT,
    COUP_INPUT_ADD_PLAYER,
    COUP_INPUT_ADD_BOT,
    COUP_INPUT_REMOVE_PLAYER,
    COUP_INPUT_SET_READY
} coup_input_type_t;

typedef struct {
    coup_input_type_t type;
    uint8_t player_id;

    union {
        /* COUP_INPUT_ACTION */
        struct {
            uint8_t action;
            uint8_t target_id;
        } action;

        /* COUP_INPUT_RESPONSE */
        struct {
            uint8_t response;   /* RESP_PASS, RESP_CHALLENGE, RESP_BLOCK */
        } response;

        /* COUP_INPUT_BLOCK_CLAIM */
        struct {
            uint8_t character;  /* which character the blocker claims */
        } block_claim;

        /* COUP_INPUT_LOSE_INFLUENCE */
        struct {
            uint8_t card_idx;   /* 0 or 1 */
        } lose_influence;

        /* COUP_INPUT_EXCHANGE_CHOICE */
        struct {
            uint8_t keep[2];    /* indices into the 4-card offer */
        } exchange_choice;

        /* COUP_INPUT_SET_READY */
        struct {
            uint8_t ready;      /* 0 = not ready, 1 = ready */
        } set_ready;
    } data;
} coup_input_t;

/*============================================================================
 * Event Types
 *============================================================================*/

typedef enum {
    COUP_EVT_GAME_STARTED,
    COUP_EVT_TURN_STARTED,
    COUP_EVT_ACTION_DECLARED,
    COUP_EVT_CHALLENGE_OPENED,
    COUP_EVT_CHALLENGE_RESULT,
    COUP_EVT_BLOCK_OPENED,
    COUP_EVT_BLOCK_DECLARED,
    COUP_EVT_BLOCK_CHALLENGE_OPENED,
    COUP_EVT_BLOCK_CHALLENGE_RESULT,
    COUP_EVT_INFLUENCE_LOSS_REQUESTED,
    COUP_EVT_INFLUENCE_LOST,
    COUP_EVT_EXCHANGE_OFFERED,
    COUP_EVT_EXCHANGE_RESOLVED,
    COUP_EVT_COINS_CHANGED,
    COUP_EVT_PLAYER_ELIMINATED,
    COUP_EVT_ACTION_RESOLVED,
    COUP_EVT_ACTION_CANCELLED,
    COUP_EVT_CARD_REPLACED,
    COUP_EVT_ROUND_ADVANCED,
    COUP_EVT_GAME_OVER,
    COUP_EVT_PLAYER_JOINED,
    COUP_EVT_PLAYER_LEFT,
    COUP_EVT_READY_CHANGED
} coup_event_type_t;

typedef struct {
    coup_event_type_t type;

    union {
        /* COUP_EVT_GAME_STARTED */
        struct {
            uint8_t player_count;
        } game_started;

        /* COUP_EVT_TURN_STARTED */
        struct {
            uint8_t player_id;
            uint8_t valid_actions;  /* bitmask */
        } turn_started;

        /* COUP_EVT_ACTION_DECLARED */
        struct {
            uint8_t actor_id;
            uint8_t action;
            uint8_t target_id;      /* 0xFF if no target */
        } action_declared;

        /* COUP_EVT_CHALLENGE_OPENED */
        struct {
            uint8_t defender_id;
            uint8_t claimed_char;
        } challenge_opened;

        /* COUP_EVT_CHALLENGE_RESULT */
        struct {
            uint8_t challenger_id;
            uint8_t defender_id;
            bool    defender_had_card;
            uint8_t revealed_char;
        } challenge_result;

        /* COUP_EVT_BLOCK_OPENED */
        struct {
            uint8_t blockable_by;   /* bitmask of characters that can block */
            uint8_t target_only;    /* true if only target can block */
        } block_opened;

        /* COUP_EVT_BLOCK_DECLARED */
        struct {
            uint8_t blocker_id;
            uint8_t character;
        } block_declared;

        /* COUP_EVT_BLOCK_CHALLENGE_OPENED */
        struct {
            uint8_t blocker_id;
            uint8_t claimed_char;
        } block_challenge_opened;

        /* COUP_EVT_BLOCK_CHALLENGE_RESULT */
        struct {
            uint8_t challenger_id;
            uint8_t blocker_id;
            bool    blocker_had_card;
            uint8_t revealed_char;
        } block_challenge_result;

        /* COUP_EVT_INFLUENCE_LOSS_REQUESTED */
        struct {
            uint8_t player_id;
        } influence_loss_requested;

        /* COUP_EVT_INFLUENCE_LOST */
        struct {
            uint8_t player_id;
            uint8_t card_idx;
            uint8_t revealed_char;
        } influence_lost;

        /* COUP_EVT_EXCHANGE_OFFERED */
        struct {
            uint8_t player_id;
            uint8_t cards[4];
            uint8_t count;
        } exchange_offered;

        /* COUP_EVT_EXCHANGE_RESOLVED */
        struct {
            uint8_t player_id;
        } exchange_resolved;

        /* COUP_EVT_COINS_CHANGED */
        struct {
            uint8_t player_id;
            uint8_t old_coins;
            uint8_t new_coins;
        } coins_changed;

        /* COUP_EVT_PLAYER_ELIMINATED */
        struct {
            uint8_t player_id;
        } player_eliminated;

        /* COUP_EVT_ACTION_RESOLVED */
        struct {
            uint8_t action;
            uint8_t actor_id;
            uint8_t target_id;
        } action_resolved;

        /* COUP_EVT_ACTION_CANCELLED */
        struct {
            uint8_t action;
            uint8_t actor_id;
            uint8_t reason;     /* 0=challenged, 1=blocked */
        } action_cancelled;

        /* COUP_EVT_CARD_REPLACED */
        struct {
            uint8_t player_id;
            uint8_t card_idx;
            uint8_t new_char;   /* private: only sent to card owner */
        } card_replaced;

        /* COUP_EVT_ROUND_ADVANCED */
        struct {
            uint8_t round_number;
        } round_advanced;

        /* COUP_EVT_GAME_OVER */
        struct {
            uint8_t winner_id;
        } game_over;

        /* COUP_EVT_PLAYER_JOINED */
        struct {
            uint8_t player_id;
            uint8_t is_bot;
        } player_joined;

        /* COUP_EVT_PLAYER_LEFT */
        struct {
            uint8_t player_id;
        } player_left;

        /* COUP_EVT_READY_CHANGED */
        struct {
            uint8_t player_id;
            uint8_t ready;
        } ready_changed;
    } data;
} coup_event_t;

/*============================================================================
 * Player State (internal to rule engine)
 *============================================================================*/

typedef struct {
    uint8_t cards[COUP_RULES_CARDS_PER_PLAYER];
    bool    revealed[COUP_RULES_CARDS_PER_PLAYER];
    uint8_t coins;
    bool    alive;
    bool    ready;
    bool    is_bot;
} coup_rules_player_t;

/*============================================================================
 * Rule Engine State
 *============================================================================*/

typedef struct {
    /* Players */
    coup_rules_player_t players[COUP_RULES_MAX_PLAYERS];
    int player_count;

    /* Deck */
    uint8_t deck[COUP_RULES_DECK_SIZE];
    int deck_count;

    /* Turn order */
    uint8_t turn_order[COUP_RULES_MAX_PLAYERS];
    int turn_order_count;
    int current_turn_idx;
    int round_number;

    /* Phase */
    coup_turn_phase_t phase;
    bool game_active;

    /* Current action state */
    uint8_t action_actor;
    uint8_t action_type;
    uint8_t action_target;  /* 0xFF = no target */

    /* Challenge/block tracking */
    uint8_t blocker_id;     /* 0xFF = no blocker */
    uint8_t block_char;
    uint8_t influence_loser; /* 0xFF = none */
    bool    pending_responses[COUP_RULES_MAX_PLAYERS];
    int     pending_count;

    /* After-challenge routing:
     *  -1 = not set
     *   0 = action fails (challenge succeeded, defender was bluffing)
     *   1 = action proceeds (challenge failed, defender had card)
     *   2 = block stands (block-challenge failed, blocker had card)
     */
    int8_t after_challenge_result;

    /* Exchange state */
    uint8_t exchange_cards[4];
    int exchange_count;
    uint8_t exchange_player;

    /* Blockable-by mask for current action (set by open_block_window) */
    uint8_t current_blockable_by;

    /* PRNG */
    uint32_t rng_state;

    /* Per-instance emit state (avoids static globals for thread safety) */
    coup_event_t* _emit_buf;
    int _emit_max;
    int _emit_idx;
} coup_rules_t;

/*============================================================================
 * Action Metadata
 * NOTE: These tables mirror coup_action_claim[] and coup_action_needs_target[]
 * in coup.h (the game UI layer). Keep both in sync when modifying.
 *============================================================================*/

/* Which character each action claims (-1 = no claim) */
static const int __attribute__((unused)) coup_rules_action_claim[COUP_RULES_NUM_ACTIONS] = {
    -1, -1, -1,
    COUP_RULES_CHAR_DUKE,
    COUP_RULES_CHAR_ASSASSIN,
    COUP_RULES_CHAR_CAPTAIN,
    COUP_RULES_CHAR_AMBASSADOR
};

/* Does this action need a target player? */
static const bool __attribute__((unused)) coup_rules_action_needs_target[COUP_RULES_NUM_ACTIONS] = {
    false, false, true,
    false, true, true, false
};

/*============================================================================
 * API
 *============================================================================*/

/**
 * Initialize a rule engine with an empty lobby.
 * Players are added via COUP_INPUT_ADD_PLAYER / COUP_INPUT_ADD_BOT.
 * Cards are dealt when START_GAME is submitted (requires 2+ ready).
 */
void coup_rules_init(coup_rules_t* rules, uint32_t seed);

/**
 * Submit a player input to the rule engine.
 * Validates the input against current state.
 * Produces zero or more events describing what happened.
 *
 * Returns: number of events written to events_out, or -1 on invalid input.
 */
int coup_rules_submit(coup_rules_t* rules, const coup_input_t* input,
                      coup_event_t* events_out, int max_events);

/**
 * Get the current player whose turn it is (by player index in turn_order).
 */
uint8_t coup_rules_current_player(const coup_rules_t* rules);

/**
 * Compute valid actions bitmask for the current player.
 */
uint8_t coup_rules_valid_actions(const coup_rules_t* rules);

/**
 * Check card visibility: can viewer_id see player_id's card at card_idx?
 * Returns the actual card character if visible (revealed or own card),
 * COUP_RULES_CHAR_NONE otherwise.
 */
uint8_t coup_rules_visible_card(const coup_rules_t* rules,
                                uint8_t viewer_id, uint8_t player_id,
                                int card_idx);

#endif /* COUP_RULES_H */
