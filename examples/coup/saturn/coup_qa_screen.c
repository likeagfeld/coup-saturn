/**
 * coup_qa_screen.c - Force a screen at boot, for capture QA only.
 * See coup_qa_screen.h. Compiles to nothing without -DCOUP_QA_SCREEN.
 */

#include "coup_qa_screen.h"

#ifdef COUP_QA_SCREEN

#include "coup.h"
#include "saturn_bg.h"
#include "saturn_cd.h"
#include "coup_bg_index.h"

#include <string.h>

/*
 * COUP_QA_SCREEN >= 100 selects the CD STRESS mode instead of a screen.
 *
 * The question it answers: does slCdAbort() actually release the file handle
 * after a successful load, or does the handle leak? slCdInit() is given a
 * fixed open-file budget, so a leak shows up as slCdOpen() failing after N
 * loads - and N could easily be larger than a short play session, which is
 * exactly how a bug like this reaches a player and never a tester.
 *
 * This cycles every scene repeatedly at boot and leaves the outcome in
 * g_saturn_cd_stats for qa_cd_stress.py to read over READ_CORE_RAM. A leak
 * appears as last_result going non-zero with loads stuck below the target.
 */
#define QA_STRESS_BASE  100
#define QA_STRESS_LOADS 64

/* coup_get_state() hands out a const pointer because nothing in the game has
 * any business writing the state from outside coup_game.c. This is the one
 * exception, it exists only in a QA build, and it writes fields the renderer
 * reads rather than anything the rules or the protocol depend on. */
static coup_state_t* qa_state(void)
{
    return (coup_state_t*)(void*)coup_get_state();
}

static void qa_seat(coup_state_t* st, int i, const char* name, int coins,
                    int c0, int c1, int self)
{
    coup_player_t* p = &st->players[i];

    p->id = (uint8_t)i;
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
    p->coins = (uint8_t)coins;
    p->cards[0] = (uint8_t)c0;
    p->cards[1] = (uint8_t)c1;
    p->alive = true;
    p->ready = true;
    p->is_self = self ? true : false;
    p->is_bot = self ? false : true;
    p->difficulty = 1;
}

/** Cycle every scene off the disc, many times, and leave the stats behind. */
static void qa_cd_stress(int target_loads)
{
    int n = 0;
    int scene = 0;

    while (n < target_loads) {
        /* Step through every scene so each load is a genuine read with a
         * seek, not the no-op that saturn_bg_set_scene() does when the
         * requested scene is already resident. */
        scene = (scene + 1) % COUP_BG_SCENE_COUNT;
        saturn_bg_set_scene(scene);
        if (saturn_cd_stats()->last_result != 0) {
            return;         /* leave the failure in place for the gate */
        }
        n++;
    }
}

void coup_qa_force_screen(void)
{
    coup_state_t* st = qa_state();
    int i;

    if (COUP_QA_SCREEN >= QA_STRESS_BASE) {
        qa_cd_stress(QA_STRESS_LOADS);
        return;
    }

    /* A full table, so seats, names, coin counts and card backs all render. */
    st->player_count = 4;
    qa_seat(st, 0, "GARY", 7, COUP_CHAR_DUKE, COUP_CHAR_CONTESSA, 1);
    qa_seat(st, 1, "MARLOWE", 3, COUP_CHAR_FACEDOWN, COUP_CHAR_FACEDOWN, 0);
    qa_seat(st, 2, "VESPER", 12, COUP_CHAR_FACEDOWN, COUP_CHAR_FACEDOWN, 0);
    qa_seat(st, 3, "RAVEN", 0, COUP_CHAR_FACEDOWN, COUP_CHAR_FACEDOWN, 0);

    st->my_cards[0] = COUP_CHAR_DUKE;
    st->my_cards[1] = COUP_CHAR_CONTESSA;
    st->current_turn_id = 0;
    st->menu_cursor = 1;

    /* Log lines, so the log panel is not an empty box in the capture. */
    for (i = 0; i < 3; i++) {
        static const char* const lines[3] = {
            "VESPER takes tax",
            "MARLOWE blocks with Duke",
            "RAVEN challenges"
        };
        coup_log(lines[i]);
    }

    st->screen = (coup_screen_t)COUP_QA_SCREEN;

    switch (st->screen) {
    case COUP_SCREEN_GAME:
        /* The response phase is the busiest layout: title, list, timer bar. */
        st->phase = COUP_PHASE_CHALLENGE_WAIT;
        st->declared_actor = 2;
        st->declared_action = COUP_ACT_TAX;
        st->declared_claim = COUP_CHAR_DUKE;
        st->response_timer = 8;
        st->response_timeout = 12;
        break;
    case COUP_SCREEN_GAME_OVER:
        st->winner_id = 0;              /* self wins -> VICTORY backdrop */
        strncpy(st->winner_name, "GARY", sizeof(st->winner_name) - 1);
        st->winner_name[sizeof(st->winner_name) - 1] = '\0';
        break;
    case COUP_SCREEN_CONNECTING:
        st->connect_stage = 2;
        break;
    default:
        break;
    }
}

#else

void coup_qa_force_screen(void)
{
}

#endif /* COUP_QA_SCREEN */
