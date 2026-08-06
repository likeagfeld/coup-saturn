/**
 * test_coup_reveal.c - The card-reveal state machine.
 *
 * WHY THIS EXISTS
 *   pal/saturn/saturn_distort.c has been built, unit-tested and linked since
 *   the facelift branch opened, and nothing called it, because reveals in
 *   this client are INSTANT: a card's character changes in the state and the
 *   next frame draws the new face. There was nowhere to ask "this card is at
 *   frame 7 of 12 of its reveal", so there was nothing for a distorted-sprite
 *   flip to be a function of.
 *
 *   This is that missing piece. It is pure: it observes coup_state_t exactly
 *   the way coup_fx_on_transition() already does (spec D9 - the server stays
 *   turnkey, no wire-protocol or rule-engine change), so every frame of every
 *   animation is a host-testable fact rather than something to be confirmed
 *   by watching a screen.
 *
 * THE TWO SEQUENCES (design doc 2026-08-04-saturn-visual-facelift-design.md
 * section 4.2 "Game", line 124-126)
 *   reveal          : distorted-sprite Y-axis flip, 12 frames, texture swap
 *                     at the midpoint (card back -> revealed face)
 *   influence loss  : the same flip, run face -> card back, followed by a
 *                     mesh-dissolve of the back
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "saturn_distort.h"

#include <string.h>

/*============================================================================
 * Fixtures
 *============================================================================*/

/** Two players, both alive, all four cards face down. */
static void two_players(coup_state_t* st)
{
    memset(st, 0, sizeof(*st));
    st->screen = COUP_SCREEN_GAME;
    st->player_count = 2;

    st->players[0].id = 0;
    st->players[0].is_self = true;
    st->players[0].alive = true;
    st->players[0].cards[0] = COUP_CHAR_FACEDOWN;
    st->players[0].cards[1] = COUP_CHAR_FACEDOWN;
    st->my_cards[0] = COUP_CHAR_DUKE;
    st->my_cards[1] = COUP_CHAR_CONTESSA;

    st->players[1].id = 1;
    st->players[1].alive = true;
    st->players[1].cards[0] = COUP_CHAR_FACEDOWN;
    st->players[1].cards[1] = COUP_CHAR_FACEDOWN;
}

/** Advance the machine n frames with the state held still. */
static void run(coup_reveal_t* rv, const coup_state_t* st, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        coup_reveal_observe(rv, st);
        coup_reveal_tick(rv);
    }
}

/*============================================================================
 * Slot addressing
 *============================================================================*/

CUI_TEST(reveal_every_player_card_has_its_own_slot)
{
    int seen[COUP_REVEAL_SLOTS];
    int p, c;

    memset(seen, 0, sizeof(seen));
    for (p = 0; p < COUP_MAX_PLAYERS; p++) {
        for (c = 0; c < COUP_CARDS_PER_PLAYER; c++) {
            int s = coup_reveal_slot_index(p, c);
            CUI_ASSERT_GE(s, 0);
            CUI_ASSERT_LT(s, COUP_REVEAL_SLOTS);
            CUI_ASSERT_EQ(0, seen[s]);   /* no two cards share a slot */
            seen[s] = 1;
        }
    }
}

CUI_TEST(reveal_slot_index_rejects_out_of_range_cards)
{
    CUI_ASSERT_EQ(-1, coup_reveal_slot_index(-1, 0));
    CUI_ASSERT_EQ(-1, coup_reveal_slot_index(COUP_MAX_PLAYERS, 0));
    CUI_ASSERT_EQ(-1, coup_reveal_slot_index(0, -1));
    CUI_ASSERT_EQ(-1, coup_reveal_slot_index(0, COUP_CARDS_PER_PLAYER));
}

/*============================================================================
 * Seeding - joining a game in progress must not fire every animation at once
 *============================================================================*/

CUI_TEST(reveal_first_observation_animates_nothing)
{
    coup_reveal_t rv;
    coup_state_t st;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
}

