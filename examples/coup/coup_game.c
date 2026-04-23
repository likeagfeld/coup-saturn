/**
 * coup_game.c - Coup Card Game Logic
 *
 * Manages all game state, input handling, and network message processing
 * for the Coup card game running on Sega Saturn via NetLink modem.
 *
 * Screen flow:
 *   TITLE -> CONNECTING -> NAME_ENTRY -> LOBBY -> GAME -> GAME_OVER
 *
 * Game phases (within GAME screen):
 *   IDLE -> SELECT_ACTION -> SELECT_TARGET -> CHALLENGE_WAIT ->
 *   BLOCK_WAIT -> BLOCK_CHALLENGE -> LOSE_INFLUENCE -> EXCHANGE_PICK ->
 *   RESOLVING
 */

#include "coup.h"
#include "coup_ui.h"
#include "coup_rules.h"
#include "coup_table_view.h"
#include "coup_bot.h"
#include "coup_protocol.h"
#include "cui_pal.h"
#include "cui_transport.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define HEARTBEAT_INTERVAL   600   /* 10 seconds at 60fps */
#define BLINK_INTERVAL       30    /* Half-second blink at 60fps */
#define AUTH_TIMEOUT_FRAMES  600   /* 10 seconds to wait for auth reply */
#define AUTH_MAX_RETRIES     5

/* Bot configuration */
#define BOT_MAX              6     /* Maximum bots (COUP_MAX_PLAYERS - 1) */
#define BOT_DEFAULT          3     /* Default bot count */

/* Name entry character set: A-Z (26), 0-9 (10), space (1) = 37 */
#define NAME_CHARSET_LEN     37
static const char NAME_CHARSET[NAME_CHARSET_LEN + 1] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";

/*============================================================================
 * Static State
 *============================================================================*/

static coup_state_t         g_state;
static const cui_transport_t* g_transport;

/* Receive framing state machine */
static uint8_t              g_rx_buf[COUP_RX_FRAME_SIZE];
static coup_rx_state_t      g_rx;

/* Transmit scratch buffer */
static uint8_t              g_tx_buf[COUP_TX_FRAME_SIZE];

/* Name entry: index into NAME_CHARSET for each cursor position */
static int                  g_name_char_idx[COUP_MAX_NAME];

/* Rule engine state (authoritative for bot mode) */
static coup_rules_t         g_rules;
static coup_event_t         g_rule_events[COUP_RULES_MAX_EVENTS];

/* Previous screen for BGM control on screen transitions */
static coup_screen_t        g_prev_screen;

/* Input pipe — abstracts local engine vs network */
typedef struct { void (*submit)(const coup_input_t* input); } coup_pipe_t;
static coup_pipe_t g_pipe;

/*============================================================================
 * Forward Declarations
 *============================================================================*/

static void process_message(const uint8_t* frame, int len);
static void send_connect(void);
static void send_heartbeat(void);
static void update_title(cui_input_action_t action);
static void update_settings(cui_input_action_t action);
static void update_connecting(cui_input_action_t action);
static void update_name_entry(cui_input_action_t action);
static void update_lobby(cui_input_action_t action);
static void update_game(cui_input_action_t action);
static void update_game_over(cui_input_action_t action);
static void update_phase_select_action(cui_input_action_t action);
static void update_phase_select_target(cui_input_action_t action);
static void update_phase_challenge_wait(cui_input_action_t action);
static void update_phase_block_wait(cui_input_action_t action);
static void update_phase_block_challenge(cui_input_action_t action);
static void update_phase_lose_influence(cui_input_action_t action);
static void update_phase_exchange_pick(cui_input_action_t action);
static int  count_alive_opponents(void);
static int  get_alive_opponent_by_index(int index);
static int  count_alive_cards(void);
static int  count_selected_exchange(void);
static coup_player_t* find_player(uint8_t id);
static void enter_human_lose_influence(void);
static void enter_human_exchange_phase(void);
static void sync_ui_phase(void);
static void submit_local(const coup_input_t* input);
static void submit_online(const coup_input_t* input);
static void process_rule_events(const coup_event_t* events, int count);
static void populate_block_chars(void);
static void bot_tick(void);

/*============================================================================
 * Persistent Auth (Backup RAM)
 *============================================================================*/

static void coup_auth_load(void)
{
    cui_pal_storage_t* storage = CUI_STORAGE();
    coup_save_data_t save;
    uint32_t out_size = 0;

    if (!storage || !storage->load) return;
    if (storage->init) storage->init();

    if (!storage->load(COUP_SAVE_FILENAME, &save, sizeof(save), &out_size))
        return;
    if (out_size < sizeof(save) || save.magic != COUP_SAVE_MAGIC)
        return;

    /* Restore UUID (auth token) */
    memcpy(g_state.my_uuid, save.uuid, sizeof(g_state.my_uuid));
    g_state.my_uuid[sizeof(g_state.my_uuid) - 1] = '\0';
    g_state.has_uuid = true;

    /* Restore username (convenience pre-fill only) */
    save.username[COUP_MAX_NAME - 1] = '\0';
    coup_strcpy(g_state.name_buf, save.username, sizeof(g_state.name_buf));
    g_state.name_len = coup_strlen(g_state.name_buf);

    /* Sync charset indices so name entry shows the loaded characters */
    {
        int ci;
        for (ci = 0; ci < g_state.name_len; ci++) {
            int j;
            g_name_char_idx[ci] = 0; /* fallback to 'A' */
            for (j = 0; j < NAME_CHARSET_LEN; j++) {
                if (NAME_CHARSET[j] == g_state.name_buf[ci]) {
                    g_name_char_idx[ci] = j;
                    break;
                }
            }
        }
        g_state.name_cursor = g_state.name_len;
    }
}

static void coup_auth_save(void)
{
    cui_pal_storage_t* storage = CUI_STORAGE();
    coup_save_data_t save;

    if (!storage || !storage->save) return;
    if (!g_state.has_uuid) return;
    if (storage->init) storage->init();

    save.magic = COUP_SAVE_MAGIC;
    memcpy(save.uuid, g_state.my_uuid, sizeof(save.uuid));
    coup_strcpy(save.username, g_state.name_buf, sizeof(save.username));

    storage->save(COUP_SAVE_FILENAME, &save, sizeof(save));
}

/*============================================================================
 * Public API
 *============================================================================*/

void coup_init(void)
{
    int i;

    memset(&g_state, 0, sizeof(g_state));
    memset(g_name_char_idx, 0, sizeof(g_name_char_idx));

    g_state.screen          = COUP_SCREEN_TITLE;
    g_state.phase           = COUP_PHASE_IDLE;
    g_state.frame_count     = 0;
    g_state.heartbeat_timer = HEARTBEAT_INTERVAL;
    g_state.title_blink     = 0;
    g_state.anim_timer      = 0;
    g_state.music_vol        = 10;
    g_state.sfx_vol          = 10;
    g_state.settings_cursor  = 0;
    g_state.bot_difficulty   = 1;  /* Medium */
    g_state.bot_count        = BOT_DEFAULT;
    g_state.lobby_cursor     = 0;
    g_state.lobby_naming     = true;

    /* Initialize all cards as NONE */
    for (i = 0; i < COUP_CARDS_PER_PLAYER; i++) {
        g_state.my_cards[i] = COUP_CHAR_NONE;
    }

    coup_rx_init(&g_rx, g_rx_buf, COUP_RX_FRAME_SIZE);

    g_transport = NULL;

    /* BGM screen tracker (title will be the first screen with music) */
    g_prev_screen = COUP_SCREEN_TITLE;

    /* Restore UUID + username from backup RAM (no-op if unavailable) */
    coup_auth_load();
}

void coup_update(cui_input_action_t action)
{
    /* X/Y/Z: log scrolling (available during any game phase) */
    if (g_state.screen == COUP_SCREEN_GAME) {
        if (action == CUI_INPUT_LOG_UP) {
            int max_scroll = g_state.log_count - COUP_UI.game.log.max_visible;
            if (max_scroll < 0) max_scroll = 0;
            if (g_state.log_scroll < max_scroll)
                g_state.log_scroll++;
            return;
        } else if (action == CUI_INPUT_LOG_DOWN) {
            if (g_state.log_scroll > 0)
                g_state.log_scroll--;
            return;
        } else if (action == CUI_INPUT_LOG_RESET) {
            g_state.log_scroll = 0;
            return;
        }
    }

    switch (g_state.screen) {
    case COUP_SCREEN_TITLE:
        update_title(action);
        break;
    case COUP_SCREEN_SETTINGS:
        update_settings(action);
        break;
    case COUP_SCREEN_RULES:
        /* Rules viewer: LEFT/RIGHT = pages, B or R = back */
        if (action == CUI_INPUT_RIGHT) {
            if (g_state.rules_page < COUP_RULES_PAGES - 1) {
                g_state.rules_page++;
                coup_audio_play_sfx(COUP_SFX_CONFIRM);
            }
        } else if (action == CUI_INPUT_LEFT) {
            if (g_state.rules_page > 0) {
                g_state.rules_page--;
                coup_audio_play_sfx(COUP_SFX_CONFIRM);
            }
        } else if (action == CUI_INPUT_CANCEL
                   || action == CUI_INPUT_PAGE_DOWN) {
            g_state.screen = g_state.rules_return_screen;
            coup_audio_play_sfx(COUP_SFX_CANCEL);
        }
        break;
    case COUP_SCREEN_CONNECTING:
        update_connecting(action);
        break;
    case COUP_SCREEN_NAME_ENTRY:
        update_name_entry(action);
        break;
    case COUP_SCREEN_LOBBY:
        update_lobby(action);
        break;
    case COUP_SCREEN_GAME:
        update_game(action);
        break;
    case COUP_SCREEN_GAME_OVER:
        update_game_over(action);
        break;
    default:
        break;
    }
}

void coup_tick(void)
{
    int frame_len;

    g_state.frame_count++;
    g_state.title_blink = g_state.frame_count;
    g_state.name_blink  = g_state.frame_count;
    g_state.anim_timer++;

    /* Heartbeat */
    if (g_state.connected && g_transport) {
        g_state.heartbeat_timer--;
        if (g_state.heartbeat_timer <= 0) {
            send_heartbeat();
            g_state.heartbeat_timer = HEARTBEAT_INTERVAL;
        }
    }

    /* Response timer countdown */
    if (g_state.response_timer > 0) {
        g_state.response_timer--;
    }

    /* Resolving timeout: request resync if stuck too long (online only) */
    if (g_state.online_mode && g_state.phase == COUP_PHASE_RESOLVING
        && g_state.resolving_timer > 0) {
        g_state.resolving_timer--;
        if (g_state.resolving_timer == 0 && g_transport) {
            uint16_t last_seen = (g_state.relay_expected_seq - 1) & 0xFFFF;
            int sz = coup_encode_resync_req(g_tx_buf, last_seen);
            cui_transport_send(g_transport, g_tx_buf, sz);
            g_state.resolving_timer = 600;  /* reset for another attempt */
            coup_log("Requesting resync...");
        }
    }

    /* Auth timeout */
    if (g_state.screen == COUP_SCREEN_CONNECTING && g_state.connected) {
        if (g_state.auth_timer > 0) {
            g_state.auth_timer--;
            if (g_state.auth_timer == 0) {
                /* Retry or disconnect */
                g_state.auth_retries++;
                if (g_state.auth_retries >= AUTH_MAX_RETRIES) {
                    coup_on_disconnected();
                } else {
                    send_connect();
                    g_state.auth_timer = AUTH_TIMEOUT_FRAMES;
                }
            }
        }
    }

    /* Poll network */
    if (g_transport) {
        frame_len = coup_rx_poll(&g_rx, g_transport);
        if (frame_len > 0) {
            process_message(g_rx_buf, frame_len);
        }
    }

    /* Bot AI tick */
    bot_tick();

    /* ---- Screen-based BGM control ----
     *
     * On any screen change, stop current music and restart fresh from
     * sample 0 — UNLESS the new screen is CONNECTING (no BGM there).
     *
     * The connecting/dialing screen is also handled by explicit
     * coup_audio_stop_music() calls in update_title() and
     * coup_enter_offline(), because some transitions skip through
     * CONNECTING within a single frame (e.g., no modem detected
     * goes TITLE -> CONNECTING -> OFFLINE in one coup_update call). */
    if (g_state.screen != g_prev_screen) {
        /* Screen changed — always stop current playback first */
        coup_audio_stop_music();

        /* Restart fresh if the new screen wants music */
        if (g_state.screen != COUP_SCREEN_CONNECTING) {
            coup_audio_start_music();
        }
        g_prev_screen = g_state.screen;
    }
}

