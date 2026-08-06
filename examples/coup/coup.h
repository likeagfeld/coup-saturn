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

/* Longest name a player may ENTER, excluding the terminator.
 *
 * COUP_MAX_NAME (16) is the protocol's field width and does NOT change - the
 * server and the web client keep sending up to 15 characters, so nothing on
 * the wire moves.
 *
 * This is a UI limit on what this client lets you type, and it exists because
 * of a measurement: the game screen's seat column is GAME_SEAT_W = 68 px, and
 * the body face draws 8 px per character, so 8 characters is what physically
 * fits. A 15-character name is 120 px - 56 px past its own box, over the
 * table art.
 *
 * A cap at ENTRY is not the same thing as truncating at draw time, which was
 * ruled out. The player sees the limit while typing and chooses a name that
 * fits; nothing they picked is silently cut afterwards.
 *
 * LIMITATION, stated plainly: this only bounds names typed on THIS client. A
 * web player can still join with 15 characters, and their seat label will
 * still overrun. Fixing that for everyone means either the server enforcing
 * it or the seat column growing, and both are larger decisions than this. */
#define COUP_NAME_ENTRY_MAX     8
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
 *
 * Generated, not hand-written: the ids are indices into the PCM tables in
 * coup_sfx_data.h, so the two have to be produced together or a call site
 * plays whatever waveform happens to sit at that index.  Both come out of
 * examples/coup/assets/convert_sfx.py.
 *
 * This header carries ids and byte totals only - no sample data - because
 * coup.h is included by every translation unit and `static const` arrays in
 * a header are duplicated once per unit.
 *============================================================================*/

#include "coup_sfx_ids.h"

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

    /* SEAT INDEX, valid only for the lifetime of the game that produced it.
     * coup_start_game() normalises players[i].id to i, so during a game seat
     * index and id agree - but a LOBBY_STATE message arriving while the
     * game-over screen is still up rewrites players[].id back to WIRE ids,
     * and my_id is not restored until the player confirms. Anything derived
     * from this after that point is wrong. Use i_won, not this. */
    uint8_t winner_id;

    /* Did the local player win? Snapshotted when GAME_OVER fires, for the
     * same reason winner_name is: it must survive a LOBBY_STATE overwrite.
     *
     * The banner used to recompute this live as
     *     find_self(st)->id == st->winner_id
     * and find_self() scans for is_self, which is precisely what that
     * overwrite corrupts - so a winning player was shown DEFEAT. The name
     * beside the banner stayed correct throughout, because it was already
     * snapshotted, which is why the two disagreed on screen. */
    bool i_won;

    char winner_name[COUP_MAX_NAME];  /* Snapshot at game-over time */

    /* The winner's character, for the game-over portrait (design doc section
     * 4.2 "Game over": "winner portrait scaled up with a gouraud
     * spotlight"). Snapshotted at the SAME COUP_EVT_GAME_OVER moment as
     * winner_id/i_won/winner_name and for the identical reason: players[]
     * is a seat-indexed array that a LOBBY_STATE arriving on this screen
     * rewrites to wire ids, so anything derived from it later is wrong.
     * A COUP_CHAR_* value, or COUP_CHAR_NONE if the winner's hand somehow
     * held no identifiable character (should not happen for a live winner,
     * but coup_pick_winner_char() returns it rather than an invalid index
     * if it does). */
    uint8_t winner_char;

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

/** Snapshot of the fields the effect trigger diffs against. */
typedef struct {
    int action;
    int phase;
    int blocker_id;
} coup_fx_prev_t;

/**
 * Left edge that centres `text_w` pixels inside `container_w` pixels.
 *
 * Pure arithmetic, at global scope so it can be unit tested on the host.
 * Most screens are behind online play and cannot be reached in an offline
 * capture, so the centring RULE is proven here rather than screen by screen.
 * Never returns a negative x - a label wider than its container is clamped to
 * the left edge, because a negative x wraps on VDP1 rather than clipping.
 */
