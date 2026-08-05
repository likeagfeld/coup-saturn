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
#include "coup_fx_loader.h"
#include "coup_fx_data.h"
#include "saturn_pal.h"          /* cui_saturn_font_set_active */
#include "saturn_font.h"         /* saturn_font_registry_t, advance_x */
#include "saturn_bg.h"
#include "saturn_fade.h"
#include "saturn_vdp1.h"
#include "saturn_vdp2.h"
#include "coup_bg_index.h"  /* COUP_BG_SCENE_* */
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

#ifdef __SATURN__
/* Gouraud pool slots, uploaded once by coup_render_init_shading(). */
enum {
    COUP_GRD_PANEL = 0,   /* soft top-lit panel   */
    COUP_GRD_RAISED,      /* stronger, for plates */
    COUP_GRD_COUNT
};

/**
 * Upload the gradient tables. Call once after the PAL is up.
 *
 * Gouraud is a write-only VDP1 mode, so a shaded panel costs exactly what the
 * flat rectangle it replaces cost - the gradients below are free.
 */
void coup_render_init_shading(void)
{
    uint16_t tbl[4];

    saturn_vdp1_gouraud_vshade(tbl, +5, -5);
    saturn_vdp1_set_gouraud_table(COUP_GRD_PANEL, tbl);

    saturn_vdp1_gouraud_vshade(tbl, +9, -8);
    saturn_vdp1_set_gouraud_table(COUP_GRD_RAISED, tbl);
}
#endif

#ifdef __SATURN__
/**
 * Draw a panel lit from above.
 *
 * A gouraud-shaded polygon: same command count and same fill cost as the flat
 * rectangle it replaces, because gouraud is write-only. Falls back to a flat
 * fill if the VDP1 command budget is exhausted.
 *
 * Saturn-only: the gradient has no meaning on the other platforms, and both
 * call sites are already inside Saturn blocks.
 */
static void panel_lit(int x, int y, int w, int h, uint32_t color, int slot)
{
    uint16_t rgb555 = saturn_rgba_to_rgb555(color);
    if (saturn_vdp1_draw_rect_gouraud(x, y, w, h, rgb555, slot)) {
        return;
    }
    CUI_DISPLAY()->draw_rect(x, y, w, h, color);
}
#endif

/**
 * Width in pixels of `s` in the currently active sprite font.
 *
 * MEASURED: the 16x16 Alagard display face has advance_x = 8, NOT 16. Assuming
 * the advance matches the cell size put every label 16 px left of centre on its
 * button. Always ask the font for its advance rather than inferring it from the
 * cell.
 */
static int text_px_w(const char* s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
#ifdef __SATURN__
    {
        const saturn_font_registry_t* reg = cui_saturn_font_get_registry();
        const saturn_font_entry_t* e = reg ? saturn_font_get_active_entry(reg)
                                           : 0;
        int adv = (e && e->desc.advance_x > 0) ? e->desc.advance_x
                                               : COUP_FONT_ADVANCE;
        return n * adv;
    }
#else
    return n * COUP_FONT_ADVANCE;
#endif
}

int coup_centre_x(int container_w, int text_w)
{
    int x = (container_w - text_w) / 2;

    /* A label wider than its container clamps left. A negative x would wrap
     * on VDP1 rather than clip, putting the label on the far right. */
    return x < 0 ? 0 : x;
}

/**
 * Draw `text` centred inside the pixel span `x`..`x + w`, at pixel row `y`.
 *
 * The grid-row form below centres on the whole screen, which is right for a
 * heading but wrong for a label that belongs to a specific plate.
 */
static void draw_centered_in(int x, int w, int y, const char* text,
                             uint32_t color)
{
    CUI_DISPLAY()->draw_text_sprite(x + coup_centre_x(w, text_px_w(text)), y,
                                    text, color);
}

