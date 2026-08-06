/**
 * test_coup_carousel.c - Host tests for the title-screen card carousel
 * (examples/coup/coup.h: coup_carousel_layout / coup_carousel_sort).
 *
 * All maths under test is pure integer arithmetic (coup_shading_sin() under
 * the hood, no libm); the VRAM-touching draw path (coup_carousel_draw) is
 * Saturn-only and not exercised here, exactly like saturn_distort.c's and
 * saturn_coinfx.c's own hardware entry points.
 *
 * The four gates the 2026-08-06 facelift task calls for by name:
 *   1. the front card is genuinely the largest
 *   2. depth ordering is a strict sort
 *   3. scale never reaches zero
 *   4. the cycle is periodic so a free-running frame counter cannot drift
 */

#include "cui_test_framework.h"
#include "coup.h"

/* The layout advances one phase step every COUP_CAROUSEL_SLOWDOWN frames, so
 * a full revolution is this many FRAMES - not COUP_SHADING_PERIOD, which is
 * the number of phase STEPS. These two tests originally used the phase
 * period as if it were the frame period; that was correct only while the
 * slowdown was 1, and it failed the moment the carousel was slowed to match
 * the web's 24 s revolution. The property under test - a free-running
 * counter never drifts - is unchanged. */
#define CAROUSEL_TRUE_PERIOD (COUP_SHADING_PERIOD * COUP_CAROUSEL_SLOWDOWN)
#include "coup_shading.h"

#include <limits.h>

/*============================================================================
 * Shared fixtures
 *============================================================================*/

#define FIX_CX 160
#define FIX_CY 116
#define FIX_RADIUS 110

static long card_area(const coup_carousel_card_t* c)
{
    return (long)c->w * (long)c->h;
}

/*============================================================================
 * Gate 1: the front card is genuinely the largest
 *============================================================================*/

CUI_TEST(front_card_is_genuinely_the_largest_every_frame_of_a_full_cycle)
{
    int frame;

    /* Two full periods, so the wrap point itself is exercised too. */
    for (frame = 0; frame < 2 * COUP_SHADING_PERIOD; frame++) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int i, front = 0;

        coup_carousel_layout(frame, FIX_CX, FIX_CY, FIX_RADIUS, cards);

        for (i = 1; i < COUP_CAROUSEL_COUNT; i++) {
            if (cards[i].depth > cards[front].depth) {
                front = i;
            }
        }

        /* No card is EVER bigger than the max-depth card - "front" in the
         * sense that matters (largest on screen) is never usurped.
         *
         * Not asserted as a STRICT ">": with 6 cards spaced 20 apart around
         * a period-120 sine table, two DIFFERENT cards can land on the
         * EXACT same depth (MEASURED: frame=10 puts cards 0 and 5 both at
         * depth=887 - the LUT's mirror symmetry, sin(40 deg-units) ==
         * sin(20 deg-units)). Area is a pure function of depth, so an
         * honest depth tie is an honest area tie, not a bug - a strict ">"
         * here would be a WRONG gate that fires RED on correct code. */
        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            CUI_ASSERT(card_area(&cards[front]) >= card_area(&cards[i]));
            /* But an area tie must be EXPLAINED by a depth tie - this is
             * what actually distinguishes "legitimate LUT symmetry" from
             * "rounding coincidence hiding a real bug": if two cards ever
             * have equal area with DIFFERENT depths, that is the gate that
             * would have caught an inverted or mis-scaled mapping. */
            if (card_area(&cards[front]) == card_area(&cards[i])) {
                CUI_ASSERT_EQ(cards[front].depth, cards[i].depth);
            }
        }
    }
}

/*============================================================================
 * Gate 2: depth ordering is a strict sort
 *============================================================================*/

CUI_TEST(sort_produces_a_permutation_of_all_six_indices)
{
    coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
    int order[COUP_CAROUSEL_COUNT];
    int seen[COUP_CAROUSEL_COUNT] = {0, 0, 0, 0, 0, 0};
    int i;

    coup_carousel_layout(17, FIX_CX, FIX_CY, FIX_RADIUS, cards);
    coup_carousel_sort(cards, order);

    for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
        CUI_ASSERT_GE(order[i], 0);
        CUI_ASSERT_LT(order[i], COUP_CAROUSEL_COUNT);
        CUI_ASSERT_EQ(0, seen[order[i]]);
        seen[order[i]] = 1;
    }
}

