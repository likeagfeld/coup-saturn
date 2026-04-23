/**
 * coup_rules.c - Authoritative Coup Game Rule Engine
 *
 * Implements the complete Coup card game state machine.
 * Purely reactive: no timers, no I/O, fully deterministic.
 */

#include "coup_rules.h"
#include <string.h>

/*============================================================================
 * PRNG (xorshift32)
 *============================================================================*/

static uint32_t rules_rand(coup_rules_t* rules)
{
    uint32_t x = rules->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rules->rng_state = x;
    return x;
}

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static void shuffle_deck(coup_rules_t* rules)
{
    int i;
    for (i = rules->deck_count - 1; i > 0; i--) {
        int j = (int)(rules_rand(rules) % (uint32_t)(i + 1));
        uint8_t tmp = rules->deck[i];
        rules->deck[i] = rules->deck[j];
        rules->deck[j] = tmp;
    }
}

/*============================================================================
 * Public API
 *============================================================================*/

void coup_rules_init(coup_rules_t* rules, uint32_t seed)
{
    int c, i;

    memset(rules, 0, sizeof(*rules));

    rules->rng_state = seed ? seed : 1;
    rules->player_count = 0;
    rules->phase = COUP_TURN_LOBBY;
    rules->game_active = false;
    rules->action_target = 0xFF;
    rules->blocker_id = 0xFF;
    rules->influence_loser = 0xFF;
    rules->after_challenge_result = -1;
    rules->current_blockable_by = 0;

    /* Build deck: 3 copies of each character (dealt at START_GAME) */
    rules->deck_count = 0;
    for (c = 0; c < COUP_RULES_NUM_CHARACTERS; c++) {
        for (i = 0; i < 3; i++) {
            rules->deck[rules->deck_count++] = (uint8_t)c;
        }
    }

    rules->turn_order_count = 0;
    rules->current_turn_idx = 0;
    rules->round_number = 0;
}

/*============================================================================
 * Event Emit Helper (per-instance, no static globals)
 *============================================================================*/

static void emit_begin(coup_rules_t* rules, coup_event_t* buf, int max)
{
    rules->_emit_buf = buf;
    rules->_emit_max = max;
    rules->_emit_idx = 0;
}

static void emit_event(coup_rules_t* rules, coup_event_type_t type)
{
    if (rules->_emit_idx < rules->_emit_max) {
        memset(&rules->_emit_buf[rules->_emit_idx], 0, sizeof(coup_event_t));
        rules->_emit_buf[rules->_emit_idx].type = type;
    }
}

static coup_event_t* emit_current(coup_rules_t* rules)
{
    if (rules->_emit_idx < rules->_emit_max) return &rules->_emit_buf[rules->_emit_idx];
    return NULL;
}

static void emit_push(coup_rules_t* rules)
{
    if (rules->_emit_idx < rules->_emit_max) rules->_emit_idx++;
}

static int emit_count(coup_rules_t* rules)
{
    return rules->_emit_idx;
}

/* Forward declaration */
static void clear_pending(coup_rules_t* rules);

/*============================================================================
 * Emit Helpers (reduce repeated event emission patterns)
 *============================================================================*/

static void emit_coins_changed(coup_rules_t* rules, uint8_t player_id,
                                uint8_t old_coins, uint8_t new_coins)
{
    emit_event(rules, COUP_EVT_COINS_CHANGED);
    emit_current(rules)->data.coins_changed.player_id = player_id;
    emit_current(rules)->data.coins_changed.old_coins = old_coins;
    emit_current(rules)->data.coins_changed.new_coins = new_coins;
    emit_push(rules);
}

static void emit_card_replaced(coup_rules_t* rules, uint8_t player_id,
                                uint8_t card_idx, uint8_t new_char)
{
    emit_event(rules, COUP_EVT_CARD_REPLACED);
    emit_current(rules)->data.card_replaced.player_id = player_id;
    emit_current(rules)->data.card_replaced.card_idx = card_idx;
    emit_current(rules)->data.card_replaced.new_char = new_char;
    emit_push(rules);
}

static void emit_influence_loss_req(coup_rules_t* rules, uint8_t player_id)
{
    emit_event(rules, COUP_EVT_INFLUENCE_LOSS_REQUESTED);
    emit_current(rules)->data.influence_loss_requested.player_id = player_id;
    emit_push(rules);
}

/**
 * Find an unrevealed card matching `character` in a player's hand.
 * Returns card index, or -1 if not found.
 */
static int find_unrevealed_card(const coup_rules_t* rules, uint8_t player_id,
                                uint8_t character)
{
    int ci;
    for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
        if (!rules->players[player_id].revealed[ci] &&
            rules->players[player_id].cards[ci] == character) {
            return ci;
        }
    }
    return -1;
}

/**
 * Get the character of the first unrevealed card for a player.
 * Used to determine what gets revealed when a bluffer loses a challenge.
 */
