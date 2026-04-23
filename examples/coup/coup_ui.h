/**
 * coup_ui.h - Centralized UI Layout Configuration
 *
 * All panel positions, text coordinates, list spacings, and color constants
 * for every screen in the Coup card game. Edit this file to tweak the layout
 * without touching rendering logic in coup_render.c.
 *
 * Screen: COUP_SCREEN_W x COUP_SCREEN_H pixels, 8x8 character grid.
 */

#ifndef COUP_UI_H
#define COUP_UI_H

#include "cui_types.h"
#include <stdint.h>

/*============================================================================
 * Screen Dimensions
 *============================================================================*/
#define COUP_SCREEN_W        320
#define COUP_SCREEN_H        224
#define COUP_FULLSCREEN_BG   {0, 0, COUP_SCREEN_W, COUP_SCREEN_H}

/*============================================================================
 * Font Configuration
 *============================================================================*/
#define COUP_FONT_ADVANCE   8    /* Horizontal pixels per character */
#define COUP_FONT_ROW_H     8    /* Vertical pixels per text row */

/*============================================================================
 * Color Palette - Dark Panel Aesthetic
 *============================================================================*/

/* Panel / background colors */
#define COUP_BG_DARK         0x08081AFF   /* Near-black navy */
#define COUP_BG_GRID         0x404080FF   /* Grid/border gap color (between panels) */
#define COUP_PANEL_DARK      0x141428FF   /* Dark panel fill */
#define COUP_PANEL_MID       0x1E1E3CFF   /* Mid panel */
#define COUP_PANEL_LIGHT     0x282850FF   /* Light panel accent */
#define COUP_PANEL_HEADER    0x1A1A40FF   /* Header bar background */
#define COUP_PANEL_STATUS    0x101030FF   /* Status bar background */
#define COUP_PANEL_LOG       0x0C0C20FF   /* Game log background */
#define COUP_PANEL_ALERT     0x3C1420FF   /* Red alert background */
#define COUP_PANEL_PROMPT    0x142840FF   /* Blue prompt background */
#define COUP_PANEL_SELECT    0x283014FF   /* Green selection background */

/* Accent / border colors */
#define COUP_ACCENT_GOLD     0xD0A820FF   /* Gold border accent */
#define COUP_ACCENT_BLUE     0x4060D0FF   /* Blue divider accent */
#define COUP_ACCENT_RED      0xC04040FF   /* Red warning accent */
#define COUP_ACCENT_GREEN    0x40B060FF   /* Green ready accent */
#define COUP_ACCENT_PURPLE   0x8050C0FF   /* Purple highlight */
#define COUP_ACCENT_DIM      0x303050FF   /* Dim accent line */

/* Button prompt colors */
#define COUP_BTN_A_COLOR     0x60C080FF   /* A button - green */
#define COUP_BTN_B_COLOR     0xC06060FF   /* B button - red */

/* Inline colors extracted from render code */
#define COUP_PANEL_MY_TURN   0x142814FF   /* Dark green status bar (my turn) */
#define COUP_PANEL_REF_BG    0x0C0C24FF   /* Reference card overlay bg */

/*============================================================================
 * Game Screen Layout Anchors
 *
 * Adjust these to reposition major regions. Most game-screen coordinates
 * are derived from these anchors, so changing one value here updates all
 * dependent positions automatically.
 *============================================================================*/

/* Seat columns (left/right opponent panels) */
#define GAME_SEAT_W          68
#define GAME_SEAT_H          40
#define GAME_SEAT_GAP         3
#define GAME_LEFT_X           0
#define GAME_RIGHT_X         (COUP_SCREEN_W - GAME_SEAT_W)            /* 252 */

/* Center column (derived from seat width + 4px gutter) */
#define GAME_CENTER_X        (GAME_SEAT_W + 4)                        /* 72  */
#define GAME_CENTER_W        (COUP_SCREEN_W - 2 * GAME_CENTER_X)      /* 176 */

