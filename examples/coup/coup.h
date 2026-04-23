/**
 * coup.h - Coup Card Game for Sega Saturn NetLink
 *
 * 2-6 player online bluffing card game.
 * Uses SNCP binary framing over UART/modem transport.
 *
 * Characters: Duke, Assassin, Captain, Ambassador, Contessa
 * Each player has 2 influence cards and starts with 2 coins.
 * Last player with influence wins.
 */

#ifndef COUP_H
#define COUP_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "cui_types.h"

/*============================================================================
 * Configuration
 *============================================================================*/

#define COUP_MAX_PLAYERS       7
#define COUP_CARDS_PER_PLAYER  2
#define COUP_DECK_SIZE         15   /* 3 copies of 5 characters */
#define COUP_MAX_NAME          16
#define COUP_LOG_LINES         6
#define COUP_LOG_LINE_LEN      39   /* 40 cols - 1 for null */
#define COUP_INITIAL_COINS     2
#define COUP_COUP_COST         7
#define COUP_ASSASSINATE_COST  3
#define COUP_FORCE_COUP_COINS  10
#define COUP_CHALLENGE_TIMEOUT  10   /* seconds */
#define COUP_BLOCK_TIMEOUT      10
#define COUP_INFLUENCE_TIMEOUT  15
#define COUP_EXCHANGE_TIMEOUT   15
#define COUP_TURN_TIMEOUT       60

/*============================================================================
 * Persistent Auth (Backup RAM)
 *============================================================================*/

#define COUP_SAVE_FILENAME  "COUP_AUTH"
#define COUP_SAVE_MAGIC     0x434F5550  /* "COUP" */

typedef struct {
    uint32_t magic;
    char uuid[40];               /* 36 UUID + null + padding */
    char username[COUP_MAX_NAME]; /* 16 bytes — convenience only, not auth */
} coup_save_data_t;

/*============================================================================
 * Character Definitions
 *============================================================================*/

#define COUP_CHAR_DUKE         0
#define COUP_CHAR_ASSASSIN     1
#define COUP_CHAR_CAPTAIN      2
#define COUP_CHAR_AMBASSADOR   3
#define COUP_CHAR_CONTESSA     4
#define COUP_CHAR_FACEDOWN     5   /* Hidden card (other players) */
#define COUP_CHAR_NONE         6   /* Eliminated card slot */
#define COUP_NUM_CHARACTERS    5

/*============================================================================
 * Action Definitions
 *============================================================================*/

#define COUP_ACT_INCOME        0   /* +1 coin, no claim */
#define COUP_ACT_FOREIGN_AID   1   /* +2 coins, no claim, blockable by Duke */
#define COUP_ACT_COUP          2   /* -7 coins, target loses influence */
#define COUP_ACT_TAX           3   /* +3 coins, claims Duke */
#define COUP_ACT_ASSASSINATE   4   /* -3 coins, claims Assassin, target loses influence */
#define COUP_ACT_STEAL         5   /* Take 2 from target, claims Captain */
#define COUP_ACT_EXCHANGE      6   /* Draw 2, keep 2, claims Ambassador */
#define COUP_NUM_ACTIONS       7

/*============================================================================
 * Response Types
 *============================================================================*/

#define COUP_RESP_PASS         0
#define COUP_RESP_CHALLENGE    1
#define COUP_RESP_BLOCK        2

/*============================================================================
 * Screen States
 *============================================================================*/

typedef enum {
    COUP_SCREEN_TITLE,
    COUP_SCREEN_SETTINGS,
    COUP_SCREEN_RULES,
    COUP_SCREEN_CONNECTING,
    COUP_SCREEN_NAME_ENTRY,
    COUP_SCREEN_LOBBY,
    COUP_SCREEN_GAME,
    COUP_SCREEN_GAME_OVER
} coup_screen_t;

#define COUP_RULES_PAGES  6

/*============================================================================
 * Game Phases (within COUP_SCREEN_GAME)
 *============================================================================*/