static uint8_t first_unrevealed_char(const coup_rules_t* rules, uint8_t player_id)
{
    int ci;
    for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
        if (!rules->players[player_id].revealed[ci]) {
            return rules->players[player_id].cards[ci];
        }
    }
    return 0;
}

/**
 * Resolve a challenge: defender either has or doesn't have the claimed card.
 * Handles card replacement, sets influence_loser and after_challenge_result.
 * This is the shared logic for both action-challenge and block-challenge.
 */
static void resolve_challenge(coup_rules_t* rules, uint8_t challenger_id,
                               uint8_t defender_id, uint8_t claimed_char,
                               bool is_block_challenge, int after_if_defender_wins,
                               int after_if_defender_loses)
{
    int card_idx = find_unrevealed_card(rules, defender_id, claimed_char);
    bool has_card = (card_idx >= 0);

    /* Determine revealed character for event */
    uint8_t reveal_char = has_card ? claimed_char
                                   : first_unrevealed_char(rules, defender_id);

    /* Emit challenge result event */
    if (is_block_challenge) {
        emit_event(rules, COUP_EVT_BLOCK_CHALLENGE_RESULT);
        emit_current(rules)->data.block_challenge_result.challenger_id = challenger_id;
        emit_current(rules)->data.block_challenge_result.blocker_id = defender_id;
        emit_current(rules)->data.block_challenge_result.blocker_had_card = has_card;
        emit_current(rules)->data.block_challenge_result.revealed_char = reveal_char;
        emit_push(rules);
    } else {
        emit_event(rules, COUP_EVT_CHALLENGE_RESULT);
        emit_current(rules)->data.challenge_result.challenger_id = challenger_id;
        emit_current(rules)->data.challenge_result.defender_id = defender_id;
        emit_current(rules)->data.challenge_result.defender_had_card = has_card;
        emit_current(rules)->data.challenge_result.revealed_char = reveal_char;
        emit_push(rules);
    }

    clear_pending(rules);

    if (has_card) {
        /* Defender had the card — replace it from deck, challenger loses influence */
        rules->deck[rules->deck_count++] = claimed_char;
        shuffle_deck(rules);
        rules->players[defender_id].cards[card_idx] =
            rules->deck[--rules->deck_count];

        emit_card_replaced(rules, defender_id, (uint8_t)card_idx,
                           rules->players[defender_id].cards[card_idx]);

        rules->influence_loser = challenger_id;
        rules->after_challenge_result = (int8_t)after_if_defender_wins;
        rules->phase = COUP_TURN_WAITING_FOR_INFLUENCE_LOSS;

        emit_influence_loss_req(rules, challenger_id);
    } else {
        /* Defender was bluffing — defender loses influence */
        rules->influence_loser = defender_id;
        rules->after_challenge_result = (int8_t)after_if_defender_loses;
        rules->phase = COUP_TURN_WAITING_FOR_INFLUENCE_LOSS;

        emit_influence_loss_req(rules, defender_id);
    }
}

/*============================================================================
 * Turn Advancement
 *============================================================================*/

static void advance_turn(coup_rules_t* rules)
{
    int attempts = 0;
    do {
        rules->current_turn_idx = (rules->current_turn_idx + 1)
                                  % rules->turn_order_count;
        if (rules->current_turn_idx == 0) {
            rules->round_number++;
            emit_event(rules, COUP_EVT_ROUND_ADVANCED);
            emit_current(rules)->data.round_advanced.round_number =
                (uint8_t)rules->round_number;
            emit_push(rules);
        }
        attempts++;
    } while (!rules->players[rules->turn_order[rules->current_turn_idx]].alive
             && attempts < rules->turn_order_count);
}

static void emit_turn_started(coup_rules_t* rules)
{
    uint8_t pid = coup_rules_current_player(rules);
    rules->phase = COUP_TURN_WAITING_FOR_ACTION;

    emit_event(rules, COUP_EVT_TURN_STARTED);
    emit_current(rules)->data.turn_started.player_id = pid;
    emit_current(rules)->data.turn_started.valid_actions = coup_rules_valid_actions(rules);
    emit_push(rules);
}

/*============================================================================
 * Pending Response Helpers
 *============================================================================*/

static void clear_pending(coup_rules_t* rules)
{
    int i;
    for (i = 0; i < COUP_RULES_MAX_PLAYERS; i++)
        rules->pending_responses[i] = false;
    rules->pending_count = 0;
}

static void set_pending_all_except(coup_rules_t* rules, uint8_t except_id)
{
    int i;
    clear_pending(rules);
    for (i = 0; i < rules->player_count; i++) {
        if ((uint8_t)i != except_id && rules->players[i].alive) {
            rules->pending_responses[i] = true;
            rules->pending_count++;
        }
    }
}

/*============================================================================
 * Action Resolution
 *============================================================================*/