/* Vertical zones — adjust GAME_ROW_Y to move the seat/content row */
#define GAME_ROW_Y            56
#define GAME_HAND_Y          150
#define GAME_HAND_H           74

/* Phase panel insets within center column */
#define GAME_CONTENT_X       (GAME_CENTER_X + 4)                      /* 76  */
#define GAME_CONTENT_W       (GAME_CENTER_W - 8)                      /* 168 */
#define GAME_TEXT_X           (GAME_CENTER_X + 8)                      /* 80  */
#define GAME_CONTENT_Y       (GAME_ROW_Y + 2)                         /* 58  */
#define GAME_TITLE_MAX_CHARS 20  /* max chars fitting in content area: (GAME_CONTENT_X + GAME_CONTENT_W - GAME_TEXT_X) / 8 */

/* Derived panel heights (auto-adjust when anchors change) */
#define GAME_LOG_H           (GAME_ROW_Y - 2)                         /* 54  */
#define GAME_CENTER_H        (GAME_HAND_Y - GAME_ROW_Y - 2)           /* 92  */

/* Corner panels (flush to screen bottom) */
#define GAME_CORNER_H         20
#define GAME_CORNER_Y        (COUP_SCREEN_H - GAME_CORNER_H)          /* 204 */

/*--- Shared response-phase layout (challenge / block / block-challenge) ---*/
#define GAME_RESPONSE_LAYOUT {                                          \
    .alert_panel      = {GAME_CONTENT_X, GAME_CONTENT_Y,               \
                         GAME_CONTENT_W, 56},                           \
    .title_x          = GAME_TEXT_X,                                    \
    .title_y          = GAME_CONTENT_Y + 2,                            \
    .line1_x          = GAME_TEXT_X,                                    \
    .line1_y          = GAME_CONTENT_Y + 14,                           \
    .line2_x          = GAME_TEXT_X,                                    \
    .line2_y          = GAME_CONTENT_Y + 24,                           \
    .btn_left         = {GAME_TEXT_X, GAME_CONTENT_Y + 38, 76, 14},    \
    .btn_right        = {GAME_TEXT_X + 84, GAME_CONTENT_Y + 38,        \
                         76, 14},                                       \
    .btn_left_text_x  = GAME_TEXT_X + 8,                               \
    .btn_right_text_x = GAME_TEXT_X + 92,                              \
    .btn_timer_x      = GAME_TEXT_X + 136,                             \
    .btn_text_y       = GAME_CONTENT_Y + 40,                           \
}

/*============================================================================
 * Layout Helper Types
 *============================================================================*/

/** List layout for repeated items at a fixed x, varying y */
typedef struct {
    int x, base_y, spacing;
} coup_list_layout_t;

/*============================================================================
 * Per-Screen Layout Structs
 *============================================================================*/

/* --- Title Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t header_panel;
    /* Title logo sprite position */
    cui_point_t logo_pos;
    /* ASCII-art fallback title position (grid col, row) */
    int ascii_col;
    int ascii_start_row;
    /* Horizontal menu */
    int menu_y;
    int item_x[3];
    int item_w[3];
    /* Portrait scroll constants */
    int scroll_zone_x;
    int portrait_y;
    int portrait_slot;
    int scroll_total;
    /* Bottom bar hint positions (grid col, row) */
    int hint_row;
    int hint_l_col;
    int hint_r_col;
} coup_title_layout_t;

/* --- Settings Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t header_panel;
    int header_col;
    int header_row;
    /* Difficulty row */
    int diff_y;
    cui_rect_t diff_panel;
    int diff_label_x;
    int diff_cursor_x;
    int diff_option_x;
    int diff_option_spacing;
    int diff_option_w;
    int diff_option_h;
    int diff_text_offset_y;
    int diff_arrow_left_offset;
    int diff_arrow_right_offset;
    /* Hint bar (grid col, row) */
    int hint_row;
    int hint_left_col;
    int hint_right_col;
} coup_settings_layout_t;