typedef enum {
    COUP_PHASE_IDLE,               /* Not your turn */
    COUP_PHASE_SELECT_ACTION,      /* Choose action */
    COUP_PHASE_SELECT_TARGET,      /* Choose target player */
    COUP_PHASE_CHALLENGE_WAIT,     /* Can challenge declared action? */
    COUP_PHASE_BLOCK_WAIT,         /* Can block the action? */
    COUP_PHASE_BLOCK_CHALLENGE,    /* Can challenge the block? */
    COUP_PHASE_LOSE_INFLUENCE,     /* Choose which card to lose */
    COUP_PHASE_EXCHANGE_PICK,      /* Choose cards from exchange */
    COUP_PHASE_RESOLVING           /* Server resolving */
} coup_phase_t;

/*============================================================================
 * Sound Effect IDs
 *============================================================================*/

#define COUP_SFX_CONFIRM       0
#define COUP_SFX_CANCEL        1
#define COUP_SFX_CARD_REVEAL   2
#define COUP_SFX_COINS         3
#define COUP_SFX_CHALLENGE     4
#define COUP_SFX_ELIMINATED    5
#define COUP_SFX_VICTORY       6
#define COUP_SFX_TURN_START    7

/*============================================================================
 * Player Structure
 *============================================================================*/

typedef struct {
    uint8_t id;
    char name[COUP_MAX_NAME];
    uint8_t coins;
    uint8_t cards[COUP_CARDS_PER_PLAYER]; /* Character IDs or FACEDOWN/NONE */
    bool alive;
    uint8_t difficulty; /* 0=Easy, 1=Medium, 2=Hard (bots only) */
    bool ready;      /* Lobby ready state */
    bool is_self;
    bool is_bot;     /* true for AI-controlled players */
} coup_player_t;

/*============================================================================
 * Card Color Definitions (RGBA for draw_rect)
 *============================================================================*/

#define COUP_COLOR_DUKE        0xA050D0FF   /* Purple */
#define COUP_COLOR_ASSASSIN    0xC03030FF   /* Dark Red */
#define COUP_COLOR_CAPTAIN     0x3060C0FF   /* Blue */
#define COUP_COLOR_AMBASSADOR  0x30A040FF   /* Green */
#define COUP_COLOR_CONTESSA    0xD09020FF   /* Gold */
#define COUP_COLOR_FACEDOWN    0x203060FF   /* Dark Blue */
#define COUP_COLOR_NONE        0x101010FF   /* Near Black */

/* Text colors (RGBA) */
#define COUP_TEXT_WHITE         0xFFFFFFFF
#define COUP_TEXT_YELLOW        0xF9E2AFFF
#define COUP_TEXT_BLUE          0x89B4FAFF
#define COUP_TEXT_RED           0xF38BA8FF
#define COUP_TEXT_GREEN         0xA6E3A1FF
#define COUP_TEXT_GRAY          0x6C7086FF
#define COUP_TEXT_PINK          0xF5C2E7FF
#define COUP_TEXT_ORANGE        0xFAB387FF
#define COUP_TEXT_GOLD          0xD4A830FF

/*============================================================================
 * Game State
 *============================================================================*/