static void reset_action_state(coup_rules_t* rules)
{
    rules->action_actor = 0;
    rules->action_type = 0;
    rules->action_target = 0xFF;
    rules->blocker_id = 0xFF;
    rules->block_char = 0;
    rules->influence_loser = 0xFF;
    rules->after_challenge_result = -1;
    rules->current_blockable_by = 0;
    clear_pending(rules);
}

static void resolve_action(coup_rules_t* rules)
{
    uint8_t actor = rules->action_actor;
    uint8_t action = rules->action_type;
    uint8_t target = rules->action_target;

    switch (action) {
        case COUP_RULES_ACT_INCOME: {
            uint8_t old = rules->players[actor].coins;
            rules->players[actor].coins++;
            emit_coins_changed(rules, actor, old, rules->players[actor].coins);
            break;
        }
        case COUP_RULES_ACT_FOREIGN_AID: {
            uint8_t old = rules->players[actor].coins;
            rules->players[actor].coins += 2;
            emit_coins_changed(rules, actor, old, rules->players[actor].coins);
            break;
        }
        case COUP_RULES_ACT_TAX: {
            uint8_t old = rules->players[actor].coins;
            rules->players[actor].coins += 3;
            emit_coins_changed(rules, actor, old, rules->players[actor].coins);
            break;
        }
        case COUP_RULES_ACT_STEAL: {
            uint8_t steal_amount = rules->players[target].coins;
            if (steal_amount > 2) steal_amount = 2;
            {
                uint8_t old_target = rules->players[target].coins;
                rules->players[target].coins -= steal_amount;
                emit_coins_changed(rules, target, old_target, rules->players[target].coins);
            }
            {
                uint8_t old_actor = rules->players[actor].coins;
                rules->players[actor].coins += steal_amount;
                emit_coins_changed(rules, actor, old_actor, rules->players[actor].coins);
            }
            break;
        }
        case COUP_RULES_ACT_COUP:
        case COUP_RULES_ACT_ASSASSINATE: {
            /* If target is already dead (e.g., eliminated during block-challenge),
             * skip influence loss and fall through to ACTION_RESOLVED */
            if (!rules->players[target].alive) {
                break;
            }
            /* Coins already deducted on declaration. Target loses influence. */
            rules->influence_loser = target;
            rules->phase = COUP_TURN_WAITING_FOR_INFLUENCE_LOSS;
            emit_influence_loss_req(rules, target);
            return; /* Don't advance turn yet — wait for influence loss */
        }
        case COUP_RULES_ACT_EXCHANGE: {
            /* Draw 2 from deck, combine with hand */
            int ci;
            rules->exchange_count = 0;
            rules->exchange_player = actor;
            /* Add unrevealed hand cards */
            for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
                if (!rules->players[actor].revealed[ci]) {
                    rules->exchange_cards[rules->exchange_count++] =
                        rules->players[actor].cards[ci];
                }
            }
            /* Draw 2 from deck */
            {
                int draw;
                for (draw = 0; draw < 2 && rules->deck_count > 0; draw++) {
                    rules->exchange_cards[rules->exchange_count++] =
                        rules->deck[--rules->deck_count];
                }
            }

            rules->phase = COUP_TURN_WAITING_FOR_EXCHANGE;

            emit_event(rules, COUP_EVT_EXCHANGE_OFFERED);
            emit_current(rules)->data.exchange_offered.player_id = actor;
            emit_current(rules)->data.exchange_offered.count = (uint8_t)rules->exchange_count;
            for (ci = 0; ci < rules->exchange_count; ci++) {
                emit_current(rules)->data.exchange_offered.cards[ci] =
                    rules->exchange_cards[ci];
            }
            emit_push(rules);
            return; /* Wait for exchange choice */
        }
        default:
            break;
    }

    emit_event(rules, COUP_EVT_ACTION_RESOLVED);
    emit_current(rules)->data.action_resolved.action = action;
    emit_current(rules)->data.action_resolved.actor_id = actor;
    emit_current(rules)->data.action_resolved.target_id = target;
    emit_push(rules);

    reset_action_state(rules);
    advance_turn(rules);
    emit_turn_started(rules);
}

static void cancel_action(coup_rules_t* rules, uint8_t reason)
{
    emit_event(rules, COUP_EVT_ACTION_CANCELLED);
    emit_current(rules)->data.action_cancelled.action = rules->action_type;
    emit_current(rules)->data.action_cancelled.actor_id = rules->action_actor;
    emit_current(rules)->data.action_cancelled.reason = reason;
    emit_push(rules);

    reset_action_state(rules);
    advance_turn(rules);
    emit_turn_started(rules);
}

/*============================================================================
 * Block Window Helpers
 *============================================================================*/