int  coup_centre_x(int container_w, int text_w);

/* Frames each effect frame is held for. Declared here so the pacing can be
 * asserted on the host - MEASURED at 3 the shortest effect ran 0.30 s, which
 * reads as a flicker rather than an event. At 14 the 6-frame effects ran
 * 1.40 s and were still reported too fast; 18 puts them at 1.80-2.40 s. */
#define COUP_FX_HOLD_FRAMES 18

/* Frames each PORTRAIT idle frame is held for. This was a bare `/ 8` inline
 * in coup_render.c and so was never revisited when the effect pacing was
 * slowed from 3 to 14 - it ran a full 8-frame idle cycle in 8*8/60 = 1.07 s,
 * which is the last animation on screen still moving at its original rate.
 * At 20 the cycle is 2.67 s, a breathing idle rather than a fidget. Named
 * here so tests/coup/test_pacing.c can assert it. */
#define COUP_ANIM_HOLD_FRAMES 20

/* NTSC field rate. Every pacing figure above is quoted in seconds at this
 * rate, so the tests convert rather than restating magic numbers. */
#define COUP_FIELD_HZ 60

/* Rows of match recap shown on the game-over screen. Shared so the input
 * handler clamps scrolling to exactly what the renderer draws. */
#define COUP_GAMEOVER_RECAP_ROWS 5

/* Frames the flash-white ramp takes to fade back to normal after a challenge
 * resolves (design doc section 4.2 "All transitions": "Flash-white for
 * challenge results (+k ramp)"). Short and sharp - a flash, not a fade - so
 * it reads as punctuation on the reveal that follows rather than a wash that
 * competes with it. Matches the scale of the existing per-screen fade-in
 * (COUP_GAMEOVER_DISSOLVE_FRAMES / the 12-frame black ramp in
 * coup_render_screen()) rather than inventing a new pace. */
#define COUP_CHALLENGE_FLASH_FRAMES 12

/* Frames the game-over entrance dissolve runs before the winner portrait
 * settles into its normal, solid draw (design doc section 4.2 "Game over":
 * "mesh + colour-offset dissolve into it"). Matches the whole-screen
 * colour-offset fade-in's own length (coup_render_screen(),
 * saturn_fade_start(..., 12, false)) so the two halves of the composed
 * effect complete together. */
#define COUP_GAMEOVER_DISSOLVE_FRAMES 12

/*============================================================================
 * Card-reveal state machine
 *
 * Reveals in this client are instantaneous: a card's character changes in the
 * state and the next frame draws the new face. That left nowhere to ask "this
 * card is at frame 7 of 12 of its reveal", which is why the distorted-sprite
 * flip in pal/saturn/saturn_distort.c sat built, tested and linked with no
 * caller. This machine supplies that missing time axis.
 *
 * It is driven purely by OBSERVING coup_state_t transitions, exactly as
 * coup_fx_on_transition() already is, so no wire message, message format or
 * rule-engine behaviour changes (spec D9 - the server stays turnkey).
 *
 * The two sequences come from the design doc
 * (docs/superpowers/specs/2026-08-04-saturn-visual-facelift-design.md
 * section 4.2 "Game"):
 *   reveal          - "distorted-sprite Y-axis flip (trapezoid collapse to
 *                      the centre line over 12 frames, texture swap at the
 *                      midpoint, then expand)" from the card back to the
 *                      revealed face
 *   influence loss  - "card flip to back + mesh-dissolve out": the same flip
 *                      run face -> back, then the back drawn with CMDPMOD's
 *                      Mesh Enable bit (ST-013-R3 section 6.3)
 *
 * Pure - no hardware access, unit tested in tests/coup/test_coup_reveal.c.
 *============================================================================*/

/* Frames per phase. 12 is the design doc's figure for the flip; the dissolve
 * matches it so a loss reads as one two-beat gesture (0.40 s at 60 Hz). */