/* --- Rules Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t header_bar;
    cui_rect_t content_panel;
    cui_rect_t nav_bar;
    /* Header accent */
    int header_hline_y;
    /* Content accent */
    int content_hline_y;
    /* Nav accent */
    int nav_hline_y;
    /* Header text (grid col, row) */
    int header_col;
    int header_row;
    /* Portrait position */
    cui_point_t portrait_pos;
    /* Background tile row y */
    int bg_tile_y;
    /* Navigation text */
    int page_str_col;
    int page_str_row;
    cui_point_t prev_pos;
    cui_point_t next_pos;
    cui_point_t back_pos;
} coup_rules_layout_t;

/* --- Connecting Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t main_panel;
    /* Title */
    cui_point_t title_pos;
    int title_hline_y;
    int title_hline_x;
    int title_hline_w;
    /* Stage/detail text */
    int text_x;
    int stage_y;
    int detail_y;
    /* Progress bar */
    cui_rect_t progress_bg;
    int progress_bar_x;
    int progress_bar_y;
    int progress_bar_max_w;
    int progress_bar_h;
    /* Log list */
    coup_list_layout_t log_list;
    int log_max_visible;
    /* Cancel button */
    cui_rect_t cancel_panel;
    cui_point_t cancel_text_pos;
    /* Retry text */
    cui_point_t retry_pos;
    /* Background tile row y */
    int bg_tile_y;
} coup_connecting_layout_t;

/* --- Name Entry Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t header_panel;
    cui_rect_t name_panel;
    cui_rect_t char_scroll_panel;
    cui_rect_t controls_panel;
    /* Header text (grid col, row) */
    int header_col;
    int header_row;
    /* Name display (grid col, row) */
    int name_col;
    int name_row;
    /* Character indicator (grid col, row) */
    int indicator_col;
    int indicator_row;
    /* Controls text (grid col, row) */
    int ctrl_col;
    int ctrl_start_row;
    int submit_row;
    /* Sprite position */
    cui_point_t sprite_pos;
    /* Background tile row y */
    int bg_tile_y;
} coup_name_entry_layout_t;

/* --- Lobby Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t header_bar;
    cui_rect_t player_area;
    /* Player slot list */
    int slot_x;
    int slot_base_y;
    int slot_spacing;
    int slot_w;
    int slot_h;
    int ready_bar_w;
    /* Player text */
    int text_x;
    int text_base_y;
    int text_spacing;
    int ready_text_x;
    /* Controls panel */
    cui_rect_t controls_panel;
    int ctrl_text_x;
    int ctrl_ready_y;
    int ctrl_leave_y;
    int ctrl_start_y;
    /* Status bar */
    cui_rect_t status_bar;
    int status_text_x;
    int status_text_y;
    int status_detail_y;
    /* Background tile row y */
    int bg_tile_y;
} coup_lobby_layout_t;

/* --- Game Screen --- */

/* Single opponent seat — computed at render time from coup_seats_layout_t */
typedef struct {
    cui_rect_t box;        /* seat panel bounds (inset from grid) */
    int name_x, name_y;
    int cards_x, cards_y;
    int coins_x, coins_y;
} coup_seat_layout_t;

/* Parametric layout for all 6 opponent seats (3 left, 3 right) */
typedef struct {
    int left_x;            /* Left column box x */
    int right_x;           /* Right column box x */
    int w;                 /* Seat box width */
    int h;                 /* Seat box height */
    int start_y;           /* First seat row y */
    int gap;               /* Vertical gap between seat rows */
    int text_inset;        /* Text x inset from box edge */
    int name_offset_y;     /* Name text y offset from box top */
    int cards_offset_y;    /* Cards text y offset from box top */
    int coins_offset_y;    /* Coins text y offset from box top (3rd row) */
    int left_coins_inset;  /* Left seats: coins x inset from box edge */
    int right_cards_inset; /* Right seats: cards x inset from box edge */
    int card_spacing;      /* Horizontal space between card abbreviations */
    int max_name_chars;    /* Max chars for name in seat */
} coup_seats_layout_t;