static void open_block_window(coup_rules_t* rules)
{
    uint8_t action = rules->action_type;
    uint8_t blockable_by = 0;
    bool target_only = false;

    switch (action) {
        case COUP_RULES_ACT_FOREIGN_AID:
            blockable_by = 1 << COUP_RULES_CHAR_DUKE;
            target_only = false;
            set_pending_all_except(rules, rules->action_actor);
            break;
        case COUP_RULES_ACT_STEAL:
            blockable_by = (1 << COUP_RULES_CHAR_CAPTAIN) |
                           (1 << COUP_RULES_CHAR_AMBASSADOR);
            target_only = true;
            clear_pending(rules);
            if (rules->action_target != 0xFF &&
                rules->players[rules->action_target].alive) {
                rules->pending_responses[rules->action_target] = true;
                rules->pending_count = 1;
            }
            break;
        case COUP_RULES_ACT_ASSASSINATE:
            blockable_by = 1 << COUP_RULES_CHAR_CONTESSA;
            target_only = true;
            clear_pending(rules);
            if (rules->action_target != 0xFF &&
                rules->players[rules->action_target].alive) {
                rules->pending_responses[rules->action_target] = true;
                rules->pending_count = 1;
            }
            break;
        default:
            return;
    }

    rules->current_blockable_by = blockable_by;

    /* If no one can block (e.g. target died during challenge), skip to resolve */
    if (rules->pending_count == 0) {
        resolve_action(rules);
        return;
    }

    rules->phase = COUP_TURN_BLOCK_WINDOW;

    emit_event(rules, COUP_EVT_BLOCK_OPENED);
    emit_current(rules)->data.block_opened.blockable_by = blockable_by;
    emit_current(rules)->data.block_opened.target_only = target_only ? 1 : 0;
    emit_push(rules);
}

/*============================================================================
 * Lobby Handlers
 *============================================================================*/

static int handle_add_player(coup_rules_t* rules)
{
    if (rules->phase != COUP_TURN_LOBBY) return -1;
    if (rules->player_count >= COUP_RULES_MAX_PLAYERS) return -1;

    int pid = rules->player_count;
    rules->players[pid].is_bot = false;
    rules->players[pid].ready = false;
    rules->players[pid].alive = true;
    rules->players[pid].coins = COUP_RULES_INITIAL_COINS;
    rules->player_count++;

    emit_event(rules, COUP_EVT_PLAYER_JOINED);
    emit_current(rules)->data.player_joined.player_id = (uint8_t)pid;
    emit_current(rules)->data.player_joined.is_bot = 0;
    emit_push(rules);

    return emit_count(rules);
}

static int handle_add_bot(coup_rules_t* rules)
{
    if (rules->phase != COUP_TURN_LOBBY) return -1;
    if (rules->player_count >= COUP_RULES_MAX_PLAYERS) return -1;

    int pid = rules->player_count;
    rules->players[pid].is_bot = true;
    rules->players[pid].ready = true;
    rules->players[pid].alive = true;
    rules->players[pid].coins = COUP_RULES_INITIAL_COINS;
    rules->player_count++;

    emit_event(rules, COUP_EVT_PLAYER_JOINED);
    emit_current(rules)->data.player_joined.player_id = (uint8_t)pid;
    emit_current(rules)->data.player_joined.is_bot = 1;
    emit_push(rules);

    return emit_count(rules);
}

static int handle_remove_player(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;

    if (rules->phase != COUP_TURN_LOBBY) return -1;
    if (pid >= (uint8_t)rules->player_count) return -1;

    emit_event(rules, COUP_EVT_PLAYER_LEFT);
    emit_current(rules)->data.player_left.player_id = pid;
    emit_push(rules);

    /* Compact: shift players above removed slot down by 1 */
    {
        int i;
        for (i = pid; i < rules->player_count - 1; i++) {
            rules->players[i] = rules->players[i + 1];
        }
    }
    rules->player_count--;

    /* Clear the vacated slot */
    memset(&rules->players[rules->player_count], 0, sizeof(coup_rules_player_t));

    return emit_count(rules);
}

static int handle_set_ready(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;

    if (rules->phase != COUP_TURN_LOBBY) return -1;
    if (pid >= (uint8_t)rules->player_count) return -1;
    if (rules->players[pid].is_bot) return -1; /* bots always ready */

    rules->players[pid].ready = input->data.set_ready.ready ? true : false;

    emit_event(rules, COUP_EVT_READY_CHANGED);
    emit_current(rules)->data.ready_changed.player_id = pid;
    emit_current(rules)->data.ready_changed.ready = rules->players[pid].ready ? 1 : 0;
    emit_push(rules);

    return emit_count(rules);
}

/*============================================================================
 * Input Handlers
 *============================================================================*/