CUI_TEST(reveal_a_still_state_never_starts_anything)
{
    coup_reveal_t rv;
    coup_state_t st;

    two_players(&st);
    coup_reveal_init(&rv);
    run(&rv, &st, 30);

    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
}

CUI_TEST(reveal_leaving_the_game_screen_reseeds)
{
    /* Between two matches the previous match's cards are still in prev[].
     * Without a reset the next deal would look like five simultaneous
     * reveals. The lobby also zero-fills players[].cards, and zero is
     * COUP_CHAR_DUKE, so the reset has to happen on screen and not on a
     * card value. */
    coup_reveal_t rv;
    coup_state_t st;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[0] = COUP_CHAR_ASSASSIN;   /* would be a reveal */
    st.screen = COUP_SCREEN_LOBBY;
    coup_reveal_observe(&rv, &st);
    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));

    /* Back on the game screen the new hand is seeded, still silently. */
    st.screen = COUP_SCREEN_GAME;
    coup_reveal_observe(&rv, &st);
    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
}

CUI_TEST(reveal_a_fresh_deal_from_empty_slots_is_silent)
{
    /* NONE -> a real card is a deal, not a reveal. Only a card that was
     * face down (or already face up) can be revealed. */
    coup_reveal_t rv;
    coup_state_t st;

    two_players(&st);
    st.players[1].cards[0] = COUP_CHAR_NONE;
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[0] = COUP_CHAR_CAPTAIN;
    coup_reveal_observe(&rv, &st);

    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
}

/*============================================================================
 * Reveal: face down -> a named character
 *============================================================================*/

CUI_TEST(reveal_a_facedown_card_turning_over_starts_a_flip)
{
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step = -1, frames = -1, card = -1;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[0] = COUP_CHAR_CAPTAIN;
    coup_reveal_observe(&rv, &st);

    slot = coup_reveal_slot_index(1, 0);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(0, step);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP_FRAMES, frames);
    CUI_ASSERT_EQ(COUP_CHAR_CAPTAIN, card);
    CUI_ASSERT_EQ(1, coup_reveal_active_count(&rv));
}

CUI_TEST(reveal_flip_advances_one_step_per_frame_then_retires)
{
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step, frames, card, i;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);
    st.players[1].cards[0] = COUP_CHAR_CAPTAIN;
    slot = coup_reveal_slot_index(1, 0);

    for (i = 0; i <= COUP_REVEAL_FLIP_FRAMES; i++) {
        coup_reveal_observe(&rv, &st);
        step = -1;
        CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                      coup_reveal_stage(&rv, slot, &step, &frames, &card));
        CUI_ASSERT_EQ(i, step);
        coup_reveal_tick(&rv);
    }

    /* One step past the last frame the slot is free again. */
    coup_reveal_observe(&rv, &st);
    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
}

CUI_TEST(reveal_an_exchanged_card_flips_to_the_new_face)
{
    /* Ambassador exchange swaps one known face for another. That is still a
     * reveal - the card visibly becomes a different character. */
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step, frames, card = -1;

    two_players(&st);
    st.players[1].cards[0] = COUP_CHAR_DUKE;
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[0] = COUP_CHAR_CONTESSA;
    coup_reveal_observe(&rv, &st);

    slot = coup_reveal_slot_index(1, 0);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_CHAR_CONTESSA, card);
}

CUI_TEST(reveal_our_own_hand_is_read_from_my_cards)
{
    /* players[self].cards stays FACEDOWN on the wire; the client's real hand
     * is st->my_cards. Reading the seat array for our own seat would mean our
     * own cards never animated at all. */
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step, frames, card = -1;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.my_cards[1] = COUP_CHAR_NONE;      /* we just lost that influence */
    coup_reveal_observe(&rv, &st);

    slot = coup_reveal_slot_index(0, 1);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_CHAR_CONTESSA, card);   /* the card we are losing */
}

/*============================================================================
 * Influence loss: flip to the back, then mesh-dissolve out
 *============================================================================*/