/* Phase: select action */
typedef struct {
    cui_rect_t title_bar;
    int title_text_x;
    int title_text_y;
    int action_start_y;
    int items_offset_y;
    int item_spacing;
    int item_x;
    int item_w;
    int item_h;
    int timer_bar_h;     /* Height of countdown bar (4px) */
} coup_phase_select_action_t;

/* Phase: select target */
typedef struct {
    cui_rect_t title_bar;
    int title_text_x;
    int title_text_y;
    int item_base_y;
    int item_spacing;
    int item_x;
    int item_w;
    int item_h;
    int item_text_offset_y;
} coup_phase_select_target_t;

/* Phase: challenge/block/block-challenge (shared layout) */
typedef struct {
    cui_rect_t alert_panel;
    int title_x;
    int title_y;
    int line1_x;
    int line1_y;
    int line2_x;
    int line2_y;
    /* Button row */
    cui_rect_t btn_left;
    cui_rect_t btn_right;
    int btn_left_text_x;
    int btn_right_text_x;
    int btn_timer_x;
    int btn_text_y;
} coup_phase_response_t;

/* Phase: idle/resolving */
typedef struct {
    cui_rect_t panel;
    int text_x;
    int text_y;
    int bar_x;
    int bar_y;
    int bar_max_w;
    int bar_h;
} coup_phase_idle_t;

/* Your Hand: center-bottom outlined panel */
typedef struct {
    cui_rect_t panel;       /* outlined border rect */
    int card0_x, card0_y;   /* left card animated portrait (32x48) */
    int card1_x, card1_y;   /* right card animated portrait (32x48) */
    int name_x, name_y;     /* player name text */
    int coins_x, coins_y;   /* coin text */
    int coin_sprite_x;      /* coin sprite icon position */
    int coin_sprite_y;
    int label0_x, label0_y; /* card 0 abbreviation */
    int label1_x, label1_y; /* card 1 abbreviation */
} coup_game_hand_layout_t;

/* Game log */
typedef struct {
    int text_x;
    int base_y;
    int spacing;
    int max_visible;
    int scroll_arrow_x;
} coup_game_log_layout_t;

/* Corner shortcuts (bottom-left / bottom-right) */
typedef struct {
    cui_rect_t left_panel;
    int left_text_x, left_text_y;
    cui_rect_t right_panel;
    int right_text_x, right_text_y;
} coup_game_corners_layout_t;

typedef struct {
    /* Parametric layout for all 6 opponent seats */
    coup_seats_layout_t seats;
    /* Player hand */
    coup_game_hand_layout_t hand;
    /* Split center panels */
    cui_rect_t log_panel;       /* top center for logs */
    cui_rect_t center_panel;    /* mid center for phase content */
    /* Center content */
    coup_game_log_layout_t log;
    coup_phase_select_action_t select_action;
    coup_phase_select_target_t select_target;
    coup_phase_response_t challenge_wait;
    coup_phase_response_t block_wait;
    coup_phase_response_t block_challenge;
    coup_phase_idle_t idle;
    /* Corner shortcuts */
    coup_game_corners_layout_t corners;
} coup_game_layout_t;

/* --- Game Over Screen --- */
typedef struct {
    cui_rect_t bg;
    /* ASCII game over text (grid col, row) — non-Saturn only */
    int gameover_col;
    int gameover_row;
    /* Winner text (grid row, centered horizontally) */
    int winner_row;
    /* "Return to..." text (grid col, row) — non-Saturn only */
    int return_col;
    int return_row;
} coup_gameover_layout_t;

/* --- Reference Card Screen --- */
typedef struct {
    cui_rect_t bg;
    cui_rect_t header_bar;
    int header_hline_y;
    /* Header text */
    int title_x;
    int title_y;
    int dismiss_x;
    int dismiss_y;
    /* Character rows */
    int row_base_y;
    int row_spacing;
    int color_band_w;
    int color_band_h;
    int panel_x;
    int panel_w;
    int panel_h;
    /* Text offsets within each row */
    int name_x;
    int name_offset_y;
    int line1_offset_y;
    int line2_offset_y;
    int line3_offset_y;
    /* Portrait position offset from panel right */
    int portrait_x;
    int portrait_offset_y;
    /* Footer */
    int footer_x;
    int footer_y;
} coup_reference_layout_t;