static int handle_start_game(coup_rules_t* rules)
{
    int i, ready_count;

    if (rules->phase != COUP_TURN_LOBBY) return -1;

    /* Count ready players — need at least 2 */
    ready_count = 0;
    for (i = 0; i < rules->player_count; i++) {
        if (rules->players[i].ready) ready_count++;
    }
    if (ready_count < 2) return -1;

    /* Shuffle deck and deal cards */
    shuffle_deck(rules);
    for (i = 0; i < rules->player_count; i++) {
        rules->players[i].cards[0] = rules->deck[--rules->deck_count];
        rules->players[i].cards[1] = rules->deck[--rules->deck_count];
        rules->players[i].revealed[0] = false;
        rules->players[i].revealed[1] = false;
        rules->players[i].coins = COUP_RULES_INITIAL_COINS;
        rules->players[i].alive = true;
        rules->turn_order[i] = (uint8_t)i;
    }
    rules->turn_order_count = rules->player_count;

    rules->game_active = true;

    emit_event(rules, COUP_EVT_GAME_STARTED);
    emit_current(rules)->data.game_started.player_count = (uint8_t)rules->player_count;
    emit_push(rules);

    emit_turn_started(rules);
    return emit_count(rules);
}

static int handle_action(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;
    uint8_t action = input->data.action.action;
    uint8_t target = input->data.action.target_id;

    if (rules->phase != COUP_TURN_WAITING_FOR_ACTION) return -1;
    if (pid != coup_rules_current_player(rules)) return -1;

    /* Validate action is allowed */
    uint8_t valid = coup_rules_valid_actions(rules);
    if (!(valid & (1 << action))) return -1;

    /* Validate target requirement */
    if (coup_rules_action_needs_target[action]) {
        if (target >= (uint8_t)rules->player_count) return -1;
        if (!rules->players[target].alive) return -1;
        if (target == pid) return -1;
    }

    /* Store action state */
    rules->action_actor = pid;
    rules->action_type = action;
    rules->action_target = target;

    /* Deduct costs upfront for Coup and Assassinate */
    if (action == COUP_RULES_ACT_COUP) {
        uint8_t old = rules->players[pid].coins;
        rules->players[pid].coins -= COUP_RULES_COUP_COST;
        emit_coins_changed(rules, pid, old, rules->players[pid].coins);
    } else if (action == COUP_RULES_ACT_ASSASSINATE) {
        uint8_t old = rules->players[pid].coins;
        rules->players[pid].coins -= COUP_RULES_ASSASSINATE_COST;
        emit_coins_changed(rules, pid, old, rules->players[pid].coins);
    }

    /* Emit ACTION_DECLARED */
    emit_event(rules, COUP_EVT_ACTION_DECLARED);
    emit_current(rules)->data.action_declared.actor_id = pid;
    emit_current(rules)->data.action_declared.action = action;
    emit_current(rules)->data.action_declared.target_id = target;
    emit_push(rules);

    /* Route based on action type */
    int claimed_char = coup_rules_action_claim[action];

    if (action == COUP_RULES_ACT_INCOME) {
        /* Income: no challenge, no block — resolve immediately */
        resolve_action(rules);
    } else if (action == COUP_RULES_ACT_FOREIGN_AID) {
        /* Foreign Aid: no challenge, but blockable by Duke */
        open_block_window(rules);
    } else if (action == COUP_RULES_ACT_COUP) {
        /* Coup: no challenge, no block — resolve immediately */
        resolve_action(rules);
    } else if (claimed_char >= 0) {
        /* Character action: opens challenge window */
        set_pending_all_except(rules, pid);
        rules->phase = COUP_TURN_CHALLENGE_WINDOW;

        emit_event(rules, COUP_EVT_CHALLENGE_OPENED);
        emit_current(rules)->data.challenge_opened.defender_id = pid;
        emit_current(rules)->data.challenge_opened.claimed_char = (uint8_t)claimed_char;
        emit_push(rules);
    }

    return emit_count(rules);
}