typedef struct {
    /* Screen/phase */
    coup_screen_t screen;
    coup_phase_t phase;

    /* Players */
    int player_count;
    coup_player_t players[COUP_MAX_PLAYERS];
    uint8_t my_id;
    uint8_t server_user_id;    /* Server-assigned user_id (persistent across games) */
    uint8_t my_cards[COUP_CARDS_PER_PLAYER]; /* Our actual hand */

    /* Turn state */
    uint8_t current_turn_id;
    uint8_t declared_action;
    uint8_t declared_actor;
    uint8_t declared_target;
    uint8_t declared_claim;  /* Character being claimed */

    /* Block state */
    uint8_t blocker_id;
    uint8_t block_claim;     /* Character the blocker claims */
    uint8_t block_claim_chars[3]; /* Characters to choose from when blocking */
    int block_claim_count;        /* Number of block claim options */

    /* Exchange state */
    uint8_t exchange_cards[4]; /* Cards to choose from */
    int exchange_count;
    int exchange_sel[2];       /* Currently selected keep indices */
    int exchange_cursor;

    /* UI cursors */
    int menu_cursor;
    int menu_count;
    int target_cursor;
    int lose_cursor;           /* For choosing which card to lose */
    uint8_t valid_actions;     /* Bitmask of available actions */

    /* Game log */
    char log[COUP_LOG_LINES][COUP_LOG_LINE_LEN + 1];
    int log_count;
    int log_head;              /* Ring buffer head */
    int log_scroll;            /* Scroll offset from newest (0 = show latest) */

    /* Timers */
    int response_timer;        /* Frames remaining to respond */
    int response_timeout;      /* Initial timer value (for bar fraction) */
    int frame_count;
    int title_blink;
    int heartbeat_timer;
    int anim_timer;

    /* Network */
    bool connected;
    bool online_mode;
    bool is_spectator;

    /* Relay sequence tracking (online mode) */
    uint16_t relay_expected_seq;
    bool     relay_seq_valid;       /* false until first relay received */
    bool     resync_pending;        /* suppresses UI during replay */
    uint16_t resync_total;          /* relay count expected in full resync */
    uint16_t resync_received;       /* relays received so far */
    int      resolving_timer;       /* frames in RESOLVING; 0 = not timing */

    /* Name entry */
    char name_buf[COUP_MAX_NAME];
    int name_len;
    int name_cursor;
    int name_blink;

    /* Lobby */
    bool my_ready;
    int lobby_cursor;        /* Player slot cursor (0 = self, 1-6 = bot slots) */
    bool lobby_naming;       /* true = name entry overlay active */

    /* Game over */
    uint8_t winner_id;
    char winner_name[COUP_MAX_NAME];  /* Snapshot at game-over time */
    int round_number;

    /* Rules viewer */
    int rules_page;
    coup_screen_t rules_return_screen;  /* Screen to return to from rules */

    /* Auth */
    char my_uuid[40];
    bool has_uuid;
    int auth_timer;
    int auth_retries;

    /* Connection status detail */
    int connect_stage;    /* 0=probing, 1=init, 2=dialing, 3=connected */
    char connect_msg[40]; /* Current status message */

    /* Modem availability (detected at startup) */
    bool modem_available;

    /* Title menu cursor (0=Online, 1=Vs Bots, 2=Options) */
    int title_cursor;

    /* Settings screen */
    int settings_cursor;  /* 0=Music vol, 1=SFX vol, 2=Bot difficulty */
    int music_vol;        /* 0-10 user scale */
    int sfx_vol;          /* 0-10 user scale */
    int bot_difficulty;   /* 0=Easy, 1=Medium, 2=Hard */
    coup_screen_t settings_return_screen;  /* Screen to return to from settings */

    /* Local game (rule engine + bot AI run locally) */
    bool local_mode;
    int bot_think_timer;     /* Frames until bot acts */
    int bot_count;           /* Number of bots (1-6), default 3 */
} coup_state_t;

/*============================================================================
 * Action Metadata Tables
 *============================================================================*/

static const char* const __attribute__((unused)) coup_char_names[COUP_NUM_CHARACTERS] = {
    "Duke", "Assassin", "Captain", "Ambassador", "Contessa"
};

static const char* const __attribute__((unused)) coup_char_short[COUP_NUM_CHARACTERS + 2] = {
    "Du", "As", "Ca", "Am", "Co", "??", "  "
};

static const char* const __attribute__((unused)) coup_action_names[COUP_NUM_ACTIONS] = {
    "Income", "Foreign Aid", "Coup",
    "Tax", "Assassinate", "Steal", "Exchange"
};

/* Which character each action claims (-1 = no claim)
 * NOTE: Mirrors coup_rules_action_claim[] in coup_rules.h. Keep both in sync. */
static const int __attribute__((unused)) coup_action_claim[COUP_NUM_ACTIONS] = {
    -1, -1, -1,
    COUP_CHAR_DUKE,       /* Tax */
    COUP_CHAR_ASSASSIN,   /* Assassinate */
    COUP_CHAR_CAPTAIN,    /* Steal */
    COUP_CHAR_AMBASSADOR  /* Exchange */
};

/* Does this action need a target player?
 * NOTE: Mirrors coup_rules_action_needs_target[] in coup_rules.h. Keep both in sync. */
static const bool __attribute__((unused)) coup_action_needs_target[COUP_NUM_ACTIONS] = {
    false, false, true,  /* Income, Foreign Aid, Coup */
    false, true, true, false  /* Tax, Assassinate, Steal, Exchange */
};