const coup_state_t* coup_get_state(void)
{
    return &g_state;
}

coup_screen_t coup_get_screen(void)
{
    return g_state.screen;
}

void coup_log(const char* text)
{
    int dest;
    int i;

    if (!text) return;

    if (g_state.log_count < COUP_LOG_LINES) {
        dest = g_state.log_count;
        g_state.log_count++;
    } else {
        dest = g_state.log_head;
        g_state.log_head = (g_state.log_head + 1) % COUP_LOG_LINES;
    }

    /* Safe copy with truncation */
    for (i = 0; i < COUP_LOG_LINE_LEN && text[i]; i++) {
        g_state.log[dest][i] = text[i];
    }
    g_state.log[dest][i] = '\0';

    /* Reset scroll to show newest entry */
    g_state.log_scroll = 0;
}

void coup_render(void)
{
    coup_render_screen(&g_state);
}

void coup_render_now(void)
{
    coup_render_screen(&g_state);
}

void coup_set_transport(const struct cui_transport* t)
{
    g_transport = t;
}

void coup_on_connected(void)
{
    g_state.connected = true;
    g_state.screen    = COUP_SCREEN_CONNECTING;
    send_connect();
    g_state.auth_timer   = AUTH_TIMEOUT_FRAMES;
    g_state.auth_retries = 0;
}

void coup_on_disconnected(void)
{
    g_state.connected    = false;
    g_state.screen       = COUP_SCREEN_TITLE;
    g_state.phase        = COUP_PHASE_IDLE;
    g_state.online_mode  = false;
    coup_log("Disconnected from server.");
}

void coup_send_disconnect(void)
{
    int sz;
    if (!g_transport || !g_state.connected) return;
    sz = coup_encode_disconnect(g_tx_buf);
    cui_transport_send(g_transport, g_tx_buf, sz);
}

void coup_enter_offline(void)
{
    g_state.connected   = false;
    g_state.online_mode = false;
    g_state.screen      = COUP_SCREEN_LOBBY;
}

void coup_set_connect_stage(int stage, const char* msg)
{
    g_state.connect_stage = stage;
    coup_strcpy(g_state.connect_msg, msg, sizeof(g_state.connect_msg));
}

void coup_set_modem_available(bool available)
{
    g_state.modem_available = available;
}

/*============================================================================
 * Local Game (Rule Engine + Bot AI)
 *============================================================================*/

#define BOT_THINK_MIN     120  /* Minimum frames before bot acts (~2s at 60fps) */
#define BOT_THINK_MAX     240  /* Maximum frames before bot acts (~4s) */

static const char* const bot_names[BOT_MAX] = {
    "DANTE", "RANDAL", "JAY", "SILENT BOB", "ELIAS", "BECKY"
};

/* Simple PRNG for UI timers (separate from rule engine and bot AI PRNGs) */
static uint32_t bot_rng_state = 12345;
static uint32_t bot_rand(void)
{
    bot_rng_state ^= bot_rng_state << 13;
    bot_rng_state ^= bot_rng_state >> 17;
    bot_rng_state ^= bot_rng_state << 5;
    return bot_rng_state;
}

/* Per-bot AI RNG state (used by coup_bot_decide) */
static uint32_t bot_ai_rng[COUP_RULES_MAX_PLAYERS];

/*--- Sync UI state from rule engine (bot mode) ---*/

/**
 * Sync all game-mechanical state from the rules engine via the shared table
 * view.  This is the single source of truth for what the human player sees —
 * the same snapshot builder the bot AI uses.
 */
static void sync_ui_state(void)
{
    coup_table_view_t tv;
    int i, j;

    coup_table_view_from_rules(&tv, &g_rules, g_state.my_id);

    /* Per-player state */
    for (i = 0; i < tv.seat_count; i++) {
        const coup_seat_view_t* s = &tv.seats[i];
        g_state.players[i].coins = s->coins;
        g_state.players[i].alive = s->alive;
        for (j = 0; j < COUP_CARDS_PER_PLAYER; j++) {
            if (s->revealed[j]) {
                /* Revealed: show actual character */
                g_state.players[i].cards[j] = s->cards[j];
            } else if (s->is_self) {
                /* Own unrevealed: show actual character */
                g_state.players[i].cards[j] = s->cards[j];
            } else {
                /* Opponent unrevealed: face-down */
                g_state.players[i].cards[j] = COUP_CHAR_FACEDOWN;
            }
        }
    }

    /* Own hand (unrevealed = card value, revealed = NONE) */
    for (j = 0; j < COUP_CARDS_PER_PLAYER; j++) {
        if (tv.seats[g_state.my_id].revealed[j]) {
            g_state.my_cards[j] = COUP_CHAR_NONE;
        } else {
            g_state.my_cards[j] = tv.seats[g_state.my_id].cards[j];
        }
    }

    /* Turn state */
    g_state.current_turn_id = tv.current_turn_player;
    g_state.valid_actions = tv.valid_actions;

    /* Action context */
    g_state.declared_action = tv.action_type;
    g_state.declared_actor = tv.action_actor;
    g_state.declared_target = tv.action_target;
    g_state.declared_claim = tv.action_claim;

    /* Block context */
    g_state.blocker_id = tv.blocker_id;
    g_state.block_claim = tv.block_char;

    /* Exchange (only populated when viewer is exchange player) */
    g_state.exchange_count = tv.exchange_count;
    for (i = 0; i < tv.exchange_count && i < 4; i++) {
        g_state.exchange_cards[i] = tv.exchange_cards[i];
    }

    /* Game metadata */
    g_state.round_number = tv.round_number;

    /* Winner (only valid when game over) */
    if (!tv.game_active && tv.winner_id != 0xFF) {
        g_state.winner_id = tv.winner_id;
    }
}

/*--- Process events: LOG + SFX only ---*/
/*
 * All game-mechanical state (coins, cards, alive, turn, etc.) is now
 * synced from the rules engine via sync_ui_state() → table view.
 * This function only handles:
 *   - Game log messages (narrative text for the log panel)
 *   - Sound effects (triggered by specific game events)
 *   - Screen transitions (game over)
 */