static int handle_response(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;
    uint8_t resp = input->data.response.response;

    /* Must be in a window phase */
    if (rules->phase != COUP_TURN_CHALLENGE_WINDOW &&
        rules->phase != COUP_TURN_BLOCK_WINDOW &&
        rules->phase != COUP_TURN_BLOCK_CHALLENGE_WINDOW)
        return -1;

    /* Must be a pending responder */
    if (!rules->pending_responses[pid]) return -1;

    if (resp == COUP_RULES_RESP_PASS) {
        rules->pending_responses[pid] = false;
        rules->pending_count--;

        /* If everyone has passed, resolve based on current phase */
        if (rules->pending_count <= 0) {
            if (rules->phase == COUP_TURN_CHALLENGE_WINDOW) {
                /* No challenge — check if action is blockable */
                bool blockable = (rules->action_type == COUP_RULES_ACT_STEAL ||
                                  rules->action_type == COUP_RULES_ACT_ASSASSINATE);
                if (blockable) {
                    open_block_window(rules);
                } else {
                    resolve_action(rules);
                }
            } else if (rules->phase == COUP_TURN_BLOCK_WINDOW) {
                /* No block — resolve action */
                resolve_action(rules);
            } else if (rules->phase == COUP_TURN_BLOCK_CHALLENGE_WINDOW) {
                /* No challenge to the block — block stands, cancel action */
                cancel_action(rules, 1); /* 1 = blocked */
            }
        }
    } else if (resp == COUP_RULES_RESP_BLOCK) {
        if (rules->phase != COUP_TURN_BLOCK_WINDOW) return -1;

        /* Mark this player as the blocker — need block claim next */
        rules->blocker_id = pid;
        rules->pending_responses[pid] = false;
        rules->pending_count--;
        rules->phase = COUP_TURN_RESOLVING; /* waiting for block claim input */

        emit_event(rules, COUP_EVT_BLOCK_DECLARED);
        emit_current(rules)->data.block_declared.blocker_id = pid;
        emit_current(rules)->data.block_declared.character = 0; /* filled in by claim */
        emit_push(rules);
    } else if (resp == COUP_RULES_RESP_CHALLENGE) {
        if (rules->phase == COUP_TURN_CHALLENGE_WINDOW) {
            /* Challenge the action's claim */
            uint8_t defender = rules->action_actor;
            int claimed_char = coup_rules_action_claim[rules->action_type];
            /* defender wins → action proceeds (1), defender loses → action fails (0) */
            resolve_challenge(rules, pid, defender, (uint8_t)claimed_char,
                              false, 1, 0);
        } else if (rules->phase == COUP_TURN_BLOCK_CHALLENGE_WINDOW) {
            /* Challenge the blocker's claim */
            uint8_t blocker = rules->blocker_id;
            /* blocker wins → block stands (2), blocker loses → action proceeds (1) */
            resolve_challenge(rules, pid, blocker, rules->block_char,
                              true, 2, 1);
        }
    }

    return emit_count(rules);
}

static int handle_block_claim(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;
    uint8_t character = input->data.block_claim.character;

    if (rules->phase != COUP_TURN_RESOLVING) return -1;
    if (rules->blocker_id != pid) return -1;

    /* Validate claimed character can actually block this action */
    if (character >= COUP_RULES_NUM_CHARACTERS) return -1;
    if (!(rules->current_blockable_by & (1 << character))) return -1;

    rules->block_char = character;

    /* Open block-challenge window — everyone except blocker can challenge */
    set_pending_all_except(rules, pid);
    rules->phase = COUP_TURN_BLOCK_CHALLENGE_WINDOW;

    emit_event(rules, COUP_EVT_BLOCK_CHALLENGE_OPENED);
    emit_current(rules)->data.block_challenge_opened.blocker_id = pid;
    emit_current(rules)->data.block_challenge_opened.claimed_char = character;
    emit_push(rules);

    return emit_count(rules);
}

static int handle_lose_influence(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;
    uint8_t idx = input->data.lose_influence.card_idx;

    if (rules->phase != COUP_TURN_WAITING_FOR_INFLUENCE_LOSS) return -1;
    if (rules->influence_loser != pid) return -1;

    /* Validate card index — auto-pick if invalid or already revealed */
    if (idx >= COUP_RULES_CARDS_PER_PLAYER || rules->players[pid].revealed[idx]) {
        /* Auto-pick first unrevealed card */
        int ci;
        idx = 0;
        for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
            if (!rules->players[pid].revealed[ci]) {
                idx = (uint8_t)ci;
                break;
            }
        }
    }

    /* If only one card, auto-reveal it */
    {
        int unrevealed = 0;
        int last_unrevealed = 0;
        int ci;
        for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
            if (!rules->players[pid].revealed[ci]) {
                unrevealed++;
                last_unrevealed = ci;
            }
        }
        if (unrevealed == 1) {
            idx = (uint8_t)last_unrevealed;
        }
    }

    rules->players[pid].revealed[idx] = true;

    emit_event(rules, COUP_EVT_INFLUENCE_LOST);
    emit_current(rules)->data.influence_lost.player_id = pid;
    emit_current(rules)->data.influence_lost.card_idx = idx;
    emit_current(rules)->data.influence_lost.revealed_char = rules->players[pid].cards[idx];
    emit_push(rules);

    /* Check if player is eliminated */
    {
        bool all_revealed = true;
        int ci;
        for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
            if (!rules->players[pid].revealed[ci]) {
                all_revealed = false;
                break;
            }
        }
        if (all_revealed) {
            rules->players[pid].alive = false;
            emit_event(rules, COUP_EVT_PLAYER_ELIMINATED);
            emit_current(rules)->data.player_eliminated.player_id = pid;
            emit_push(rules);

            /* Check for game over — last player standing */
            {
                int alive_count = 0;
                uint8_t last_alive = 0;
                int pi;
                for (pi = 0; pi < rules->player_count; pi++) {
                    if (rules->players[pi].alive) {
                        alive_count++;
                        last_alive = (uint8_t)pi;
                    }
                }
                if (alive_count <= 1) {
                    rules->game_active = false;
                    emit_event(rules, COUP_EVT_GAME_OVER);
                    emit_current(rules)->data.game_over.winner_id = last_alive;
                    emit_push(rules);
                    return emit_count(rules);
                }
            }
        }
    }

    /* Route based on after_challenge_result */
    rules->influence_loser = 0xFF;

    if (rules->after_challenge_result == 0) {
        /* Challenge succeeded, action fails */
        rules->after_challenge_result = -1;
        cancel_action(rules, 0); /* 0 = challenged */
    } else if (rules->after_challenge_result == 1) {
        /* Challenge failed, action proceeds */
        rules->after_challenge_result = -1;
        /* Check if action is blockable and needs block window */
        bool blockable = (rules->action_type == COUP_RULES_ACT_STEAL ||
                          rules->action_type == COUP_RULES_ACT_ASSASSINATE);
        if (blockable && rules->blocker_id == 0xFF) {
            open_block_window(rules);
        } else {
            resolve_action(rules);
        }
    } else if (rules->after_challenge_result == 2) {
        /* Block-challenge failed, block stands — cancel action */
        rules->after_challenge_result = -1;
        cancel_action(rules, 1); /* 1 = blocked */
    } else {
        /* No after-challenge routing — this was a direct influence loss (Coup) */
        emit_event(rules, COUP_EVT_ACTION_RESOLVED);
        emit_current(rules)->data.action_resolved.action = rules->action_type;
        emit_current(rules)->data.action_resolved.actor_id = rules->action_actor;
        emit_current(rules)->data.action_resolved.target_id = rules->action_target;
        emit_push(rules);

        reset_action_state(rules);
        advance_turn(rules);
        emit_turn_started(rules);
    }

    return emit_count(rules);
}