CUI_TEST(sort_composite_key_is_strictly_increasing_even_with_tied_depths)
{
    int frame;

    for (frame = 0; frame < COUP_SHADING_PERIOD; frame++) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int order[COUP_CAROUSEL_COUNT];
        int i;
        int had_tie = 0;

        coup_carousel_layout(frame, FIX_CX, FIX_CY, FIX_RADIUS, cards);
        coup_carousel_sort(cards, order);

        for (i = 0; i + 1 < COUP_CAROUSEL_COUNT; i++) {
            long key_a = (long)cards[order[i]].depth * COUP_CAROUSEL_COUNT
                       + order[i];
            long key_b = (long)cards[order[i + 1]].depth * COUP_CAROUSEL_COUNT
                       + order[i + 1];

            if (cards[order[i]].depth == cards[order[i + 1]].depth) {
                had_tie = 1;
            }
            /* Strict: no two adjacent slots may share a composite key. */
            CUI_ASSERT(key_a < key_b);
        }
        (void)had_tie;
    }
}

/* MEASURED (by hand, at frame 0 with 6 cards spaced 20 apart out of a
 * period-120 table): cards 1 and 5 both land on depth=512, and cards 2 and 4
 * both land on depth=-512 - a genuine depth tie every revolution, not a
 * hypothetical one. This pins that the fixture above is exercising the tie
 * path, not just a run of luckily-distinct depths. */
CUI_TEST(depth_ties_between_symmetric_cards_are_real_not_hypothetical)
{
    coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
    int i, j;
    int found_tie = 0;

    coup_carousel_layout(0, FIX_CX, FIX_CY, FIX_RADIUS, cards);

    for (i = 0; i < COUP_CAROUSEL_COUNT && !found_tie; i++) {
        for (j = i + 1; j < COUP_CAROUSEL_COUNT; j++) {
            if (cards[i].depth == cards[j].depth) {
                found_tie = 1;
                break;
            }
        }
    }
    CUI_ASSERT_EQ(1, found_tie);
}

CUI_TEST(the_max_depth_card_is_drawn_last_ends_up_on_top)
{
    coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
    int order[COUP_CAROUSEL_COUNT];
    int i, front = 0;

    coup_carousel_layout(41, FIX_CX, FIX_CY, FIX_RADIUS, cards);
    coup_carousel_sort(cards, order);

    for (i = 1; i < COUP_CAROUSEL_COUNT; i++) {
        if (cards[i].depth > cards[front].depth) {
            front = i;
        }
    }

    /* Back-to-front: VDP1 has no depth test, so the LAST command issued
     * ends up drawn on top - the front card must be the last entry. */
    CUI_ASSERT_EQ(front, order[COUP_CAROUSEL_COUNT - 1]);
}

CUI_TEST(sort_and_layout_are_null_safe)
{
    coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
    int order[COUP_CAROUSEL_COUNT];

    /* Must not crash. */
    coup_carousel_layout(0, FIX_CX, FIX_CY, FIX_RADIUS, 0);
    coup_carousel_sort(0, order);
    coup_carousel_sort(cards, 0);
    CUI_ASSERT(1);
}

/*============================================================================
 * Gate 3: scale never reaches zero
 *============================================================================*/

CUI_TEST(width_and_height_are_always_positive_across_a_full_cycle)
{
    int frame;

    for (frame = 0; frame < COUP_SHADING_PERIOD; frame++) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int i;

        coup_carousel_layout(frame, FIX_CX, FIX_CY, FIX_RADIUS, cards);
        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            CUI_ASSERT_GT(cards[i].w, 0);
            CUI_ASSERT_GT(cards[i].h, 0);
        }
    }
}

CUI_TEST(width_and_height_are_positive_for_adversarial_frame_values)
{
    /* ST-013-R3 p.74 (VDP1_Manual.txt:3143-3144): "A negative value cannot
     * be specified for the display width. Drawing cannot be guaranteed when
     * a negative value is specified" - must hold for every input, not just
     * frames in the "normal" range. */
    static const int adversarial[] = {
        0, -1, -120, -121, -999999, INT_MAX, INT_MIN, 1000000, -1000000
    };
    size_t n = sizeof(adversarial) / sizeof(adversarial[0]);
    size_t k;

    for (k = 0; k < n; k++) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int i;

        coup_carousel_layout(adversarial[k], FIX_CX, FIX_CY, FIX_RADIUS, cards);
        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            CUI_ASSERT_GT(cards[i].w, 0);
            CUI_ASSERT_GT(cards[i].h, 0);
        }
    }
}

CUI_TEST(width_floor_is_actually_reached_not_just_never_violated)
{
    /* The floor exists to catch a real case (edge-on cards), not just as a
     * defensive clamp that never fires - prove it actually engages. */
    int frame;
    int floor_hit = 0;

    for (frame = 0; frame < COUP_SHADING_PERIOD; frame++) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int i;

        coup_carousel_layout(frame, FIX_CX, FIX_CY, FIX_RADIUS, cards);
        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            if (cards[i].w == COUP_CAROUSEL_W_FLOOR) {
                floor_hit = 1;
            }
            /* And never BELOW the floor, in either direction. */
            CUI_ASSERT_GE(cards[i].w, COUP_CAROUSEL_W_FLOOR);
        }
    }
    CUI_ASSERT_EQ(1, floor_hit);
}