static void process_rule_events(const coup_event_t* events, int count)
{
    char buf[COUP_LOG_LINE_LEN + 1];
    int i;

    for (i = 0; i < count; i++) {
        const coup_event_t* e = &events[i];
        switch (e->type) {

        case COUP_EVT_GAME_STARTED:
        case COUP_EVT_CHALLENGE_OPENED:
        case COUP_EVT_BLOCK_OPENED:
        case COUP_EVT_BLOCK_CHALLENGE_OPENED:
        case COUP_EVT_INFLUENCE_LOSS_REQUESTED:
        case COUP_EVT_CARD_REPLACED:
        case COUP_EVT_ROUND_ADVANCED:
        case COUP_EVT_PLAYER_JOINED:
        case COUP_EVT_PLAYER_LEFT:
        case COUP_EVT_READY_CHANGED:
            /* No log or SFX needed */
            break;

        case COUP_EVT_TURN_STARTED: {
            uint8_t pid = e->data.turn_started.player_id;
            if (pid == g_state.my_id) {
                coup_log("Your turn!");
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[pid].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = "'s turn"; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
                coup_log(buf);
            }
            break;
        }

        case COUP_EVT_ACTION_DECLARED: {
            uint8_t actor = e->data.action_declared.actor_id;
            uint8_t action = e->data.action_declared.action;
            uint8_t target = e->data.action_declared.target_id;
            int pos = 0;
            const char* s;

            if (actor == g_state.my_id) {
                for (s = "You play "; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
            } else {
                for (s = g_state.players[actor].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " plays "; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
            }
            for (s = coup_action_names[action];
                 *s && pos < COUP_LOG_LINE_LEN; )
                buf[pos++] = *s++;
            if (target != 0xFF) {
                for (s = " on "; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = g_state.players[target].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
            }
            buf[pos] = '\0';
            coup_log(buf);
            break;
        }

        case COUP_EVT_CHALLENGE_RESULT: {
            uint8_t challenger = e->data.challenge_result.challenger_id;
            uint8_t defender = e->data.challenge_result.defender_id;
            bool had_card = e->data.challenge_result.defender_had_card;

            /* Log who challenged */
            if (challenger == g_state.my_id) {
                coup_log("You challenge!");
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[challenger].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " challenges!"; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
                coup_log(buf);
            }

            if (had_card) {
                int pos = 0;
                const char* s;
                if (defender == g_state.my_id) {
                    for (s = "You had the card! "; *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                } else {
                    for (s = g_state.players[defender].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " had the card! "; *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                }
                if (challenger == g_state.my_id) {
                    for (s = "You lose influence."; *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                } else {
                    for (s = g_state.players[challenger].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " loses influence."; *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                }
                buf[pos] = '\0';
            } else {
                if (defender == g_state.my_id) {
                    coup_strcpy(buf, "Caught bluffing! Lose a card.",
                                sizeof(buf));
                } else {
                    int pos = 0;
                    const char* s;
                    for (s = g_state.players[defender].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " was bluffing!"; *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    buf[pos] = '\0';
                }
            }
            coup_log(buf);
            coup_audio_play_sfx(COUP_SFX_CHALLENGE);
            break;
        }

        case COUP_EVT_BLOCK_DECLARED: {
            uint8_t blocker = e->data.block_declared.blocker_id;
            if (blocker == g_state.my_id) {
                coup_strcpy(buf, "You block!", sizeof(buf));
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[blocker].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " blocks!"; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
            }
            coup_log(buf);
            coup_audio_play_sfx(COUP_SFX_CHALLENGE);
            break;
        }

        case COUP_EVT_BLOCK_CHALLENGE_RESULT: {
            uint8_t blk_challenger = e->data.block_challenge_result.challenger_id;
            bool had_card = e->data.block_challenge_result.blocker_had_card;
            uint8_t blocker = e->data.block_challenge_result.blocker_id;

            /* Log who challenged the block */
            if (blk_challenger == g_state.my_id) {
                coup_log("You challenge the block!");
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[blk_challenger].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " challenges the block!";
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
                coup_log(buf);
            }

            if (had_card) {
                if (blocker == g_state.my_id) {
                    coup_strcpy(buf,
                        "You had the card! Block stands.", sizeof(buf));
                } else {
                    int pos = 0;
                    const char* s;
                    for (s = g_state.players[blocker].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " had the card! Block stands.";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    buf[pos] = '\0';
                }
            } else {
                if (blocker == g_state.my_id) {
                    coup_strcpy(buf,
                        "You were bluffing the block!", sizeof(buf));
                } else {
                    int pos = 0;
                    const char* s;
                    for (s = g_state.players[blocker].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " was bluffing the block!";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    buf[pos] = '\0';
                }
            }
            coup_log(buf);
            coup_audio_play_sfx(COUP_SFX_CHALLENGE);
            break;
        }

        case COUP_EVT_INFLUENCE_LOST: {
            uint8_t pid = e->data.influence_lost.player_id;
            uint8_t revealed = e->data.influence_lost.revealed_char;
            int pos = 0;
            const char* s;

            if (pid == g_state.my_id) {
                for (s = "You reveal "; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
            } else {
                for (s = g_state.players[pid].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " reveals "; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
            }
            if (revealed < COUP_NUM_CHARACTERS) {
                for (s = coup_char_names[revealed];
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
            }
            buf[pos] = '\0';
            coup_log(buf);
            coup_audio_play_sfx(COUP_SFX_CARD_REVEAL);
            break;
        }

        case COUP_EVT_EXCHANGE_OFFERED:
            /* State handled by sync_ui_state() via table view */
            break;

        case COUP_EVT_EXCHANGE_RESOLVED: {
            uint8_t pid = e->data.exchange_resolved.player_id;
            if (pid == g_state.my_id) {
                coup_log("You exchange cards");
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[pid].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " exchanges cards";
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
                coup_log(buf);
            }
            break;
        }

        case COUP_EVT_COINS_CHANGED:
            /* SFX only — state synced from table view */
            if (e->data.coins_changed.new_coins >
                e->data.coins_changed.old_coins) {
                coup_audio_play_sfx(COUP_SFX_COINS);
            }
            break;

        case COUP_EVT_PLAYER_ELIMINATED: {
            uint8_t pid = e->data.player_eliminated.player_id;
            if (pid == g_state.my_id) {
                coup_strcpy(buf, "You have been eliminated!",
                            sizeof(buf));
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[pid].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " eliminated!"; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
            }
            coup_log(buf);
            coup_audio_play_sfx(COUP_SFX_ELIMINATED);
            break;
        }

        case COUP_EVT_ACTION_RESOLVED: {
            uint8_t act = e->data.action_resolved.action;
            uint8_t actor = e->data.action_resolved.actor_id;
            uint8_t tgt = e->data.action_resolved.target_id;
            int pos = 0;
            const char* s;

            switch (act) {
            case COUP_ACT_INCOME:
                if (actor == g_state.my_id) {
                    coup_strcpy(buf, "You take 1 coin", sizeof(buf));
                } else {
                    for (s = g_state.players[actor].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " takes 1 coin";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    buf[pos] = '\0';
                }
                coup_log(buf);
                break;
            case COUP_ACT_FOREIGN_AID:
                if (actor == g_state.my_id) {
                    coup_strcpy(buf, "You take 2 coins", sizeof(buf));
                } else {
                    for (s = g_state.players[actor].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " takes 2 coins";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    buf[pos] = '\0';
                }
                coup_log(buf);
                break;
            case COUP_ACT_TAX:
                if (actor == g_state.my_id) {
                    coup_strcpy(buf, "You collect tax (+3)",
                                sizeof(buf));
                } else {
                    for (s = g_state.players[actor].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " collects tax (+3)";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    buf[pos] = '\0';
                }
                coup_log(buf);
                break;
            case COUP_ACT_STEAL:
                if (actor == g_state.my_id) {
                    for (s = "You steal from ";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                } else {
                    for (s = g_state.players[actor].name;
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                    for (s = " steals from ";
                         *s && pos < COUP_LOG_LINE_LEN; )
                        buf[pos++] = *s++;
                }
                if (tgt != 0xFF) {
                    if (tgt == g_state.my_id) {
                        for (s = "you";
                             *s && pos < COUP_LOG_LINE_LEN; )
                            buf[pos++] = *s++;
                    } else {
                        for (s = g_state.players[tgt].name;
                             *s && pos < COUP_LOG_LINE_LEN; )
                            buf[pos++] = *s++;
                    }
                }
                buf[pos] = '\0';
                coup_log(buf);
                break;
            default:
                break;
            }
            break;
        }

        case COUP_EVT_ACTION_CANCELLED:
            if (e->data.action_cancelled.reason == 0) {
                coup_log("Action cancelled");
            } else {
                coup_log("Block succeeds, action cancelled");
            }
            break;

        case COUP_EVT_GAME_OVER: {
            uint8_t winner = e->data.game_over.winner_id;
            g_state.screen = COUP_SCREEN_GAME_OVER;
            g_state.phase = COUP_PHASE_IDLE;
            /* Snapshot winner name now — LOBBY_STATE may overwrite players[] */
            coup_strcpy(g_state.winner_name,
                        g_state.players[winner].name, COUP_MAX_NAME);
            if (winner == g_state.my_id) {
                coup_log("You win! Victory!");
                coup_audio_play_sfx(COUP_SFX_VICTORY);
            } else {
                int pos = 0;
                const char* s;
                for (s = g_state.players[winner].name;
                     *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                for (s = " wins!"; *s && pos < COUP_LOG_LINE_LEN; )
                    buf[pos++] = *s++;
                buf[pos] = '\0';
                coup_log(buf);
                coup_audio_play_sfx(COUP_SFX_ELIMINATED);
            }
            break;
        }
        }
    }
}

/*--- Derive block characters from engine bitmask ---*/

static void populate_block_chars(void)
{
    uint8_t mask = g_rules.current_blockable_by;
    int c;
    g_state.block_claim_count = 0;
    for (c = 0; c < COUP_RULES_NUM_CHARACTERS; c++) {
        if (mask & (1 << c))
            g_state.block_claim_chars[g_state.block_claim_count++] = (uint8_t)c;
    }
}

/*--- Derive UI phase from engine state (single source of truth) ---*/

#define BOT_RESPONSE_MIN  60   /* ~1s at 60fps */
#define BOT_RESPONSE_MAX  120  /* ~2s */

static void sync_ui_phase(void)
{
    sync_ui_state();
    if (g_state.screen != COUP_SCREEN_GAME || !g_rules.game_active) return;

    /* Spectators stay in IDLE — no prompts, no pending_responses access */
    if (g_state.is_spectator) {
        g_state.phase = COUP_PHASE_IDLE;
        return;
    }

    switch (g_rules.phase) {
    case COUP_TURN_WAITING_FOR_ACTION:
        if (coup_rules_current_player(&g_rules) == g_state.my_id) {
            /* Don't re-enter if already selecting action or target */
            if (g_state.phase != COUP_PHASE_SELECT_ACTION &&
                g_state.phase != COUP_PHASE_SELECT_TARGET) {
                int k;
                g_state.phase = COUP_PHASE_SELECT_ACTION;
                g_state.menu_cursor = 0;
                for (k = 0; k < COUP_NUM_ACTIONS; k++) {
                    if (g_state.valid_actions & (1 << k)) {
                        g_state.menu_cursor = k;
                        break;
                    }
                }
                coup_audio_play_sfx(COUP_SFX_TURN_START);
            }
        } else {
            if (g_state.phase != COUP_PHASE_IDLE) {
                g_state.phase = COUP_PHASE_IDLE;
                g_state.bot_think_timer = BOT_THINK_MIN +
                    (int)(bot_rand() % (uint32_t)(BOT_THINK_MAX - BOT_THINK_MIN));
            }
        }
        break;

    case COUP_TURN_CHALLENGE_WINDOW:
        if (g_rules.pending_responses[g_state.my_id]) {
            if (g_state.phase != COUP_PHASE_CHALLENGE_WAIT) {
                g_state.phase = COUP_PHASE_CHALLENGE_WAIT;
                g_state.menu_cursor = 0;
                coup_audio_play_sfx(COUP_SFX_CHALLENGE);
            }
        } else {
            g_state.phase = COUP_PHASE_RESOLVING;
            g_state.bot_think_timer = BOT_RESPONSE_MIN +
                (int)(bot_rand() % (uint32_t)(BOT_RESPONSE_MAX - BOT_RESPONSE_MIN));
        }
        break;

    case COUP_TURN_BLOCK_WINDOW:
        if (g_rules.pending_responses[g_state.my_id]) {
            if (g_state.phase != COUP_PHASE_BLOCK_WAIT) {
                g_state.phase = COUP_PHASE_BLOCK_WAIT;
                g_state.menu_cursor = 0;
                populate_block_chars();
            }
        } else {
            g_state.phase = COUP_PHASE_RESOLVING;
            g_state.bot_think_timer = BOT_RESPONSE_MIN +
                (int)(bot_rand() % (uint32_t)(BOT_RESPONSE_MAX - BOT_RESPONSE_MIN));
        }
        break;

    case COUP_TURN_BLOCK_CHALLENGE_WINDOW:
        if (g_rules.pending_responses[g_state.my_id]) {
            if (g_state.phase != COUP_PHASE_BLOCK_CHALLENGE) {
                g_state.phase = COUP_PHASE_BLOCK_CHALLENGE;
                g_state.menu_cursor = 0;
                coup_audio_play_sfx(COUP_SFX_CHALLENGE);
            }
        } else {
            g_state.phase = COUP_PHASE_RESOLVING;
            g_state.bot_think_timer = BOT_RESPONSE_MIN +
                (int)(bot_rand() % (uint32_t)(BOT_RESPONSE_MAX - BOT_RESPONSE_MIN));
        }
        break;

    case COUP_TURN_WAITING_FOR_INFLUENCE_LOSS:
        if (g_rules.influence_loser == g_state.my_id) {
            if (g_state.phase != COUP_PHASE_LOSE_INFLUENCE)
                enter_human_lose_influence();
        } else {
            g_state.phase = COUP_PHASE_RESOLVING;
            g_state.bot_think_timer = BOT_RESPONSE_MIN +
                (int)(bot_rand() % (uint32_t)(BOT_RESPONSE_MAX - BOT_RESPONSE_MIN));
        }
        break;

    case COUP_TURN_WAITING_FOR_EXCHANGE:
        if (g_rules.exchange_player == g_state.my_id) {
            if (g_state.phase != COUP_PHASE_EXCHANGE_PICK)
                enter_human_exchange_phase();
        } else {
            g_state.phase = COUP_PHASE_RESOLVING;
            g_state.bot_think_timer = BOT_RESPONSE_MIN +
                (int)(bot_rand() % (uint32_t)(BOT_RESPONSE_MAX - BOT_RESPONSE_MIN));
        }
        break;

    case COUP_TURN_RESOLVING:
        g_state.phase = COUP_PHASE_RESOLVING;
        break;

    default:
        break;
    }
}

/*--- Input pipe implementations ---*/

static void submit_local(const coup_input_t* input)
{
    int n = coup_rules_submit(&g_rules, input, g_rule_events,
                              COUP_RULES_MAX_EVENTS);
    if (n > 0) process_rule_events(g_rule_events, n);
    sync_ui_phase();
}

static void submit_online(const coup_input_t* input)
{
    int sz;
    if (!g_transport) return;
    if (g_state.is_spectator) return;
    if (input->player_id != g_state.my_id) return;

    switch (input->type) {
    case COUP_INPUT_ACTION:
        sz = coup_encode_action(g_tx_buf, input->data.action.action,
                                input->data.action.target_id);
        cui_transport_send(g_transport, g_tx_buf, sz);
        break;
    case COUP_INPUT_RESPONSE:
        sz = coup_encode_response(g_tx_buf, input->data.response.response);
        cui_transport_send(g_transport, g_tx_buf, sz);
        break;
    case COUP_INPUT_BLOCK_CLAIM:
        sz = coup_encode_block_claim(g_tx_buf,
                                     input->data.block_claim.character);
        cui_transport_send(g_transport, g_tx_buf, sz);
        break;
    case COUP_INPUT_LOSE_INFLUENCE:
        sz = coup_encode_lose_influence(g_tx_buf,
                                        input->data.lose_influence.card_idx);
        cui_transport_send(g_transport, g_tx_buf, sz);
        break;
    case COUP_INPUT_EXCHANGE_CHOICE:
        sz = coup_encode_exchange_choice(g_tx_buf,
                                         input->data.exchange_choice.keep[0],
                                         input->data.exchange_choice.keep[1]);
        cui_transport_send(g_transport, g_tx_buf, sz);
        break;
    default:
        return;
    }
    g_state.phase = COUP_PHASE_RESOLVING;
    g_state.resolving_timer = 600;  /* 10s at 60fps */
}

/*--- Bot Response Helpers (now delegated to coup_bot library) ---*/

static void enter_human_exchange_phase(void)
{
    g_state.phase = COUP_PHASE_EXCHANGE_PICK;
    g_state.exchange_cursor = 0;
    g_state.exchange_sel[0] = -1;
    g_state.exchange_sel[1] = -1;
    coup_audio_play_sfx(COUP_SFX_CARD_REVEAL);
}

static void enter_human_lose_influence(void)
{
    coup_player_t* self = find_player(g_state.my_id);
    int k, alive = 0, last_alive = 0;

    if (!self || !self->alive) {
        coup_input_t inp;
        memset(&inp, 0, sizeof(inp));
        inp.type = COUP_INPUT_LOSE_INFLUENCE;
        inp.player_id = g_state.my_id;
        inp.data.lose_influence.card_idx = 0;
        g_pipe.submit(&inp);
        return;
    }

    /* Count alive cards and find the last one */
    for (k = 0; k < COUP_CARDS_PER_PLAYER; k++) {
        if (g_state.my_cards[k] != COUP_CHAR_NONE) {
            alive++;
            last_alive = k;
        }
    }

    /* If only 1 card remains, auto-submit — no real choice */
    if (alive <= 1) {
        coup_input_t inp;
        memset(&inp, 0, sizeof(inp));
        inp.type = COUP_INPUT_LOSE_INFLUENCE;
        inp.player_id = g_state.my_id;
        inp.data.lose_influence.card_idx = (uint8_t)last_alive;
        g_pipe.submit(&inp);
        return;
    }

    g_state.phase = COUP_PHASE_LOSE_INFLUENCE;
    g_state.lose_cursor = 0;
    for (k = 0; k < COUP_CARDS_PER_PLAYER; k++) {
        if (g_state.my_cards[k] != COUP_CHAR_NONE) {
            g_state.lose_cursor = k;
            break;
        }
    }
    coup_log("Choose a card to lose!");
}

/* (bot_pick_target, feed_bot_action removed — replaced by coup_bot library) */

/*--- Game Start ---*/

/**
 * Populate the lobby player list from bot_count.
 * P1 = "YOU" (self), P2..P(bot_count+1) = bot names.
 */
static void lobby_sync_players(void)
{
    int i;
    g_state.players[0].id      = 0;
    /* Use name_buf if available, otherwise "YOU" */
    if (g_state.name_len > 0) {
        coup_strcpy(g_state.players[0].name, g_state.name_buf, COUP_MAX_NAME);
    } else {
        coup_strcpy(g_state.players[0].name, "YOU", COUP_MAX_NAME);
    }
    g_state.players[0].is_self = true;
    g_state.players[0].is_bot  = false;
    g_state.players[0].difficulty = 0;

    for (i = 0; i < g_state.bot_count; i++) {
        int pi = i + 1;
        g_state.players[pi].id      = (uint8_t)pi;
        coup_strcpy(g_state.players[pi].name, bot_names[i], COUP_MAX_NAME);
        g_state.players[pi].is_self = false;
        g_state.players[pi].is_bot  = true;
        g_state.players[pi].ready   = true;
        /* Default all bots to global difficulty setting */
        g_state.players[pi].difficulty = (uint8_t)g_state.bot_difficulty;
    }
    g_state.player_count = 1 + g_state.bot_count;
    g_state.my_id = 0;
}

void coup_start_game(uint32_t seed, uint8_t my_pid)
{
    int i;

    /* Set core state */
    g_state.is_spectator = (my_pid == 0xFF);
    g_state.my_id        = my_pid;

    /* Normalize player IDs to engine PID namespace (0-based array indices).
     * LOBBY_STATE uses server user_ids; the engine uses 0-based PIDs. */
    for (i = 0; i < g_state.player_count; i++) {
        g_state.players[i].id = (uint8_t)i;
        g_state.players[i].is_self = (i == (int)my_pid);
    }

    /* Reset game-specific UI state (prevents stale values from previous game) */
    for (i = 0; i < COUP_CARDS_PER_PLAYER; i++)
        g_state.my_cards[i] = COUP_CHAR_NONE;
    for (i = 0; i < g_state.player_count; i++) {
        g_state.players[i].alive = true;
        g_state.players[i].coins = COUP_INITIAL_COINS;
    }
    g_state.menu_cursor       = 0;
    g_state.menu_count        = 0;
    g_state.target_cursor     = 0;
    g_state.lose_cursor       = 0;
    g_state.exchange_cursor   = 0;
    g_state.exchange_count    = 0;
    g_state.exchange_sel[0]   = -1;
    g_state.exchange_sel[1]   = -1;
    g_state.log_scroll        = 0;
    g_state.response_timer    = 0;
    g_state.response_timeout  = 0;
    g_state.bot_think_timer   = 0;
    g_state.winner_id         = 0;
    g_state.winner_name[0]    = '\0';
    g_state.declared_action   = 0;
    g_state.declared_actor    = 0;
    g_state.declared_target   = 0xFF;
    g_state.declared_claim    = 0xFF;
    g_state.blocker_id        = 0xFF;
    g_state.block_claim       = 0xFF;
    g_state.block_claim_count = 0;
    g_state.valid_actions     = 0;

    g_state.screen       = COUP_SCREEN_GAME;
    g_state.phase        = COUP_PHASE_IDLE;
    g_state.round_number = 1;
    g_state.log_count    = 0;
    g_state.log_head     = 0;

    /* Reset relay tracking */
    g_state.relay_expected_seq = 0;
    g_state.relay_seq_valid    = false;
    g_state.resync_pending     = false;
    g_state.resync_total       = 0;
    g_state.resync_received    = 0;
    g_state.resolving_timer    = 0;

    /* Wire pipe: local engine vs network */
    g_pipe.submit = g_state.online_mode ? submit_online : submit_local;

    /* Seed RNGs */
    bot_rng_state = seed;
    {
        int bi;
        for (bi = 0; bi < COUP_RULES_MAX_PLAYERS; bi++)
            bot_ai_rng[bi] = seed ^ ((uint32_t)(bi + 1) * 2654435761u);
    }

    /* Initialize rule engine */
    coup_rules_init(&g_rules, seed);

    /* Register players from lobby state */
    for (i = 0; i < g_state.player_count; i++) {
        coup_input_t add_inp;
        memset(&add_inp, 0, sizeof(add_inp));
        if (g_state.players[i].is_bot) {
            add_inp.type = COUP_INPUT_ADD_BOT;
        } else {
            add_inp.type = COUP_INPUT_ADD_PLAYER;
        }
        coup_rules_submit(&g_rules, &add_inp,
                          g_rule_events, COUP_RULES_MAX_EVENTS);

        /* Humans need SET_READY */
        if (!g_state.players[i].is_bot) {
            coup_input_t rdy_inp;
            memset(&rdy_inp, 0, sizeof(rdy_inp));
            rdy_inp.type = COUP_INPUT_SET_READY;
            rdy_inp.player_id = (uint8_t)i;
            rdy_inp.data.set_ready.ready = 1;
            coup_rules_submit(&g_rules, &rdy_inp,
                              g_rule_events, COUP_RULES_MAX_EVENTS);
        }
    }

    /* Offline: start engine immediately. Online: wait for INPUT_RELAY(START_GAME). */
    if (!g_state.online_mode) {
        coup_input_t start_inp;
        memset(&start_inp, 0, sizeof(start_inp));
        start_inp.type = COUP_INPUT_START_GAME;
        submit_local(&start_inp);
    }

    {
        char msg[40];
        snprintf(msg, sizeof(msg), "Game started! %d player%s.",
                 g_state.player_count, g_state.player_count == 1 ? "" : "s");
        coup_log(msg);
    }
    coup_audio_play_sfx(COUP_SFX_TURN_START);
}

void coup_start_local_game(void)
{
    uint32_t seed;

    g_state.local_mode   = true;
    g_state.connected    = false;
    g_state.online_mode  = false;

    lobby_sync_players();
    seed = (uint32_t)(g_state.frame_count * 7919 + 31337);
    coup_start_game(seed, 0);
}

static void bot_tick(void)
{
    coup_table_view_t view;
    coup_bot_decision_t decision;
    uint8_t bot_id;

    if (!g_state.local_mode) return;
    if (g_state.screen != COUP_SCREEN_GAME) return;
    if (!g_rules.game_active) return;

    /* Think timer countdown */
    if (g_state.bot_think_timer > 0) {
        g_state.bot_think_timer--;
        return;
    }

    /* Determine which bot needs to act */
    switch (g_rules.phase) {
    case COUP_TURN_WAITING_FOR_ACTION: {
        bot_id = coup_rules_current_player(&g_rules);
        if (bot_id == g_state.my_id) return;
        break;
    }

    case COUP_TURN_CHALLENGE_WINDOW:
    case COUP_TURN_BLOCK_WINDOW:
    case COUP_TURN_BLOCK_CHALLENGE_WINDOW: {
        /* Find first pending bot */
        int i;
        bot_id = 0xFF;
        for (i = 0; i < g_rules.player_count; i++) {
            if (g_rules.pending_responses[i] && i != (int)g_state.my_id) {
                bot_id = (uint8_t)i;
                break;
            }
        }
        if (bot_id == 0xFF) return;
        break;
    }

    case COUP_TURN_WAITING_FOR_INFLUENCE_LOSS:
        bot_id = g_rules.influence_loser;
        if (bot_id == g_state.my_id) return;
        break;

    case COUP_TURN_WAITING_FOR_EXCHANGE:
        bot_id = g_rules.exchange_player;
        if (bot_id == g_state.my_id) return;
        break;

    default:
        return;
    }

    /* Build view and get decision from shared bot library */
    coup_table_view_from_rules(&view, &g_rules, bot_id);
    decision = coup_bot_decide(&view, g_state.players[bot_id].difficulty,
                               bot_ai_rng[bot_id]);
    bot_ai_rng[bot_id] = decision.rng_state;

    if (!decision.valid) return;

    /* Log challenge/block from bot */
    if (decision.input.type == COUP_INPUT_RESPONSE &&
        decision.input.data.response.response == COUP_RULES_RESP_CHALLENGE) {
        char buf[COUP_LOG_LINE_LEN + 1];
        int pos = 0;
        const char* s;
        for (s = g_state.players[bot_id].name;
             *s && pos < COUP_LOG_LINE_LEN; )
            buf[pos++] = *s++;
        for (s = " challenges!"; *s && pos < COUP_LOG_LINE_LEN; )
            buf[pos++] = *s++;
        buf[pos] = '\0';
        coup_log(buf);
    }

    g_pipe.submit(&decision.input);

    /* If bot chose to block, also send the block claim */
    if (decision.has_block_claim &&
        g_rules.phase == COUP_TURN_RESOLVING &&
        g_rules.blocker_id == bot_id) {
        coup_input_t claim;
        memset(&claim, 0, sizeof(claim));
        claim.type = COUP_INPUT_BLOCK_CLAIM;
        claim.player_id = bot_id;
        claim.data.block_claim.character = decision.block_claim_char;
        g_pipe.submit(&claim);
    }

    /* Reset think timer */
    g_state.bot_think_timer = BOT_RESPONSE_MIN +
        (int)(bot_rand() % (uint32_t)(BOT_RESPONSE_MAX - BOT_RESPONSE_MIN));
}

/*============================================================================
 * Screen Update Handlers
 *============================================================================*/

static void update_title(cui_input_action_t action)
{
    /* Title menu: A/START = Play (go to lobby).
     * L = Settings, R = Rules. */

    switch (action) {
    case CUI_INPUT_CONFIRM:
    case CUI_INPUT_QUIT:
        /* Play → go to lobby with name entry overlay */
        g_state.online_mode = false;
        g_state.screen = COUP_SCREEN_LOBBY;
        g_state.lobby_naming = true;
        g_state.lobby_cursor = 0;
        g_state.my_ready = false;
        lobby_sync_players();
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        break;

    case CUI_INPUT_PAGE_UP:
        /* L-button opens settings */
        g_state.settings_return_screen = COUP_SCREEN_TITLE;
        g_state.screen = COUP_SCREEN_SETTINGS;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        break;

    case CUI_INPUT_PAGE_DOWN:
        /* R-button opens rules */
        g_state.rules_page = 0;
        g_state.rules_return_screen = COUP_SCREEN_TITLE;
        g_state.screen = COUP_SCREEN_RULES;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        break;

    default:
        break;
    }
}

static void update_settings(cui_input_action_t action)
{
    /* Settings: LEFT/RIGHT = adjust bot difficulty, B = back */
    switch (action) {
    case CUI_INPUT_LEFT:
        if (g_state.bot_difficulty > 0) {
            g_state.bot_difficulty--;
            coup_audio_play_sfx(COUP_SFX_CONFIRM);
        }
        break;

    case CUI_INPUT_RIGHT:
        if (g_state.bot_difficulty < 2) {
            g_state.bot_difficulty++;
            coup_audio_play_sfx(COUP_SFX_CONFIRM);
        }
        break;

    case CUI_INPUT_CANCEL:
        g_state.screen = g_state.settings_return_screen;
        coup_audio_play_sfx(COUP_SFX_CANCEL);
        break;

    default:
        break;
    }
}

static void update_connecting(cui_input_action_t action)
{
    /* CANCEL goes back to title */
    if (action == CUI_INPUT_CANCEL) {
        g_state.screen    = COUP_SCREEN_TITLE;
        g_state.connected = false;
        /* BGM restart handled by centralized screen-transition logic */
    }
}

static void update_name_entry(cui_input_action_t action)
{
    int idx;

    switch (action) {
    case CUI_INPUT_UP:
        /* Scroll current character forward in charset */
        idx = g_name_char_idx[g_state.name_cursor];
        idx = (idx + 1) % NAME_CHARSET_LEN;
        g_name_char_idx[g_state.name_cursor] = idx;
        /* Auto-extend name if cursor is at the end */
        if (g_state.name_cursor >= g_state.name_len
            && g_state.name_cursor < COUP_MAX_NAME - 1) {
            g_state.name_len = g_state.name_cursor + 1;
            g_state.name_buf[g_state.name_len] = '\0';
        }
        /* Update name buffer */
        if (g_state.name_cursor < g_state.name_len) {
            g_state.name_buf[g_state.name_cursor] = NAME_CHARSET[idx];
        }
        break;

    case CUI_INPUT_DOWN:
        /* Scroll current character backward in charset */
        idx = g_name_char_idx[g_state.name_cursor];
        idx = (idx + NAME_CHARSET_LEN - 1) % NAME_CHARSET_LEN;
        g_name_char_idx[g_state.name_cursor] = idx;
        /* Auto-extend name if cursor is at the end */
        if (g_state.name_cursor >= g_state.name_len
            && g_state.name_cursor < COUP_MAX_NAME - 1) {
            g_state.name_len = g_state.name_cursor + 1;
            g_state.name_buf[g_state.name_len] = '\0';
        }
        if (g_state.name_cursor < g_state.name_len) {
            g_state.name_buf[g_state.name_cursor] = NAME_CHARSET[idx];
        }
        break;

    case CUI_INPUT_RIGHT:
        /* Move cursor right, extend name if at end */
        if (g_state.name_cursor < COUP_MAX_NAME - 2) {
            if (g_state.name_cursor == g_state.name_len) {
                /* Append new character (default 'A') */
                g_state.name_buf[g_state.name_len] =
                    NAME_CHARSET[g_name_char_idx[g_state.name_len]];
                g_state.name_len++;
                g_state.name_buf[g_state.name_len] = '\0';
            }
            g_state.name_cursor++;
        }
        break;

    case CUI_INPUT_LEFT:
        /* Move cursor left */
        if (g_state.name_cursor > 0) {
            g_state.name_cursor--;
        }
        break;

    case CUI_INPUT_CANCEL:
        /* Delete last character (backspace) */
        if (g_state.name_len > 0) {
            g_state.name_len--;
            g_state.name_buf[g_state.name_len] = '\0';
            if (g_state.name_cursor > g_state.name_len) {
                g_state.name_cursor = g_state.name_len;
            }
        }
        break;

    case CUI_INPUT_CONFIRM:
        /* Submit name if non-empty */
        if (g_state.name_len > 0 && g_transport) {
            int sz;
            g_state.name_buf[g_state.name_len] = '\0';
            sz = coup_encode_set_username(g_tx_buf, g_state.name_buf);
            cui_transport_send(g_transport, g_tx_buf, sz);
            /* Wait for WELCOME response - stay on NAME_ENTRY until server
               confirms. Phase transitions on WELCOME message. */
        }
        break;

    default:
        break;
    }
}

static void update_lobby(cui_input_action_t action)
{
    /* --- Name entry overlay active --- */
    if (g_state.lobby_naming) {
        switch (action) {
        case CUI_INPUT_UP:
        case CUI_INPUT_DOWN:
        case CUI_INPUT_LEFT:
        case CUI_INPUT_RIGHT:
            /* Delegate character editing to name entry logic */
            update_name_entry(action);
            break;

        case CUI_INPUT_CANCEL:
            /* B = backspace in name entry */
            update_name_entry(action);
            break;

        case CUI_INPUT_CONFIRM:
            /* A = confirm name, dismiss overlay */
            if (g_state.name_len > 0) {
                g_state.lobby_naming = false;
                g_state.name_buf[g_state.name_len] = '\0';
                /* Update player[0] name from name_buf */
                coup_strcpy(g_state.players[0].name, g_state.name_buf, COUP_MAX_NAME);
                coup_audio_play_sfx(COUP_SFX_CONFIRM);
                /* Save name for next session */
                coup_auth_save();
            }
            break;

        default:
            break;
        }
        return;
    }

    /* --- Player list interactive --- */

    {
        /* === Unified lobby input (offline + online) === */
        int max_cursor = g_state.player_count; /* first empty slot */
        if (max_cursor > COUP_MAX_PLAYERS - 1) max_cursor = COUP_MAX_PLAYERS - 1;

        switch (action) {
        case CUI_INPUT_UP:
            if (g_state.lobby_cursor > 0) {
                g_state.lobby_cursor--;
                coup_audio_play_sfx(COUP_SFX_CONFIRM);
            }
            break;

        case CUI_INPUT_DOWN:
            if (g_state.lobby_cursor < max_cursor) {
                g_state.lobby_cursor++;
                coup_audio_play_sfx(COUP_SFX_CONFIRM);
            }
            break;

        case CUI_INPUT_LOG_DOWN:
            /* X = add/remove bot at cursor */
            if (g_state.lobby_cursor == 0) {
                /* Can't remove self */
            } else if (g_state.lobby_cursor < g_state.player_count) {
                /* Remove bot at cursor position */
                if (g_state.online_mode) {
                    if (g_transport) {
                        int sz = coup_encode_remove_bot(g_tx_buf);
                        cui_transport_send(g_transport, g_tx_buf, sz);
                        coup_audio_play_sfx(COUP_SFX_CANCEL);
                    }
                } else if (g_state.bot_count > 1) {
                    int slot = g_state.lobby_cursor;
                    int j;
                    for (j = slot; j < g_state.player_count - 1; j++) {
                        g_state.players[j] = g_state.players[j + 1];
                        g_state.players[j].id = (uint8_t)j;
                    }
                    g_state.bot_count--;
                    g_state.player_count--;
                    if (g_state.lobby_cursor >= g_state.player_count && g_state.lobby_cursor > 0) {
                        g_state.lobby_cursor = g_state.player_count;
                    }
                    coup_audio_play_sfx(COUP_SFX_CANCEL);
                }
            } else if (g_state.lobby_cursor == g_state.player_count) {
                /* Add bot at empty slot */
                if (g_state.online_mode) {
                    if (g_transport) {
                        int sz = coup_encode_add_bot(g_tx_buf,
                            (uint8_t)g_state.bot_difficulty);
                        cui_transport_send(g_transport, g_tx_buf, sz);
                        coup_audio_play_sfx(COUP_SFX_CONFIRM);
                    }
                } else if (g_state.bot_count < BOT_MAX) {
                    int pi = g_state.player_count;
                    g_state.players[pi].id = (uint8_t)pi;
                    coup_strcpy(g_state.players[pi].name, bot_names[g_state.bot_count], COUP_MAX_NAME);
                    g_state.players[pi].is_self = false;
                    g_state.players[pi].is_bot  = true;
                    g_state.players[pi].ready = true;
                    g_state.players[pi].difficulty = (uint8_t)g_state.bot_difficulty;
                    g_state.bot_count++;
                    g_state.player_count++;
                    coup_audio_play_sfx(COUP_SFX_CONFIRM);
                }
            }
            break;

        case CUI_INPUT_LEFT:
            /* Left = decrease bot difficulty at cursor */
            if (g_state.lobby_cursor > 0 && g_state.lobby_cursor < g_state.player_count &&
                g_state.players[g_state.lobby_cursor].is_bot) {
                if (g_state.players[g_state.lobby_cursor].difficulty > 0) {
                    g_state.players[g_state.lobby_cursor].difficulty--;
                    coup_audio_play_sfx(COUP_SFX_CONFIRM);
                    if (g_state.online_mode && g_transport) {
                        /* Count bots before cursor to get lobby_bots[] index */
                        int bi = 0, k;
                        for (k = 0; k < g_state.lobby_cursor; k++)
                            if (g_state.players[k].is_bot) bi++;
                        {
                            int sz = coup_encode_set_bot_difficulty(g_tx_buf,
                                (uint8_t)bi,
                                g_state.players[g_state.lobby_cursor].difficulty);
                            cui_transport_send(g_transport, g_tx_buf, sz);
                        }
                    }
                }
            }
            break;

        case CUI_INPUT_RIGHT:
            /* Right = increase bot difficulty at cursor */
            if (g_state.lobby_cursor > 0 && g_state.lobby_cursor < g_state.player_count &&
                g_state.players[g_state.lobby_cursor].is_bot) {
                if (g_state.players[g_state.lobby_cursor].difficulty < 2) {
                    g_state.players[g_state.lobby_cursor].difficulty++;
                    coup_audio_play_sfx(COUP_SFX_CONFIRM);
                    if (g_state.online_mode && g_transport) {
                        int bi = 0, k;
                        for (k = 0; k < g_state.lobby_cursor; k++)
                            if (g_state.players[k].is_bot) bi++;
                        {
                            int sz = coup_encode_set_bot_difficulty(g_tx_buf,
                                (uint8_t)bi,
                                g_state.players[g_state.lobby_cursor].difficulty);
                            cui_transport_send(g_transport, g_tx_buf, sz);
                        }
                    }
                }
            }
            break;

        case CUI_INPUT_CONFIRM:
            /* A = toggle ready */
            g_state.my_ready = !g_state.my_ready;
            g_state.players[0].ready = g_state.my_ready;
            coup_audio_play_sfx(g_state.my_ready ? COUP_SFX_CONFIRM : COUP_SFX_CANCEL);
            if (g_state.online_mode && g_transport) {
                int sz = coup_encode_ready(g_tx_buf);
                cui_transport_send(g_transport, g_tx_buf, sz);
            }
            break;

        case CUI_INPUT_QUIT:
            /* START = begin game */
            if (g_state.online_mode) {
                /* Send request — server validates readiness */
                if (g_transport) {
                    int sz = coup_encode_start_game(g_tx_buf);
                    cui_transport_send(g_transport, g_tx_buf, sz);
                    coup_audio_play_sfx(COUP_SFX_CONFIRM);
                    coup_log("Requesting game start...");
                }
            } else {
                if (g_state.my_ready) {
                    coup_start_local_game();
                    coup_audio_play_sfx(COUP_SFX_CONFIRM);
                } else {
                    coup_audio_play_sfx(COUP_SFX_CANCEL);
                }
            }
            break;

        case CUI_INPUT_CANCEL:
            /* B = back to title (offline) or leave lobby (online) */
            if (g_state.online_mode) {
                coup_send_disconnect();
                coup_on_disconnected();
            } else {
                g_state.screen = COUP_SCREEN_TITLE;
            }
            coup_audio_play_sfx(COUP_SFX_CANCEL);
            break;

        case CUI_INPUT_LOG_RESET:
            /* Z = go online (offline only) */
            if (!g_state.online_mode) {
                g_state.bot_count = 0;
                g_state.player_count = 1;
                g_state.screen = COUP_SCREEN_CONNECTING;
                g_state.connect_stage = 0;
                coup_strcpy(g_state.connect_msg, "Connecting...",
                            sizeof(g_state.connect_msg));
                coup_audio_play_sfx(COUP_SFX_CONFIRM);
                coup_render_now();
                coup_platform_try_connect();
            }
            break;

        default:
            break;
        }
    }

    /* L/R available in both modes */
    if (action == CUI_INPUT_PAGE_UP) {
        g_state.settings_return_screen = COUP_SCREEN_LOBBY;
        g_state.screen = COUP_SCREEN_SETTINGS;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
    } else if (action == CUI_INPUT_PAGE_DOWN) {
        g_state.rules_page = 0;
        g_state.rules_return_screen = COUP_SCREEN_LOBBY;
        g_state.screen = COUP_SCREEN_RULES;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
    }
}

static void update_game(cui_input_action_t action)
{
    /* In local mode, START returns to lobby */
    if (g_state.local_mode && action == CUI_INPUT_QUIT) {
        g_state.local_mode   = false;
        g_state.online_mode  = false;
        g_state.screen       = COUP_SCREEN_LOBBY;
        g_state.phase        = COUP_PHASE_IDLE;
        g_state.log_count    = 0;
        g_state.log_head     = 0;
        g_state.lobby_naming = false;
        g_state.my_ready     = false;
        g_state.lobby_cursor = 0;
        lobby_sync_players();
        return;
    }

    /* R-button opens rules */
    if (action == CUI_INPUT_PAGE_DOWN) {
        g_state.rules_page = 0;
        g_state.rules_return_screen = COUP_SCREEN_GAME;
        g_state.screen = COUP_SCREEN_RULES;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        return;
    }

    switch (g_state.phase) {
    case COUP_PHASE_SELECT_ACTION:
        update_phase_select_action(action);
        break;
    case COUP_PHASE_SELECT_TARGET:
        update_phase_select_target(action);
        break;
    case COUP_PHASE_CHALLENGE_WAIT:
        update_phase_challenge_wait(action);
        break;
    case COUP_PHASE_BLOCK_WAIT:
        update_phase_block_wait(action);
        break;
    case COUP_PHASE_BLOCK_CHALLENGE:
        update_phase_block_challenge(action);
        break;
    case COUP_PHASE_LOSE_INFLUENCE:
        update_phase_lose_influence(action);
        break;
    case COUP_PHASE_EXCHANGE_PICK:
        update_phase_exchange_pick(action);
        break;
    case COUP_PHASE_IDLE:
    case COUP_PHASE_RESOLVING:
    default:
        break;
    }
}

static void update_game_over(cui_input_action_t action)
{
    if (action == CUI_INPUT_CONFIRM) {
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        if (g_state.local_mode) {
            /* Local game: return to lobby for rematch / adjust */
            g_state.local_mode   = false;
            g_state.online_mode  = false;
            g_state.screen       = COUP_SCREEN_LOBBY;
            g_state.phase        = COUP_PHASE_IDLE;
            g_state.log_count    = 0;
            g_state.log_head     = 0;
            g_state.lobby_naming = false;
            g_state.my_ready     = false;
            g_state.lobby_cursor = 0;
            lobby_sync_players();
        } else {
            /* Online: return to lobby */
            g_state.is_spectator = false;
            g_state.my_id    = g_state.server_user_id;  /* Restore for lobby is_self checks */
            g_state.screen   = COUP_SCREEN_LOBBY;
            g_state.phase    = COUP_PHASE_IDLE;
            g_state.my_ready = false;
        }
    }
}

/*============================================================================
 * Game Phase Handlers
 *============================================================================*/

static void update_phase_select_action(cui_input_action_t action)
{
    int pos, tries, next;

    switch (action) {
    case CUI_INPUT_UP:
        /* Find current display position */
        for (pos = 0; pos < COUP_NUM_ACTIONS; pos++) {
            if (coup_action_display_order[pos] == g_state.menu_cursor) break;
        }
        /* Move up in display order, skipping unavailable actions */
        tries = COUP_NUM_ACTIONS;
        do {
            pos = (pos + COUP_NUM_ACTIONS - 1) % COUP_NUM_ACTIONS;
            next = coup_action_display_order[pos];
            tries--;
        } while (tries > 0 && !(g_state.valid_actions & (1 << next)));
        g_state.menu_cursor = next;
        break;

    case CUI_INPUT_DOWN:
        /* Find current display position */
        for (pos = 0; pos < COUP_NUM_ACTIONS; pos++) {
            if (coup_action_display_order[pos] == g_state.menu_cursor) break;
        }
        /* Move down in display order, skipping unavailable actions */
        tries = COUP_NUM_ACTIONS;
        do {
            pos = (pos + 1) % COUP_NUM_ACTIONS;
            next = coup_action_display_order[pos];
            tries--;
        } while (tries > 0 && !(g_state.valid_actions & (1 << next)));
        g_state.menu_cursor = next;
        break;

    case CUI_INPUT_CONFIRM:
        if (!(g_state.valid_actions & (1 << g_state.menu_cursor))) break;

        if (coup_action_needs_target[g_state.menu_cursor]) {
            /* Go to target selection */
            g_state.phase         = COUP_PHASE_SELECT_TARGET;
            g_state.target_cursor = 0;
        } else {
            coup_input_t inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = COUP_INPUT_ACTION;
            inp.player_id = g_state.my_id;
            inp.data.action.action = (uint8_t)g_state.menu_cursor;
            inp.data.action.target_id = 0xFF;
            g_pipe.submit(&inp);
        }
        break;

    default:
        break;
    }
}

static void update_phase_select_target(cui_input_action_t action)
{
    int alive_count;

    switch (action) {
    case CUI_INPUT_UP:
        alive_count = count_alive_opponents();
        if (alive_count > 0) {
            g_state.target_cursor =
                (g_state.target_cursor + alive_count - 1) % alive_count;
        }
        break;

    case CUI_INPUT_DOWN:
        alive_count = count_alive_opponents();
        if (alive_count > 0) {
            g_state.target_cursor =
                (g_state.target_cursor + 1) % alive_count;
        }
        break;

    case CUI_INPUT_CONFIRM: {
        int target_player_id = get_alive_opponent_by_index(g_state.target_cursor);
        if (target_player_id >= 0) {
            coup_input_t inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = COUP_INPUT_ACTION;
            inp.player_id = g_state.my_id;
            inp.data.action.action = (uint8_t)g_state.menu_cursor;
            inp.data.action.target_id = (uint8_t)target_player_id;
            g_pipe.submit(&inp);
        }
        break;
    }

    case CUI_INPUT_CANCEL:
        /* Go back to action selection */
        g_state.phase = COUP_PHASE_SELECT_ACTION;
        break;

    default:
        break;
    }
}

static void update_phase_challenge_wait(cui_input_action_t action)
{
    /* Menu: 0=Allow, 1=Challenge */
    switch (action) {
    case CUI_INPUT_UP:
        if (g_state.menu_cursor > 0) g_state.menu_cursor--;
        break;
    case CUI_INPUT_DOWN:
        if (g_state.menu_cursor < 1) g_state.menu_cursor++;
        break;
    case CUI_INPUT_CONFIRM: {
        bool challenge = (g_state.menu_cursor == 1);
        coup_input_t inp;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        if (challenge) coup_log("You challenge!");

        memset(&inp, 0, sizeof(inp));
        inp.type = COUP_INPUT_RESPONSE;
        inp.player_id = g_state.my_id;
        inp.data.response.response = challenge
            ? COUP_RULES_RESP_CHALLENGE : COUP_RULES_RESP_PASS;
        g_pipe.submit(&inp);
        break;
    }
    default:
        break;
    }
}

static void update_phase_block_wait(cui_input_action_t action)
{
    /* Menu: 0=Allow, 1..N=Block with character[i-1] */
    int max_cursor = g_state.block_claim_count; /* Allow + N block options */

    switch (action) {
    case CUI_INPUT_UP:
        if (g_state.menu_cursor > 0) g_state.menu_cursor--;
        break;
    case CUI_INPUT_DOWN:
        if (g_state.menu_cursor < max_cursor) g_state.menu_cursor++;
        break;
    case CUI_INPUT_CONFIRM: {
        coup_audio_play_sfx(COUP_SFX_CONFIRM);

        if (g_state.menu_cursor == 0) {
            /* Allow — pass */
            coup_input_t inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = COUP_INPUT_RESPONSE;
            inp.player_id = g_state.my_id;
            inp.data.response.response = COUP_RULES_RESP_PASS;
            g_pipe.submit(&inp);
        } else {
            /* Block with character at index (cursor - 1) */
            uint8_t chosen = g_state.block_claim_chars[g_state.menu_cursor - 1];
            coup_input_t inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = COUP_INPUT_RESPONSE;
            inp.player_id = g_state.my_id;
            inp.data.response.response = COUP_RULES_RESP_BLOCK;
            g_pipe.submit(&inp);

            /* Follow up with block claim */
            {
                coup_input_t claim;
                memset(&claim, 0, sizeof(claim));
                claim.type = COUP_INPUT_BLOCK_CLAIM;
                claim.player_id = g_state.my_id;
                claim.data.block_claim.character = chosen;
                g_pipe.submit(&claim);
            }
        }
        break;
    }
    default:
        break;
    }
}

static void update_phase_block_challenge(cui_input_action_t action)
{
    /* Menu: 0=Allow, 1=Challenge */
    switch (action) {
    case CUI_INPUT_UP:
        if (g_state.menu_cursor > 0) g_state.menu_cursor--;
        break;
    case CUI_INPUT_DOWN:
        if (g_state.menu_cursor < 1) g_state.menu_cursor++;
        break;
    case CUI_INPUT_CONFIRM: {
        bool challenge = (g_state.menu_cursor == 1);
        coup_input_t inp;
        coup_audio_play_sfx(COUP_SFX_CONFIRM);
        if (challenge) coup_log("You challenge the block!");

        memset(&inp, 0, sizeof(inp));
        inp.type = COUP_INPUT_RESPONSE;
        inp.player_id = g_state.my_id;
        inp.data.response.response = challenge
            ? COUP_RULES_RESP_CHALLENGE : COUP_RULES_RESP_PASS;
        g_pipe.submit(&inp);
        break;
    }
    default:
        break;
    }
}

static void update_phase_lose_influence(cui_input_action_t action)
{
    int i, found;

    switch (action) {
    case CUI_INPUT_UP:
        /* Move cursor to previous alive card */
        found = -1;
        for (i = g_state.lose_cursor - 1; i >= 0; i--) {
            if (g_state.my_cards[i] != COUP_CHAR_NONE) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            g_state.lose_cursor = found;
        }
        break;

    case CUI_INPUT_DOWN:
        /* Move cursor to next alive card */
        found = -1;
        for (i = g_state.lose_cursor + 1; i < COUP_CARDS_PER_PLAYER; i++) {
            if (g_state.my_cards[i] != COUP_CHAR_NONE) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            g_state.lose_cursor = found;
        }
        break;

    case CUI_INPUT_CONFIRM:
        /* Confirm card to lose */
        if (g_state.my_cards[g_state.lose_cursor] != COUP_CHAR_NONE) {
            coup_input_t inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = COUP_INPUT_LOSE_INFLUENCE;
            inp.player_id = g_state.my_id;
            inp.data.lose_influence.card_idx =
                (uint8_t)g_state.lose_cursor;
            g_pipe.submit(&inp);
        }
        break;

    default:
        break;
    }
}

static void update_phase_exchange_pick(cui_input_action_t action)
{
    int i, sel_count;
    int cards_to_keep = count_alive_cards();

    switch (action) {
    case CUI_INPUT_UP:
        if (g_state.exchange_cursor > 0) {
            g_state.exchange_cursor--;
        }
        break;

    case CUI_INPUT_DOWN:
        if (g_state.exchange_cursor < g_state.exchange_count - 1) {
            g_state.exchange_cursor++;
        }
        break;

    case CUI_INPUT_CONFIRM:
        /* Toggle selection of current card */
        sel_count = count_selected_exchange();

        /* Check if already selected */
        for (i = 0; i < 2; i++) {
            if (g_state.exchange_sel[i] == g_state.exchange_cursor) {
                /* Deselect */
                g_state.exchange_sel[i] = -1;
                return;
            }
        }

        /* Not selected - add if room */
        if (sel_count < cards_to_keep) {
            for (i = 0; i < 2; i++) {
                if (g_state.exchange_sel[i] < 0) {
                    g_state.exchange_sel[i] = g_state.exchange_cursor;
                    break;
                }
            }
        }

        /* Check if we have enough selected and can submit */
        if (count_selected_exchange() == cards_to_keep) {
            coup_input_t inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = COUP_INPUT_EXCHANGE_CHOICE;
            inp.player_id = g_state.my_id;
            inp.data.exchange_choice.keep[0] =
                (uint8_t)g_state.exchange_sel[0];
            inp.data.exchange_choice.keep[1] =
                (uint8_t)g_state.exchange_sel[1];
            g_pipe.submit(&inp);
        }
        break;

    default:
        break;
    }
}

/*============================================================================
 * Network: Send Helpers
 *============================================================================*/

static void send_connect(void)
{
    int sz;
    if (!g_transport) return;

    if (g_state.has_uuid) {
        sz = coup_encode_connect_uuid(g_tx_buf, g_state.my_uuid);
    } else {
        sz = coup_encode_connect(g_tx_buf);
    }
    cui_transport_send(g_transport, g_tx_buf, sz);
}

static void send_heartbeat(void)
{
    int sz;
    if (!g_transport) return;
    sz = coup_encode_heartbeat(g_tx_buf);
    cui_transport_send(g_transport, g_tx_buf, sz);
}

/* translate_message_to_events() removed — replaced by INPUT_RELAY handler */

/*============================================================================
 * Network: Message Processor
 *============================================================================*/

static void process_message(const uint8_t* frame, int len)
{
    uint8_t msg_type;
    const uint8_t* p;
    int remaining;

    if (!frame || len < 1) return;

    msg_type  = frame[0];
    p         = frame + 1;
    remaining = len - 1;

    switch (msg_type) {

    /*------------------------------------------------------------------------
     * Auth / Session
     *----------------------------------------------------------------------*/

    case SNCP_MSG_USERNAME_REQUIRED:
        /* Server wants us to set a username.
         * If we already have a name (from lobby overlay or save), auto-submit. */
        if (g_state.name_buf[0] && g_state.name_len > 0 && g_transport) {
            int sz;
            g_state.name_buf[g_state.name_len] = '\0';
            sz = coup_encode_set_username(g_tx_buf, g_state.name_buf);
            cui_transport_send(g_transport, g_tx_buf, sz);
        } else {
            /* No name yet — show name entry screen */
            g_state.screen      = COUP_SCREEN_NAME_ENTRY;
            g_state.name_cursor = 0;
            g_state.name_len    = 0;
            g_state.name_buf[0] = '\0';
            memset(g_name_char_idx, 0, sizeof(g_name_char_idx));
            coup_log("Enter your name to play.");
        }
        break;

    case SNCP_MSG_WELCOME:
    case SNCP_MSG_WELCOME_BACK: {
        /* Server sends: [msg_type:1][user_id:1][uuid:36][username:LP_string] */
        int i;
        if (remaining < 1 + SNCP_UUID_LEN) break;

        g_state.my_id = p[0];
        g_state.server_user_id = p[0];   /* Preserve for lobby use */

        for (i = 0; i < SNCP_UUID_LEN; i++) {
            g_state.my_uuid[i] = (char)p[1 + i];
        }
        g_state.my_uuid[SNCP_UUID_LEN] = '\0';
        g_state.has_uuid  = true;

        /* Extract username from WELCOME message so we have our name
         * even when skipping name entry (WELCOME_BACK reconnect). */
        {
            const uint8_t* uname_p = p + 1 + SNCP_UUID_LEN;
            int uname_rem = remaining - 1 - SNCP_UUID_LEN;
            if (uname_rem > 0) {
                char name_tmp[COUP_MAX_NAME];
                int consumed = coup_read_string(uname_p, uname_rem,
                                                name_tmp, COUP_MAX_NAME);
                if (consumed > 0 && name_tmp[0]) {
                    coup_strcpy(g_state.name_buf, name_tmp,
                                sizeof(g_state.name_buf));
                    g_state.name_len = coup_strlen(g_state.name_buf);
                }
            }
        }

        g_state.screen       = COUP_SCREEN_LOBBY;
        g_state.my_ready     = false;
        g_state.online_mode  = true;
        g_state.lobby_naming = false;
        g_state.lobby_cursor = 0;

        /* Add self to player list immediately so the lobby is never
         * blank.  Server LOBBY_STATE will overwrite when it arrives. */
        g_state.player_count = 1;
        g_state.players[0].id      = g_state.my_id;
        coup_strcpy(g_state.players[0].name,
                    g_state.name_buf[0] ? g_state.name_buf : "YOU",
                    COUP_MAX_NAME);
        g_state.players[0].coins   = 0;
        g_state.players[0].alive   = true;
        g_state.players[0].is_self = true;
        g_state.players[0].is_bot  = false;
        g_state.players[0].ready   = false;

        coup_log("Connected! Waiting for players...");
        coup_auth_save();
        break;
    }

    case SNCP_MSG_USERNAME_TAKEN:
        coup_log("Name taken. Choose another.");
        g_state.screen = COUP_SCREEN_NAME_ENTRY;
        break;

    /*------------------------------------------------------------------------
     * Lobby
     *----------------------------------------------------------------------*/

    case COUP_MSG_LOBBY_STATE: {
        /* [count:1][{id:1, name:LP, ready:1, is_bot:1, difficulty:1}...] */
        int i;
        uint8_t count;

        if (remaining < 1) break;
        count = p[0];
        if (count > COUP_MAX_PLAYERS) count = COUP_MAX_PLAYERS;

        g_state.player_count = (int)count;
        g_state.bot_count = 0;
        p++;
        remaining--;

        for (i = 0; i < (int)count; i++) {
            int consumed;
            char name_tmp[COUP_MAX_NAME];

            if (remaining < 1) break;
            g_state.players[i].id = p[0];
            p++; remaining--;

            consumed = coup_read_string(p, remaining, name_tmp, COUP_MAX_NAME);
            if (consumed < 0) break;
            coup_strcpy(g_state.players[i].name, name_tmp, COUP_MAX_NAME);
            p += consumed; remaining -= consumed;

            if (remaining < 3) break;  /* ready + is_bot + difficulty */
            g_state.players[i].ready      = (p[0] != 0);
            g_state.players[i].is_bot     = (p[1] != 0);
            g_state.players[i].difficulty = p[2];
            g_state.players[i].alive      = true;
            g_state.players[i].is_self    = (g_state.players[i].id == g_state.my_id);
            if (g_state.players[i].is_bot) g_state.bot_count++;
            p += 3; remaining -= 3;
        }

        /* Clamp lobby cursor to valid range */
        {
            int max_c = g_state.player_count;
            if (max_c > COUP_MAX_PLAYERS - 1) max_c = COUP_MAX_PLAYERS - 1;
            if (g_state.lobby_cursor > max_c)
                g_state.lobby_cursor = max_c;
        }
        break;
    }

    /*------------------------------------------------------------------------
     * Game Start
     *----------------------------------------------------------------------*/

    case COUP_MSG_GAME_START: {
        /* [seed:4 BE][my_engine_pid:1][count:1][uid_0:1]...[uid_n:1] */
        uint32_t seed;
        uint8_t my_pid, order_count;

        if (remaining < 6) break;
        seed = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
        my_pid = p[4];
        order_count = p[5];
        p += 6; remaining -= 6;

        if (order_count > COUP_MAX_PLAYERS) order_count = COUP_MAX_PLAYERS;
        if (remaining < (int)order_count) break;

        /* Reorder players[] to match engine PID order.
         * Server sends user_ids in engine PID order; match them against
         * the LOBBY_STATE-populated players by their server-assigned id. */
        {
            coup_player_t tmp[COUP_MAX_PLAYERS];
            int i, j;
            for (i = 0; i < (int)order_count; i++) {
                uint8_t uid = p[i];
                /* Find the player with this user_id in current array */
                for (j = 0; j < g_state.player_count; j++) {
                    if (g_state.players[j].id == uid) {
                        tmp[i] = g_state.players[j];
                        break;
                    }
                }
                if (j == g_state.player_count) {
                    /* Bot slot (uid=0xFF) — keep existing entry at this index */
                    tmp[i] = g_state.players[i];
                }
            }
            for (i = 0; i < (int)order_count; i++)
                g_state.players[i] = tmp[i];
            g_state.player_count = (int)order_count;
        }

        g_state.local_mode = false;
        coup_start_game(seed, my_pid);
        /* Note: INPUT_RELAY(START_GAME) will arrive next and trigger
         * coup_rules_submit(START_GAME) → process_rule_events → sync_ui_phase */
        break;
    }

    /*------------------------------------------------------------------------
     * INPUT_RELAY: feed input to local rule engine
     *
     * This replaces all the old per-message handlers. The server relays raw
     * player inputs; the client feeds them to its local engine copy.
     *----------------------------------------------------------------------*/

    case COUP_MSG_INPUT_RELAY: {
        coup_input_t relay_input;
        uint16_t seq;
        int n;

        memset(&relay_input, 0, sizeof(relay_input));
        if (coup_decode_input_relay(p, remaining, &relay_input, &seq) < 0)
            break;

        /* Sequence validation (online mode) */
        if (g_state.online_mode) {
            if (!g_state.relay_seq_valid) {
                /* First relay: accept and initialize tracking */
                g_state.relay_seq_valid = true;
                g_state.relay_expected_seq = seq;
            } else if (seq != g_state.relay_expected_seq) {
                /* Desync detected — request resync */
                int sz;
                uint16_t last_seen = (g_state.relay_expected_seq - 1) & 0xFFFF;
                coup_log("Sync lost, requesting resync...");
                sz = coup_encode_resync_req(g_tx_buf, last_seen);
                cui_transport_send(g_transport, g_tx_buf, sz);
                break;
            }
            g_state.relay_expected_seq = (seq + 1) & 0xFFFF;
            g_state.resolving_timer = 0;
        }

        /* During full resync, count down but suppress UI sync */
        if (g_state.resync_pending) {
            g_state.resync_received++;
            n = coup_rules_submit(&g_rules, &relay_input,
                                  g_rule_events, COUP_RULES_MAX_EVENTS);
            /* Don't process events or sync UI during replay */
            if (g_state.resync_received >= g_state.resync_total) {
                g_state.resync_pending = false;
                if (n > 0) process_rule_events(g_rule_events, n);
                sync_ui_phase();
                coup_log("Resync complete.");
            }
            break;
        }

        n = coup_rules_submit(&g_rules, &relay_input,
                              g_rule_events, COUP_RULES_MAX_EVENTS);
        if (n > 0) process_rule_events(g_rule_events, n);
        sync_ui_phase();

        /* Set response timers from engine phase */
        switch (g_rules.phase) {
        case COUP_TURN_CHALLENGE_WINDOW:
        case COUP_TURN_BLOCK_CHALLENGE_WINDOW:
            g_state.response_timer = COUP_CHALLENGE_TIMEOUT * 60;
            g_state.response_timeout = g_state.response_timer;
            break;
        case COUP_TURN_BLOCK_WINDOW:
            g_state.response_timer = COUP_BLOCK_TIMEOUT * 60;
            g_state.response_timeout = g_state.response_timer;
            break;
        case COUP_TURN_WAITING_FOR_INFLUENCE_LOSS:
            g_state.response_timer = COUP_INFLUENCE_TIMEOUT * 60;
            g_state.response_timeout = g_state.response_timer;
            break;
        case COUP_TURN_WAITING_FOR_EXCHANGE:
            g_state.response_timer = COUP_EXCHANGE_TIMEOUT * 60;
            g_state.response_timeout = g_state.response_timer;
            break;
        default:
            break;
        }
        break;
    }

    /*------------------------------------------------------------------------
     * ACTION_REJECTED: server dropped our input
     *----------------------------------------------------------------------*/

    case COUP_MSG_ACTION_REJECTED: {
        /* [current_seq:2 BE][phase:1] */
        uint16_t server_seq;
        uint16_t last_received;

        if (remaining < 3) break;
        server_seq = (uint16_t)((p[0] << 8) | p[1]);

        /* Check if engines are still in sync */
        last_received = (g_state.relay_expected_seq - 1) & 0xFFFF;
        if (server_seq == last_received) {
            /* In sync — action was just invalid for current phase */
            sync_ui_phase();
            coup_log("Action rejected by server.");
        } else {
            /* Out of sync — request resync */
            int sz = coup_encode_resync_req(g_tx_buf, last_received);
            cui_transport_send(g_transport, g_tx_buf, sz);
            coup_log("Out of sync, requesting resync...");
        }
        break;
    }

    /*------------------------------------------------------------------------
     * RESYNC: incremental batch of missed relays
     *----------------------------------------------------------------------*/

    case COUP_MSG_RESYNC: {
        /* [count:1][{entry_len:1,seq:2 BE,type:1,pid:1,data...}...] */
        int count, off, ei;
        if (remaining < 1) break;
        count = p[0];
        off = 1;

        for (ei = 0; ei < count && off < remaining; ei++) {
            int entry_len;
            uint16_t eseq;
            coup_input_t rinput;
            int rn;

            if (off >= remaining) break;
            entry_len = p[off]; off++;
            if (off + entry_len > remaining) break;

            /* Parse entry: [seq:2 BE][type:1][pid:1][data...] */
            eseq = (uint16_t)((p[off] << 8) | p[off + 1]);
            memset(&rinput, 0, sizeof(rinput));
            /* Decode from the entry data (skip seq, use type+pid+data) */
            rinput.player_id = p[off + 3];
            {
                uint8_t itype = p[off + 2];
                switch (itype) {
                case COUP_RELAY_START_GAME:
                    rinput.type = COUP_INPUT_START_GAME; break;
                case COUP_RELAY_ACTION:
                    rinput.type = COUP_INPUT_ACTION;
                    if (entry_len >= 6) {
                        rinput.data.action.action    = p[off + 4];
                        rinput.data.action.target_id = p[off + 5];
                    }
                    break;
                case COUP_RELAY_RESPONSE:
                    rinput.type = COUP_INPUT_RESPONSE;
                    if (entry_len >= 5) rinput.data.response.response = p[off + 4];
                    break;
                case COUP_RELAY_BLOCK_CLAIM:
                    rinput.type = COUP_INPUT_BLOCK_CLAIM;
                    if (entry_len >= 5) rinput.data.block_claim.character = p[off + 4];
                    break;
                case COUP_RELAY_LOSE_INFLUENCE:
                    rinput.type = COUP_INPUT_LOSE_INFLUENCE;
                    if (entry_len >= 5) rinput.data.lose_influence.card_idx = p[off + 4];
                    break;
                case COUP_RELAY_EXCHANGE_CHOICE:
                    rinput.type = COUP_INPUT_EXCHANGE_CHOICE;
                    if (entry_len >= 6) {
                        rinput.data.exchange_choice.keep[0] = p[off + 4];
                        rinput.data.exchange_choice.keep[1] = p[off + 5];
                    }
                    break;
                case COUP_RELAY_TIMEOUT:
                    rinput.type = COUP_INPUT_TIMEOUT; break;
                default:
                    off += entry_len; continue;
                }
            }
            rn = coup_rules_submit(&g_rules, &rinput,
                                   g_rule_events, COUP_RULES_MAX_EVENTS);
            if (rn > 0) process_rule_events(g_rule_events, rn);
            g_state.relay_expected_seq = (eseq + 1) & 0xFFFF;
            off += entry_len;
        }
        sync_ui_phase();
        coup_log("Resync complete.");
        break;
    }

    /*------------------------------------------------------------------------
     * RESYNC_FULL: full replay header
     *----------------------------------------------------------------------*/

    case COUP_MSG_RESYNC_FULL: {
        /* [seed:4 BE][my_pid:1][total:2 BE] */
        uint32_t seed;
        uint8_t my_pid;
        uint16_t total;

        if (remaining < 7) break;
        seed = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
        my_pid = p[4];
        total = (uint16_t)((p[5] << 8) | p[6]);

        /* Re-init engine */
        coup_start_game(seed, my_pid);
        g_state.resync_pending  = true;
        g_state.resync_total    = total;
        g_state.resync_received = 0;
        g_state.relay_seq_valid = false;
        coup_log("Full resync started...");
        break;
    }

    /*------------------------------------------------------------------------
     * Server Log (no event equivalent — direct output)
     *----------------------------------------------------------------------*/

    case COUP_MSG_LOG: {
        /* [len:1][text:N] */
        char text_buf[COUP_LOG_LINE_LEN + 1];
        int tlen, copy, i;

        if (remaining < 1) break;
        tlen = (int)p[0];
        if (tlen > remaining - 1) tlen = remaining - 1;
        copy = (tlen < COUP_LOG_LINE_LEN) ? tlen : COUP_LOG_LINE_LEN;
        for (i = 0; i < copy; i++) {
            text_buf[i] = (char)p[1 + i];
        }
        text_buf[copy] = '\0';
        coup_log(text_buf);
        break;
    }

    default:
        /* Unknown message - silently ignore */
        break;
    }
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

static coup_player_t* find_player(uint8_t id)
{
    int i;
    for (i = 0; i < g_state.player_count; i++) {
        if (g_state.players[i].id == id) {
            return &g_state.players[i];
        }
    }
    return NULL;
}

static int count_alive_opponents(void)
{
    int i, count = 0;
    for (i = 0; i < g_state.player_count; i++) {
        if (g_state.players[i].alive && !g_state.players[i].is_self) {
            count++;
        }
    }
    return count;
}

static int get_alive_opponent_by_index(int index)
{
    int i, count = 0;
    for (i = 0; i < g_state.player_count; i++) {
        if (g_state.players[i].alive && !g_state.players[i].is_self) {
            if (count == index) {
                return (int)g_state.players[i].id;
            }
            count++;
        }
    }
    return -1;
}

static int count_alive_cards(void)
{
    int count = 0;
    int i;
    for (i = 0; i < COUP_CARDS_PER_PLAYER; i++) {
        if (g_state.my_cards[i] != COUP_CHAR_NONE) {
            count++;
        }
    }
    return count;
}

static int count_selected_exchange(void)
{
    int count = 0;
    int i;
    for (i = 0; i < 2; i++) {
        if (g_state.exchange_sel[i] >= 0) {
            count++;
        }
    }
    return count;
}