/* Menu display order: basic actions first, then character actions, costly last */
static const int __attribute__((unused)) coup_action_display_order[COUP_NUM_ACTIONS] = {
    COUP_ACT_INCOME, COUP_ACT_FOREIGN_AID, COUP_ACT_TAX,
    COUP_ACT_STEAL, COUP_ACT_EXCHANGE, COUP_ACT_ASSASSINATE, COUP_ACT_COUP
};

/*============================================================================
 * Game Logic API (coup_game.c)
 *============================================================================*/

/**
 * Initialize game state. Call after CUI PAL is set up.
 */
void coup_init(void);

/**
 * Process one frame of input.
 */
void coup_update(cui_input_action_t action);

/**
 * Per-frame updates (network polling, timers, animations).
 */
void coup_tick(void);

/**
 * Get current game state (for rendering).
 */
const coup_state_t* coup_get_state(void);

/**
 * Get screen state (for platform decisions).
 */
coup_screen_t coup_get_screen(void);

/**
 * Log a message to the game log.
 */
void coup_log(const char* text);

/**
 * Render the game (calls into coup_render.c).
 */
void coup_render(void);

/**
 * Render immediately (for blocking operations).
 */
void coup_render_now(void);

/*============================================================================
 * Network API (called by platform entry point)
 *============================================================================*/

struct cui_transport;

void coup_set_transport(const struct cui_transport* t);
void coup_on_connected(void);
void coup_on_disconnected(void);
void coup_send_disconnect(void);
void coup_enter_offline(void);
void coup_set_connect_stage(int stage, const char* msg);
void coup_set_modem_available(bool available);

/* Platform callback: attempt online connection (implemented in main_saturn.c) */
void coup_platform_try_connect(void);

/* Start a local game from lobby state (player + bots) */
void coup_start_local_game(void);

/* Unified game start: initializes engine from lobby players[].is_bot state */
void coup_start_game(uint32_t seed, uint8_t my_pid);

/*============================================================================
 * Rendering API (coup_render.c)
 *============================================================================*/

void coup_render_screen(const coup_state_t* st);

/*============================================================================
 * Audio API (coup_audio.c)
 *============================================================================*/

void coup_audio_init(void);
void coup_audio_tick(void);
void coup_audio_play_sfx(int sfx_id);
void coup_audio_start_music(void);
void coup_audio_stop_music(void);
void coup_audio_set_music_volume(int vol);
void coup_audio_set_sfx_volume(int vol);

/** Hidden audio debug menu (Saturn only, no-ops elsewhere).
 *  Call debug_update() with raw pad data BEFORE coup_update().
 *  Call debug_render() AFTER coup_render().
 *  Toggle overlay: hold L+R+Z. */
void coup_audio_debug_update(uint16_t pad_raw);
void coup_audio_debug_render(void);

/*============================================================================
 * Utility
 *============================================================================*/

static inline void coup_strcpy(char* dst, const char* src, int max)
{
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static inline int coup_strlen(const char* s)
{
    int i = 0;
    while (s[i]) i++;
    return i;
}

static inline uint32_t coup_card_color(int character)
{
    switch (character) {
    case COUP_CHAR_DUKE:       return COUP_COLOR_DUKE;
    case COUP_CHAR_ASSASSIN:   return COUP_COLOR_ASSASSIN;
    case COUP_CHAR_CAPTAIN:    return COUP_COLOR_CAPTAIN;
    case COUP_CHAR_AMBASSADOR: return COUP_COLOR_AMBASSADOR;
    case COUP_CHAR_CONTESSA:   return COUP_COLOR_CONTESSA;
    case COUP_CHAR_FACEDOWN:   return COUP_COLOR_FACEDOWN;
    default:                   return COUP_COLOR_NONE;
    }
}

static inline uint32_t coup_char_text_color(int character)
{
    switch (character) {
    case COUP_CHAR_DUKE:       return COUP_TEXT_ORANGE;
    case COUP_CHAR_ASSASSIN:   return COUP_TEXT_RED;
    case COUP_CHAR_CAPTAIN:    return COUP_TEXT_BLUE;
    case COUP_CHAR_AMBASSADOR: return COUP_TEXT_GREEN;
    case COUP_CHAR_CONTESSA:   return COUP_TEXT_YELLOW;
    default:                   return COUP_TEXT_GRAY;
    }
}

#endif /* COUP_H */
