/**
 * coup_render.c - VDP1 Sprite-Based Rendering for Coup Card Game
 *
 * Saturn hardware rendering strategy:
 *   VDP1 (priority 4): Colored rectangles + sprite-based text
 *   All rendering goes through VDP1 for pixel-accurate positioning.
 *
 * Screen: 320x224 pixels, 8x8 character grid (40 columns x 28 rows).
 * VDP1 budget: 2048 commands per frame (rects + text sprites).
 *
 * Design: Dark panel backgrounds with colored accent bars create depth.
 * Text is rendered as VDP1 sprites for precise pixel placement.
 */

#include "coup.h"
#include "coup_ui.h"
#include "cui_pal.h"

#include <stdio.h>
#include <string.h>

/* Saturn sprite support (compile-time conditional) */
#ifdef __SATURN__
#include "coup_sprite_loader.h"
#include "coup_gameover_loader.h"
#include "coup_anim_loader.h"
#include "coup_anim_sprites.h"
#endif

/*============================================================================
 * Grid-to-pixel helpers
 *============================================================================*/

/** Draw text at grid column/row (col*8, row*8 pixels). */
static void draw_at(int col, int row, const char* text, uint32_t color)
{
    CUI_DISPLAY()->draw_text_sprite(col * COUP_FONT_ADVANCE, row * COUP_FONT_ROW_H, text, color);
}

/** Draw a VDP1 rectangle panel. */
static void panel(int x, int y, int w, int h, uint32_t color)
{
    CUI_DISPLAY()->draw_rect(x, y, w, h, color);
}

/** Draw a thin horizontal accent line (2px tall). */
static void hline(int x, int y, int w, uint32_t color)
{
    CUI_DISPLAY()->draw_rect(x, y, w, 2, color);
}

/** Draw a thin vertical accent line (2px wide). */
static void vline(int x, int y, int h, uint32_t color)
{
    CUI_DISPLAY()->draw_rect(x, y, 2, h, color);
}

/** Draw a VDP1 rectangle panel from a rect struct. */
static void panel_r(cui_rect_t r, uint32_t color)
{
    CUI_DISPLAY()->draw_rect(r.x, r.y, r.w, r.h, color);
}

/*============================================================================
 * Shared utility helpers
 *============================================================================*/

static const char* card_short(int character)
{
    if (character >= 0 && character <= 6) {
        return coup_char_short[character];
    }
    return "??";
}

