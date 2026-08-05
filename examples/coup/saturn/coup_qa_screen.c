/**
 * coup_qa_screen.c - Force a screen at boot, for capture QA only.
 * See coup_qa_screen.h. Compiles to nothing without -DCOUP_QA_SCREEN.
 */

#include "coup_qa_screen.h"

#ifdef COUP_QA_SCREEN

#include "coup.h"

#include <string.h>

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

void coup_qa_force_screen(void)
{
    coup_state_t* st = qa_state();
    int i;

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