CUI_TEST(reveal_losing_an_influence_flips_then_dissolves)
{
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step, frames, card, i;

    two_players(&st);
    st.players[1].cards[0] = COUP_CHAR_DUKE;
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[0] = COUP_CHAR_NONE;
    slot = coup_reveal_slot_index(1, 0);

    /* Phase 1: the flip, 12 frames, carrying the card that is being lost so
     * the front face can be drawn before it turns over. */
    for (i = 0; i <= COUP_REVEAL_FLIP_FRAMES; i++) {
        coup_reveal_observe(&rv, &st);
        step = -1; card = -1;
        CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                      coup_reveal_stage(&rv, slot, &step, &frames, &card));
        CUI_ASSERT_EQ(i, step);
        CUI_ASSERT_EQ(COUP_REVEAL_FLIP_FRAMES, frames);
        CUI_ASSERT_EQ(COUP_CHAR_DUKE, card);
        coup_reveal_tick(&rv);
    }

    /* Phase 2: the mesh dissolve, restarting its own step count. */
    for (i = 0; i <= COUP_REVEAL_DISSOLVE_FRAMES; i++) {
        coup_reveal_observe(&rv, &st);
        step = -1;
        CUI_ASSERT_EQ(COUP_REVEAL_DISSOLVE,
                      coup_reveal_stage(&rv, slot, &step, &frames, &card));
        CUI_ASSERT_EQ(i, step);
        CUI_ASSERT_EQ(COUP_REVEAL_DISSOLVE_FRAMES, frames);
        coup_reveal_tick(&rv);
    }

    coup_reveal_observe(&rv, &st);
    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
}

CUI_TEST(reveal_losing_a_facedown_card_still_dissolves)
{
    /* An opponent coup'd out of a card we never saw: there is no face to
     * flip to, so the flip runs back-to-back and only the dissolve carries
     * meaning. It must still complete rather than stalling the slot. */
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step, frames, card = -1;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[1] = COUP_CHAR_NONE;
    coup_reveal_observe(&rv, &st);

    slot = coup_reveal_slot_index(1, 1);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_CHAR_NONE, card);

    run(&rv, &st, COUP_REVEAL_FLIP_FRAMES + 1);
    CUI_ASSERT_EQ(COUP_REVEAL_DISSOLVE,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));

    run(&rv, &st, COUP_REVEAL_DISSOLVE_FRAMES + 1);
    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
}

CUI_TEST(reveal_two_players_losing_at_once_animate_independently)
{
    /* A challenge can cost two influences in the same server message. */
    coup_reveal_t rv;
    coup_state_t st;
    int a, b, step, frames, card;

    two_players(&st);
    st.players[0].cards[0] = COUP_CHAR_DUKE;
    st.players[1].cards[0] = COUP_CHAR_CAPTAIN;
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.my_cards[0] = COUP_CHAR_NONE;
    st.players[1].cards[0] = COUP_CHAR_NONE;
    coup_reveal_observe(&rv, &st);

    CUI_ASSERT_EQ(2, coup_reveal_active_count(&rv));

    a = coup_reveal_slot_index(0, 0);
    b = coup_reveal_slot_index(1, 0);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, a, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_CHAR_DUKE, card);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, b, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_CHAR_CAPTAIN, card);
}