static int safe_copy(char* dst, const char* src, int max_len)
{
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static const coup_player_t* find_self(const coup_state_t* st)
{
    int i;
    for (i = 0; i < st->player_count; i++) {
        if (st->players[i].is_self) {
            return &st->players[i];
        }
    }
    return NULL;
}

/*============================================================================
 * Sprite helpers (Saturn only)
 *============================================================================*/

#ifdef __SATURN__
/** Map character ID to sprite index, or -1. */
static int char_to_sprite(int character)
{
    switch (character) {
    case COUP_CHAR_DUKE:       return COUP_SPR_DUKE;
    case COUP_CHAR_ASSASSIN:   return COUP_SPR_ASSASSIN;
    case COUP_CHAR_CAPTAIN:    return COUP_SPR_CAPTAIN;
    case COUP_CHAR_AMBASSADOR: return COUP_SPR_AMBASSADOR;
    case COUP_CHAR_CONTESSA:   return COUP_SPR_CONTESSA;
    default:                   return -1;
    }
}
#endif

#ifdef __SATURN__
/** Draw a row of background tiles across the screen width.
 *  10 tiles at 32px each = 320px. Uses 10 VDP1 commands. */
static void draw_bg_row(int y)
{
    int x;
    if (!coup_sprites_loaded()) return;
    for (x = 0; x < 320; x += 32) {
        coup_sprites_draw(COUP_SPR_BG_TILE, x, y);
    }
}
#endif

/*============================================================================
 * 1. TITLE SCREEN — Full-width layout with horizontal menu
 *
 * VDP1 rects: ~16
 *   1 full bg, 4 title border, ~3 portrait sprites,
 *   ~3 name bars, 1 selection highlight
 * VDP2: menu text, bottom hints, title logo
 *============================================================================*/

static void coup_render_title(const coup_state_t* st)
{
    const coup_title_layout_t* L = &COUP_UI.title;

    /* === VDP1 LAYER === */

    /* 1. Full-screen dark background */
    panel_r(L->bg, COUP_BG_DARK);

    /* 2. Title header panel (full width) */
    panel_r(L->header_panel, COUP_PANEL_HEADER);

#ifdef __SATURN__
    /* 3. Animated portrait sprites scrolling full-width right-to-left (2x scaled) */
    if (coup_anim_loaded()) {
        int scroll = (st->anim_timer / 2) % L->scroll_total;
        int i;

        for (i = 0; i < COUP_NUM_CHARACTERS; i++) {
            int x_in_tape = i * L->portrait_slot;
            int x_rel = ((x_in_tape - scroll) % L->scroll_total
                        + L->scroll_total) % L->scroll_total;
            int screen_x = x_rel + L->scroll_zone_x;
            int frame;

            /* Wrap if off-screen right */
            if (screen_x >= 320) {
                screen_x -= L->scroll_total;
            }

            /* Skip if fully off-screen (display size is 64x96 after 2x scale) */
            if (screen_x >= 320) continue;
            if (screen_x + 64 <= 0) continue;

            /* Staggered animation: each character at different phase */
            frame = (st->frame_count / 8 + i * 5) % COUP_ANIM_FRAMES;
            coup_anim_draw_scaled(i, frame, screen_x, L->portrait_y, 64, 96);
        }
    } else if (coup_sprites_loaded()) {
        /* Fallback to static portraits if animated not available */
        int scroll = (st->anim_timer / 2) % L->scroll_total;
        int i;

        for (i = 0; i < COUP_NUM_CHARACTERS; i++) {
            int x_in_tape = i * L->portrait_slot;
            int x_rel = ((x_in_tape - scroll) % L->scroll_total
                        + L->scroll_total) % L->scroll_total;
            int screen_x = x_rel + L->scroll_zone_x;

            if (screen_x >= 320) {
                screen_x -= L->scroll_total;
            }

            if (screen_x >= 320) continue;
            if (screen_x + 64 <= 0) continue;

            coup_sprites_draw(char_to_sprite(i), screen_x, L->portrait_y);
        }
    }
#endif

    /* === VDP2 LAYER: Text overlay === */

#ifdef __SATURN__
    /* Title logo sprite */
    if (coup_sprites_loaded()) {
        coup_sprites_draw(COUP_SPR_TITLE, L->logo_pos.x, L->logo_pos.y);
    } else {
#endif
    /* ASCII-art fallback title */
    draw_at(L->ascii_col, L->ascii_start_row,     " ####  ####  #  # ####", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 1,  "#      #  #  #  # #  #", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 2,  "#      #  #  #  # ####", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 3,  "#      #  #  #  # #   ", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 4,  " ####  ####  #### #   ", COUP_TEXT_YELLOW);
#ifdef __SATURN__
    }
#endif

    /* Single centered "Play" button */
    {
        int play_x = (COUP_SCREEN_W - 32) / 2;  /* center "Play" (4 chars * 8px) */
        panel(play_x - 8, L->menu_y - 2, 48, 12, COUP_PANEL_SELECT);
        CUI_DISPLAY()->draw_text_sprite(play_x, L->menu_y, "Play", COUP_TEXT_GREEN);
    }

    /* Bottom bar hint: [R] Rules centered */
    draw_at((COUP_SCREEN_W / COUP_FONT_ADVANCE - 8) / 2, L->hint_row, "[R]Rules", COUP_TEXT_GRAY);
}

/*============================================================================
 * 1a. SETTINGS SCREEN — Music vol, SFX vol, Bot difficulty
 *
 * VDP1 rects: ~20  (bg + 2 slider tracks + 2*10 segments + difficulty + highlight)
 *============================================================================*/

static void coup_render_settings(const coup_state_t* st)
{
    const coup_settings_layout_t* L = &COUP_UI.settings;
    static const char* diff_names[3] = { "Easy", "Medium", "Hard" };
    static const uint32_t diff_colors[3] = { COUP_TEXT_GREEN, COUP_TEXT_YELLOW, COUP_TEXT_RED };
    int diff = st->bot_difficulty;
    int j;

    if (diff < 0) diff = 0;
    if (diff > 2) diff = 2;

    /* Dark background */
    panel_r(L->bg, COUP_BG_DARK);

    /* Header */
    panel_r(L->header_panel, COUP_PANEL_HEADER);
    draw_at(L->header_col, L->header_row, "Settings", COUP_TEXT_YELLOW);

    /* Bot difficulty - single setting */
    panel_r(L->diff_panel, COUP_PANEL_MID);
    CUI_DISPLAY()->draw_text_sprite(L->diff_cursor_x, L->diff_y + L->diff_text_offset_y, ">", COUP_TEXT_GREEN);
    CUI_DISPLAY()->draw_text_sprite(L->diff_label_x, L->diff_y + L->diff_text_offset_y, "Bots", COUP_TEXT_WHITE);

    /* Difficulty display: three options with selection highlight */
    for (j = 0; j < 3; j++) {
        int dx = L->diff_option_x + j * L->diff_option_spacing;
        uint32_t text_col;

        if (j == diff) {
            panel(dx - 2, L->diff_y, L->diff_option_w, L->diff_option_h, COUP_PANEL_SELECT);
            text_col = diff_colors[j];
        } else {
            text_col = COUP_TEXT_GRAY;
        }
        CUI_DISPLAY()->draw_text_sprite(dx, L->diff_y + L->diff_text_offset_y, diff_names[j], text_col);
    }

    /* Left/right arrows */
    if (diff > 0) {
        CUI_DISPLAY()->draw_text_sprite(L->diff_option_x + L->diff_arrow_left_offset,
                                 L->diff_y + L->diff_text_offset_y, "<", COUP_TEXT_WHITE);
    }
    if (diff < 2) {
        CUI_DISPLAY()->draw_text_sprite(L->diff_option_x + L->diff_arrow_right_offset,
                                 L->diff_y + L->diff_text_offset_y, ">", COUP_TEXT_WHITE);
    }

    /* Hint bar */
    draw_at(L->hint_left_col, L->hint_row, "</>  Adjust", COUP_TEXT_GRAY);
    draw_at(L->hint_right_col, L->hint_row, "(B) Back", COUP_TEXT_GRAY);
}

/*============================================================================
 * 1b. RULES / HELP SCREEN  (5 pages, [LEFT]/[RIGHT] to navigate, [B] back)
 *
 * VDP1 rects: ~10
 *============================================================================*/

static void coup_render_rules(const coup_state_t* st)
{
    const coup_rules_layout_t* L = &COUP_UI.rules;
    char page_str[24];
    int pg = st->rules_page;

    /* Dark background */
    panel_r(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
    /* Decorative background tiles */
    draw_bg_row(L->bg_tile_y);
    /* Different character portrait per rules page (decorative backdrop) */
    if (pg > 0 && coup_sprites_loaded()) {
        static const int page_portrait[COUP_RULES_PAGES - 1] = {
            COUP_SPR_AMBASSADOR, COUP_SPR_CAPTAIN, COUP_SPR_DUKE,
            COUP_SPR_ASSASSIN, COUP_SPR_CONTESSA
        };
        coup_sprites_draw(page_portrait[pg - 1], L->portrait_pos.x, L->portrait_pos.y);
    }
#endif

    /* Header bar */
    panel_r(L->header_bar, COUP_PANEL_HEADER);
    hline(L->header_bar.x, L->header_hline_y, L->header_bar.w, COUP_ACCENT_GOLD);

    /* Content panel */
    panel_r(L->content_panel, COUP_PANEL_DARK);
    hline(L->content_panel.x, L->content_hline_y, L->content_panel.w, COUP_ACCENT_DIM);

    /* Navigation bar */
    panel_r(L->nav_bar, COUP_PANEL_MID);
    hline(L->nav_bar.x, L->nav_hline_y, L->nav_bar.w, COUP_ACCENT_BLUE);

    /* Header */
    draw_at(L->header_col, L->header_row,
            pg == 0 ? " CHARACTER REFERENCE" : " HOW TO PLAY COUP",
            COUP_TEXT_YELLOW);

    /* Rules page text — page 0 is character reference, pages 1-5 are rules */
    switch (pg) {
    case 0: /* Character reference card */
        draw_at(2, 3,  "CHARACTERS        [Page 1/6]", COUP_ACCENT_GOLD);
        draw_at(2, 5,  "DUKE", COUP_TEXT_PINK);
        draw_at(2, 6,  "  Tax: Take 3 coins", COUP_TEXT_WHITE);
        draw_at(2, 7,  "  Blocks: Foreign Aid", COUP_TEXT_GRAY);
        draw_at(2, 9,  "ASSASSIN", COUP_TEXT_RED);
        draw_at(2, 10, "  Assassinate: Pay 3, kill 1", COUP_TEXT_WHITE);
        draw_at(2, 11, "  Blocked by: Contessa", COUP_TEXT_GRAY);
        draw_at(2, 13, "CAPTAIN", COUP_TEXT_BLUE);
        draw_at(2, 14, "  Steal: Take 2 from target", COUP_TEXT_WHITE);
        draw_at(2, 15, "  Blocked by: Capt/Ambass.", COUP_TEXT_GRAY);
        draw_at(2, 17, "AMBASSADOR", COUP_TEXT_GREEN);
        draw_at(2, 18, "  Exchange: Draw 2, keep 2", COUP_TEXT_WHITE);
        draw_at(2, 19, "  Also blocks: Steal", COUP_TEXT_GRAY);
        draw_at(2, 21, "CONTESSA", COUP_TEXT_GOLD);
        draw_at(2, 22, "  Blocks: Assassination", COUP_TEXT_WHITE);
        break;

    case 1: /* Overview */
        draw_at(2, 3,  "OVERVIEW          [Page 2/6]", COUP_ACCENT_GOLD);
        draw_at(2, 5,  "Coup is a bluffing game for", COUP_TEXT_WHITE);
        draw_at(2, 6,  "2-6 players. Each player has", COUP_TEXT_WHITE);
        draw_at(2, 7,  "2 hidden influence cards and", COUP_TEXT_WHITE);
        draw_at(2, 8,  "starts with 2 coins.", COUP_TEXT_WHITE);
        draw_at(2, 10, "GOAL: Be the last player", COUP_TEXT_GREEN);
        draw_at(2, 11, "with influence remaining.", COUP_TEXT_GREEN);
        draw_at(2, 13, "On your turn, choose ONE", COUP_TEXT_WHITE);
        draw_at(2, 14, "action. You may BLUFF about", COUP_TEXT_WHITE);
        draw_at(2, 15, "which cards you hold!", COUP_TEXT_YELLOW);
        draw_at(2, 17, "If challenged and caught", COUP_TEXT_ORANGE);
        draw_at(2, 18, "bluffing, you lose a card.", COUP_TEXT_ORANGE);
        draw_at(2, 20, "Lose both cards = eliminated", COUP_TEXT_RED);
        break;

    case 2: /* Basic actions */
        draw_at(2, 3,  "BASIC ACTIONS     [Page 3/6]", COUP_ACCENT_GOLD);
        draw_at(2, 5,  "INCOME: Take 1 coin.", COUP_TEXT_WHITE);
        draw_at(2, 6,  "  Cannot be blocked.", COUP_TEXT_GRAY);
        draw_at(2, 8,  "FOREIGN AID: Take 2 coins.", COUP_TEXT_WHITE);
        draw_at(2, 9,  "  Blocked by: Duke", COUP_TEXT_GRAY);
        draw_at(2, 11, "COUP: Pay 7 coins, target", COUP_TEXT_WHITE);
        draw_at(2, 12, "  loses 1 influence.", COUP_TEXT_WHITE);
        draw_at(2, 13, "  Cannot be blocked.", COUP_TEXT_GRAY);
        draw_at(2, 15, "MANDATORY COUP: If you have", COUP_TEXT_ORANGE);
        draw_at(2, 16, "  10+ coins, you MUST coup.", COUP_TEXT_ORANGE);
        break;

    case 3: /* Character actions */
        draw_at(2, 3,  "CHARACTER ACTIONS [Page 4/6]", COUP_ACCENT_GOLD);
        draw_at(2, 5,  "DUKE - Tax: Take 3 coins.", COUP_TEXT_WHITE);
        draw_at(2, 6,  "  Also blocks Foreign Aid.", COUP_TEXT_GRAY);
        draw_at(2, 8,  "ASSASSIN - Assassinate:", COUP_TEXT_WHITE);
        draw_at(2, 9,  "  Pay 3, target loses card.", COUP_TEXT_WHITE);
        draw_at(2, 10, "  Blocked by: Contessa", COUP_TEXT_GRAY);
        draw_at(2, 12, "CAPTAIN - Steal:", COUP_TEXT_WHITE);
        draw_at(2, 13, "  Take 2 coins from target.", COUP_TEXT_WHITE);
        draw_at(2, 14, "  Blocked by: Captain/Ambas", COUP_TEXT_GRAY);
        draw_at(2, 16, "AMBASSADOR - Exchange:", COUP_TEXT_WHITE);
        draw_at(2, 17, "  Draw 2 from deck, keep 2.", COUP_TEXT_WHITE);
        draw_at(2, 18, "  Also blocks Steal.", COUP_TEXT_GRAY);
        draw_at(2, 20, "CONTESSA - Blocks Assassin.", COUP_TEXT_WHITE);
        break;

    case 4: /* Challenging and blocking */
        draw_at(2, 3,  "CHALLENGES/BLOCKS [Page 5/6]", COUP_ACCENT_GOLD);
        draw_at(2, 5,  "CHALLENGING:", COUP_TEXT_YELLOW);
        draw_at(2, 6,  "Any player can challenge an", COUP_TEXT_WHITE);
        draw_at(2, 7,  "action or block claim.", COUP_TEXT_WHITE);
        draw_at(2, 9,  "If challenge SUCCEEDS:", COUP_TEXT_GREEN);
        draw_at(2, 10, "  Liar loses 1 influence.", COUP_TEXT_WHITE);
        draw_at(2, 12, "If challenge FAILS:", COUP_TEXT_RED);
        draw_at(2, 13, "  Challenger loses 1 card.", COUP_TEXT_WHITE);
        draw_at(2, 14, "  Proven card is replaced.", COUP_TEXT_WHITE);
        draw_at(2, 16, "BLOCKING:", COUP_TEXT_YELLOW);
        draw_at(2, 17, "Target of an action may try", COUP_TEXT_WHITE);
        draw_at(2, 18, "to block using a character.", COUP_TEXT_WHITE);
        draw_at(2, 19, "Blocks can also be", COUP_TEXT_WHITE);
        draw_at(2, 20, "challenged!", COUP_TEXT_ORANGE);
        break;

    case 5: /* Controls */
        draw_at(2, 3,  "CONTROLS          [Page 6/6]", COUP_ACCENT_GOLD);
        draw_at(2, 5,  "D-PAD UP/DOWN:", COUP_TEXT_YELLOW);
        draw_at(4, 6,  "Navigate menus", COUP_TEXT_WHITE);
        draw_at(2, 8,  "A BUTTON:", COUP_TEXT_GREEN);
        draw_at(4, 9,  "Confirm / Challenge", COUP_TEXT_WHITE);
        draw_at(2, 11, "B BUTTON:", COUP_TEXT_RED);
        draw_at(4, 12, "Cancel / Pass / Allow", COUP_TEXT_WHITE);
        draw_at(2, 14, "START:", COUP_TEXT_YELLOW);
        draw_at(4, 15, "Begin game from title", COUP_TEXT_WHITE);
        draw_at(2, 17, "R BUTTON:", COUP_TEXT_YELLOW);
        draw_at(4, 18, "Rules (any time)", COUP_TEXT_WHITE);
        draw_at(2, 20, "TIP: You can bluff! Claim", COUP_TEXT_ORANGE);
        draw_at(2, 21, "any character, even if you", COUP_TEXT_WHITE);
        draw_at(2, 22, "don't have it.", COUP_TEXT_WHITE);
        break;
    }

    /* Navigation */
    snprintf(page_str, sizeof(page_str), "Page %d / %d", pg + 1, COUP_RULES_PAGES);
    draw_at(L->page_str_col, L->page_str_row, page_str, COUP_TEXT_WHITE);

    if (pg > 0) {
        CUI_DISPLAY()->draw_text_sprite(L->prev_pos.x, L->prev_pos.y, "[<] Prev", COUP_TEXT_GRAY);
    }
    if (pg < COUP_RULES_PAGES - 1) {
        CUI_DISPLAY()->draw_text_sprite(L->next_pos.x, L->next_pos.y, "Next [>]", COUP_TEXT_GRAY);
    }
    CUI_DISPLAY()->draw_text_sprite(L->back_pos.x, L->back_pos.y, "[B] Back", COUP_BTN_B_COLOR);
}

/*============================================================================
 * 2. CONNECTING SCREEN
 *
 * VDP1 rects: ~8
 *============================================================================*/

static void coup_render_connecting(const coup_state_t* st)
{
    const coup_connecting_layout_t* L = &COUP_UI.connecting;
    int anim_phase;
    const char* stage_text;
    const char* detail_text;

    /* Dark background */
    panel_r(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
    /* Decorative background tiles */
    draw_bg_row(L->bg_tile_y);
#endif

    /* Connection panel */
    panel_r(L->main_panel, COUP_PANEL_HEADER);
    hline(L->main_panel.x, L->main_panel.y, L->main_panel.w, COUP_ACCENT_BLUE);
    hline(L->main_panel.x, L->main_panel.y + L->main_panel.h, L->main_panel.w, COUP_ACCENT_BLUE);
    vline(L->main_panel.x, L->main_panel.y, L->main_panel.h, COUP_ACCENT_BLUE);
    vline(L->main_panel.x + L->main_panel.w - 2, L->main_panel.y, L->main_panel.h, COUP_ACCENT_BLUE);

    /* Title */
    CUI_DISPLAY()->draw_text_sprite(L->title_pos.x, L->title_pos.y, "CONNECTING", COUP_TEXT_YELLOW);
    hline(L->title_hline_x, L->title_hline_y, L->title_hline_w, COUP_ACCENT_GOLD);

    /* Connection stage messages */
    switch (st->connect_stage) {
    case 0:
        stage_text = "Probing NetLink modem...";
        detail_text = "Detecting UART hardware";
        break;
    case 1:
        stage_text = "Initializing modem...";
        detail_text = "Sending AT commands";
        break;
    case 2:
        stage_text = "Dialing server...";
        detail_text = "Connecting via phone line";
        break;
    case 3:
        stage_text = "Authenticating...";
        detail_text = "Waiting for server response";
        break;
    default:
        stage_text = "Connecting...";
        detail_text = "Please wait";
        break;
    }

    CUI_DISPLAY()->draw_text_sprite(L->text_x, L->stage_y, stage_text, COUP_TEXT_WHITE);
    CUI_DISPLAY()->draw_text_sprite(L->text_x, L->detail_y, detail_text, COUP_TEXT_GRAY);

    /* Progress bar background */
    panel_r(L->progress_bg, COUP_PANEL_DARK);
    hline(L->progress_bg.x, L->progress_bg.y, L->progress_bg.w, COUP_ACCENT_DIM);

    /* Animated progress bar */
    anim_phase = (st->anim_timer / 3) % 40;
    {
        int bar_w = anim_phase * 6;
        if (bar_w > L->progress_bar_max_w) bar_w = L->progress_bar_max_w;
        panel(L->progress_bar_x, L->progress_bar_y, bar_w, L->progress_bar_h, COUP_ACCENT_BLUE);
    }

    /* Animated dots after stage text */
    {
        int dots = (st->anim_timer / 20) % 4;
        char dotstr[8];
        int i;
        for (i = 0; i < dots; i++) dotstr[i] = '.';
        dotstr[dots] = '\0';
        CUI_DISPLAY()->draw_text_sprite(
            L->text_x + (int)strlen(stage_text) * COUP_FONT_ADVANCE - COUP_FONT_ADVANCE,
            L->stage_y, dotstr, COUP_TEXT_YELLOW);
    }

    /* Game log messages (show modem status) */
    {
        int li;
        int visible = (st->log_count < L->log_max_visible) ? st->log_count : L->log_max_visible;
        for (li = 0; li < visible; li++) {
            int idx;
            if (st->log_count <= L->log_max_visible) {
                idx = (st->log_head + li) % COUP_LOG_LINES;
            } else {
                int start = (st->log_head + st->log_count - L->log_max_visible) % COUP_LOG_LINES;
                idx = (start + li) % COUP_LOG_LINES;
            }
            CUI_DISPLAY()->draw_text_sprite(L->log_list.x,
                                     L->log_list.base_y + li * L->log_list.spacing,
                                     st->log[idx], COUP_TEXT_GRAY);
        }
    }

    /* Cancel hint */
    panel_r(L->cancel_panel, COUP_PANEL_MID);
    CUI_DISPLAY()->draw_text_sprite(L->cancel_text_pos.x, L->cancel_text_pos.y, "[B] Cancel", COUP_BTN_B_COLOR);

    /* Retry count if applicable */
    if (st->auth_retries > 0) {
        char retry[24];
        snprintf(retry, sizeof(retry), "Retry %d/%d",
                 st->auth_retries, 5);
        CUI_DISPLAY()->draw_text_sprite(L->retry_pos.x, L->retry_pos.y, retry, COUP_TEXT_ORANGE);
    }
}

/*============================================================================
 * 3. NAME ENTRY SCREEN
 *
 * VDP1 rects: ~12
 *============================================================================*/

static void coup_render_name_entry(const coup_state_t* st)
{
    const coup_name_entry_layout_t* L = &COUP_UI.name_entry;
    int i;

    /* Dark background */
    panel_r(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
    /* Decorative background tiles and card_back sprite */
    draw_bg_row(L->bg_tile_y);
    if (coup_sprites_loaded()) {
        coup_sprites_draw(COUP_SPR_CARD_BACK, L->sprite_pos.x, L->sprite_pos.y);
    }
#endif

    /* Header panel */
    panel_r(L->header_panel, COUP_PANEL_HEADER);
    hline(L->header_panel.x, L->header_panel.y, L->header_panel.w, COUP_ACCENT_GOLD);
    hline(L->header_panel.x, L->header_panel.y + L->header_panel.h, L->header_panel.w, COUP_ACCENT_GOLD);

    /* Name input panel */
    panel_r(L->name_panel, COUP_PANEL_MID);
    hline(L->name_panel.x, L->name_panel.y, L->name_panel.w, COUP_ACCENT_BLUE);
    hline(L->name_panel.x, L->name_panel.y + L->name_panel.h, L->name_panel.w, COUP_ACCENT_BLUE);

    /* Character scroll indicator panel */
    panel_r(L->char_scroll_panel, COUP_PANEL_LIGHT);

    /* Controls help panel */
    panel_r(L->controls_panel, COUP_PANEL_DARK);
    hline(L->controls_panel.x, L->controls_panel.y, L->controls_panel.w, COUP_ACCENT_DIM);

    /* === Text === */

    draw_at(L->header_col, L->header_row, "     ENTER YOUR NAME", COUP_TEXT_YELLOW);

    /* Name buffer with cursor blink */
    {
        char name_display[COUP_MAX_NAME + 4];
        int pos = 0;
        name_display[pos++] = ' ';
        name_display[pos++] = ' ';

        for (i = 0; i < COUP_MAX_NAME - 1; i++) {
            if (i < st->name_len) {
                if (i == st->name_cursor) {
                    if ((st->name_blink / 15) % 2 == 0) {
                        name_display[pos++] = st->name_buf[i];
                    } else {
                        name_display[pos++] = '_';
                    }
                } else {
                    name_display[pos++] = st->name_buf[i];
                }
            } else if (i == st->name_cursor) {
                if ((st->name_blink / 15) % 2 == 0) {
                    name_display[pos++] = '_';
                } else {
                    name_display[pos++] = ' ';
                }
            } else {
                name_display[pos++] = ' ';
            }
        }
        name_display[pos] = '\0';
        draw_at(L->name_col, L->name_row, name_display, COUP_TEXT_WHITE);
    }

    /* Character indicator */
    {
        char cur_char = ' ';
        char indicator[24];
        if (st->name_cursor < st->name_len) {
            cur_char = st->name_buf[st->name_cursor];
        }
        snprintf(indicator, sizeof(indicator), "      ^  [%c]  v", cur_char);
        draw_at(L->indicator_col, L->indicator_row, indicator, COUP_TEXT_YELLOW);
    }

    /* Controls */
    draw_at(L->ctrl_col, L->ctrl_start_row, "UP/DOWN: Change Letter", COUP_TEXT_GRAY);
    draw_at(L->ctrl_col, L->ctrl_start_row + 1, "LEFT/RIGHT: Move Cursor", COUP_TEXT_GRAY);
    draw_at(L->ctrl_col, L->submit_row, "  [A] Submit   [B] Delete", COUP_TEXT_WHITE);
}

/*============================================================================
 * 4. LOBBY SCREEN
 *
 * VDP1 rects: ~20
 *   1 bg, 1 header, 8 player slot panels, 2 accent lines,
 *   1 controls panel, 1 status bar, ready indicator rects
 *============================================================================*/

static void coup_render_lobby(const coup_state_t* st)
{
    const coup_lobby_layout_t* L = &COUP_UI.lobby;
    char line[48];
    int i;
    bool offline = !st->online_mode;

    static const char* const diff_names[3] = { "Easy", "Med", "Hard" };
    static const uint32_t diff_colors[3] = { COUP_TEXT_GREEN, COUP_TEXT_YELLOW, COUP_TEXT_RED };

    /* Dark background */
    panel_r(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
    draw_bg_row(L->bg_tile_y);
#endif

    /* Header bar */
    panel_r(L->header_bar, COUP_PANEL_HEADER);
    hline(L->header_bar.x, L->header_bar.y + L->header_bar.h - 2, L->header_bar.w,
          offline ? COUP_ACCENT_BLUE : COUP_ACCENT_GOLD);

    /* Player slots area */
    panel_r(L->player_area, COUP_PANEL_DARK);
    hline(L->player_area.x, L->player_area.y, L->player_area.w, COUP_ACCENT_DIM);

    /* Header text */
    draw_at(0, 0, offline ? " COUP - LOBBY" : " COUP - WAITING ROOM", COUP_TEXT_YELLOW);

    /* --- Player slot backgrounds --- */
    for (i = 0; i < COUP_MAX_PLAYERS; i++) {
        int sy = L->slot_base_y + i * L->slot_spacing;
        bool is_cursor = (!st->lobby_naming && i == st->lobby_cursor);

        if (i < st->player_count) {
            const coup_player_t* p = &st->players[i];
            bool is_ready = p->is_self ? st->my_ready : p->ready;

            if (is_cursor) {
                panel(L->slot_x, sy, L->slot_w, L->slot_h, COUP_PANEL_LIGHT);
            } else if (is_ready) {
                panel(L->slot_x, sy, L->slot_w, L->slot_h, COUP_PANEL_SELECT);
            } else {
                panel(L->slot_x, sy, L->slot_w, L->slot_h, COUP_PANEL_MID);
            }
            /* Ready indicator bar */
            if (is_ready) {
                panel(L->slot_x, sy, L->ready_bar_w, L->slot_h, COUP_ACCENT_GREEN);
            }
        } else if (is_cursor) {
            /* Cursor on empty "add bot" slot */
            panel(L->slot_x, sy, L->slot_w, L->slot_h, COUP_PANEL_LIGHT);
        } else {
            panel(L->slot_x, sy, L->slot_w, L->slot_h, COUP_PANEL_DARK);
        }
    }

    /* --- Player slot text --- */
    for (i = 0; i < COUP_MAX_PLAYERS; i++) {
        int row_y = L->text_base_y + i * L->text_spacing;
        bool is_cursor = (!st->lobby_naming && i == st->lobby_cursor);
        const char* cursor_str = is_cursor ? ">" : " ";

        if (i < st->player_count) {
            const coup_player_t* p = &st->players[i];
            bool is_ready = p->is_self ? st->my_ready : p->ready;
            uint32_t name_color = p->is_self ? COUP_TEXT_BLUE : COUP_TEXT_WHITE;

            /* Cursor + name */
            snprintf(line, sizeof(line), "%s P%d %-14s", cursor_str, i + 1, p->name);
            CUI_DISPLAY()->draw_text_sprite(L->text_x, row_y, line, name_color);

            if (p->is_self) {
                /* Ready state for self */
                const char* rdy_str = is_ready ? " READY" : "------";
                uint32_t rdy_color = is_ready ? COUP_TEXT_GREEN : COUP_TEXT_GRAY;
                CUI_DISPLAY()->draw_text_sprite(L->ready_text_x, row_y, rdy_str, rdy_color);
            } else if (p->is_bot) {
                /* Bot: show difficulty (both offline and online) */
                int d = p->difficulty;
                if (d < 0) d = 0;
                if (d > 2) d = 2;
                if (is_cursor) {
                    snprintf(line, sizeof(line), "<%s>", diff_names[d]);
                } else {
                    snprintf(line, sizeof(line), " %s ", diff_names[d]);
                }
                CUI_DISPLAY()->draw_text_sprite(L->ready_text_x, row_y, line, diff_colors[d]);
            } else {
                /* Human player (online): show ready state */
                const char* rdy_str = is_ready ? " READY" : "------";
                uint32_t rdy_color = is_ready ? COUP_TEXT_GREEN : COUP_TEXT_GRAY;
                CUI_DISPLAY()->draw_text_sprite(L->ready_text_x, row_y, rdy_str, rdy_color);
            }
        } else if (i == st->player_count && st->player_count < COUP_MAX_PLAYERS) {
            /* Empty slot — "add bot" hint */
            snprintf(line, sizeof(line), "%s    + Add Bot", cursor_str);
            CUI_DISPLAY()->draw_text_sprite(L->text_x, row_y, line, COUP_TEXT_GRAY);
        } else {
            /* Fully empty slot */
            CUI_DISPLAY()->draw_text_sprite(L->text_x, row_y, " ", COUP_TEXT_GRAY);
        }
    }

    /* --- Controls panel --- */
    panel_r(L->controls_panel, COUP_PANEL_MID);
    hline(L->controls_panel.x, L->controls_panel.y, L->controls_panel.w, COUP_ACCENT_BLUE);

    /* Row 1: bot controls */
    if (offline) {
        CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x, L->ctrl_ready_y,
            "[X] Add/Rem Bot  [</>] Diff", COUP_TEXT_WHITE);
    } else {
        CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x, L->ctrl_ready_y,
            "[X] Add/Rem Bot", COUP_TEXT_WHITE);
    }
    /* Row 2: ready + back/leave */
    CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x, L->ctrl_leave_y,
        "[A] Ready", COUP_BTN_A_COLOR);
    CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x + 120, L->ctrl_leave_y,
        offline ? "[B] Back" : "[B] Leave", COUP_BTN_B_COLOR);
    /* Row 3: start + extras */
    if (st->my_ready) {
        CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x, L->ctrl_start_y,
            "[START] Begin!", COUP_TEXT_YELLOW);
    } else {
        CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x, L->ctrl_start_y,
            "[START] (Ready up first)", COUP_TEXT_GRAY);
    }
    if (offline) {
        CUI_DISPLAY()->draw_text_sprite(L->ctrl_text_x + 200, L->ctrl_start_y,
            "[Z] Online", COUP_TEXT_GRAY);
    }

    /* --- Status bar --- */
    panel_r(L->status_bar, COUP_PANEL_STATUS);
    hline(L->status_bar.x, L->status_bar.y, L->status_bar.w, COUP_ACCENT_DIM);
    snprintf(line, sizeof(line), "Players: %d/7", st->player_count);
    CUI_DISPLAY()->draw_text_sprite(L->status_text_x, L->status_text_y, line, COUP_TEXT_WHITE);

    if (offline) {
        if (st->my_ready) {
            CUI_DISPLAY()->draw_text_sprite(L->status_text_x, L->status_detail_y,
                "READY! Press START to begin", COUP_TEXT_GREEN);
        } else {
            CUI_DISPLAY()->draw_text_sprite(L->status_text_x, L->status_detail_y,
                "Press [A] when ready", COUP_TEXT_GRAY);
        }
    } else {
        int ri, ready_count = 0;
        for (ri = 0; ri < st->player_count; ri++) {
            bool rdy = st->players[ri].is_self
                     ? st->my_ready : st->players[ri].ready;
            if (rdy) ready_count++;
        }
        snprintf(line, sizeof(line), "Ready: %d/%d", ready_count, st->player_count);
        CUI_DISPLAY()->draw_text_sprite(L->status_text_x + 120, L->status_text_y, line, COUP_TEXT_WHITE);
        if (ready_count >= 2) {
            CUI_DISPLAY()->draw_text_sprite(L->status_text_x, L->status_detail_y,
                "Press START to begin!", COUP_TEXT_GREEN);
        } else {
            CUI_DISPLAY()->draw_text_sprite(L->status_text_x, L->status_detail_y,
                "Waiting for players...", COUP_TEXT_GRAY);
        }
    }

    /* --- Name entry overlay (drawn last, on top) --- */
    if (st->lobby_naming) {
        /* Semi-dark overlay background */
        cui_rect_t overlay_bg = {40, 40, 240, 144};
        panel_r(overlay_bg, COUP_BG_DARK);
        panel(40, 40, 240, 2, COUP_ACCENT_GOLD);
        panel(40, 182, 240, 2, COUP_ACCENT_GOLD);
        panel(40, 40, 2, 144, COUP_ACCENT_GOLD);
        panel(278, 40, 2, 144, COUP_ACCENT_GOLD);

        /* Header */
        {
            cui_rect_t hdr = {44, 44, 232, 20};
            panel_r(hdr, COUP_PANEL_HEADER);
            CUI_DISPLAY()->draw_text_sprite(80, 50, "ENTER YOUR NAME", COUP_TEXT_YELLOW);
        }

        /* Name input area */
        {
            cui_rect_t name_panel = {44, 68, 232, 20};
            panel_r(name_panel, COUP_PANEL_MID);
            panel(44, 68, 232, 2, COUP_ACCENT_BLUE);
            panel(44, 86, 232, 2, COUP_ACCENT_BLUE);
        }

        /* Name buffer with cursor blink */
        {
            char name_display[COUP_MAX_NAME + 4];
            int pos = 0;
            name_display[pos++] = ' ';
            name_display[pos++] = ' ';

            for (i = 0; i < COUP_MAX_NAME - 1; i++) {
                if (i < st->name_len) {
                    if (i == st->name_cursor && (st->name_blink / 15) % 2 == 0) {
                        name_display[pos++] = st->name_buf[i];
                    } else if (i == st->name_cursor) {
                        name_display[pos++] = '_';
                    } else {
                        name_display[pos++] = st->name_buf[i];
                    }
                } else if (i == st->name_cursor) {
                    name_display[pos++] = ((st->name_blink / 15) % 2 == 0) ? '_' : ' ';
                } else {
                    name_display[pos++] = ' ';
                }
            }
            name_display[pos] = '\0';
            CUI_DISPLAY()->draw_text_sprite(52, 74, name_display, COUP_TEXT_WHITE);
        }

        /* Character indicator */
        {
            char cur_char = ' ';
            char indicator[24];
            if (st->name_cursor < st->name_len) {
                cur_char = st->name_buf[st->name_cursor];
            }
            snprintf(indicator, sizeof(indicator), "      ^  [%c]  v", cur_char);
            CUI_DISPLAY()->draw_text_sprite(52, 96, indicator, COUP_TEXT_YELLOW);
        }

        /* Character scroll panel */
        {
            cui_rect_t scroll_panel = {80, 92, 160, 20};
            panel_r(scroll_panel, COUP_PANEL_LIGHT);
        }

        /* Controls help */
        {
            cui_rect_t ctrl_panel = {44, 116, 232, 60};
            panel_r(ctrl_panel, COUP_PANEL_DARK);
            panel(44, 116, 232, 2, COUP_ACCENT_DIM);
            CUI_DISPLAY()->draw_text_sprite(56, 124, "UP/DOWN: Change Letter", COUP_TEXT_GRAY);
            CUI_DISPLAY()->draw_text_sprite(56, 134, "LEFT/RIGHT: Move Cursor", COUP_TEXT_GRAY);
            CUI_DISPLAY()->draw_text_sprite(56, 152, " [A] Submit   [B] Delete", COUP_TEXT_WHITE);
        }
    }
}