static int handle_exchange_choice(coup_rules_t* rules, const coup_input_t* input)
{
    uint8_t pid = input->player_id;

    if (rules->phase != COUP_TURN_WAITING_FOR_EXCHANGE) return -1;
    if (rules->exchange_player != pid) return -1;

    /* Count how many unrevealed card slots the player has */
    int unrevealed_slots = 0;
    {
        int ci;
        for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
            if (!rules->players[pid].revealed[ci])
                unrevealed_slots++;
        }
    }

    uint8_t keep0 = input->data.exchange_choice.keep[0];
    uint8_t keep1 = input->data.exchange_choice.keep[1];

    if (unrevealed_slots == 1) {
        /* Player has 1 unrevealed card slot — keep only keep0, ignore keep1 */
        if (keep0 >= (uint8_t)rules->exchange_count) return -1;

        /* Assign kept card to the one unrevealed slot */
        {
            int ci;
            for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
                if (!rules->players[pid].revealed[ci]) {
                    rules->players[pid].cards[ci] = rules->exchange_cards[keep0];
                    break;
                }
            }
        }

        /* Return all other cards to deck */
        {
            int ei;
            for (ei = 0; ei < rules->exchange_count; ei++) {
                if ((uint8_t)ei != keep0) {
                    rules->deck[rules->deck_count++] = rules->exchange_cards[ei];
                }
            }
            shuffle_deck(rules);
        }
    } else {
        /* Normal case: 2 unrevealed card slots — keep 2 */
        if (keep0 >= (uint8_t)rules->exchange_count ||
            keep1 >= (uint8_t)rules->exchange_count ||
            keep0 == keep1) return -1;

        /* Assign kept cards to player */
        {
            int slot = 0;
            int ci;
            for (ci = 0; ci < COUP_RULES_CARDS_PER_PLAYER; ci++) {
                if (!rules->players[pid].revealed[ci]) {
                    if (slot == 0) {
                        rules->players[pid].cards[ci] = rules->exchange_cards[keep0];
                        slot++;
                    } else {
                        rules->players[pid].cards[ci] = rules->exchange_cards[keep1];
                    }
                }
            }
        }

        /* Return unkept cards to deck */
        {
            int ei;
            for (ei = 0; ei < rules->exchange_count; ei++) {
                if ((uint8_t)ei != keep0 && (uint8_t)ei != keep1) {
                    rules->deck[rules->deck_count++] = rules->exchange_cards[ei];
                }
            }
            shuffle_deck(rules);
        }
    }

    emit_event(rules, COUP_EVT_EXCHANGE_RESOLVED);
    emit_current(rules)->data.exchange_resolved.player_id = pid;
    emit_push(rules);

    emit_event(rules, COUP_EVT_ACTION_RESOLVED);
    emit_current(rules)->data.action_resolved.action = rules->action_type;
    emit_current(rules)->data.action_resolved.actor_id = rules->action_actor;
    emit_current(rules)->data.action_resolved.target_id = rules->action_target;
    emit_push(rules);

    reset_action_state(rules);
    advance_turn(rules);
    emit_turn_started(rules);

    return emit_count(rules);
}