CUI_TEST(reveal_a_loss_during_a_reveal_restarts_the_slot_as_a_loss)
{
    /* Reveal then immediately lose is the ordinary challenge outcome. The
     * slot must switch to the loss sequence rather than finish the reveal
     * and drop the loss on the floor. */
    coup_reveal_t rv;
    coup_state_t st;
    int slot, step, frames, card;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[1].cards[0] = COUP_CHAR_CAPTAIN;      /* revealed */
    coup_reveal_observe(&rv, &st);
    coup_reveal_tick(&rv);
    coup_reveal_tick(&rv);

    st.players[1].cards[0] = COUP_CHAR_NONE;         /* and lost */
    coup_reveal_observe(&rv, &st);

    slot = coup_reveal_slot_index(1, 0);
    CUI_ASSERT_EQ(COUP_REVEAL_FLIP,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
    CUI_ASSERT_EQ(0, step);                 /* restarted, not continued */
    CUI_ASSERT_EQ(COUP_CHAR_CAPTAIN, card);

    /* And it is a loss now, so it runs on into the dissolve. */
    run(&rv, &st, COUP_REVEAL_FLIP_FRAMES + 1);
    CUI_ASSERT_EQ(COUP_REVEAL_DISSOLVE,
                  coup_reveal_stage(&rv, slot, &step, &frames, &card));
}

/*============================================================================
 * The renderer's palette bank must swap on the same frame as the module's
 * texture
 *
 * saturn_distort_encode_flip() swaps CMDSRCA at the midpoint, but CMDCOLR
 * carries ONE colour bank for the whole command and the card back's palette
 * is not the faces' palette, so the CALLER has to swap the bank in step. Two
 * separate copies of the midpoint test is exactly the kind of coupling that
 * drifts: an off-by-one there paints one frame of the flip in the wrong
 * palette. This asserts the module's actual choice against the predicate
 * coup_render.c uses.
 *============================================================================*/

CUI_TEST(reveal_texture_and_palette_swap_on_the_same_frame)
{
    const uint32_t front = 0x11000u;
    const uint32_t back  = 0x12000u;
    int step;

    for (step = 0; step <= COUP_REVEAL_FLIP_FRAMES; step++) {
        saturn_vdp1_cmd_t cmd;
        /* The predicate in coup_render.c's reveal_draw_slot(). */
        int caller_uses_front = (step < COUP_REVEAL_FLIP_FRAMES / 2);
        uint32_t expect = caller_uses_front ? front : back;

        memset(&cmd, 0, sizeof(cmd));
        saturn_distort_encode_flip(&cmd, 160, 112, COUP_CARD_ART_W,
                                   COUP_CARD_ART_H, step,
                                   COUP_REVEAL_FLIP_FRAMES,
                                   front, back, 20, false);

        CUI_ASSERT_EQ((int)(expect / 8), (int)cmd.srca);
    }
}

CUI_TEST(reveal_the_flip_ends_on_the_second_texture)
{
    /* The last frame of a reveal must show the FACE, not the back - that is
     * the whole point of the animation. */
    saturn_vdp1_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    saturn_distort_encode_flip(&cmd, 160, 112, COUP_CARD_ART_W,
                               COUP_CARD_ART_H,
                               COUP_REVEAL_FLIP_FRAMES,
                               COUP_REVEAL_FLIP_FRAMES,
                               0x11000u, 0x12000u, 20, false);
    CUI_ASSERT_EQ((int)(0x12000u / 8), (int)cmd.srca);
}

/*============================================================================
 * Defensive
 *============================================================================*/

CUI_TEST(reveal_null_and_out_of_range_arguments_are_inert)
{
    coup_reveal_t rv;
    coup_state_t st;
    int step, frames, card;

    two_players(&st);
    coup_reveal_init(&rv);

    coup_reveal_observe(NULL, &st);
    coup_reveal_observe(&rv, NULL);
    coup_reveal_tick(NULL);

    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(NULL, 0, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(&rv, -1, &step, &frames, &card));
    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(&rv, COUP_REVEAL_SLOTS,
                                    &step, &frames, &card));
    CUI_ASSERT_EQ(0, coup_reveal_active_count(NULL));
}

CUI_TEST(reveal_ignores_seats_beyond_the_player_count)
{
    /* players[] is a fixed 7-entry array; only player_count of them are
     * real, and the rest are stale from the previous match. */
    coup_reveal_t rv;
    coup_state_t st;
    int step, frames, card;

    two_players(&st);
    coup_reveal_init(&rv);
    coup_reveal_observe(&rv, &st);

    st.players[5].cards[0] = COUP_CHAR_DUKE;
    coup_reveal_observe(&rv, &st);

    CUI_ASSERT_EQ(0, coup_reveal_active_count(&rv));
    CUI_ASSERT_EQ(COUP_REVEAL_IDLE,
                  coup_reveal_stage(&rv, coup_reveal_slot_index(5, 0),
                                    &step, &frames, &card));
}