#define COUP_REVEAL_FLIP_FRAMES     12
#define COUP_REVEAL_DISSOLVE_FRAMES 12

/* One slot per card of per seat. players[] is a fixed array, so this is a
 * fixed 14 - static allocation, no malloc. */
#define COUP_REVEAL_SLOTS (COUP_MAX_PLAYERS * COUP_CARDS_PER_PLAYER)

/* Authored size of every card asset - the back and all five faces are 48x72
 * (examples/coup/saturn/coup_fx_data.h). saturn_distort_encode_flip() feeds
 * the quad's w/h straight into CMDSIZE as the SOURCE character size, so a
 * flip is necessarily drawn at the art's own size; the module takes no
 * separate destination size. Named here so the renderer and the tests that
 * gate it cannot disagree about it. */
#define COUP_CARD_ART_W 48
#define COUP_CARD_ART_H 72

/* What a slot is doing right now, as reported by coup_reveal_stage(). */
#define COUP_REVEAL_IDLE     0
#define COUP_REVEAL_FLIP     1   /* distorted-sprite Y-axis flip  */
#define COUP_REVEAL_DISSOLVE 2   /* mesh-dissolve of the card back */

typedef struct {
    uint8_t active;   /* 0 = idle */
    uint8_t step;     /* frames since the sequence started */
    uint8_t card;     /* character on the face, or COUP_CHAR_NONE */
    uint8_t is_loss;  /* 1 = flip then dissolve, 0 = flip only */
} coup_reveal_slot_t;

typedef struct {
    coup_reveal_slot_t slots[COUP_REVEAL_SLOTS];
    uint8_t prev[COUP_REVEAL_SLOTS];   /* last observed card per slot */
    uint8_t seeded;                    /* prev[] holds a real observation */
} coup_reveal_t;

/** Clear every slot and forget the previous observation. */
void coup_reveal_init(coup_reveal_t* rv);

/** Slot owned by card `card_index` of seat `player_index`, or -1. */
int  coup_reveal_slot_index(int player_index, int card_index);

/**
 * Diff the state against the previous observation and start any sequence the
 * change calls for. Call once per frame, on every screen: leaving the game
 * screen re-seeds, which is what stops the next deal from looking like six
 * simultaneous reveals.
 */
void coup_reveal_observe(coup_reveal_t* rv, const coup_state_t* st);

/** Advance every running sequence by one frame. Call once per frame. */
void coup_reveal_tick(coup_reveal_t* rv);

/**
 * What slot `slot` is doing, with the step and length of the CURRENT phase.
 *
 * `out_card` receives the character on the face - the card being revealed,
 * or for a loss the card being lost - or COUP_CHAR_NONE when it was never
 * seen (an opponent coup'd out of a card that stayed face down).
 * Any out_* pointer may be NULL.
 *
 * @return COUP_REVEAL_IDLE / _FLIP / _DISSOLVE
 */
int  coup_reveal_stage(const coup_reveal_t* rv, int slot,
                       int* out_step, int* out_frames, int* out_card);

/**
 * True if this slot is running the influence-loss sequence rather than a
 * plain reveal.
 *
 * The renderer needs it because the two flips run in OPPOSITE directions: a
 * reveal turns the card back over to show the face, a loss turns the face
 * over to show the back, which is then what the mesh-dissolve dissolves.
 */
bool coup_reveal_is_loss(const coup_reveal_t* rv, int slot);

/** How many slots are currently animating. */
int  coup_reveal_active_count(const coup_reveal_t* rv);