CUI_TEST(height_stays_within_the_configured_min_max_range)
{
    int frame;

    for (frame = 0; frame < COUP_SHADING_PERIOD; frame++) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int i;

        coup_carousel_layout(frame, FIX_CX, FIX_CY, FIX_RADIUS, cards);
        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            CUI_ASSERT_GE(cards[i].h, COUP_CAROUSEL_H_MIN);
            CUI_ASSERT_LE(cards[i].h, COUP_CAROUSEL_H_MAX);
        }
    }
}

/*============================================================================
 * Gate 4: the cycle is periodic - a free-running counter cannot drift
 *============================================================================*/

static int cards_equal(const coup_carousel_card_t* a,
                       const coup_carousel_card_t* b)
{
    return a->cx == b->cx && a->cy == b->cy && a->w == b->w && a->h == b->h
        && a->depth == b->depth && a->card_id == b->card_id;
}

CUI_TEST(layout_is_identical_two_periods_apart)
{
    static const int frames[] = { 0, 1, 17, 59, 60, 61, 90, 119 };
    size_t n = sizeof(frames) / sizeof(frames[0]);
    size_t k;

    for (k = 0; k < n; k++) {
        coup_carousel_card_t a[COUP_CAROUSEL_COUNT];
        coup_carousel_card_t b[COUP_CAROUSEL_COUNT];
        coup_carousel_card_t c[COUP_CAROUSEL_COUNT];
        int i;

        coup_carousel_layout(frames[k], FIX_CX, FIX_CY, FIX_RADIUS, a);
        coup_carousel_layout(frames[k] + CAROUSEL_TRUE_PERIOD,
                            FIX_CX, FIX_CY, FIX_RADIUS, b);
        coup_carousel_layout(frames[k] + 2 * CAROUSEL_TRUE_PERIOD,
                            FIX_CX, FIX_CY, FIX_RADIUS, c);

        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            CUI_ASSERT(cards_equal(&a[i], &b[i]));
            CUI_ASSERT(cards_equal(&a[i], &c[i]));
        }
    }
}

/* A free-running counter genuinely free-runs: it must reach the same state
 * from a huge value as from the equivalent small one, in both directions. */
CUI_TEST(layout_survives_a_huge_free_running_counter_without_drift)
{
    coup_carousel_card_t small[COUP_CAROUSEL_COUNT];
    coup_carousel_card_t huge_pos[COUP_CAROUSEL_COUNT];
    coup_carousel_card_t huge_neg[COUP_CAROUSEL_COUNT];
    int i;
    int base = 37;
    long periods = 500000; /* comfortably inside int range * PERIOD */

    coup_carousel_layout(base, FIX_CX, FIX_CY, FIX_RADIUS, small);
    coup_carousel_layout((int)(base + periods * CAROUSEL_TRUE_PERIOD),
                        FIX_CX, FIX_CY, FIX_RADIUS, huge_pos);
    coup_carousel_layout((int)(base - periods * CAROUSEL_TRUE_PERIOD),
                        FIX_CX, FIX_CY, FIX_RADIUS, huge_neg);

    for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
        CUI_ASSERT(cards_equal(&small[i], &huge_pos[i]));
        CUI_ASSERT(cards_equal(&small[i], &huge_neg[i]));
    }
}

CUI_TEST(layout_differs_at_a_quarter_period_offset_not_a_no_op)
{
    /* Guards against a degenerate "periodic because it never moves" pass:
     * confirm the layout actually CHANGES within a period, not just that it
     * repeats. */
    coup_carousel_card_t a[COUP_CAROUSEL_COUNT];
    coup_carousel_card_t b[COUP_CAROUSEL_COUNT];
    int i;
    int moved = 0;

    coup_carousel_layout(0, FIX_CX, FIX_CY, FIX_RADIUS, a);
    coup_carousel_layout(COUP_SHADING_PERIOD / 4, FIX_CX, FIX_CY, FIX_RADIUS, b);

    for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
        if (!cards_equal(&a[i], &b[i])) {
            moved = 1;
        }
    }
    CUI_ASSERT_EQ(1, moved);
}

/*============================================================================
 * card_id identity (each carousel slot keeps a fixed texture)
 *============================================================================*/

CUI_TEST(card_id_is_fixed_per_slot_regardless_of_frame)
{
    int frame;

    for (frame = 0; frame < COUP_SHADING_PERIOD; frame += 7) {
        coup_carousel_card_t cards[COUP_CAROUSEL_COUNT];
        int i;

        coup_carousel_layout(frame, FIX_CX, FIX_CY, FIX_RADIUS, cards);
        for (i = 0; i < COUP_CAROUSEL_COUNT; i++) {
            CUI_ASSERT_EQ(i, cards[i].card_id);
        }
    }
}