/*============================================================================
 * 5. GAME SCREEN
 *
 * Layout (320x224):
 *   y=0-23:    Status bar (COUP + turn text, no coins)
 *   y=26-53:   Opponent boxes (compact horizontal row)
 *   y=56:      Dim hline separator
 *   y=58+:     Phase content (left 224px) | Your Hand (right 84px)
 *   y=190-213: Game log (3 lines, no background panel)
 *   y=214-223: Bottom bar
 *============================================================================*/

/* ----- Opponent boxes (y=26-53, compact horizontal row) ----- */

static void render_single_seat(const coup_seat_layout_t* seat,
                               const coup_seats_layout_t* layout,
                               const coup_state_t* st,
                               const coup_player_t* p)
{
    char name_part[16];
    char coin_str[8];
    uint32_t bg_color, name_color;
    const char* c0;
    const char* c1;
    uint32_t c0_color, c1_color;

    if (!p) {
        /* Empty seat: dim panel */
        panel_r(seat->box, COUP_PANEL_DARK);
        return;
    }

    if (!p->alive) {
        bg_color = COUP_PANEL_DARK;
        name_color = COUP_TEXT_GRAY;
    } else if (p->id == st->current_turn_id) {
        bg_color = COUP_PANEL_PROMPT;
        name_color = COUP_TEXT_BLUE;
    } else {
        bg_color = COUP_PANEL_DARK;
        name_color = COUP_TEXT_WHITE;
    }

    /* Seat panel background */
    panel_r(seat->box, bg_color);

    /* Name (truncate to max_name_chars) */
    safe_copy(name_part, p->name, layout->max_name_chars + 1);
    CUI_DISPLAY()->draw_text_sprite(seat->name_x, seat->name_y, name_part, name_color);

    /* Card abbreviations */
    if (!p->alive) {
        c0 = card_short(p->cards[0]);
        c1 = card_short(p->cards[1]);
        c0_color = (p->cards[0] < COUP_NUM_CHARACTERS)
                    ? coup_char_text_color(p->cards[0]) : COUP_TEXT_GRAY;
        c1_color = (p->cards[1] < COUP_NUM_CHARACTERS)
                    ? coup_char_text_color(p->cards[1]) : COUP_TEXT_GRAY;
    } else {
        c0 = card_short(p->cards[0]);
        c1 = card_short(p->cards[1]);
        c0_color = (p->cards[0] < COUP_NUM_CHARACTERS)
                    ? coup_char_text_color(p->cards[0]) : name_color;
        c1_color = (p->cards[1] < COUP_NUM_CHARACTERS)
                    ? coup_char_text_color(p->cards[1]) : name_color;
    }
    CUI_DISPLAY()->draw_text_sprite(seat->cards_x, seat->cards_y, c0, c0_color);
    CUI_DISPLAY()->draw_text_sprite(seat->cards_x + layout->card_spacing, seat->cards_y, c1, c1_color);

    /* Coins / dead label */
    if (!p->alive) {
        CUI_DISPLAY()->draw_text_sprite(seat->coins_x, seat->coins_y, "DEAD", COUP_TEXT_RED);
    } else {
        snprintf(coin_str, sizeof(coin_str), "$%d", p->coins);
        CUI_DISPLAY()->draw_text_sprite(seat->coins_x, seat->coins_y, coin_str, COUP_TEXT_YELLOW);
    }
}