/**
 * Ring slot holding display row `row` of a log window, or -1 if that row is
 * empty.
 *
 * st->log is a head/count ring (coup_game.c:394-417): while log_count is
 * below COUP_LOG_LINES the head stays at 0 and entry n sits in slot n, but
 * once it is full each append overwrites slot `head` and advances it, so the
 * OLDEST entry is at `head` and chronological entry n is at
 * (head + n) % COUP_LOG_LINES.
 *
 * Three screens draw a window onto that ring - the game log, the connecting
 * screen's progress list and the game-over recap - and each had its own copy
 * of the window arithmetic. One of them got it wrong. This is the single
 * copy they now share.
 *
 * Row 0 is the OLDEST row of the visible window and row (shown-1) the
 * newest, so drawing rows top to bottom prints the match in the order it
 * happened. `scroll` counts entries back from the newest and is clamped
 * internally, so a stale value can never index outside the buffer.
 *
 * Pure arithmetic - unit tested in tests/coup/test_gameover_recap.c.
 */
int  coup_log_ring_index(int head, int count, int max_rows,
                         int scroll, int row);

/**
 * Copy display row `row` of `src` word-wrapped at `max_chars` into `out`.
 * Returns the number of characters written; 0 means that row is past the end.
 *
 * Breaks on SPACES only. A label is never split inside a word, because a word
 * cut in half is a truncation whatever the reason for it - and the whole
 * point of wrapping here is to stop cutting labels. A single token longer
 * than `max_chars` is therefore emitted whole, on a row of its own, and is
 * allowed to be wider than the wrap width; no caller in this game can produce
 * one (the widest single token any of them builds is a COUP_MAX_NAME-1 name
 * plus punctuation, 16 characters, against a wrap width of 21).
 *
 * Runs of spaces at a break are consumed, so a row never begins with a space
 * and the left margin stays straight.
 *
 * Pure - no state, no allocation, unit tested in tests/coup/test_text_wrap.c.
 */
int  coup_wrap_row(const char* src, int max_chars, int row,
                   char* out, int out_sz);

/* Effect trigger - pure logic, unit tested on the host. */
int  coup_fx_for_action(int action);
int  coup_fx_on_transition(const coup_fx_prev_t* prev, const coup_state_t* st);
void coup_fx_remember(coup_fx_prev_t* prev, const coup_state_t* st);

/**
 * Detect a challenge window closing - decided, one way or the other.
 *
 * Design doc section 4.2 "All transitions": "Flash-white for challenge
 * results (+k ramp)." Reuses the SAME coup_fx_prev_t snapshot
 * coup_fx_on_transition() already keeps (it already carries `phase`), so
 * this adds no new observed state - it is driven purely by OBSERVING
 * coup_state_t transitions, exactly as coup_fx_on_transition() and
 * coup_reveal_observe() already are (spec D9 - the server stays turnkey).
 *
 * Fires once, on the frame `phase` leaves COUP_PHASE_CHALLENGE_WAIT or
 * COUP_PHASE_BLOCK_CHALLENGE for anything else. A resolved challenge reads
 * the same flash whether it was won or lost - the doc asks for a flash on
 * the RESULT, not a colour keyed to which side won it - so no outcome bit
 * is needed to decide whether to fire.
 *
 * Pure - no hardware access, unit tested on the host.
 *
 * @param prev  previously observed state (same struct coup_fx_on_transition
 *              diffs against; call coup_fx_remember() once per frame as
 *              usual - this function does not mutate it)
 * @param st    current state
 * @return true the one frame the window closes, false otherwise
 */
bool coup_challenge_resolved(const coup_fx_prev_t* prev, const coup_state_t* st);

/**
 * Pick the character to show as the game-over portrait from a hand.
 *
 * Returns the first slot holding a real character (< COUP_NUM_CHARACTERS,
 * i.e. not COUP_CHAR_FACEDOWN or COUP_CHAR_NONE), or COUP_CHAR_NONE if the
 * hand holds none. The caller passes st->my_cards for the local winner (own
 * cards are never FACEDOWN there) or st->players[winner].cards for anyone
 * else, matching reveal_visible_card()'s rule in coup_render.c (self-owned
 * cards stay FACEDOWN in players[] even for the local player).
 *
 * Pure - unit tested on the host.
 */
int coup_pick_winner_char(const uint8_t cards[COUP_CARDS_PER_PLAYER]);