/*============================================================================
 * Top-Level UI Layout
 *============================================================================*/

typedef struct {
    coup_title_layout_t title;
    coup_settings_layout_t settings;
    coup_rules_layout_t rules;
    coup_connecting_layout_t connecting;
    coup_name_entry_layout_t name_entry;
    coup_lobby_layout_t lobby;
    coup_game_layout_t game;
    coup_gameover_layout_t gameover;
    coup_reference_layout_t reference;
} coup_ui_t;

/*============================================================================
 * Static Layout Instance
 *
 * Game-screen values use GAME_* anchors — change the anchor defines above
 * to reposition entire regions without editing individual coordinates.
 *============================================================================*/

static const coup_ui_t __attribute__((unused)) COUP_UI = {

    /* ---- Title Screen ---- */
    .title = {
        .bg             = COUP_FULLSCREEN_BG,
        .header_panel   = {8, 8, COUP_SCREEN_W - 16, 56},
        .logo_pos       = {32, 12},
        .ascii_col      = 4,
        .ascii_start_row = 2,
        .menu_y         = 172,
        .item_x         = {36, 136, 220},
        .item_w         = {60, 44, 72},
        .scroll_zone_x  = 0,
        .portrait_y     = 68,
        .portrait_slot  = 112,
        .scroll_total   = 560,
        .hint_row       = 26,
        .hint_l_col     = 1,
        .hint_r_col     = 34,
    },

    /* ---- Settings Screen ---- */
    .settings = {
        .bg               = COUP_FULLSCREEN_BG,
        .header_panel     = {8, 8, COUP_SCREEN_W - 16, 40},
        .header_col       = 14,
        .header_row       = 2,
        .diff_y           = 100,
        .diff_panel       = {30, 98, 270, 20},   /* y - 2 */
        .diff_label_x     = 52,
        .diff_cursor_x    = 36,
        .diff_option_x    = 120,
        .diff_option_spacing = 44,
        .diff_option_w    = 42,
        .diff_option_h    = 16,
        .diff_text_offset_y = 2,
        .diff_arrow_left_offset = -14,
        .diff_arrow_right_offset = 134,
        .hint_row         = 25,
        .hint_left_col    = 4,
        .hint_right_col   = 30,
    },

    /* ---- Rules Screen ---- */
    .rules = {
        .bg             = COUP_FULLSCREEN_BG,
        .header_bar     = {0, 0, COUP_SCREEN_W, 16},
        .content_panel  = {8, 20, COUP_SCREEN_W - 16, 172},
        .nav_bar        = {8, 198, COUP_SCREEN_W - 16, 20},
        .header_hline_y = 14,
        .content_hline_y = 20,
        .nav_hline_y    = 198,
        .header_col     = 1,
        .header_row     = 0,
        .portrait_pos   = {256, 40},
        .bg_tile_y      = 192,
        .page_str_col   = 15,
        .page_str_row   = 25,
        .prev_pos       = {16, 200},
        .next_pos       = {216, 200},
        .back_pos       = {120, 212},
    },

    /* ---- Connecting Screen ---- */
    .connecting = {
        .bg             = COUP_FULLSCREEN_BG,
        .main_panel     = {24, 40, 272, 140},
        .title_pos      = {80, 48},
        .title_hline_y  = 58,
        .title_hline_x  = 80,
        .title_hline_w  = 160,
        .text_x         = 40,
        .stage_y        = 72,
        .detail_y       = 84,
        .progress_bg    = {40, 100, 240, 12},
        .progress_bar_x = 42,
        .progress_bar_y = 102,
        .progress_bar_max_w = 236,
        .progress_bar_h = 8,
        .log_list       = {40, 120, 10},
        .log_max_visible = 4,
        .cancel_panel   = {100, 164, 120, 14},
        .cancel_text_pos = {116, 166},
        .retry_pos      = {40, 190},
        .bg_tile_y      = 192,
    },

    /* ---- Name Entry Screen ---- */
    .name_entry = {
        .bg               = COUP_FULLSCREEN_BG,
        .header_panel     = {24, 16, 272, 24},
        .name_panel       = {24, 48, 272, 24},
        .char_scroll_panel = {80, 88, 160, 20},
        .controls_panel   = {24, 120, 272, 64},
        .header_col       = 6,
        .header_row       = 2,
        .name_col         = 4,
        .name_row         = 7,
        .indicator_col    = 4,
        .indicator_row    = 12,
        .ctrl_col         = 5,
        .ctrl_start_row   = 16,
        .submit_row       = 19,
        .sprite_pos       = {8, 44},
        .bg_tile_y        = 192,
    },

    /* ---- Lobby Screen ---- */
    .lobby = {
        .bg             = COUP_FULLSCREEN_BG,
        .header_bar     = {0, 0, COUP_SCREEN_W, 16},
        .player_area    = {8, 20, COUP_SCREEN_W - 16, 120},
        .slot_x         = 12,
        .slot_base_y    = 22,
        .slot_spacing   = 14,
        .slot_w         = 296,
        .slot_h         = 12,
        .ready_bar_w    = 8,
        .text_x         = 16,
        .text_base_y    = 24,
        .text_spacing   = 14,
        .ready_text_x   = 232,
        .controls_panel = {8, 148, COUP_SCREEN_W - 16, 32},
        .ctrl_text_x    = 16,
        .ctrl_ready_y   = 150,
        .ctrl_leave_y   = 162,
        .ctrl_start_y   = 174,
        .status_bar     = {8, 188, COUP_SCREEN_W - 16, 28},
        .status_text_x  = 16,
        .status_text_y  = 192,
        .status_detail_y = 204,
        .bg_tile_y      = 192,
    },

    /* ---- Game Screen (Card Table Layout) ---- */
    .game = {
        /* Parametric seat layout (3 left + 3 right, computed at render time) */
        .seats = {
            .left_x           = GAME_LEFT_X,
            .right_x          = GAME_RIGHT_X,
            .w                = GAME_SEAT_W,
            .h                = GAME_SEAT_H,
            .start_y          = GAME_ROW_Y,
            .gap              = GAME_SEAT_GAP,
            .text_inset       = 4,
            .name_offset_y    = 4,
            .cards_offset_y   = 16,
            .coins_offset_y   = 28,
            .left_coins_inset = 44,
            .right_cards_inset = 28,
            .card_spacing     = 24,
            .max_name_chars   = 8,
        },
        /* Player hand (center-bottom, flush to screen bottom) */
        .hand = {
            .panel       = {GAME_CENTER_X, GAME_HAND_Y, GAME_CENTER_W, GAME_HAND_H},
            .card0_x     = GAME_CENTER_X + 12,  .card0_y    = GAME_HAND_Y + 4,
            .card1_x     = GAME_CENTER_X + 124, .card1_y    = GAME_HAND_Y + 4,
            .label0_x    = GAME_CENTER_X + 16,   .label0_y  = GAME_HAND_Y + 54,
            .label1_x    = GAME_CENTER_X + 128,  .label1_y  = GAME_HAND_Y + 54,
            .name_x      = GAME_CENTER_X + 56,  .name_y     = GAME_HAND_Y + 12,
            .coin_sprite_x = GAME_CENTER_X + 68, .coin_sprite_y = GAME_HAND_Y + 28,
            .coins_x     = GAME_CENTER_X + 86,  .coins_y    = GAME_HAND_Y + 30,
        },
        /* Split center panels */
        .log_panel    = {0, 0, COUP_SCREEN_W, GAME_LOG_H},
        .center_panel = {GAME_CENTER_X, GAME_ROW_Y, GAME_CENTER_W, GAME_CENTER_H},
        /* Game log (top center) */
        .log = {
            .text_x        = 4,
            .base_y        = 4,
            .spacing       = 10,
            .max_visible   = 5,
            .scroll_arrow_x = COUP_SCREEN_W - 12,
        },
        /* Phase: select action (center column) */
        .select_action = {
            .title_bar     = {GAME_CONTENT_X, GAME_CONTENT_Y, GAME_CONTENT_W, 12},
            .title_text_x  = GAME_TEXT_X,
            .title_text_y  = GAME_CONTENT_Y + 2,
            .action_start_y = GAME_CONTENT_Y + 16,
            .items_offset_y = 10,
            .item_spacing  = 8,
            .item_x        = GAME_CONTENT_X,
            .item_w        = GAME_CONTENT_W,
            .item_h        = 8,
            .timer_bar_h   = 4,
        },
        /* Phase: select target */
        .select_target = {
            .title_bar     = {GAME_CONTENT_X, GAME_CONTENT_Y, GAME_CONTENT_W, 12},
            .title_text_x  = GAME_TEXT_X,
            .title_text_y  = GAME_CONTENT_Y + 2,
            .item_base_y   = GAME_CONTENT_Y + 18,
            .item_spacing  = 8,
            .item_x        = GAME_CONTENT_X,
            .item_w        = GAME_CONTENT_W,
            .item_h        = 8,
            .item_text_offset_y = 1,
        },
        /* Phase: challenge / block / block-challenge (shared layout) */
        .challenge_wait  = GAME_RESPONSE_LAYOUT,
        .block_wait      = GAME_RESPONSE_LAYOUT,
        .block_challenge = GAME_RESPONSE_LAYOUT,
        /* Phase: idle/resolving */
        .idle = {
            .panel     = {GAME_CONTENT_X, GAME_CONTENT_Y, GAME_CONTENT_W, 24},
            .text_x    = GAME_TEXT_X,
            .text_y    = GAME_CONTENT_Y + 6,
            .bar_x     = GAME_TEXT_X,
            .bar_y     = GAME_CONTENT_Y + 18,
            .bar_max_w = GAME_CONTENT_W - 8,
            .bar_h     = 4,
        },
        /* Corner shortcuts (flush to edges, below seats) */
        .corners = {
            .left_panel   = {GAME_LEFT_X, GAME_CORNER_Y, GAME_SEAT_W, GAME_CORNER_H},
            .left_text_x  = 5,
            .left_text_y  = GAME_CORNER_Y + 6,
            .right_panel  = {GAME_RIGHT_X, GAME_CORNER_Y, GAME_SEAT_W, GAME_CORNER_H},
            .right_text_x = GAME_RIGHT_X + 12,
            .right_text_y = GAME_CORNER_Y + 6,
        },
    },

    /* ---- Game Over Screen ---- */
    .gameover = {
        .bg            = COUP_FULLSCREEN_BG,
        .gameover_col  = 7,
        .gameover_row  = 6,
        .winner_row    = 23,
        .return_col    = 9,
        .return_row    = 18,
    },

    /* ---- Reference Card Screen ---- */
    .reference = {
        .bg             = COUP_FULLSCREEN_BG,
        .header_bar     = {0, 0, COUP_SCREEN_W, 16},
        .header_hline_y = 14,
        .title_x        = 8,
        .title_y        = 3,
        .dismiss_x      = 200,
        .dismiss_y      = 3,
        .row_base_y     = 18,
        .row_spacing    = 40,
        .color_band_w   = 8,
        .color_band_h   = 38,
        .panel_x        = 10,
        .panel_w        = 306,
        .panel_h        = 38,
        .name_x         = 16,
        .name_offset_y  = 2,
        .line1_offset_y = 12,
        .line2_offset_y = 22,
        .line3_offset_y = 30,
        .portrait_x     = 244,
        .portrait_offset_y = -4,
        .footer_x       = 8,
        .footer_y       = 216,
    },
};

#endif /* COUP_UI_H */