/**
 * Draw `text` horizontally centred on screen at grid row `row`.
 *
 * Replaces the older habit of padding a string literal with leading spaces to
 * push it towards the middle. That padding is only correct for the exact
 * string and the exact advance in place when it was counted - change either
 * and the label drifts, silently. Here the offset is derived from the string
 * actually being drawn.
 */
static void draw_centered(int row, const char* text, uint32_t color)
{
    CUI_DISPLAY()->draw_text_sprite(coup_centre_x(COUP_SCREEN_W,
                                                 text_px_w(text)),
                                    row * COUP_FONT_ROW_H, text, color);
}

#ifdef __SATURN__
/**
 * Draw a brass-framed plate with its label centred both ways.
 *
 * Centring is computed from the measured label width, so it stays correct if
 * the font, the text or the padding changes.
 */
static void button_centered(int x, int y, int w, int h, const char* label,
                            int font, uint32_t fill, uint32_t text_col,
                            int grd_slot)
{
    int prev = cui_saturn_font_get_active();
    int tw, tx, ty;

    panel(x, y, w, h, COUP_FRAME_BRASS);
    panel_lit(x + 2, y + 2, w - 4, h - 4, fill, grd_slot);

    cui_saturn_font_set_active(font);
    tw = text_px_w(label);
    tx = x + (w - tw) / 2;
    ty = y + (h - COUP_FONT_ROW_H * 2) / 2;   /* 16 px display glyph */

    CUI_DISPLAY()->draw_text_sprite(tx, ty, label, text_col);
    cui_saturn_font_set_active(prev);
}
#endif

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

/**
 * Full-screen background fill.
 *
 * On Saturn the painted backdrop lives on VDP2 NBG1 at priority 3, beneath the
 * sprites and text. Filling the screen with a VDP1 polygon would both hide that
 * layer and waste a 320*224 = 71,680 px fill every frame - measured as half of
 * the game screen's entire cost. Every screen routes its background through
 * here so the suppression is in one place rather than seven.
 */