/*============================================================================
 * Game-over entrance dissolve
 *
 * "mesh + colour-offset dissolve into it" (design doc section 4.2
 * "Game over"). The colour-offset half is the SAME whole-screen fade every
 * screen transition already gets (coup_render_screen(),
 * saturn_fade_start()); this supplies the missing mesh half, timing the
 * winner portrait's entrance so pal/saturn/saturn_distort.c's
 * saturn_distort_draw_mesh_dissolve() and saturn_fade.c's colour-offset
 * ramp run over the SAME window rather than introducing a third fade
 * mechanism.
 *
 * Driven purely by OBSERVING coup_state_t transitions, exactly as
 * coup_reveal_observe() already is: entering GAME_OVER is a `screen`
 * transition read the identical way coup_render_screen()'s own
 * `s_last_screen` diff already reads it, just made host-testable and
 * given a frame counter instead of a one-shot flag.
 *
 * Pure - no hardware access, unit tested in tests/coup/test_gameover_fx.c.
 *============================================================================*/

typedef struct {
    int  prev_screen;   /* last observed st->screen */
    int  step;          /* frames since GAME_OVER was entered, capped at
                          * COUP_GAMEOVER_DISSOLVE_FRAMES */
    bool seeded;         /* prev_screen holds a real observation */
} coup_gameover_fx_t;

/** Clear the observer so the next coup_gameover_fx_observe() only seeds. */
void coup_gameover_fx_init(coup_gameover_fx_t* gd);

/**
 * Diff `st->screen` against the previous observation and arm the entrance
 * dissolve if this is the frame GAME_OVER was just entered. Call once per
 * frame, on every screen - re-seeding off other screens is what stops the
 * NEXT match's game-over from replaying a stale dissolve.
 */
void coup_gameover_fx_observe(coup_gameover_fx_t* gd, const coup_state_t* st);

/** Advance the dissolve by one frame, if it is running. Call once per frame. */
void coup_gameover_fx_tick(coup_gameover_fx_t* gd);

/** True while the entrance dissolve is still running. */
bool coup_gameover_fx_dissolving(const coup_gameover_fx_t* gd);

/** Frames elapsed since GAME_OVER was entered, capped at the dissolve length. */
int coup_gameover_fx_step(const coup_gameover_fx_t* gd);

void coup_render_screen(const coup_state_t* st);

/*============================================================================
 * Title-screen card carousel
 *
 * "six card faces orbiting a vertical axis, continuously: cards rotate front
 * to back in a circle, and the one at the front enlarges as if approaching"
 * (2026-08-06 facelift task). Replaces the title screen's portrait parade
 * (coup_render_title() used to scroll the 5 animated character portraits
 * right-to-left in the same band) - both showcase the deck in the same
 * ~96px strip above the menu, and the parade's medallion frame
 * (portrait_medallion(): a brass border rect + a lit background rect drawn
 * SEPARATELY from the portrait sprite) is redundant here because the six
 * card assets already carry their own printed border (examples/coup/assets/
 * convert_effects.py, all_opaque(): "A card face is a complete picture with
 * its own border - there is no background to remove"). The carousel is
 * therefore BOTH the requested effect and a net reduction in VDP1 commands:
 * the parade drew 3 commands/character (2 medallion rects + 1 portrait
 * sprite) x 5 = 15; the carousel draws exactly 1 distorted-sprite command
 * per card x 6 = 6.
 *
 * All position/scale/depth maths below is a PURE function of an integer
 * frame counter (no libm - reuses coup_shading_sin(), the same fixed-point
 * quarter-wave table coup_shading.c already uses for the sheen/halo/pulse
 * gradients), so it is host-testable exactly like saturn_distort_flip_quad()
 * and saturn_coinfx_point() are (tests/coup/test_coup_carousel.c). Only
 * coup_carousel_draw() below touches VDP1 VRAM.
 *
 * Depth model: cards trace a horizontal ellipse (x = lateral, "into/out of
 * the screen" = depth), both taken from the SAME sine table 90 degrees
 * (COUP_SHADING_PERIOD/4) apart, so depth is exactly the lateral value's
 * "cosine". A card facing the camera dead-on (depth = +-1024) is at its
 * WIDEST; a card seen edge-on (depth = 0, at the left/right extremes of the
 * ellipse) is at its NARROWEST - the width term below is scaled by
 * |depth|/1024 for exactly this reason. Height is scaled by SIGNED depth
 * (near = big, far = small), which is what makes the FRONT card (depth =
 * +1024) strictly the largest of the six: MEASURED (see the sizing constants
 * below) the runner-up's area never exceeds ~10% of the front card's.
 *============================================================================*/