static void render_seats(const coup_state_t* st)
{
    const coup_seats_layout_t* S = &COUP_UI.game.seats;
    /* Row index: seats 0-2 = left (bot,mid,top), 3-5 = right (top,mid,bot) */
    static const int seat_row[6] = {2, 1, 0, 0, 1, 2};
    coup_seat_layout_t computed[6];
    int self_idx = -1, i;
    const coup_player_t* seat_player[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    int opp = 0;

    /* Compute seat positions from parametric layout */
    for (i = 0; i < 6; i++) {
        int is_right = (i >= 3);
        int col_x = is_right ? S->right_x : S->left_x;
        int row_y = S->start_y + seat_row[i] * (S->h + S->gap);

        computed[i].box = (cui_rect_t){col_x, row_y, S->w, S->h};
        computed[i].name_x = col_x + S->text_inset;
        computed[i].name_y = row_y + S->name_offset_y;

        if (is_right) {
            /* Right column: coins left, cards right */
            computed[i].coins_x = col_x + S->text_inset;
            computed[i].coins_y = row_y + S->coins_offset_y;
            computed[i].cards_x = col_x + S->right_cards_inset;
            computed[i].cards_y = row_y + S->cards_offset_y;
        } else {
            /* Left column: cards left, coins right (coins on own row) */
            computed[i].cards_x = col_x + S->text_inset;
            computed[i].cards_y = row_y + S->cards_offset_y;
            computed[i].coins_x = col_x + S->text_inset;
            computed[i].coins_y = row_y + S->coins_offset_y;
        }
    }

    for (i = 0; i < st->player_count; i++)
        if (st->players[i].is_self) { self_idx = i; break; }

    /* Clockwise: self+1, self+2, ... wrapping around */
    for (i = 1; i < st->player_count && opp < 6; i++)
        seat_player[opp++] = &st->players[(self_idx + i) % st->player_count];

    for (i = 0; i < 6; i++)
        render_single_seat(&computed[i], S, st, seat_player[i]);
}

/* ----- Reusable selection list helper ----- */

typedef struct {
    const char* label;   /* e.g. "Block with Duke" */
    uint32_t    color;   /* text color */
    bool        selected; /* multi-select: is this item toggled on? */
} coup_selection_item_t;

/**
 * Draws a titled selection list: header panel + accent line + title text
 * + item list with cursor ">" and COUP_PANEL_SELECT highlight.
 *
 * hint: if non-NULL, enables multi-select mode with [X]/[ ] markers
 *       and renders hint text below the item list.
 */
static void render_selection_list(
    const coup_phase_select_action_t* layout,
    const char* title, uint32_t title_color, uint32_t accent_color,
    const coup_selection_item_t* items, int count, int cursor,
    const char* hint)
{
    int i;
    char line[48];
    char title_buf[GAME_TITLE_MAX_CHARS + 1];
    bool multi = (hint != NULL);

    /* Title bar */
    panel_r(layout->title_bar, COUP_PANEL_HEADER);
    hline(layout->title_bar.x, layout->title_bar.y, layout->title_bar.w, accent_color);
    safe_copy(title_buf, title, sizeof(title_buf));
    CUI_DISPLAY()->draw_text_sprite(layout->title_text_x, layout->title_text_y, title_buf, title_color);

    for (i = 0; i < count; i++) {
        int py = layout->action_start_y + layout->items_offset_y + i * layout->item_spacing;
        bool at_cursor = (i == cursor);
        uint32_t color = items[i].color;
        const char* cur = at_cursor ? ">" : " ";

        if (multi) {
            /* Multi-select: toggled items always highlighted, cursor gets secondary */
            if (items[i].selected) {
                panel(layout->item_x, py, layout->item_w, layout->item_h, COUP_PANEL_SELECT);
            } else if (at_cursor) {
                panel(layout->item_x, py, layout->item_w, layout->item_h, COUP_PANEL_MID);
                color = COUP_TEXT_GREEN;
            }
            snprintf(line, sizeof(line), "%s%s %s",
                     cur, items[i].selected ? "[X]" : "[ ]", items[i].label);
        } else {
            /* Single-select: cursor item highlighted */
            if (at_cursor) {
                panel(layout->item_x, py, layout->item_w, layout->item_h, COUP_PANEL_SELECT);
                color = COUP_TEXT_GREEN;
            }
            snprintf(line, sizeof(line), "%s%s", cur, items[i].label);
        }

        CUI_DISPLAY()->draw_text_sprite(layout->item_x, py, line, color);
    }

    if (hint) {
        int hy = layout->action_start_y + layout->items_offset_y + count * layout->item_spacing + 4;
        CUI_DISPLAY()->draw_text_sprite(layout->title_text_x, hy, hint, COUP_TEXT_GRAY);
    }
}

/* ----- Timer countdown bar helper ----- */

static void render_timer_bar(const coup_phase_select_action_t* layout,
                             int item_count, int timer, int total,
                             uint32_t color)
{
    int bar_y = layout->action_start_y + layout->items_offset_y
                + item_count * layout->item_spacing;
    int bar_w;
    if (total <= 0) return;
    bar_w = (timer * layout->item_w) / total;
    if (bar_w > layout->item_w) bar_w = layout->item_w;
    panel(layout->item_x, bar_y, bar_w, layout->timer_bar_h, color);
}

/* ----- Phase renderers (rows 13-19) ----- */

static void render_phase_select_action(const coup_state_t* st)
{
    const coup_phase_select_action_t* SA = &COUP_UI.game.select_action;
    static const char* action_desc[COUP_NUM_ACTIONS] = {
        "+$1",
        "+$2",
        "-$7",
        "+$3",
        "-$3",
        "+$2",
        "Swap"
    };
    coup_selection_item_t items[COUP_NUM_ACTIONS];
    int cursor_index = 0;
    int i;

    for (i = 0; i < COUP_NUM_ACTIONS; i++) {
        static char labels[COUP_NUM_ACTIONS][32];
        int action_id = coup_action_display_order[i];
        bool available = (st->valid_actions & (1 << action_id)) != 0;

        snprintf(labels[i], sizeof(labels[i]), "%-12s %s",
                 coup_action_names[action_id], action_desc[action_id]);
        items[i].label = labels[i];
        items[i].color = available ? COUP_TEXT_WHITE : COUP_TEXT_GRAY;

        if (action_id == st->menu_cursor) {
            cursor_index = i;
        }
    }

    render_selection_list(SA, "Select Action:", COUP_TEXT_YELLOW, COUP_ACCENT_GOLD,
                          items, COUP_NUM_ACTIONS, cursor_index, NULL);
}

static void render_phase_select_target(const coup_state_t* st)
{
    const coup_phase_select_target_t* ST = &COUP_UI.game.select_target;
    char line[48];
    char title_buf[GAME_TITLE_MAX_CHARS + 1];
    int opp_idx = 0;
    int i;
    const char* act_name = (st->declared_action < COUP_NUM_ACTIONS)
                           ? coup_action_names[st->declared_action] : "???";

    panel_r(ST->title_bar, COUP_PANEL_HEADER);
    hline(ST->title_bar.x, ST->title_bar.y, ST->title_bar.w, COUP_ACCENT_BLUE);
    snprintf(line, sizeof(line), "%s - Target:", act_name);
    safe_copy(title_buf, line, sizeof(title_buf));
    CUI_DISPLAY()->draw_text_sprite(ST->title_text_x, ST->title_text_y, title_buf, COUP_TEXT_YELLOW);

    for (i = 0; i < st->player_count; i++) {
        const coup_player_t* p = &st->players[i];
        int py;
        const char* cursor;
        uint32_t color;

        if (p->is_self || !p->alive) continue;

        py = ST->item_base_y + opp_idx * ST->item_spacing;
        cursor = (opp_idx == st->target_cursor) ? ">" : " ";
        color = (opp_idx == st->target_cursor) ? COUP_TEXT_BLUE : COUP_TEXT_WHITE;

        if (opp_idx == st->target_cursor) {
            panel(ST->item_x, py, ST->item_w, ST->item_h, COUP_PANEL_PROMPT);
        }

        snprintf(line, sizeof(line), " %s %-12s $%d", cursor, p->name, p->coins);
        CUI_DISPLAY()->draw_text_sprite(ST->item_x, py + ST->item_text_offset_y, line, color);

        opp_idx++;
    }

    /* [B] Back hint below item list */
    {
        int hint_y = ST->item_base_y + opp_idx * ST->item_spacing + 4;
        CUI_DISPLAY()->draw_text_sprite(ST->title_text_x, hint_y, "[B] Back", COUP_TEXT_GRAY);
    }
}

static void render_phase_challenge_wait(const coup_state_t* st)
{
    const coup_phase_select_action_t* SA = &COUP_UI.game.select_action;
    char title[48];
    const char* actor_name = "";
    const char* claim_name;
    int ai;
    coup_selection_item_t items[2];

    for (ai = 0; ai < st->player_count; ai++) {
        if (st->players[ai].id == st->declared_actor) {
            actor_name = st->players[ai].name;
            break;
        }
    }
    claim_name = (st->declared_claim < COUP_NUM_CHARACTERS)
                  ? coup_char_names[st->declared_claim] : "???";
    snprintf(title, sizeof(title), "%.10s claims %s", actor_name, claim_name);

    items[0].label = "Allow";
    items[0].color = COUP_TEXT_WHITE;
    items[1].label = "Challenge";
    items[1].color = COUP_TEXT_RED;

    render_selection_list(SA, title, COUP_TEXT_YELLOW, COUP_ACCENT_RED,
                          items, 2, st->menu_cursor, NULL);
    render_timer_bar(SA, 2, st->response_timer, st->response_timeout, COUP_ACCENT_RED);
}

static void render_phase_block_wait(const coup_state_t* st)
{
    const coup_phase_select_action_t* SA = &COUP_UI.game.select_action;
    char title[48];
    const char* actor_name = "";
    const char* act_name;
    int ai, i;
    int count = 1 + st->block_claim_count; /* Allow + block options */
    coup_selection_item_t items[4]; /* Allow + up to 3 block chars */
    static char block_labels[3][24];

    for (ai = 0; ai < st->player_count; ai++) {
        if (st->players[ai].id == st->declared_actor) {
            actor_name = st->players[ai].name;
            break;
        }
    }
    act_name = (st->declared_action < COUP_NUM_ACTIONS)
                ? coup_action_names[st->declared_action] : "???";

    if (st->declared_action == COUP_ACT_FOREIGN_AID) {
        snprintf(title, sizeof(title), "%.10s: Foreign Aid", actor_name);
    } else {
        snprintf(title, sizeof(title), "%.10s: %s", actor_name, act_name);
    }

    items[0].label = "Allow";
    items[0].color = COUP_TEXT_WHITE;

    for (i = 0; i < st->block_claim_count && i < 3; i++) {
        uint8_t ch = st->block_claim_chars[i];
        const char* name = (ch < COUP_NUM_CHARACTERS) ? coup_char_names[ch] : "???";
        snprintf(block_labels[i], sizeof(block_labels[i]), "Block as %s", name);
        items[1 + i].label = block_labels[i];
        items[1 + i].color = coup_char_text_color(ch);
    }

    render_selection_list(SA, title, COUP_TEXT_YELLOW, COUP_ACCENT_RED,
                          items, count, st->menu_cursor, NULL);
    render_timer_bar(SA, count, st->response_timer, st->response_timeout, COUP_ACCENT_RED);
}

static void render_phase_block_challenge(const coup_state_t* st)
{
    const coup_phase_select_action_t* SA = &COUP_UI.game.select_action;
    char title[48];
    const char* blocker_name = "";
    const char* block_char_name;
    int bi;
    coup_selection_item_t items[2];

    for (bi = 0; bi < st->player_count; bi++) {
        if (st->players[bi].id == st->blocker_id) {
            blocker_name = st->players[bi].name;
            break;
        }
    }
    block_char_name = (st->block_claim < COUP_NUM_CHARACTERS)
                       ? coup_char_names[st->block_claim] : "???";
    snprintf(title, sizeof(title), "%.10s blocks w/ %s", blocker_name, block_char_name);

    items[0].label = "Allow";
    items[0].color = COUP_TEXT_WHITE;
    items[1].label = "Challenge";
    items[1].color = COUP_TEXT_RED;

    render_selection_list(SA, title, COUP_TEXT_YELLOW, COUP_ACCENT_PURPLE,
                          items, 2, st->menu_cursor, NULL);
    render_timer_bar(SA, 2, st->response_timer, st->response_timeout, COUP_ACCENT_PURPLE);
}

static void render_phase_lose_influence(const coup_state_t* st)
{
    const coup_phase_select_action_t* SA = &COUP_UI.game.select_action;
    coup_selection_item_t items[COUP_CARDS_PER_PLAYER];
    static char labels[COUP_CARDS_PER_PLAYER][32];
    int count = 0;
    int cursor_index = 0;
    int ci;

    for (ci = 0; ci < COUP_CARDS_PER_PLAYER; ci++) {
        int ch = st->my_cards[ci];
        const char* cname;

        if (ch == COUP_CHAR_NONE) continue;

        cname = (ch < COUP_NUM_CHARACTERS) ? coup_char_names[ch] : "???";
        snprintf(labels[count], sizeof(labels[count]), " %-12s", cname);
        items[count].label = labels[count];
        items[count].color = coup_char_text_color(ch);
        items[count].selected = false;

        if (count == st->lose_cursor)
            cursor_index = count;
        count++;
    }

    render_selection_list(SA, "Lose Influence:", COUP_TEXT_RED, COUP_ACCENT_RED,
                          items, count, cursor_index, NULL);
}

static void render_phase_exchange_pick(const coup_state_t* st)
{
    const coup_phase_select_action_t* SA = &COUP_UI.game.select_action;
    coup_selection_item_t items[4];
    static char labels[4][32];
    char title[48];
    char hint[48];
    int i, ci;
    int sel_count = 0;
    int cards_to_keep = 0;

    /* Count alive cards to determine how many to keep */
    for (ci = 0; ci < COUP_CARDS_PER_PLAYER; ci++) {
        if (st->my_cards[ci] != COUP_CHAR_NONE)
            cards_to_keep++;
    }

    /* Count current selections */
    if (st->exchange_sel[0] >= 0) sel_count++;
    if (st->exchange_sel[1] >= 0) sel_count++;

    snprintf(title, sizeof(title), "Keep %d of %d cards:", cards_to_keep, st->exchange_count);
    snprintf(hint, sizeof(hint), " %d/%d  [A] Toggle", sel_count, cards_to_keep);

    for (i = 0; i < st->exchange_count && i < 4; i++) {
        int ch = st->exchange_cards[i];
        const char* cname = (ch < COUP_NUM_CHARACTERS) ? coup_char_names[ch] : "???";

        snprintf(labels[i], sizeof(labels[i]), " %-12s", cname);
        items[i].label = labels[i];
        items[i].color = coup_char_text_color(ch);
        items[i].selected = (st->exchange_sel[0] == i) || (st->exchange_sel[1] == i);
    }

    render_selection_list(SA, title, COUP_TEXT_YELLOW, COUP_ACCENT_BLUE,
                          items, st->exchange_count < 4 ? st->exchange_count : 4,
                          st->exchange_cursor, hint);
}

static void render_phase_idle_resolving(const coup_state_t* st)
{
    const coup_phase_idle_t* ID = &COUP_UI.game.idle;

    panel_r(ID->panel, COUP_PANEL_DARK);

    char line[48];

    if (st->is_spectator) {
        snprintf(line, sizeof(line), "SPECTATING");
        CUI_DISPLAY()->draw_text_sprite(ID->text_x, ID->text_y, line, COUP_TEXT_YELLOW);
        return;
    }

    const char* turn_name = "";
    int ti;
    bool is_my_turn;
    for (ti = 0; ti < st->player_count; ti++) {
        if (st->players[ti].id == st->current_turn_id) {
            turn_name = st->players[ti].name;
            break;
        }
    }
    is_my_turn = (st->my_id == st->current_turn_id);
    if (is_my_turn) {
        snprintf(line, sizeof(line), "Waiting for decision...");
        CUI_DISPLAY()->draw_text_sprite(ID->text_x, ID->text_y, line, COUP_TEXT_GREEN);
    } else {
        snprintf(line, sizeof(line), "Waiting for %s...", turn_name);
        CUI_DISPLAY()->draw_text_sprite(ID->text_x, ID->text_y, line, COUP_TEXT_GRAY);
    }
}

/* ----- All Characters: animated sprite display (right side) ----- */

static void render_your_hand(const coup_state_t* st)
{
    const coup_game_hand_layout_t* H = &COUP_UI.game.hand;
    const coup_player_t* self = find_self(st);
    bool is_my_turn = (self && self->id == st->current_turn_id);
    uint32_t fill_color = is_my_turn ? COUP_PANEL_MY_TURN : COUP_PANEL_DARK;

    /* Hand panel (grid gaps provide borders) */
    panel_r(H->panel, fill_color);

    if (!self) return;

    if (!self->alive) {
        CUI_DISPLAY()->draw_text_sprite(H->name_x, H->name_y, self->name, COUP_TEXT_GRAY);
        CUI_DISPLAY()->draw_text_sprite(H->coins_x, H->coins_y, "DEAD", COUP_TEXT_RED);
        return;
    }

    /* Player name (green highlight when it's our turn) */
    {
        uint32_t name_color = is_my_turn ? COUP_TEXT_GREEN : COUP_TEXT_WHITE;
        CUI_DISPLAY()->draw_text_sprite(H->name_x, H->name_y, self->name, name_color);
    }

    /* Card 0 */
    {
        int c0 = st->my_cards[0];
#ifdef __SATURN__
        if (coup_anim_loaded() && c0 < COUP_NUM_CHARACTERS) {
            int frame = (st->frame_count / 8 + c0 * 5) % COUP_ANIM_FRAMES;
            coup_anim_draw(c0, frame, H->card0_x, H->card0_y);
        } else {
            uint32_t color = (c0 < COUP_NUM_CHARACTERS) ? coup_card_color(c0) : COUP_PANEL_MID;
            panel(H->card0_x, H->card0_y, 32, 48, color);
        }
#else
        {
            uint32_t color = (c0 < COUP_NUM_CHARACTERS) ? coup_card_color(c0) : COUP_PANEL_MID;
            panel(H->card0_x, H->card0_y, 32, 48, color);
        }
#endif
        if (c0 < COUP_NUM_CHARACTERS) {
            CUI_DISPLAY()->draw_text_sprite(H->label0_x, H->label0_y,
                coup_char_short[c0], coup_char_text_color(c0));
        }
    }

    /* Card 1 */
    {
        int c1 = st->my_cards[1];
#ifdef __SATURN__
        if (coup_anim_loaded() && c1 < COUP_NUM_CHARACTERS) {
            int frame = (st->frame_count / 8 + c1 * 5) % COUP_ANIM_FRAMES;
            coup_anim_draw(c1, frame, H->card1_x, H->card1_y);
        } else {
            uint32_t color = (c1 < COUP_NUM_CHARACTERS) ? coup_card_color(c1) : COUP_PANEL_MID;
            panel(H->card1_x, H->card1_y, 32, 48, color);
        }
#else
        {
            uint32_t color = (c1 < COUP_NUM_CHARACTERS) ? coup_card_color(c1) : COUP_PANEL_MID;
            panel(H->card1_x, H->card1_y, 32, 48, color);
        }
#endif
        if (c1 < COUP_NUM_CHARACTERS) {
            CUI_DISPLAY()->draw_text_sprite(H->label1_x, H->label1_y,
                coup_char_short[c1], coup_char_text_color(c1));
        }
    }

    /* Coins display */
    {
        char coin_str[8];
#ifdef __SATURN__
        if (coup_sprites_loaded()) {
            coup_sprites_draw(COUP_SPR_COIN, H->coin_sprite_x, H->coin_sprite_y);
        }
#endif
        snprintf(coin_str, sizeof(coin_str), "$%d", self->coins);
        CUI_DISPLAY()->draw_text_sprite(H->coins_x, H->coins_y, coin_str, COUP_TEXT_YELLOW);
    }
}

/* ----- Game log (center-top area) ----- */

static void render_game_log(const coup_state_t* st)
{
    const coup_game_log_layout_t* GL = &COUP_UI.game.log;
    int i;
    int visible_lines = (st->log_count < GL->max_visible) ? st->log_count : GL->max_visible;
    int scroll = st->log_scroll;

    /* Clamp scroll to valid range */
    {
        int max_scroll = st->log_count - GL->max_visible;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll > max_scroll) scroll = max_scroll;
    }

    /* Scroll indicator: show arrows when scrollable */
    if (scroll > 0) {
        CUI_DISPLAY()->draw_text_sprite(GL->scroll_arrow_x,
            GL->base_y + (GL->max_visible - 1) * GL->spacing, "v", COUP_TEXT_YELLOW);
    }
    if (st->log_count > GL->max_visible && scroll < st->log_count - GL->max_visible) {
        CUI_DISPLAY()->draw_text_sprite(GL->scroll_arrow_x,
            GL->base_y, "^", COUP_TEXT_YELLOW);
    }

    /* Show log lines with scroll offset applied */
    for (i = 0; i < visible_lines; i++) {
        int ring_idx;
        int py = GL->base_y + i * GL->spacing;
        int age;
        uint32_t log_color;

        if (st->log_count <= GL->max_visible) {
            ring_idx = (st->log_head + i) % COUP_LOG_LINES;
        } else {
            int start = (st->log_head + st->log_count - GL->max_visible - scroll) % COUP_LOG_LINES;
            ring_idx = (start + i) % COUP_LOG_LINES;
        }

        age = visible_lines - 1 - i;
        log_color = (age == 0 && scroll == 0) ? COUP_TEXT_WHITE : COUP_TEXT_GRAY;
        CUI_DISPLAY()->draw_text_sprite(GL->text_x, py, st->log[ring_idx], log_color);
    }
}