static void screen_bg(cui_rect_t r, uint32_t color)
{
#ifdef __SATURN__
    (void)r;
    (void)color;
#else
    panel_r(r, color);
#endif
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

/*============================================================================
 * Action effects
 *
 * The TRIGGER is pure logic and compiles on every platform, so it is unit
 * tested on the host (tests/coup/test_fx_trigger.c). Only the VDP1 draw is
 * Saturn-only. Effects are driven by OBSERVING state transitions; nothing in
 * coup_game.c or the protocol is touched, so the turnkey server contract in
 * spec section 8 is unaffected.
 *============================================================================*/

#define COUP_FX_HOLD_FRAMES 3

/* Sentinel for "no player", per coup_table_view.h. */
#define COUP_FX_NO_PLAYER 0xFF

/* Effect ids. Kept in step with COUP_FX_* in the generated header, which is
 * asserted at compile time on Saturn below. */
#define COUP_FXID_NONE        (-1)
#define COUP_FXID_COUP          0
#define COUP_FXID_ASSASSINATE   1
#define COUP_FXID_STEAL         2
#define COUP_FXID_TAX           3
#define COUP_FXID_EXCHANGE      4
#define COUP_FXID_BLOCK         5
#define COUP_FXID_CHALLENGE     6

/** Map a declared action to the effect that dramatises it. Pure. */
int coup_fx_for_action(int action)
{
    switch (action) {
    case COUP_ACT_COUP:         return COUP_FXID_COUP;
    case COUP_ACT_ASSASSINATE:  return COUP_FXID_ASSASSINATE;
    case COUP_ACT_STEAL:        return COUP_FXID_STEAL;
    case COUP_ACT_EXCHANGE:     return COUP_FXID_EXCHANGE;
    case COUP_ACT_TAX:
    case COUP_ACT_INCOME:
    case COUP_ACT_FOREIGN_AID:  return COUP_FXID_TAX;
    default:                    return COUP_FXID_NONE;
    }
}

/**
 * Decide which effect a state transition should fire, if any. Pure.
 *
 * @param prev  previously observed state
 * @param st    current state
 * @return effect id, or COUP_FXID_NONE
 */
int coup_fx_on_transition(const coup_fx_prev_t* prev, const coup_state_t* st)
{
    if (!prev || !st) {
        return COUP_FXID_NONE;
    }
    /* A block is the most specific event, so it wins over the action that
     * provoked it. */
    /* 0xFF means "no blocker" (coup_table_view.h). These fields are uint8_t,
     * so a >= 0 test would be vacuous. */
    if (st->blocker_id != COUP_FX_NO_PLAYER &&
        (int)st->blocker_id != prev->blocker_id) {
        return COUP_FXID_BLOCK;
    }
    if ((int)st->phase != prev->phase &&
        st->phase == COUP_PHASE_CHALLENGE_WAIT) {
        return COUP_FXID_CHALLENGE;
    }
    if (st->declared_actor != COUP_FX_NO_PLAYER &&
        (int)st->declared_action != prev->action) {
        return coup_fx_for_action((int)st->declared_action);
    }
    return COUP_FXID_NONE;
}

/** Record the state just observed, so the next call can diff against it. */
void coup_fx_remember(coup_fx_prev_t* prev, const coup_state_t* st)
{
    if (!prev || !st) {
        return;
    }
    prev->action = (int)st->declared_action;
    prev->phase = (int)st->phase;
    prev->blocker_id = st->blocker_id;
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
/**
 * Draw an opaque framed medallion behind an animated portrait.
 *
 * The portrait sprites are cut-outs, and their source art fades to black at
 * the bottom, so 56-74% of each sprite is transparent (MEASURED per character).
 * That was invisible while the backdrop was flat black, but over the painted
 * VDP2 background the characters lose their torsos and the damask shows
 * straight through them. An opaque backing removes the problem entirely and
 * reads as a framed portrait, which suits a game about court intrigue.
 *
 * Two VDP1 polygons per portrait: a brass rectangle and an inset fill, which
 * leaves a 2 px frame. Cheaper than drawing four separate edges, and at
 * 64x96 that is 12,288 px of fill per portrait.
 *
 * The frame is 2 px rather than 1 because a single-pixel edge does not survive
 * being scaled: the emulator renders into a 704-wide canvas and the capture is
 * area-downsampled back to 320, which blends a 1 px line into its neighbours
 * until the brass reads as grey. It is also simply more legible on a CRT.
 */
static void portrait_medallion(int x, int y, int w, int h)
{
    panel(x, y, w, h, COUP_FRAME_BRASS);
    panel_lit(x + 2, y + 2, w - 4, h - 4, COUP_PORTRAIT_BG, COUP_GRD_PANEL);
}

#ifdef __SATURN__
static int s_fx_active = COUP_FXID_NONE;
static int s_fx_tick = 0;
static int s_fx_x, s_fx_y;
static coup_fx_prev_t s_fx_prev = { -1, -1, -1 };

/** Observe, then start whatever the transition calls for. */
static void fx_observe(const coup_state_t* st)
{
    int fx = coup_fx_on_transition(&s_fx_prev, st);
    if (fx != COUP_FXID_NONE && coup_fx_loaded()) {
        s_fx_active = fx;
        s_fx_tick = 0;
        s_fx_x = (COUP_SCREEN_W - 64) / 2;
        s_fx_y = 84;
    }
    coup_fx_remember(&s_fx_prev, st);
}

/** Draw and advance the running effect, if any. */
static void fx_render(void)
{
    int frames, frame;

    if (s_fx_active == COUP_FXID_NONE) {
        return;
    }
    frames = coup_fx_frames(s_fx_active);
    if (frames <= 0) {
        s_fx_active = COUP_FXID_NONE;
        return;
    }
    frame = s_fx_tick / COUP_FX_HOLD_FRAMES;
    if (frame >= frames) {
        s_fx_active = COUP_FXID_NONE;
        return;
    }
    coup_fx_draw(s_fx_active, frame, s_fx_x, s_fx_y);
    s_fx_tick++;
}
#endif

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

#ifndef __SATURN__
    (void)st;  /* animated portrait parade is Saturn-only */
#endif

    /* === VDP1 LAYER === */

    /* 1. Full-screen dark background */
    screen_bg(L->bg, COUP_BG_DARK);

    /* 2. Title header panel (full width).
     *    Suppressed on Saturn: a filled plate across the sky would bury the
     *    skyline the backdrop art exists to show. The wordmark below is keyed,
     *    so it needs no plate to read against. */
#ifndef __SATURN__
    panel_r(L->header_panel, COUP_PANEL_HEADER);
#endif

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
            portrait_medallion(screen_x, L->portrait_y, 64, 96);
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

#ifdef __SATURN__
    /* 4. The COUP wordmark.
     *
     * This sprite was once removed on the theory that the backdrop already
     * contained a painted logo - a conclusion drawn from counting gold pixels
     * in the title band. That count was the sunset. Measuring STRUCTURE rather
     * than colour settles it: thresholded at the 90th percentile of
     * brightness, the wordmark art yields five letter-sized blobs sharing a
     * baseline (863/414/394/314/307 px), while the same band of B1_title.png
     * yields a single 1,753 px mass with nothing else above 41 px. A skyline,
     * not letterforms. The sprite is the only branding the screen has.
     *
     * Keyed on its dark backing, so no plate is drawn behind it. */
    if (coup_fx_loaded()) {
        coup_ui_draw(COUP_UI_WORDMARK, L->logo_pos.x, L->logo_pos.y);
    }
#endif

    /* === VDP2 LAYER: Text overlay === */

#ifndef __SATURN__
    /* ASCII-art fallback title for platforms without the sprite. */
    draw_at(L->ascii_col, L->ascii_start_row,     " ####  ####  #  # ####", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 1,  "#      #  #  #  # #  #", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 2,  "#      #  #  #  # ####", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 3,  "#      #  #  #  # #   ", COUP_TEXT_YELLOW);
    draw_at(L->ascii_col, L->ascii_start_row + 4,  " ####  ####  #### #   ", COUP_TEXT_YELLOW);
#endif

    /* Single centered "Play" button */
#ifdef __SATURN__
    {
        /* Plate sized from the MEASURED label width, not an assumed advance. */
        const int pad_x = 14, pad_y = 5;
        int tw, bw, bh, bx;

        cui_saturn_font_set_active(COUP_FONT_DISPLAY);
        tw = text_px_w("PLAY");
        cui_saturn_font_set_active(COUP_FONT_BODY);

        bw = tw + pad_x * 2;
        bh = COUP_FONT_ROW_H * 2 + pad_y * 2;
        bx = (COUP_SCREEN_W - bw) / 2;

        button_centered(bx, L->menu_y - pad_y, bw, bh, "PLAY",
                        COUP_FONT_DISPLAY, COUP_PANEL_SELECT,
                        COUP_TEXT_GOLD, COUP_GRD_RAISED);
    }
#else
    {
        int play_x = (COUP_SCREEN_W - 32) / 2;  /* center "Play" (4 chars * 8px) */
        panel(play_x - 8, L->menu_y - 2, 48, 12, COUP_PANEL_SELECT);
        CUI_DISPLAY()->draw_text_sprite(play_x, L->menu_y, "Play", COUP_TEXT_GREEN);
    }
#endif

    /* Bottom bar hint.
     *
     * It sits over the brightest part of the skyline - the reflective floor -
     * with nothing behind it. MEASURED at 40.4 ink-to-background contrast
     * against 95-135 everywhere else, the least legible text in the game, and
     * that was true before any blending. A small plate fixes it; blended
     * against the backdrop it reads as a scrim rather than a box. */
    {
        const char* hint = "[R] Rules";
        int tw = text_px_w(hint);
        int pad = 6;
        int hx = coup_centre_x(COUP_SCREEN_W, tw + pad * 2);
        int hy = L->hint_row * COUP_FONT_ROW_H;

        panel(hx, hy - 2, tw + pad * 2, COUP_FONT_ROW_H + 4, COUP_PANEL_DARK);
        CUI_DISPLAY()->draw_text_sprite(hx + pad, hy, hint, COUP_TEXT_WHITE);
    }
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
    screen_bg(L->bg, COUP_BG_DARK);

    /* Header */
    panel_r(L->header_panel, COUP_PANEL_HEADER);
    /* Centred by measurement. At col 14 this sat at x 112 with a width of
     * 64, centring on 144 - 16 px left of the panel, whose centre is 160. */
    draw_centered(L->header_row, "Settings", COUP_TEXT_YELLOW);

    /* Bot difficulty - single setting */
    panel_r(L->diff_panel, COUP_PANEL_MID);
    CUI_DISPLAY()->draw_text_sprite(L->diff_cursor_x, L->diff_y + L->diff_text_offset_y, ">", COUP_TEXT_GREEN);
    CUI_DISPLAY()->draw_text_sprite(L->diff_label_x, L->diff_y + L->diff_text_offset_y, "Bots", COUP_TEXT_WHITE);

    /* Difficulty display: three options with selection highlight */
    for (j = 0; j < 3; j++) {
        int dx = L->diff_option_x + j * L->diff_option_spacing;
        uint32_t text_col;

        if (j == diff) {
            panel(dx, L->diff_y, L->diff_option_w, L->diff_option_h,
                  COUP_PANEL_SELECT);
            text_col = diff_colors[j];
        } else {
            text_col = COUP_TEXT_GRAY;
        }
        /* Centred on the plate. The three labels are different widths, so a
         * shared left edge could only ever have suited one of them. */
        draw_centered_in(dx, L->diff_option_w,
                         L->diff_y + L->diff_text_offset_y,
                         diff_names[j], text_col);
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
    screen_bg(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
    /* The official rules table IS the backdrop on Saturn (COUP_BG_SCENE_RULES),
     * so the decorative tiles and per-page portrait would only obscure it.
     * Keep the page indicator and hints, which the art does not provide. */
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
    /* Two different strings shared one fixed column, so at most one of them
     * could ever have been centred; the leading space was a nudge for the
     * other. Measuring each string centres both. */
    draw_centered(L->header_row,
                  pg == 0 ? "CHARACTER REFERENCE" : "HOW TO PLAY COUP",
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
    screen_bg(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
#endif

    /* Connection panel */
    panel_r(L->main_panel, COUP_PANEL_HEADER);
    hline(L->main_panel.x, L->main_panel.y, L->main_panel.w, COUP_ACCENT_BLUE);
    hline(L->main_panel.x, L->main_panel.y + L->main_panel.h, L->main_panel.w, COUP_ACCENT_BLUE);
    vline(L->main_panel.x, L->main_panel.y, L->main_panel.h, COUP_ACCENT_BLUE);
    vline(L->main_panel.x + L->main_panel.w - 2, L->main_panel.y, L->main_panel.h, COUP_ACCENT_BLUE);

    /* Title, centred on the panel.
     *
     * It used to be drawn at a literal x of 80. "CONNECTING" is 10 characters
     * at an 8 px advance, so it spanned 80..160 and centred on 120 - while
     * the gold rule immediately beneath it spans 80..240 and centres on 160.
     * The heading was 40 px left of its own underline. */
    draw_centered_in(L->main_panel.x, L->main_panel.w, L->title_pos.y,
                     "CONNECTING", COUP_TEXT_YELLOW);
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
    draw_centered_in(L->cancel_panel.x, L->cancel_panel.w,
                     L->cancel_text_pos.y, "[B] Cancel", COUP_BTN_B_COLOR);

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
    screen_bg(L->bg, COUP_BG_DARK);

#ifdef __SATURN__
    /* The official card back. The legacy tile row that used to run behind it
     * was decorative filler designed for a flat dark screen; over painted
     * backdrop art it is clutter, so it is gone from every screen that now
     * has a real scene. */
    if (coup_fx_loaded()) {
        coup_ui_draw(COUP_UI_CARD_BACK, L->sprite_pos.x, L->sprite_pos.y);
    } else if (coup_sprites_loaded()) {
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

    draw_centered(L->header_row, "ENTER YOUR NAME", COUP_TEXT_YELLOW);

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
        snprintf(indicator, sizeof(indicator), "^  [%c]  v", cur_char);
        draw_centered(L->indicator_row, indicator, COUP_TEXT_YELLOW);
    }

    /* Controls */
    draw_at(L->ctrl_col, L->ctrl_start_row, "UP/DOWN: Change Letter", COUP_TEXT_GRAY);
    draw_at(L->ctrl_col, L->ctrl_start_row + 1, "LEFT/RIGHT: Move Cursor", COUP_TEXT_GRAY);
    draw_centered(L->submit_row, "[A] Submit   [B] Delete", COUP_TEXT_WHITE);
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
    screen_bg(L->bg, COUP_BG_DARK);

    /* No decorative tile row: the lobby has its own painted backdrop now, and
     * the tiles were filler for a flat dark screen. */

    /* Header bar */
    panel_r(L->header_bar, COUP_PANEL_HEADER);
    hline(L->header_bar.x, L->header_bar.y + L->header_bar.h - 2, L->header_bar.w,
          offline ? COUP_ACCENT_BLUE : COUP_ACCENT_GOLD);

    /* Player slots area */
    panel_r(L->player_area, COUP_PANEL_DARK);
    hline(L->player_area.x, L->player_area.y, L->player_area.w, COUP_ACCENT_DIM);

    /* Header text */
    /* Two different strings shared column 0 with a leading space as a nudge,
     * so neither was centred and they could not both be. Measured, they are
     * 13 and 19 characters - the difference is 48 px. */
    draw_centered_in(L->header_bar.x, L->header_bar.w, L->header_bar.y + 2,
                     offline ? "COUP - LOBBY" : "COUP - WAITING ROOM",
                     COUP_TEXT_YELLOW);

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
            /* Was drawn at a literal x of 80: 15 characters, 120 px, centring
             * on 140 when the header panel centres on 160. */
            draw_centered_in(hdr.x, hdr.w, 50, "ENTER YOUR NAME",
                             COUP_TEXT_YELLOW);
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
            snprintf(indicator, sizeof(indicator), "^  [%c]  v", cur_char);
            {
                int ix = 52 + (160 - text_px_w(indicator)) / 2;
                CUI_DISPLAY()->draw_text_sprite(ix, 96, indicator,
                                                COUP_TEXT_YELLOW);
            }
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
            /* The leading space put this one character right of the two
             * lines above it, which start at 56. */
            CUI_DISPLAY()->draw_text_sprite(56, 152, "[A] Submit   [B] Delete",
                                            COUP_TEXT_WHITE);
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

        CUI_DISPLAY()->draw_text_sprite(layout->item_x + COUP_ITEM_TEXT_INSET,
                                        py, line, color);
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
        CUI_DISPLAY()->draw_text_sprite(ST->item_x + COUP_ITEM_TEXT_INSET,
                                        py + ST->item_text_offset_y, line,
                                        color);

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
            portrait_medallion(H->card0_x, H->card0_y, 32, 48);
            /* Portraits are authored at 64x96; the hand slots are
             * 32x48, so VDP1 scales them down here. */
            coup_anim_draw_scaled(c0, frame, H->card0_x,
                                  H->card0_y, 32, 48);
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
            portrait_medallion(H->card1_x, H->card1_y, 32, 48);
            /* Portraits are authored at 64x96; the hand slots are
             * 32x48, so VDP1 scales them down here. */
            coup_anim_draw_scaled(c1, frame, H->card1_x,
                                  H->card1_y, 32, 48);
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
        /* Tiered coin stack rather than a single coin: the art has 1/2/3/5/10
         * variants, so the pile visibly grows with the player's holdings. */
        if (coup_fx_loaded()) {
            coup_ui_draw_coins(self->coins, H->coin_sprite_x, H->coin_sprite_y);
        } else if (coup_sprites_loaded()) {
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
    screen_bg(COUP_UI.title.bg, COUP_BG_GRID);

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

    /* === 3b. Action effect over the table === */
#ifdef __SATURN__
    fx_observe(st);
    fx_render();
#endif

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
    /* No fill at all. The backdrop for this screen is the streamed VICTORY or
     * DEFEAT scene, chosen in the scene switch by the same win test the
     * banner below uses. Anything drawn here would cover it. */
    (void)GO;
#else
    panel_r(GO->bg, COUP_BG_DARK);
    draw_centered(GO->gameover_row, "GAME OVER", COUP_TEXT_RED);
#endif

#ifdef __SATURN__
    /* VICTORY or DEFEAT banner from the official art, centred above the text.
     * The banner is 128x32; centring is computed, not hard-coded. */
    if (coup_fx_loaded()) {
        const coup_player_t* me = find_self(st);
        int won = (me && me->id == st->winner_id);
        coup_ui_draw(won ? COUP_UI_VICTORY : COUP_UI_DEFEAT,
                     (COUP_SCREEN_W - 128) / 2, 40);
    }
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
    draw_centered(GO->return_row, "[A] Return to Lobby", COUP_TEXT_WHITE);
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

#ifdef __SATURN__
    /* Fade each new screen in from black.
     *
     * VDP2 colour offset dims the backdrop, the sprites and the text together
     * and costs no VDP1 commands, so it is safe to run on any frame. Only a
     * fade-IN is driven here: fading out would have to delay the screen change
     * itself, which would mean holding up input and the network poll. */
    {
        static int s_last_screen = -1;

        /* Refresh the gouraud tables every frame.
         *
         * MEASURED: uploading them once at init left the panels shaded by
         * garbage - banded, hue-shifted stripes rather than a smooth ramp -
         * and relocating the pool only changed WHICH garbage appeared. Some
         * other writer reaches this region between init and draw. Two tables
         * is 16 VRAM writes per frame, which is far cheaper than diagnosing
         * the interloper, and makes the shading independent of whatever else
         * touches VDP1 VRAM. */
        coup_render_init_shading();

        if ((int)st->screen != s_last_screen) {
            s_last_screen = (int)st->screen;

            /* Pick the backdrop for this screen. saturn_bg_set_scene skips the
             * VRAM copy when the requested scene is already resident, so this
             * is free on same-scene transitions. */
            {
                int scene;
                switch (st->screen) {
                case COUP_SCREEN_GAME:
                    scene = COUP_BG_SCENE_GAME;
                    break;
                case COUP_SCREEN_RULES:
                    scene = COUP_BG_SCENE_RULES;
                    break;
                case COUP_SCREEN_LOBBY:
                    scene = COUP_BG_SCENE_LOBBY;
                    break;
                case COUP_SCREEN_CONNECTING:
                    scene = COUP_BG_SCENE_CONNECTING;
                    break;
                case COUP_SCREEN_GAME_OVER: {
                    /* The outcome decides the backdrop. Same test the VICTORY
                     * / DEFEAT banner uses, so art and backdrop can never
                     * disagree about who won. */
                    const coup_player_t* me = find_self(st);
                    scene = (me && me->id == st->winner_id)
                            ? COUP_BG_SCENE_VICTORY
                            : COUP_BG_SCENE_DEFEAT;
                    break;
                }
                /* Title, settings and name entry share the title art
                 * deliberately - they are one continuous front-end. */
                default:
                    scene = COUP_BG_SCENE_TITLE;
                    break;
                }
                saturn_bg_set_scene(scene);
            }

            saturn_fade_start(SATURN_FADE_BLACK, SATURN_FADE_NONE, 12, false);
        }
        saturn_fade_tick();
    }
#endif

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