#define COUP_CAROUSEL_COUNT 6

/* Card height range, px. H_MIN is a hardware floor as much as an artistic
 * one - ST-013-R3 p.74 (VDP1_Manual.txt:3143-3144): "A negative value cannot
 * be specified for the display width. Drawing cannot be guaranteed when a
 * negative value is specified" - so this and W_FLOOR below must both be > 0
 * for EVERY output, not just the common case. */
#define COUP_CAROUSEL_H_MIN 24
#define COUP_CAROUSEL_H_MAX 96

/* Width floor, px. Reached when a card is edge-on (depth near 0) and the
 * |depth|/1024 foreshortening term would otherwise collapse it toward zero -
 * same ST-013-R3 citation as H_MIN above. */
#define COUP_CAROUSEL_W_FLOOR 6

/* Frames per phase step. One revolution is COUP_SHADING_PERIOD phase steps,
 * so the period is 120 * 12 = 1440 frames = 24 s at 60 Hz, matching the web
 * ring's --tc-spin. Undivided it was 2 s - twelve times faster than the web,
 * and too quick to read a card as it passes. */
#define COUP_CAROUSEL_SLOWDOWN 12

typedef struct {
    int cx, cy;      /* card centre, screen px */
    int w, h;        /* display size, px - always > 0 */
    int depth;       /* -1024..1024; higher = nearer the camera */
    int card_id;     /* fixed per carousel SLOT (0..COUP_CAROUSEL_COUNT-1) -
                       * identifies which of the six card textures this slot
                       * shows; see coup_render.c's coup_carousel_card_ui()
                       * for the card_id -> COUP_UI_* mapping */
} coup_carousel_card_t;

/**
 * Compute all six cards' position/size/depth for animation frame `frame`.
 *
 * Periodic: coup_carousel_layout(frame, ...) == coup_carousel_layout(frame +
 * K*COUP_SHADING_PERIOD, ...) for any integer K (positive, negative, or
 * zero) - a free-running frame counter can run forever without drift,
 * because every phase is reduced modulo COUP_SHADING_PERIOD before it
 * reaches coup_shading_sin(). Pure - no hardware access.
 *
 * @param frame     any int (free-running counter; wraps internally)
 * @param center_x  ellipse centre, screen px
 * @param center_y  card vertical centre, screen px (constant for every card
 *                  - the axis of rotation is vertical, so height never
 *                  changes a card's ROW, only its size)
 * @param radius_x  ellipse horizontal radius, screen px
 * @param out       6 cards, out[i].card_id == i always (slot identity is
 *                  fixed; only its position in the circle moves with frame)
 */
void coup_carousel_layout(int frame, int center_x, int center_y, int radius_x,
                           coup_carousel_card_t out[COUP_CAROUSEL_COUNT]);