/* ----- Corner shortcut (bottom-right "Rules") ----- */

static void render_corners(void)
{
    const coup_game_corners_layout_t* C = &COUP_UI.game.corners;

    panel_r(C->right_panel, COUP_PANEL_STATUS);
    CUI_DISPLAY()->draw_text_sprite(C->right_text_x, C->right_text_y, "Rules", COUP_TEXT_GRAY);
}

/* ----- Main game screen compositor ----- */

static void coup_render_game(const coup_state_t* st)
{
    /* Grid/border background — gaps between panels reveal this color */
    panel_r(COUP_UI.title.bg, COUP_BG_GRID);

    /* Split center panels */
    panel_r(COUP_UI.game.log_panel, COUP_BG_DARK);
    panel_r(COUP_UI.game.center_panel, COUP_BG_DARK);

    /* === 1. Opponent seats (left/right columns) === */
    render_seats(st);

    /* === 2. Game log (top center) === */
    render_game_log(st);

    /* === 3. Phase content (mid center) === */
    switch (st->phase) {
    case COUP_PHASE_SELECT_ACTION:
        render_phase_select_action(st);
        break;
    case COUP_PHASE_SELECT_TARGET:
        render_phase_select_target(st);
        break;
    case COUP_PHASE_CHALLENGE_WAIT:
        render_phase_challenge_wait(st);
        break;
    case COUP_PHASE_BLOCK_WAIT:
        render_phase_block_wait(st);
        break;
    case COUP_PHASE_BLOCK_CHALLENGE:
        render_phase_block_challenge(st);
        break;
    case COUP_PHASE_LOSE_INFLUENCE:
        render_phase_lose_influence(st);
        break;
    case COUP_PHASE_EXCHANGE_PICK:
        render_phase_exchange_pick(st);
        break;
    case COUP_PHASE_IDLE:
    case COUP_PHASE_RESOLVING:
    default:
        render_phase_idle_resolving(st);
        break;
    }

    /* === 4. Your hand (center-bottom, outlined panel) === */
    render_your_hand(st);

    /* === 5. Corner shortcuts === */
    render_corners();
}

