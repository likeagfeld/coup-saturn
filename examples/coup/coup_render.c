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
#include "coup_shading.h"
#ifdef __SATURN__
#include "saturn_linescroll.h"
#include "saturn_coinfx.h"
#endif
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
#include "saturn_distort.h"      /* card flip + mesh dissolve */
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

/* Gouraud pool slots. Outside the __SATURN__ guard because they are just
 * integer identifiers and the shared screen code names them when it asks for
 * a gradient - off-Saturn panel_grd() ignores the value. */
enum {
    COUP_GRD_PANEL = 0,   /* soft top-lit panel   */
    COUP_GRD_RAISED,      /* stronger, for plates */

    /* Animated slots. Re-uploaded every frame from coup_shading.c, which
     * generates them as pure functions of a phase counter so the motion is
     * host-testable (tests/coup/test_coup_shading.c). Gouraud is write-only
     * on VDP1, so an animated gradient costs exactly what a flat fill costs;
     * the only per-frame price is 8 bytes of table upload each. */
    COUP_GRD_SHEEN,       /* travelling highlight, title wordmark  */
    COUP_GRD_HALO,        /* amber breath, seat whose turn it is   */
    COUP_GRD_PULSE,       /* lobby slot that has readied up        */
    COUP_GRD_OCCUPIED,    /* lobby slot with a player in it        */
    COUP_GRD_EMPTY,       /* lobby slot with nobody in it          */
    COUP_GRD_SPOTLIGHT,   /* winner's portrait on the game over    */
    COUP_GRD_TIMER,       /* response timer, green -> amber -> red */
    COUP_GRD_COUNT
};

/* Mid-grey, RGBA like every other colour here - panel_lit() runs it through
 * saturn_rgba_to_rgb555(), so a raw RGB555 word would be misread as RGBA.
 * 0x80 per channel lands on 16 of 31 after the >>3, which is exactly the
 * midpoint: gouraud corrections are ADDED to the source and clamp at 0x1F,
 * so only a mid base leaves the full -16..+15 range reachable both ways. */
#define COUP_TIMER_BASE 0x808080FFu


#ifdef __SATURN__

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

/**
 * Re-upload the ANIMATED gradients for this frame.
 *
 * Split from coup_render_init_shading() because these depend on a phase and
 * the static two do not. Six tables is 48 VRAM writes per frame; the draws
 * that use them cost nothing extra, because gouraud is a write-only VDP1 mode
 * (ST-013-R3 section 5.3) - a shaded quad is the same command and the same
 * fill as the flat quad it replaces.
 *
 * The generators live in coup_shading.c as pure functions of the phase, so
 * every one of these is asserted on the host rather than by watching it.
 */