/**
 * Back-to-front draw order for a computed layout.
 *
 * VDP1 has no depth test - command order IS z-order (ST-013-R3 section 6.1 /
 * the general Distorted/Normal/Scaled Sprite draw model: later commands
 * paint over earlier ones with no comparison against anything already in the
 * framebuffer). `out_order` lists card INDICES (into the same `cards` array
 * passed in) from back (draw first) to front (draw last, so it ends up on
 * top) - draw cards[out_order[0]] first, cards[out_order[
 * COUP_CAROUSEL_COUNT-1]] last.
 *
 * Two cards symmetric about the front/back axis land on EXACTLY the same
 * depth (coup_shading_sin() is a mirror-symmetric lookup table), so depth
 * alone is not a strict order. The card's fixed card_id (0..5, unique)
 * breaks every tie, so the composite (depth, card_id) key - and therefore
 * this function's output order - is always a genuine total order: no two
 * cards ever produce the same sort key, for any layout.
 *
 * Pure - no hardware access.
 *
 * @param cards      a layout, e.g. from coup_carousel_layout()
 * @param out_order  [out] 6 indices into `cards`, a permutation of 0..5
 */
void coup_carousel_sort(const coup_carousel_card_t cards[COUP_CAROUSEL_COUNT],
                         int out_order[COUP_CAROUSEL_COUNT]);

#ifdef __SATURN__
/**
 * Compute and draw one frame of the carousel.
 *
 * Sorts back-to-front and issues one VDP1 Distorted Sprite command per card
 * (saturn_vdp1_draw_sprite_scaled() - SOURCE size first, then DESTINATION;
 * see that function's own doc comment for the shipped bug this parameter
 * order caused elsewhere in this file). No-ops before coup_fx_load().
 *
 * @param frame     free-running frame counter (e.g. st->frame_count)
 * @param cx,cy     ellipse centre / card row, screen px
 * @param radius_x  ellipse horizontal radius, screen px
 */
void coup_carousel_draw(int frame, int cx, int cy, int radius_x);
#endif

#ifdef __SATURN__
/** Upload the VDP1 gouraud gradient tables. Call once after the PAL is up. */
void coup_render_init_shading(void);
#endif

/*============================================================================
 * Audio API (coup_audio.c)
 *============================================================================*/

void coup_audio_init(void);
void coup_audio_tick(void);

/** Play an effect at the pitch it was sampled at. */
void coup_audio_play_sfx(int sfx_id);

/**
 * Play an effect retuned to a character's voice.
 *
 * The design calls for eight per-character cues across five characters -
 * forty sounds, and at the shipped encoding forty sounds do not fit in the
 * 81,920 bytes Sound RAM leaves for SFX.  They do not need to: the SCSP
 * retunes a slot in hardware through its OCT/FNS pitch register, so one
 * sample serves all five characters at five pitches, and the player learns
 * WHO is acting without reading the screen.  Duke is lowest through Contessa
 * highest, matching the COUP_CHAR_* order.
 *
 * `character` is a COUP_CHAR_* value.  Anything outside 0..COUP_NUM_CHARACTERS-1
 * (COUP_CHAR_FACEDOWN, COUP_CHAR_NONE, a bot with no claimed card) plays at
 * the sampled pitch, so callers never have to special-case it.
 *
 * The encoding lives in coup_sfx_pitch.h and is tested on the host.
 */
void coup_audio_play_sfx_as(int sfx_id, int character);

void coup_audio_start_music(void);

/**
 * Re-issue CD-DA playback after a disc read has taken the pickup.
 *
 * There is one pickup and it is exclusive: streaming a backdrop replaces the
 * play range and the repeat mode, stopping the music, and nothing in the CD
 * library restores it. Safe to call unconditionally - a no-op unless music
 * was supposed to be playing.
 */
void coup_audio_restore_music(void);

/** Counters proving the CD-DA restore path runs. Peeked over READ_CORE_RAM. */
typedef struct {
    uint32_t magic;
    int32_t  restore_calls;         /* entered after a streamed scene load  */
    int32_t  reissued;              /* CDC_CdPlay actually re-issued        */
    int32_t  skipped_not_ready;     /* audio subsystem was not up           */
    int32_t  skipped_not_playing;   /* this screen wants no music           */
} coup_audio_witness_t;

#define COUP_AUDIO_WITNESS_MAGIC 0x41554457u   /* 'AUDW' */

extern coup_audio_witness_t g_coup_audio_witness;
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