static int handle_timeout(coup_rules_t* rules)
{
    if (rules->phase == COUP_TURN_CHALLENGE_WINDOW ||
        rules->phase == COUP_TURN_BLOCK_WINDOW ||
        rules->phase == COUP_TURN_BLOCK_CHALLENGE_WINDOW) {
        /* Treat timeout as all remaining players passing */
        clear_pending(rules);

        if (rules->phase == COUP_TURN_CHALLENGE_WINDOW) {
            bool blockable = (rules->action_type == COUP_RULES_ACT_STEAL ||
                              rules->action_type == COUP_RULES_ACT_ASSASSINATE);
            if (blockable) {
                open_block_window(rules);
            } else {
                resolve_action(rules);
            }
        } else if (rules->phase == COUP_TURN_BLOCK_WINDOW) {
            resolve_action(rules);
        } else if (rules->phase == COUP_TURN_BLOCK_CHALLENGE_WINDOW) {
            cancel_action(rules, 1);
        }
    } else if (rules->phase == COUP_TURN_RESOLVING) {
        /* Blocker timed out without claiming a character — resolve action */
        rules->blocker_id = 0xFF;
        resolve_action(rules);
    }

    return emit_count(rules);
}

/*============================================================================
 * Public API: Submit
 *============================================================================*/

int coup_rules_submit(coup_rules_t* rules, const coup_input_t* input,
                      coup_event_t* events_out, int max_events)
{
    emit_begin(rules, events_out, max_events);

    /* Reject all inputs after game over (except START_GAME is already
     * guarded by the LOBBY phase check) */
    if (!rules->game_active && rules->phase != COUP_TURN_LOBBY)
        return -1;

    switch (input->type) {
        case COUP_INPUT_START_GAME:
            return handle_start_game(rules);
        case COUP_INPUT_ACTION:
            return handle_action(rules, input);
        case COUP_INPUT_RESPONSE:
            return handle_response(rules, input);
        case COUP_INPUT_BLOCK_CLAIM:
            return handle_block_claim(rules, input);
        case COUP_INPUT_LOSE_INFLUENCE:
            return handle_lose_influence(rules, input);
        case COUP_INPUT_EXCHANGE_CHOICE:
            return handle_exchange_choice(rules, input);
        case COUP_INPUT_TIMEOUT:
            return handle_timeout(rules);
        case COUP_INPUT_ADD_PLAYER:
            return handle_add_player(rules);
        case COUP_INPUT_ADD_BOT:
            return handle_add_bot(rules);
        case COUP_INPUT_REMOVE_PLAYER:
            return handle_remove_player(rules, input);
        case COUP_INPUT_SET_READY:
            return handle_set_ready(rules, input);
    }

    return 0;
}

uint8_t coup_rules_current_player(const coup_rules_t* rules)
{
    if (rules->turn_order_count == 0) return 0;
    return rules->turn_order[rules->current_turn_idx];
}

uint8_t coup_rules_visible_card(const coup_rules_t* rules,
                                uint8_t viewer_id, uint8_t player_id,
                                int card_idx)
{
    if (card_idx < 0 || card_idx >= COUP_RULES_CARDS_PER_PLAYER) return COUP_RULES_CHAR_NONE;
    if (player_id >= (uint8_t)rules->player_count) return COUP_RULES_CHAR_NONE;
    if (rules->players[player_id].revealed[card_idx] || viewer_id == player_id)
        return rules->players[player_id].cards[card_idx];
    return COUP_RULES_CHAR_NONE;
}

uint8_t coup_rules_valid_actions(const coup_rules_t* rules)
{
    uint8_t player_id = coup_rules_current_player(rules);
    const coup_rules_player_t* p = &rules->players[player_id];

    /* At 10+ coins, must Coup */
    if (p->coins >= COUP_RULES_FORCE_COUP_COINS) {
        return 1 << COUP_RULES_ACT_COUP;
    }

    /* All character actions are always available (bluffing is the game) */
    uint8_t mask = (1 << COUP_RULES_ACT_INCOME)
                 | (1 << COUP_RULES_ACT_FOREIGN_AID)
                 | (1 << COUP_RULES_ACT_TAX)
                 | (1 << COUP_RULES_ACT_STEAL)
                 | (1 << COUP_RULES_ACT_EXCHANGE);

    /* Coup requires 7 coins */
    if (p->coins >= COUP_RULES_COUP_COST) {
        mask |= (1 << COUP_RULES_ACT_COUP);
    }

    /* Assassinate requires 3 coins */
    if (p->coins >= COUP_RULES_ASSASSINATE_COST) {
        mask |= (1 << COUP_RULES_ACT_ASSASSINATE);
    }

    return mask;
}