static void coup_render_update_shading(int phase)
{
    uint16_t tbl[4];

    coup_shading_sheen(tbl, phase);
    saturn_vdp1_set_gouraud_table(COUP_GRD_SHEEN, tbl);

    coup_shading_halo(tbl, phase);
    saturn_vdp1_set_gouraud_table(COUP_GRD_HALO, tbl);

    coup_shading_pulse(tbl, phase);
    saturn_vdp1_set_gouraud_table(COUP_GRD_PULSE, tbl);

    coup_shading_wash(tbl, true);
    saturn_vdp1_set_gouraud_table(COUP_GRD_OCCUPIED, tbl);

    coup_shading_wash(tbl, false);
    saturn_vdp1_set_gouraud_table(COUP_GRD_EMPTY, tbl);

    coup_shading_spotlight(tbl, phase);
    saturn_vdp1_set_gouraud_table(COUP_GRD_SPOTLIGHT, tbl);
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
 * A panel shaded by a gouraud slot, falling back to a flat fill off-Saturn.
 *
 * Lets the shared screen code ask for a gradient without being wrapped in
 * #ifdef at every call site. On the other platforms the gradient has no
 * meaning and this is exactly panel().
 */
static void panel_grd(int x, int y, int w, int h, uint32_t color, int slot)
{
#ifdef __SATURN__
    panel_lit(x, y, w, h, color, slot);
#else
    (void)slot;
    panel(x, y, w, h, color);
#endif
}

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

/*============================================================================
 * Card-reveal state machine
 *
 * See coup.h for the contract and the design-doc citations. Pure: it reads
 * coup_state_t and writes only its own struct, so every frame of every
 * animation is asserted on the host (tests/coup/test_coup_reveal.c) instead
 * of being confirmed by watching a screen.
 *============================================================================*/

int coup_reveal_slot_index(int player_index, int card_index)
{
    if (player_index < 0 || player_index >= COUP_MAX_PLAYERS) {
        return -1;
    }
    if (card_index < 0 || card_index >= COUP_CARDS_PER_PLAYER) {
        return -1;
    }
    return player_index * COUP_CARDS_PER_PLAYER + card_index;
}

void coup_reveal_init(coup_reveal_t* rv)
{
    int i;

    if (!rv) {
        return;
    }
    for (i = 0; i < COUP_REVEAL_SLOTS; i++) {
        rv->slots[i].active = 0;
        rv->slots[i].step = 0;
        rv->slots[i].card = COUP_CHAR_NONE;
        rv->slots[i].is_loss = 0;
        rv->prev[i] = COUP_CHAR_NONE;
    }
    rv->seeded = 0;
}

/**
 * Last step value a slot's sequence reaches.
 *
 * The flip occupies steps 0..COUP_REVEAL_FLIP_FRAMES INCLUSIVE, because both
 * endpoints are drawn: step 0 is the full-width front face and step
 * COUP_REVEAL_FLIP_FRAMES the full-width back face, with the collapsed sliver
 * at the midpoint between them (saturn_distort_flip_quad's envelope). A loss
 * then hands over to the dissolve, which starts its own count at the next
 * step - hence the +1.
 */
static int reveal_last_step(const coup_reveal_slot_t* s)
{
    return s->is_loss
           ? (COUP_REVEAL_FLIP_FRAMES + 1 + COUP_REVEAL_DISSOLVE_FRAMES)
           : COUP_REVEAL_FLIP_FRAMES;
}

/**
 * The card this client can currently see in a given slot.
 *
 * For our own seat that is st->my_cards: players[self].cards carries
 * FACEDOWN on the wire like everyone else's, so reading the seat array for
 * our own seat would mean our own cards never animated at all.
 */
static uint8_t reveal_visible_card(const coup_state_t* st, int p, int c)
{
    if (st->players[p].is_self) {
        return st->my_cards[c];
    }
    return st->players[p].cards[c];
}

void coup_reveal_observe(coup_reveal_t* rv, const coup_state_t* st)
{
    int p, c;

    if (!rv || !st) {
        return;
    }

    /* Off the game screen there are no cards on the table, and the lobby
     * zero-fills players[].cards - and zero is COUP_CHAR_DUKE, not "no
     * card". Re-seeding here is what keeps the next deal silent; testing a
     * card VALUE for "unset" could not. */
    if (st->screen != COUP_SCREEN_GAME) {
        coup_reveal_init(rv);
        return;
    }

    for (p = 0; p < COUP_MAX_PLAYERS; p++) {
        for (c = 0; c < COUP_CARDS_PER_PLAYER; c++) {
            int slot = coup_reveal_slot_index(p, c);
            uint8_t prev, cur;

            /* players[] is a fixed 7 entries; past player_count they hold
             * the previous match's cards. */
            if (p >= st->player_count) {
                continue;
            }

            prev = rv->prev[slot];
            cur = reveal_visible_card(st, p, c);
            rv->prev[slot] = cur;

            if (!rv->seeded) {
                continue;   /* first look: record only, animate nothing */
            }
            if (cur == prev) {
                continue;
            }

            if (cur == COUP_CHAR_NONE) {
                /* Influence lost. The face that goes with it is whatever we
                 * last knew; a card that was never shown dissolves without a
                 * face, which the renderer draws as the card back. */
                rv->slots[slot].active = 1;
                rv->slots[slot].step = 0;
                rv->slots[slot].is_loss = 1;
                rv->slots[slot].card =
                    (prev < COUP_NUM_CHARACTERS) ? prev : COUP_CHAR_NONE;
            } else if (cur < COUP_NUM_CHARACTERS &&
                       (prev == COUP_CHAR_FACEDOWN ||
                        prev < COUP_NUM_CHARACTERS)) {
                /* Turned face up, or exchanged for a different character.
                 * NONE -> a card is a DEAL, not a reveal, and is silent. */
                rv->slots[slot].active = 1;
                rv->slots[slot].step = 0;
                rv->slots[slot].is_loss = 0;
                rv->slots[slot].card = cur;
            }
        }
    }

    rv->seeded = 1;
}

void coup_reveal_tick(coup_reveal_t* rv)
{
    int i;

    if (!rv) {
        return;
    }
    for (i = 0; i < COUP_REVEAL_SLOTS; i++) {
        coup_reveal_slot_t* s = &rv->slots[i];

        if (!s->active) {
            continue;
        }
        if ((int)s->step >= reveal_last_step(s)) {
            s->active = 0;
            s->step = 0;
            continue;
        }
        s->step++;
    }
}

int coup_reveal_stage(const coup_reveal_t* rv, int slot,
                      int* out_step, int* out_frames, int* out_card)
{
    const coup_reveal_slot_t* s;
    int step, frames, stage;

    if (!rv || slot < 0 || slot >= COUP_REVEAL_SLOTS) {
        return COUP_REVEAL_IDLE;
    }
    s = &rv->slots[slot];
    if (!s->active) {
        return COUP_REVEAL_IDLE;
    }

    if ((int)s->step <= COUP_REVEAL_FLIP_FRAMES) {
        stage = COUP_REVEAL_FLIP;
        step = s->step;
        frames = COUP_REVEAL_FLIP_FRAMES;
    } else {
        stage = COUP_REVEAL_DISSOLVE;
        step = (int)s->step - COUP_REVEAL_FLIP_FRAMES - 1;
        frames = COUP_REVEAL_DISSOLVE_FRAMES;
    }

    if (out_step)   *out_step = step;
    if (out_frames) *out_frames = frames;
    if (out_card)   *out_card = (int)s->card;
    return stage;
}

bool coup_reveal_is_loss(const coup_reveal_t* rv, int slot)
{
    if (!rv || slot < 0 || slot >= COUP_REVEAL_SLOTS) {
        return false;
    }
    return rv->slots[slot].active != 0 && rv->slots[slot].is_loss != 0;
}

int coup_reveal_active_count(const coup_reveal_t* rv)
{
    int i, n = 0;

    if (!rv) {
        return 0;
    }
    for (i = 0; i < COUP_REVEAL_SLOTS; i++) {
        if (rv->slots[i].active) {
            n++;
        }
    }
    return n;
}

/*============================================================================
 * Game-over entrance dissolve
 *
 * See coup.h for the contract and the design-doc citation. Pure, in the
 * exact style of the card-reveal machine above it: coup_gameover_fx_observe()
 * diffs `st->screen` the same way coup_render_screen()'s own s_last_screen
 * static already does, just made host-testable and given a frame counter.
 *============================================================================*/

void coup_gameover_fx_init(coup_gameover_fx_t* gd)
{
    if (!gd) {
        return;
    }
    gd->prev_screen = -1;
    gd->step = COUP_GAMEOVER_DISSOLVE_FRAMES;   /* not dissolving until seen */
    gd->seeded = false;
}

void coup_gameover_fx_observe(coup_gameover_fx_t* gd, const coup_state_t* st)
{
    if (!gd || !st) {
        return;
    }
    /* Only arm on a genuine ENTRY, and only once we have a real previous
     * observation - the first frame ever seen must not look like a
     * transition, exactly the reason coup_reveal_observe() gates on
     * rv->seeded before reacting to a diff. */
    if (gd->seeded && gd->prev_screen != (int)st->screen &&
        st->screen == COUP_SCREEN_GAME_OVER) {
        gd->step = 0;
    }
    gd->prev_screen = (int)st->screen;
    gd->seeded = true;
}

void coup_gameover_fx_tick(coup_gameover_fx_t* gd)
{
    if (!gd) {
        return;
    }
    if (gd->step < COUP_GAMEOVER_DISSOLVE_FRAMES) {
        gd->step++;
    }
}

bool coup_gameover_fx_dissolving(const coup_gameover_fx_t* gd)
{
    return gd && gd->step < COUP_GAMEOVER_DISSOLVE_FRAMES;
}

int coup_gameover_fx_step(const coup_gameover_fx_t* gd)
{
    return gd ? gd->step : COUP_GAMEOVER_DISSOLVE_FRAMES;
}

/*============================================================================
 * Log window arithmetic
 *
 * See coup.h for what this is and why all three log views share it.
 *============================================================================*/

int coup_log_ring_index(int head, int count, int max_rows, int scroll, int row)
{
    int shown;
    int chrono;

    if (count <= 0 || max_rows <= 0) {
        return -1;
    }
    if (count > COUP_LOG_LINES) {
        count = COUP_LOG_LINES;
    }

    shown = (count < max_rows) ? count : max_rows;
    if (row < 0 || row >= shown) {
        return -1;
    }

    if (scroll < 0) {
        scroll = 0;
    }
    if (scroll > count - shown) {
        scroll = count - shown;
    }

    chrono = count - shown - scroll + row;
    if (chrono < 0 || chrono >= count) {
        return -1;
    }

    /* The step that was missing from the game-over recap. `chrono` is an
     * entry NUMBER; the slot it lives in is that many places on from the
     * oldest entry, which is the one at `head`. The two only coincide while
     * the ring has never wrapped, and with COUP_LOG_LINES == 6 a finished
     * match has always wrapped - so every recap row was reading somebody
     * else's line. */
    if (head < 0) {
        head = 0;
    }
    return (head + chrono) % COUP_LOG_LINES;
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

/* COUP_FX_HOLD_FRAMES is declared in coup.h so the pacing is host-testable. */

/* Effects are authored 32x32 to 64x64, small against a 320x224 screen. VDP1
 * scales sprites for free, so they are drawn at 2x about their centre. */
#define COUP_FX_SCALE_NUM 2
#define COUP_FX_SCALE_DEN 1

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

/** See coup.h for the contract - reuses coup_fx_prev_t's `phase`, added no
 * new observed state. Must be called BEFORE coup_fx_remember() updates
 * `prev` for this frame, the same ordering fx_observe() already uses for
 * coup_fx_on_transition(). */
bool coup_challenge_resolved(const coup_fx_prev_t* prev, const coup_state_t* st)
{
    if (!prev || !st) {
        return false;
    }
    if ((int)st->phase == prev->phase) {
        return false;
    }
    return prev->phase == COUP_PHASE_CHALLENGE_WAIT ||
           prev->phase == COUP_PHASE_BLOCK_CHALLENGE;
}

/** See coup.h for the contract. */
int coup_pick_winner_char(const uint8_t cards[COUP_CARDS_PER_PLAYER])
{
    int k;

    if (!cards) {
        return COUP_CHAR_NONE;
    }
    for (k = 0; k < COUP_CARDS_PER_PLAYER; k++) {
        if (cards[k] < COUP_NUM_CHARACTERS) {
            return (int)cards[k];
        }
    }
    return COUP_CHAR_NONE;
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
/** Character -> official 48x72 card face, or -1 if unknown. */
static int char_to_card_ui(int character)
{
    switch (character) {
    case COUP_CHAR_DUKE:       return COUP_UI_DUKE;
    case COUP_CHAR_ASSASSIN:   return COUP_UI_ASSASSIN;
    case COUP_CHAR_CAPTAIN:    return COUP_UI_CAPTAIN;
    case COUP_CHAR_AMBASSADOR: return COUP_UI_AMBASSADOR;
    case COUP_CHAR_CONTESSA:   return COUP_UI_CONTESSA;
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
        /* Centre point now, not a top-left corner - the scaled draw grows
         * about this point. */
        s_fx_x = COUP_SCREEN_W / 2;
        s_fx_y = 84 + 32;
    }

    /* "Flash-white for challenge results" (design doc section 4.2, "All
     * transitions"). Must run BEFORE coup_fx_remember() updates s_fx_prev
     * for this frame - coup_challenge_resolved() reads the phase s_fx_prev
     * still holds from last frame. Composed entirely from the existing
     * colour-offset fade module (saturn_fade.h); this is a trigger and a
     * call site, not new PAL code. */
    if (coup_challenge_resolved(&s_fx_prev, st)) {
        saturn_fade_start(SATURN_FADE_WHITE, SATURN_FADE_NONE,
                          COUP_CHALLENGE_FLASH_FRAMES, true);
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
    coup_fx_draw_scaled(s_fx_active, frame, s_fx_x, s_fx_y,
                        COUP_FX_SCALE_NUM, COUP_FX_SCALE_DEN);
    s_fx_tick++;
}

/*----------------------------------------------------------------------------
 * Card reveal / influence loss - the distorted-sprite animations
 *
 * The state machine above decides WHAT is animating and at which step; this
 * turns that into VDP1 Distorted Sprite commands via pal/saturn/
 * saturn_distort.c.
 *
 * Geometry note: saturn_distort_encode_flip() passes the quad's w/h straight
 * into CMDSIZE as the SOURCE character size, so the drawn card is always the
 * art's authored size. Every card asset here - the back and all five faces -
 * is 48x72 (coup_fx_data.h:6762-6768), so that is the one size these
 * animations use. Scaling a flip would need a second source-size parameter
 * the module does not take.
 *
 * Palette note: CMDCOLR carries ONE colour bank for the whole command, but
 * the flip swaps TEXTURE at the midpoint and the card back's palette is not
 * the faces' palette. The bank therefore has to be swapped in step with the
 * texture, which the caller does here using the same midpoint test
 * saturn_distort_encode_flip() applies internally (step < frames/2).
 *--------------------------------------------------------------------------*/

/* Where an opponent's card animates. Their seat boxes are text-sized, so a
 * 48x72 card cannot play in place; it plays on the table instead, the same
 * spot the action effects use. */
#define COUP_REVEAL_STAGE_X (COUP_SCREEN_W / 2)
#define COUP_REVEAL_STAGE_Y (84 + 32)
#define COUP_REVEAL_STAGE_PITCH 56

static coup_reveal_t s_reveal;

/* Game-over entrance dissolve (coup.h) - observed on every screen alongside
 * s_reveal, for the identical re-seeding reason. */
static coup_gameover_fx_t s_gameover_fx;

/** Is this card mid-animation? Used to suppress the static face under it. */
static bool reveal_busy(int player_index, int card_index)
{
    return coup_reveal_stage(&s_reveal,
                             coup_reveal_slot_index(player_index, card_index),
                             NULL, NULL, NULL) != COUP_REVEAL_IDLE;
}

/** Seat index of the local player, or -1. */
static int reveal_self_index(const coup_state_t* st)
{
    int i;
    for (i = 0; i < st->player_count; i++) {
        if (st->players[i].is_self) {
            return i;
        }
    }
    return -1;
}

/** Draw one card's current animation frame. Returns false if it drew nothing. */
static bool reveal_draw_slot(int stage, int step, int frames, int card,
                             bool is_loss, int cx, int cy)
{
    uint32_t back_tex, face_tex;
    int back_bank, face_bank;
    uint32_t slot_addr;
    int face_ui;

    if (!coup_ui_texture(COUP_UI_CARD_BACK, &back_tex, &back_bank)) {
        return false;
    }

    /* A card that was never shown has no face; it flips back-to-back, which
     * still reads as "something was turned over" before it dissolves. */
    face_ui = char_to_card_ui(card);
    if (face_ui < 0 || !coup_ui_texture(face_ui, &face_tex, &face_bank)) {
        face_tex = back_tex;
        face_bank = back_bank;
    }

    slot_addr = saturn_vdp1_reserve_cmd_slot();
    if (slot_addr == 0) {
        return false;   /* command budget gone - skip this frame's effect */
    }

    if (stage == COUP_REVEAL_DISSOLVE) {
        /* "flip to back + mesh-dissolve out": the back is what is on screen
         * when the flip ends, so it is the back that dissolves. */
        return saturn_distort_draw_mesh_dissolve(slot_addr, cx, cy,
                                                 COUP_CARD_ART_W, COUP_CARD_ART_H,
                                                 back_tex, back_bank);
    }

    /* A reveal turns card back -> face. A loss turns face -> card back,
     * because the dissolve that follows it dissolves the back. */
    {
        uint32_t first_tex = is_loss ? face_tex : back_tex;
        uint32_t second_tex = is_loss ? back_tex : face_tex;
        int first_bank = is_loss ? face_bank : back_bank;
        int second_bank = is_loss ? back_bank : face_bank;

        return saturn_distort_draw_flip(slot_addr, cx, cy,
                                        COUP_CARD_ART_W, COUP_CARD_ART_H,
                                        step, frames,
                                        first_tex, second_tex,
                                        (step < frames / 2)
                                            ? first_bank : second_bank,
                                        false);
    }
}

/** Draw every running card animation. */
static void reveal_render(const coup_state_t* st)
{
    const coup_game_hand_layout_t* H = &COUP_UI.game.hand;
    int self_idx = reveal_self_index(st);
    int stage_n = 0;
    int stage_i = 0;
    int p, c;

    /* Count the table-side animations first so they can be laid out about the
     * centre instead of stacking on one another. */
    for (p = 0; p < st->player_count; p++) {
        for (c = 0; c < COUP_CARDS_PER_PLAYER; c++) {
            if (p != self_idx && reveal_busy(p, c)) {
                stage_n++;
            }
        }
    }

    for (p = 0; p < st->player_count; p++) {
        for (c = 0; c < COUP_CARDS_PER_PLAYER; c++) {
            int step = 0, frames = COUP_REVEAL_FLIP_FRAMES;
            int card = COUP_CHAR_NONE;
            int stage = coup_reveal_stage(&s_reveal,
                                          coup_reveal_slot_index(p, c),
                                          &step, &frames, &card);
            int cx, cy;

            if (stage == COUP_REVEAL_IDLE) {
                continue;
            }

            if (p == self_idx) {
                /* Our own cards animate in place, over their hand slot. */
                int x = (c == 0) ? H->card0_x : H->card1_x;
                int y = (c == 0) ? H->card0_y : H->card1_y;
                cx = x + COUP_CARD_ART_W / 2;
                cy = y + COUP_CARD_ART_H / 2;
            } else {
                cx = COUP_REVEAL_STAGE_X
                     + (stage_i - (stage_n - 1) / 2) * COUP_REVEAL_STAGE_PITCH;
                cy = COUP_REVEAL_STAGE_Y;
                stage_i++;

                /* Keep the quad on screen. A negative VDP1 x wraps rather
                 * than clipping (see coup_centre_x in coup.h), so a wide
                 * row of simultaneous losses must be clamped, not trusted. */
                if (cx < COUP_CARD_ART_W / 2) {
                    cx = COUP_CARD_ART_W / 2;
                } else if (cx > COUP_SCREEN_W - COUP_CARD_ART_W / 2) {
                    cx = COUP_SCREEN_W - COUP_CARD_ART_W / 2;
                }
            }

            reveal_draw_slot(stage, step, frames, card,
                             coup_reveal_is_loss(&s_reveal,
                                 coup_reveal_slot_index(p, c)),
                             cx, cy);
        }
    }
}
#endif

#endif

/*============================================================================
 * Title-screen card carousel - pure maths
 *
 * See coup.h for the full design rationale (why this replaces the portrait
 * parade, the depth model, the periodicity/ordering guarantees).
 *============================================================================*/

void coup_carousel_layout(int frame, int center_x, int center_y, int radius_x,
                          coup_carousel_card_t out[COUP_CAROUSEL_COUNT])
{
    int spacing = COUP_SHADING_PERIOD / COUP_CAROUSEL_COUNT;   /* 20 */
    int i;

    if (!out) {
        return;
    }

    for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
        int raw = frame + i * spacing;
        int phase = raw % COUP_SHADING_PERIOD;
        int lateral, depth;
        int h, w, abs_depth;

        if (phase < 0) {
            phase += COUP_SHADING_PERIOD;
        }

        /* x and depth are the same sine table, a quarter-period apart - the
         * standard parametric-circle trick (x = R*sin(t), z = R*cos(t)),
         * reusing coup_shading_sin() rather than adding a second table
         * (2026-08-06 facelift task: "reuse it rather than adding a second
         * one"). */
        lateral = coup_shading_sin(phase);
        depth   = coup_shading_sin(phase + COUP_SHADING_PERIOD / 4);

        /* Height: near (depth=+1024) is biggest, far (depth=-1024) is
         * smallest. Always in [H_MIN, H_MAX], both > 0 by definition -
         * satisfies the zero/negative display-size precaution for every
         * input, not just the common case. */
        h = COUP_CAROUSEL_H_MIN
          + ((COUP_CAROUSEL_H_MAX - COUP_CAROUSEL_H_MIN) * (depth + 1024))
            / 2048;

        /* Width: same height-derived value, foreshortened by how close to
         * "edge-on" the card is (|depth|/1024 - see coup.h's depth-model
         * note). Floored at W_FLOOR rather than let it reach 0 at depth=0. */
        abs_depth = depth < 0 ? -depth : depth;
        w = (h * COUP_CARD_ART_W) / COUP_CARD_ART_H;
        w = (w * abs_depth) / 1024;
        if (w < COUP_CAROUSEL_W_FLOOR) {
            w = COUP_CAROUSEL_W_FLOOR;
        }

        out[i].cx = center_x + (radius_x * lateral) / 1024;
        out[i].cy = center_y;
        out[i].w  = w;
        out[i].h  = h;
        out[i].depth = depth;
        out[i].card_id = i;
    }
}

void coup_carousel_sort(const coup_carousel_card_t cards[COUP_CAROUSEL_COUNT],
                        int out_order[COUP_CAROUSEL_COUNT])
{
    int key[COUP_CAROUSEL_COUNT];
    int i, j;

    if (!cards || !out_order) {
        return;
    }

    for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
        out_order[i] = i;
        /* Composite sort key: depth is the primary (back-to-front z-order),
         * card_id the tiebreaker (see coup_carousel_sort()'s doc comment in
         * coup.h for why depth alone ties). COUP_CAROUSEL_COUNT as the
         * multiplier keeps every card_id (0..5) strictly inside the gap
         * between adjacent depth buckets, so no two cards can ever produce
         * the same composite key - the resulting order is a genuine strict
         * total order, never a coin-flip between two equally-ranked draws. */
        key[i] = cards[i].depth * COUP_CAROUSEL_COUNT + i;
    }

    /* Insertion sort - 6 elements, ascending key (back-most first). */
    for (i = 1; i < COUP_CAROUSEL_COUNT; i++) {
        int k = key[i];
        int v = out_order[i];
        j = i - 1;
        while (j >= 0 && key[j] > k) {
            key[j + 1] = key[j];
            out_order[j + 1] = out_order[j];
            j--;
        }
        key[j + 1] = k;
        out_order[j + 1] = v;
    }
}

#ifdef __SATURN__
/** card_id (0..5, fixed per carousel slot) -> COUP_UI_* texture index.
 * card_id 0..4 are the five character faces in COUP_CHAR_* order (which is
 * also coup_fx_data.h's COUP_UI_DUKE.. COUP_UI_CONTESSA order - both are
 * generated/declared in that same fixed sequence); card_id 5 is the back. */
static int coup_carousel_card_ui(int card_id)
{
    if (card_id < 0 || card_id >= COUP_NUM_CHARACTERS) {
        return COUP_UI_CARD_BACK;
    }
    return COUP_UI_DUKE + card_id;
}

void coup_carousel_draw(int frame, int cx, int cy, int radius_x)
{
    coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
    int order[COUP_CAROUSEL_COUNT];
    int i;

    if (!coup_fx_loaded()) {
        return;
    }

    coup_carousel_layout(frame, cx, cy, radius_x, cards);
    coup_carousel_sort(cards, order);

    /* Back-to-front: VDP1 has no depth test, command order IS z-order, so
     * the front card's command must land LAST. */
    for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
        const coup_carousel_card_t* c = &cards[order[i]];
        uint32_t tex_offset;
        int bank;

        if (!coup_ui_texture(coup_carousel_card_ui(c->card_id),
                             &tex_offset, &bank)) {
            continue;
        }

        /* SOURCE size first, then DESTINATION (saturn_vdp1.h:399-402) - the
         * shipped bug from getting this backwards is documented at
         * coup_fx_loader.c's coup_fx_draw_scaled(). */
        saturn_vdp1_draw_sprite_scaled(c->cx - c->w / 2, c->cy - c->h / 2,
                                       COUP_CARD_ART_W, COUP_CARD_ART_H,
                                       c->w, c->h,
                                       tex_offset, bank);
    }
}
#endif

/*============================================================================
 * 1. TITLE SCREEN — Full-width layout with horizontal menu
 *
 * VDP1 rects: ~7
 *   6 carousel cards (1 distorted-sprite command each), 1 PLAY button plate
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
    /* 3. Card carousel: the six cards (five character faces + the back)
     * orbiting a vertical axis, the one nearest the camera enlarging as it
     * approaches. Replaces the portrait parade that used to scroll here -
     * see coup.h's coup_carousel_layout() doc comment for why. */
    coup_carousel_draw(st->frame_count, L->carousel_cx, L->carousel_cy,
                      L->carousel_radius_x);
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
     * Keyed on its dark backing, so no plate is drawn behind it.
     *
     * NOT gouraud-lit: see coup_fx_loader.h / convert_effects.py's
     * --wordmark-rgb555 flag for why - MEASURED 2026-08-06, the RGB555
     * texture a sheen needs (+32,768 B of WRAM-H .rodata, this bare-SGL
     * build's whole program including .rodata loads into WRAM-H, so a VDP1
     * texture asset is ALSO a WRAM-H cost, not just a VRAM one) overruns
     * SGL's SortList by 2,900 B on top of the carousel's own footprint,
     * against a 30,188 B baseline slack. VDP1 VRAM had 192,608 B free
     * (gate D) - comfortable - but WRAM-H did not (gate F). Fixing this
     * needs the sprite streamed from CD like the background scenes already
     * are, not linked into the resident binary; out of scope for this
     * pass. */
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

    /* Connection panel.
     *
     * Reported repeatedly as "SO DARK" during "Dialing server...". Measured
     * luma of the fills available here: PANEL_HEADER 30.3, PANEL_MID 33.4,
     * PANEL_PROMPT 36.8, PANEL_LIGHT 44.6. This screen was using the DARKEST
     * of them - and on Saturn screen_bg() is a no-op, so the only thing
     * behind this panel is the streamed backdrop, which is itself the dimmest
     * scene in the game (source median 15, the only one that hit the gamma
     * clamp). Darkest panel over darkest scene is why lifting the artwork
     * alone did not fix it.
     *
     * PANEL_LIGHT is +47% luma over PANEL_HEADER, and the gouraud lift on top
     * costs nothing - it is the same command and the same fill as the flat
     * rect it replaces (ST-013-R3 section 5.3). */
    panel_grd(L->main_panel.x, L->main_panel.y,
              L->main_panel.w, L->main_panel.h,
              COUP_PANEL_LIGHT, COUP_GRD_RAISED);
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
            /* This screen does not scroll, so the window is always the tail. */
            int idx = coup_log_ring_index(st->log_head, st->log_count,
                                          L->log_max_visible, 0, li);
            if (idx < 0) {
                continue;
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

            /* An occupied slot is washed with light; see coup_shading.c. The
             * design doc's lobby treatment asks for the seated players to
             * read as lit and the empty chairs as recessed, at a glance. */
            if (is_cursor) {
                panel_grd(L->slot_x, sy, L->slot_w, L->slot_h,
                          COUP_PANEL_LIGHT, COUP_GRD_OCCUPIED);
            } else if (is_ready) {
                panel_grd(L->slot_x, sy, L->slot_w, L->slot_h,
                          COUP_PANEL_SELECT, COUP_GRD_OCCUPIED);
            } else {
                panel_grd(L->slot_x, sy, L->slot_w, L->slot_h,
                          COUP_PANEL_MID, COUP_GRD_OCCUPIED);
            }
            /* Ready indicator bar - this one PULSES, so a player who has
             * readied up is visible without reading any text. */
            if (is_ready) {
                panel_grd(L->slot_x, sy, L->ready_bar_w, L->slot_h,
                          COUP_ACCENT_GREEN, COUP_GRD_PULSE);
            }
        } else if (is_cursor) {
            /* Cursor on empty "add bot" slot */
            panel_grd(L->slot_x, sy, L->slot_w, L->slot_h,
                      COUP_PANEL_LIGHT, COUP_GRD_EMPTY);
        } else {
            panel_grd(L->slot_x, sy, L->slot_w, L->slot_h,
                      COUP_PANEL_DARK, COUP_GRD_EMPTY);
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
        /* Empty seat: dim panel, pushed below neutral so it reads as
         * recessed rather than merely a darker colour. */
        panel_grd(seat->box.x, seat->box.y, seat->box.w, seat->box.h,
                  COUP_PANEL_DARK, COUP_GRD_EMPTY);
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

#ifdef __SATURN__
    /* Coins ARRIVING at this seat launch a payout animation toward it.
     *
     * Observed from state rather than driven by the protocol: a seat's coin
     * count going up is the whole trigger, so nothing in coup_game.c or the
     * wire format is touched and the turnkey server contract holds. Coins
     * leaving a seat are not animated - the same transfer would otherwise
     * fire twice, once at each end.
     *
     * Indexed by player id, which is bounded by COUP_MAX_PLAYERS. */
    {
        static int s_prev_coins[COUP_MAX_PLAYERS];
        static bool s_seeded = false;
        int idx = p->id;

        if (!s_seeded) {
            int k;
            for (k = 0; k < COUP_MAX_PLAYERS; k++) {
                s_prev_coins[k] = -1;
            }
            s_seeded = true;
        }
        if (idx >= 0 && idx < COUP_MAX_PLAYERS) {
            int prev = s_prev_coins[idx];
            /* -1 is "never seen": seed silently, so joining a game in
             * progress does not fire a payout for every seat at once. */
            if (prev >= 0 && p->coins > prev) {
                saturn_coinfx_payout(COUP_SCREEN_W / 2, COUP_SCREEN_H / 2,
                                     seat->box.x + seat->box.w / 2,
                                     seat->box.y + seat->box.h / 2,
                                     p->coins - prev,
                                     SATURN_COINFX_DEFAULT_FRAMES);
            }
            s_prev_coins[idx] = p->coins;
        }
    }
#endif

    /* Seat panel background.
     *
     * The seat whose turn it is carries the breathing amber halo, so whose
     * turn it is reads from across the room without parsing any text. It is
     * part of the panel's own gradient rather than a translucent overlay
     * drawn on top - design doc section 4.6 items 7-8 rules the overlay out.
     * A dead seat gets the recessed wash; everyone else the ordinary panel. */
    {
        int grd = COUP_GRD_PANEL;
        if (!p->alive) {
            grd = COUP_GRD_EMPTY;
        } else if (p->id == st->current_turn_id) {
            grd = COUP_GRD_HALO;
        }
        panel_grd(seat->box.x, seat->box.y, seat->box.w, seat->box.h,
                  bg_color, grd);
    }

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

    /* The bar's COLOUR carries the urgency, not just its length: it ramps
     * green -> amber -> red as the response window closes. A shortening bar
     * alone is easy to miss while reading the action list; a reddening one
     * is not. The ramp is generated by saturn_coinfx_timer_colors(), which
     * is asserted on the host for its endpoints and for a monotonically
     * rising red channel (tests/coup/test_saturn_coinfx.c).
     *
     * The base colour is mid-grey rather than the caller's accent, because
     * gouraud corrections are ADDED to the source and clamp at 0x1F - from a
     * mid base the full -16..+15 range is reachable in both directions, and
     * from a saturated accent most of the ramp would clip. The caller's
     * colour is still used off-Saturn, where there is no gradient. */
#ifdef __SATURN__
    {
        uint16_t tbl[4];
        int remaining_pct = (timer * 100) / total;
        saturn_coinfx_timer_colors(remaining_pct, tbl);
        saturn_vdp1_set_gouraud_table(COUP_GRD_TIMER, tbl);
        panel_grd(layout->item_x, bar_y, bar_w, layout->timer_bar_h,
                  COUP_TIMER_BASE, COUP_GRD_TIMER);
    }
    (void)color;
#else
    panel(layout->item_x, bar_y, bar_w, layout->timer_bar_h, color);
#endif
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
        /* Declared outside the Saturn block: the label suppression below is
         * shared code and needs to know whether a card face was drawn.
         * Off-target there are no sprites, so it stays -1 and the text label
         * is drawn as before. */
        int card0 = -1;
#ifdef __SATURN__
        card0 = char_to_card_ui(c0);
        /* A card that is mid-flip is drawn by reveal_render(); drawing the
         * static face too would leave it standing behind the collapsing
         * quad. The label is suppressed with it, hence card0 >= 0 below. */
        if (reveal_busy(reveal_self_index(st), 0)) {
            /* animating - reveal_render() owns this slot this frame */
        } else if (coup_fx_loaded() && card0 >= 0) {
            /* Official card face at its authored 48x72 - no scaling, so no
             * downscale blockiness. */
            coup_ui_draw(card0, H->card0_x, H->card0_y);
        } else if (coup_anim_loaded() && c0 < COUP_NUM_CHARACTERS) {
            int frame = (st->frame_count / COUP_ANIM_HOLD_FRAMES + c0 * 5) % COUP_ANIM_FRAMES;
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
        /* The official card face prints its own name, so the abbreviation
         * would sit on top of it saying the same thing. Only label the
         * fallback, which is a bare portrait with no text of its own. */
        if (c0 < COUP_NUM_CHARACTERS && card0 < 0) {
            CUI_DISPLAY()->draw_text_sprite(H->label0_x, H->label0_y,
                coup_char_short[c0], coup_char_text_color(c0));
        }
    }

    /* Card 1 */
    {
        int c1 = st->my_cards[1];
        /* Declared outside the Saturn block: the label suppression below is
         * shared code and needs to know whether a card face was drawn.
         * Off-target there are no sprites, so it stays -1 and the text label
         * is drawn as before. */
        int card1 = -1;
#ifdef __SATURN__
        card1 = char_to_card_ui(c1);
        /* Same suppression as card 0 above. */
        if (reveal_busy(reveal_self_index(st), 1)) {
            /* animating - reveal_render() owns this slot this frame */
        } else if (coup_fx_loaded() && card1 >= 0) {
            /* Official card face at its authored 48x72 - no scaling, so no
             * downscale blockiness. */
            coup_ui_draw(card1, H->card1_x, H->card1_y);
        } else if (coup_anim_loaded() && c1 < COUP_NUM_CHARACTERS) {
            int frame = (st->frame_count / COUP_ANIM_HOLD_FRAMES + c1 * 5) % COUP_ANIM_FRAMES;
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
        /* The official card face prints its own name, so the abbreviation
         * would sit on top of it saying the same thing. Only label the
         * fallback, which is a bare portrait with no text of its own. */
        if (c1 < COUP_NUM_CHARACTERS && card1 < 0) {
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
        int ring_idx = coup_log_ring_index(st->log_head, st->log_count,
                                           GL->max_visible, scroll, i);
        int py = GL->base_y + i * GL->spacing;
        int age;
        uint32_t log_color;

        if (ring_idx < 0) {
            continue;
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

    /* === 4b. Card reveals and influence losses ===
     *
     * After the hand so a flipping card is drawn over its own slot rather
     * than under it: VDP1 has no depth test, so command ORDER is the only
     * z-order there is. */
#ifdef __SATURN__
    reveal_render(st);
#endif

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
    /* Winner portrait, gouraud spotlight, and the VICTORY/DEFEAT banner from
     * the official art - design doc section 4.2 "Game over": "winner
     * portrait scaled up with a gouraud spotlight; mesh + colour-offset
     * dissolve into it." The banner is 128x32; its position is unchanged
     * from before this treatment existed, so it now draws OVER the lower
     * portion of the portrait, matching the doc's own draw-order rule
     * (section 4.6 item 2: draw order is blend order, later covers
     * earlier) - the same z-order trick reveal_render() already uses to
     * crop a flipping card against its hand slot. */
    if (coup_fx_loaded()) {
        /* Snapshotted at GAME_OVER, not recomputed. find_self() scans for
         * is_self, which a LOBBY_STATE arriving while this screen is up
         * corrupts - that is what showed DEFEAT to a winner. */
        int won = st->i_won;
        /* st->winner_char is likewise a GAME_OVER-time snapshot (coup.h) -
         * a COUP_CHAR_* value, or COUP_CHAR_NONE if coup_pick_winner_char()
         * found no identifiable card (should not happen for a live winner;
         * guarded rather than assumed). */
        bool have_portrait = st->winner_char < COUP_NUM_CHARACTERS
                              && coup_anim_loaded();

        /* Enlarged 1.5x from the 64x96 native asset (coup_anim_loader.h,
         * COUP_ANIM_W/H) - "scaled up", per the doc. Centred on the same
         * horizontal axis the banner already uses; top-anchored so its
         * upper (face/shoulders) portion stays clear of both the banner
         * and the winner-name panel below. */
        {
            const int port_w = (COUP_ANIM_W * 3) / 2;   /* 96 */
            const int port_h = (COUP_ANIM_H * 3) / 2;   /* 144 */
            const int port_x = (COUP_SCREEN_W - port_w) / 2;
            const int port_y = 0;
            const int port_cx = port_x + port_w / 2;
            const int port_cy = port_y + port_h / 2;

            /* A blooming spotlight plate behind the portrait. Drawn BEFORE
             * it so the sprite sits inside the light rather than under a
             * wash - the plate is opaque, for the same reason the turn
             * halo is (design doc section 4.6 items 7-8: no translucent
             * overlays here). It breathes, so the game-over screen is not
             * a still image. Sized around the portrait rather than the
             * banner now, with an 8px margin so the glow reads past the
             * art's own transparent edge. */
            panel_grd(port_x - 8, port_y, port_w + 16, port_h + 8,
                      won ? COUP_PANEL_SELECT : COUP_PANEL_DARK,
                      COUP_GRD_SPOTLIGHT);

            if (have_portrait) {
                /* "mesh + colour-offset dissolve into it": for the first
                 * COUP_GAMEOVER_DISSOLVE_FRAMES frames after this screen
                 * appears, the portrait draws through
                 * saturn_distort_draw_mesh_dissolve() (VDP1 Mesh Enable, a
                 * free 50% checkerboard - ST-013-R3 section 6.3,
                 * VDP1_Manual.txt:3338-3343) instead of its normal solid
                 * draw, composed with the SAME saturn_fade_start()
                 * colour-offset ramp coup_render_screen() already starts
                 * on entry (12 frames, matching
                 * COUP_GAMEOVER_DISSOLVE_FRAMES) rather than a third fade
                 * mechanism - both effects finish together.
                 *
                 * Drawn at the asset's NATIVE size: saturn_distort_draw_
                 * mesh_dissolve() feeds one w/h into both the on-screen
                 * quad and CMDSIZE's source-texture field
                 * (saturn_distort.c), so a scaled draw here would tell
                 * VDP1 to sample past the actual 64x96 texture. The brief
                 * size difference against the settled 1.5x portrait reads
                 * as the portrait growing into place, not a mismatch. */
                if (coup_gameover_fx_dissolving(&s_gameover_fx)) {
                    uint32_t tex_offset;
                    int bank;

                    if (coup_anim_texture((int)st->winner_char, 0,
                                          &tex_offset, &bank)) {
                        uint32_t slot_addr = saturn_vdp1_reserve_cmd_slot();
                        if (slot_addr != 0) {
                            saturn_distort_draw_mesh_dissolve(
                                slot_addr, port_cx, port_cy,
                                COUP_ANIM_W, COUP_ANIM_H,
                                tex_offset, bank);
                        }
                    }
                } else {
                    coup_anim_draw_scaled((int)st->winner_char, 0,
                                          port_x, port_y, port_w, port_h);
                }
            }
        }

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

#ifdef __SATURN__
    /* ---- Winner, in the display face at double height ----
     *
     * The old line was body-font text centred by counting characters into a
     * 40-column grid. On a screen whose whole job is to announce a winner it
     * was the same size as a log entry. */
    {
        int prev = cui_saturn_font_get_active();
        int tw, tx;

        cui_saturn_font_set_active(COUP_FONT_DISPLAY);
        tw = text_px_w(line);
        tx = coup_centre_x(COUP_SCREEN_W, tw);
        /* A plate behind it: the victory art is bright gold and gold text on
         * gold is the same trap the title hint fell into. */
        /* Directly under the VICTORY/DEFEAT banner, which is 128x32 at
         * y=40 and therefore ends at 72. Reading order top to bottom is
         * banner -> who won -> how it ended -> what to press. */
        const int win_y = 80;

        panel(tx - 8, win_y - 4, tw + 16, COUP_FONT_ROW_H * 2 + 8,
              COUP_PANEL_DARK);
        hline(tx - 8, win_y - 4, tw + 16, COUP_ACCENT_GOLD);
        CUI_DISPLAY()->draw_text_sprite(tx, win_y, line, COUP_TEXT_GOLD);
        cui_saturn_font_set_active(prev);
    }

    /* ---- Scrollable recap of how the match ended ----
     *
     * Reuses st->log and st->log_scroll, which the game screen already
     * maintains and scrolls, so the recap needs no new data plumbing. The
     * LAST entry is the winning action and is drawn in gold with a marker;
     * everything above it is history and is dimmed. */
    {
        const int panel_x = 24, panel_w = COUP_SCREEN_W - 48;
        const int panel_y = 112, row_h = 10;
        const int max_rows = COUP_GAMEOVER_RECAP_ROWS;
        int total = st->log_count;
        int shown = total < max_rows ? total : max_rows;
        int scroll = st->log_scroll;
        int i;

        if (scroll > total - shown) {
            scroll = total - shown;
        }
        if (scroll < 0) {
            scroll = 0;
        }

        panel(panel_x, panel_y - 4, panel_w, row_h * max_rows + 16,
              COUP_PANEL_DARK);
        hline(panel_x, panel_y - 4, panel_w, COUP_ACCENT_GOLD);

        for (i = 0; i < shown; i++) {
            /* USER-REPORTED BUG, fixed here: this used to be
             *     int idx = total - shown - scroll + i;
             * which uses a chronological entry NUMBER as a ring SLOT number.
             * The two agree only while the ring has never wrapped, and with
             * COUP_LOG_LINES == 6 a finished match has always wrapped, so
             * the recap printed its rows out of order - and marked a
             * mid-match line as "the winning action". The mapping is now the
             * one every log view shares (tests/coup/test_gameover_recap.c). */
            int idx = coup_log_ring_index(st->log_head, total, max_rows,
                                           scroll, i);
            int y = panel_y + 6 + i * row_h;
            /* Newest entry = last row of an unscrolled window. Derived from
             * the ROW, not from the slot: a slot number says nothing about
             * how recent its entry is once the ring has wrapped. */
            int last = (scroll == 0 && i == shown - 1);

            if (idx < 0) {
                continue;
            }
            if (last) {
                /* The winning action. Marked and full-brightness. */
                panel(panel_x + 2, y - 1, panel_w - 4, row_h,
                      COUP_PANEL_SELECT);
                CUI_DISPLAY()->draw_text_sprite(panel_x + 4, y, ">",
                                                COUP_TEXT_GOLD);
            }
            CUI_DISPLAY()->draw_text_sprite(
                panel_x + 14, y, st->log[idx],
                last ? COUP_TEXT_GOLD : COUP_TEXT_GRAY);
        }

        if (total > max_rows) {
            draw_centered(24, "[UP/DOWN] Recap    [A] Lobby",
                          COUP_TEXT_WHITE);
        } else {
            draw_centered(24, "[A] Return to Lobby", COUP_TEXT_WHITE);
        }
    }
#else
    text_x = (40 - name_len) / 2;
    if (text_x < 0) text_x = 0;
    draw_at(text_x, GO->winner_row, line, COUP_TEXT_GOLD);
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
        coup_render_update_shading(st->frame_count);
        /* Safe no-op when not armed, so it needs no guard. */
        saturn_linescroll_advance();

        /* Watch for cards turning over. Run on EVERY screen, not just the
         * game screen: coup_reveal_observe() re-seeds itself off the game
         * screen, and that is what keeps the next match's deal silent. */
        coup_reveal_observe(&s_reveal, st);

        /* Arm the game-over entrance dissolve the frame GAME_OVER appears,
         * and advance it every frame after. Same "run on every screen"
         * reasoning as coup_reveal_observe() just above: re-seeding off
         * other screens is what stops the NEXT match's game-over from
         * replaying a stale dissolve. */
        coup_gameover_fx_observe(&s_gameover_fx, st);
        coup_gameover_fx_tick(&s_gameover_fx);

        /* Advance and draw any coins in flight. The draw issues its own
         * scaled-sprite commands so it can hold the coin's CENTRE fixed
         * while the size changes - saturn_vdp1_draw_sprite_scaled() anchors
         * top-left, so scaling alone would slide the coin down-and-right
         * instead of popping it in place. Both are no-ops with nothing in
         * flight. */
        saturn_coinfx_tick();
        {
            const coup_fx_info_t* ci = &coup_ui_info[COUP_UI_COIN1];
            uint32_t tex;
            int bank;
            if (coup_ui_texture(COUP_UI_COIN1, &tex, &bank)) {
                saturn_coinfx_draw(ci->w, ci->h, tex, bank);
            }
        }

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
                     * disagree about who won - which means it had the same
                     * bug, and a winner got the defeat BACKDROP too. Both now
                     * read the snapshot taken when GAME_OVER fired. */
                    scene = st->i_won ? COUP_BG_SCENE_VICTORY
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

                /* The sine shimmer belongs to the front-end only. The design
                 * doc asks for it on the title and nowhere else - a moving
                 * backdrop under the game table would fight the cards, and
                 * under the rules text it would make reading harder.
                 *
                 * Line scroll is legal on this layer even though NBG1 is a
                 * BITMAP here: ST-058-R2 section 5.3 - "both functions can be
                 * used without relationship to the cell format and bit map
                 * format" - and NBG1 is one of only two layers that support
                 * line scroll at all. */
                if (scene == COUP_BG_SCENE_TITLE) {
                    saturn_linescroll_arm();
                } else {
                    saturn_linescroll_disarm();
                }

                /* The streamed read takes the CD pickup and stops CD-DA -
                 * there is only one pickup and CDC_CdPlay against the file's
                 * FAD range replaces both the play range and the endless
                 * repeat mode. coup_update() has already restarted the music
                 * for this screen by now, so without this the music would
                 * play for a few frames per transition and then go silent.
                 * Restore AFTER the load, never before. */
                coup_audio_restore_music();
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

#ifdef __SATURN__
    /* Advance the card animations AFTER they have been drawn, so the frame
     * the observer started at step 0 is actually shown. */
    coup_reveal_tick(&s_reveal);
#endif

    CUI_DISPLAY()->end_frame();
}