/*============================================================================
 * 6. GAME OVER SCREEN
 *
 * VDP1 rects: ~15
 *============================================================================*/

static void coup_render_game_over(const coup_state_t* st)
{
    const coup_gameover_layout_t* GO = &COUP_UI.gameover;
    char line[48];
    const char* winner_name;
    int name_len, text_x;

    /* Use snapshot taken at game-over time (immune to LOBBY_STATE overwrites) */
    winner_name = st->winner_name[0] ? st->winner_name : "Unknown";

#ifdef __SATURN__
    /* Full-screen game over background image */
    if (coup_gameover_loaded()) {
        coup_gameover_draw();
    } else {
        panel_r(GO->bg, COUP_BG_DARK);
    }
#else
    panel_r(GO->bg, COUP_BG_DARK);
    draw_at(GO->gameover_col, GO->gameover_row, "     GAME  OVER", COUP_TEXT_RED);
#endif

    /* Build "WINNER_NAME WINS!" string */
    {
        int si = 0, di = 0;
        /* Copy name in uppercase */
        while (winner_name[si] && di < 40) {
            char c = winner_name[si++];
            if (c >= 'a' && c <= 'z') c -= 32;
            line[di++] = c;
        }
        /* Append " WINS!" */
        {
            const char* suffix = " WINS!";
            int sfi = 0;
            while (suffix[sfi] && di < 46) {
                line[di++] = suffix[sfi++];
            }
        }
        line[di] = '\0';
        name_len = di;
    }

    /* Center the winner text horizontally */
    text_x = (40 - name_len) / 2;
    if (text_x < 0) text_x = 0;
    draw_at(text_x, GO->winner_row, line, COUP_TEXT_GOLD);

#ifndef __SATURN__
    /* Non-Saturn: show instructions since there's no background image */
    draw_at(GO->return_col, GO->return_row, "  [A] Return to Lobby", COUP_TEXT_WHITE);
#endif
}

/*============================================================================
 * 7. OFFLINE SCREEN
 *============================================================================*/

/*============================================================================
 * Public entry point
 *============================================================================*/

void coup_render_screen(const coup_state_t* st)
{
    if (!st) return;

    /* Begin frame with dark background */
    CUI_DISPLAY()->begin_frame(COUP_BG_DARK);

    switch (st->screen) {
    case COUP_SCREEN_TITLE:
        coup_render_title(st);
        break;
    case COUP_SCREEN_SETTINGS:
        coup_render_settings(st);
        break;
    case COUP_SCREEN_RULES:
        coup_render_rules(st);
        break;
    case COUP_SCREEN_CONNECTING:
        coup_render_connecting(st);
        break;
    case COUP_SCREEN_NAME_ENTRY:
        coup_render_name_entry(st);
        break;
    case COUP_SCREEN_LOBBY:
        coup_render_lobby(st);
        break;
    case COUP_SCREEN_GAME:
        coup_render_game(st);
        break;
    case COUP_SCREEN_GAME_OVER:
        coup_render_game_over(st);
        break;
    default:
        draw_at(10, 14, "Unknown screen", COUP_TEXT_RED);
        break;
    }

    CUI_DISPLAY()->end_frame();
}
