/**
 * test_coup_identity.c - Who am I, in a lobby whose ids came off the wire?
 *
 * WHY THIS TEST EXISTS
 *
 *   Reported, dialling in from a Saturn while a web browser on the same LAN
 *   was playing:
 *
 *     1. Joining mid-match dropped the Saturn into the LOBBY, not a
 *        spectator view.
 *     2. Pressing READY on the Saturn toggled ready for BOTH the Saturn
 *        player and the separate web player.
 *     3. With everyone apparently ready, START did not start the match.
 *     4. The web client showed the Saturn player as NOT ready, while the
 *        Saturn showed itself ready.
 *
 *   This is the FOURTH appearance of the same family in this project - after
 *   the VICTORY/DEFEAT banner (7cbc219), the game-over recap ordering
 *   (a58c7ed) and the spectator name (ac3e356). Every time, one variable
 *   meant two different things at two different times, and every time the
 *   first tests written for it PASSED, because they built a table where the
 *   two meanings happened to agree.
 *
 *   So the load-bearing test here is
 *   `is_self_lands_on_the_right_row_when_the_two_id_spaces_OVERLAP`. It
 *   constructs the collision deliberately: coup_start_game() stamps SEAT
 *   INDICES 0,1,2... into players[].id, and a LOBBY_STATE then arrives
 *   carrying WIRE user_ids drawn from the SAME numeric range. A test where
 *   the two spaces do not overlap proves nothing, and that is precisely why
 *   this survived three previous fixes.
 *
 *   These tests drive the REAL network path - frames are pushed through a
 *   transport into coup_tick(), so process_message() does the decoding. They
 *   do not hand-assign the state they then check.
 */

#include "cui_test_framework.h"
#include "coup.h"
#include "coup_protocol.h"
#include "test_coup_game_helpers.h"

#include <string.h>

/* ==========================================================================
 * A transport that plays the part of the server.
 *
 * RX is a byte queue the test fills with framed messages; TX is captured so
 * a test can prove what the client actually put on the wire (symptom 4 turns
 * on whether the Saturn's READY ever reached the server at all).
 * ========================================================================== */

#define FAKE_RX_CAP  4096
#define FAKE_TX_CAP  4096

typedef struct {
    uint8_t rx[FAKE_RX_CAP];
    int     rx_len;
    int     rx_pos;
    uint8_t tx[FAKE_TX_CAP];
    int     tx_len;
} fake_link_t;

static fake_link_t g_link;

static bool fake_rx_ready(void* ctx)
{
    fake_link_t* l = (fake_link_t*)ctx;
    return l->rx_pos < l->rx_len;
}

static uint8_t fake_rx_byte(void* ctx)
{
    fake_link_t* l = (fake_link_t*)ctx;
    return (l->rx_pos < l->rx_len) ? l->rx[l->rx_pos++] : 0u;
}

static int fake_send(void* ctx, const uint8_t* data, int len)
{
    fake_link_t* l = (fake_link_t*)ctx;
    int i;
    for (i = 0; i < len && l->tx_len < FAKE_TX_CAP; i++)
        l->tx[l->tx_len++] = data[i];
    return len;
}

static bool fake_is_connected(void* ctx) { (void)ctx; return true; }

static cui_transport_t g_fake_transport;

static void link_reset(void)
{
    memset(&g_link, 0, sizeof(g_link));
    g_fake_transport.rx_ready     = fake_rx_ready;
    g_fake_transport.rx_byte      = fake_rx_byte;
    g_fake_transport.send         = fake_send;
    g_fake_transport.is_connected = fake_is_connected;
    g_fake_transport.ctx          = &g_link;
}

/** Queue one message, wrapped in the [LEN_HI][LEN_LO] framing coup_rx_poll
 *  expects, then tick until the client has consumed it. */
static void server_sends(const uint8_t* payload, int len)
{
    int i;
    g_link.rx[g_link.rx_len++] = (uint8_t)((len >> 8) & 0xFF);
    g_link.rx[g_link.rx_len++] = (uint8_t)(len & 0xFF);
    for (i = 0; i < len; i++)
        g_link.rx[g_link.rx_len++] = payload[i];

    /* COUP_RX_MAX_PER_POLL is 48 bytes, so a long roster needs several
     * frames' worth of polling before it is fully decoded. */
    for (i = 0; i < 64 && g_link.rx_pos < g_link.rx_len; i++)
        tick_frames(1);
    tick_frames(1);
}

/* ==========================================================================
 * Message builders (server -> client), per the SNCP table in
 * coup-reference.md and the encoders in coup_protocol.h.
 * ========================================================================== */

/** WELCOME_BACK: [0x83][user_id:1][uuid:36][name_len:1][name:N] */
static void send_welcome_back(uint8_t user_id, const char* name)
{
    uint8_t buf[128];
    int n = 0, i;
    int name_len = (int)strlen(name);

    buf[n++] = SNCP_MSG_WELCOME_BACK;
    buf[n++] = user_id;
    for (i = 0; i < SNCP_UUID_LEN; i++)
        buf[n++] = (uint8_t)('0' + (i % 10));
    buf[n++] = (uint8_t)name_len;
    for (i = 0; i < name_len; i++)
        buf[n++] = (uint8_t)name[i];

    server_sends(buf, n);
}

typedef struct {
    uint8_t     id;
    const char* name;
    bool        ready;
    bool        is_bot;
} roster_row_t;

/** LOBBY_STATE: [0xA0][count:1][{id:1,name:LP,ready:1,is_bot:1,diff:1}...] */
static void send_lobby_state(const roster_row_t* rows, int count)
{
    uint8_t buf[256];
    int n = 0, r, i;

    buf[n++] = COUP_MSG_LOBBY_STATE;
    buf[n++] = (uint8_t)count;
    for (r = 0; r < count; r++) {
        int name_len = (int)strlen(rows[r].name);
        buf[n++] = rows[r].id;
        buf[n++] = (uint8_t)name_len;
        for (i = 0; i < name_len; i++)
            buf[n++] = (uint8_t)rows[r].name[i];
        buf[n++] = rows[r].ready ? 1u : 0u;
        buf[n++] = rows[r].is_bot ? 1u : 0u;
        buf[n++] = 0u;                      /* difficulty */
    }

    server_sends(buf, n);
}

/* ==========================================================================
 * Scenario
 * ========================================================================== */

/* The reported table: the web player authenticated first and holds wire
 * user_id 1; the Saturn dialled in second and holds wire user_id 2. Those
 * are the ids server.py hands out - next_user_id starts at 1 and increments
 * per connection (server.py:679,1063,1104). */
#define WEB_UID     1
#define SATURN_UID  2

static const roster_row_t k_roster[2] = {
    { WEB_UID,    "FARKUS", false, false },
    { SATURN_UID, "A",      false, false }
};

/** Bring the client up as the Saturn: authenticated, in the lobby, with the
 *  two-player roster the server broadcast. */
static void saturn_joins_lobby(void)
{
    game_setup();
    link_reset();
    coup_set_transport(&g_fake_transport);
    coup_on_connected();

    send_welcome_back(SATURN_UID, "A");
    send_lobby_state(k_roster, 2);
}

/** Index of the row carrying a given wire id, or -1. */
static int row_with_wire_id(uint8_t id)
{
    int i;
    for (i = 0; i < st()->player_count; i++)
        if (st()->players[i].id == id) return i;
    return -1;
}

static int count_self_rows(void)
{
    int i, n = 0;
    for (i = 0; i < st()->player_count; i++)
        if (st()->players[i].is_self) n++;
    return n;
}

static int count_ready_rows(void)
{
    int i, n = 0;
    for (i = 0; i < st()->player_count; i++)
        if (coup_lobby_row_ready(st(), i)) n++;
    return n;
}

/* ==========================================================================
 * The roster arrives and is decoded (a control - if this fails, nothing
 * below means anything).
 * ========================================================================== */

CUI_TEST(the_lobby_roster_decodes_off_the_wire)
{
    saturn_joins_lobby();

    CUI_ASSERT_EQ((int)COUP_SCREEN_LOBBY, (int)st()->screen);
    CUI_ASSERT_EQ(2, st()->player_count);
    CUI_ASSERT_EQ(0, strcmp(st()->players[0].name, "FARKUS"));
    CUI_ASSERT_EQ(0, strcmp(st()->players[1].name, "A"));
    CUI_ASSERT_EQ(SATURN_UID, (int)st()->server_user_id);
}

/* ==========================================================================
 * SYMPTOM 2 - one READY press, two seats lit.
 * ========================================================================== */

CUI_TEST(pressing_ready_marks_exactly_one_row)
{
    saturn_joins_lobby();
    CUI_ASSERT_EQ(0, count_ready_rows());

    press(CUI_INPUT_CONFIRM);          /* A = toggle ready */

    /* THE gate for symptom 2. update_lobby() writes
     *     g_state.players[0].ready = g_state.my_ready;
     * onto seat 0 unconditionally, and seat 0 is the WEB player here, while
     * the renderer separately shows my_ready on whichever row is is_self.
     * So one press lights two seats. */
    CUI_ASSERT_EQ(1, count_ready_rows());
}

CUI_TEST(pressing_ready_marks_MY_row_and_not_the_other_players)
{
    int mine, theirs;

    saturn_joins_lobby();
    mine   = row_with_wire_id(SATURN_UID);
    theirs = row_with_wire_id(WEB_UID);
    CUI_ASSERT_GE(mine, 0);
    CUI_ASSERT_GE(theirs, 0);

    press(CUI_INPUT_CONFIRM);

    CUI_ASSERT_TRUE(coup_lobby_row_ready(st(), mine));
    CUI_ASSERT_FALSE(coup_lobby_row_ready(st(), theirs));
}

CUI_TEST(unreadying_clears_only_my_row)
{
    int theirs;

    saturn_joins_lobby();
    theirs = row_with_wire_id(WEB_UID);

    press(CUI_INPUT_CONFIRM);   /* ready */
    press(CUI_INPUT_CONFIRM);   /* unready */

    CUI_ASSERT_EQ(0, count_ready_rows());
    CUI_ASSERT_FALSE(coup_lobby_row_ready(st(), theirs));
}

/* ==========================================================================
 * SYMPTOM 3 - "Ready: 2/2, press START to begin" while the server refuses.
 *
 * The client cannot make the server start a game; what it CAN do is stop
 * claiming a readiness the server never saw. The counter the lobby prints is
 * the same expression as the seat lamps, so it inherits the same lie.
 * ========================================================================== */

CUI_TEST(the_ready_counter_does_not_count_a_player_who_is_not_ready)
{
    saturn_joins_lobby();
    press(CUI_INPUT_CONFIRM);

    /* The web player has NOT readied - the server's roster says so. A
     * count of 2 here is exactly what puts "Press START to begin!" on
     * screen while _handle_start_game_request refuses with "All players
     * must be ready" (server.py:1241). */
    CUI_ASSERT_EQ(1, count_ready_rows());
}

/* ==========================================================================
 * SYMPTOM 4 - the Saturn shows itself ready; the server and the web client
 * do not agree.
 *
 * A READY pressed while a game is still running is DROPPED by the server:
 *     if not info.authenticated or self.game_active: return   (server.py:1212)
 * The client had already set its local my_ready shadow, and because the self
 * row displays that shadow instead of the roster, no later LOBBY_STATE could
 * ever correct it. The disagreement is therefore permanent, not transient.
 * ========================================================================== */

CUI_TEST(a_lobby_state_that_contradicts_my_local_ready_wins)
{
    int mine;

    saturn_joins_lobby();
    press(CUI_INPUT_CONFIRM);
    mine = row_with_wire_id(SATURN_UID);
    CUI_ASSERT_TRUE(coup_lobby_row_ready(st(), mine));

    /* The server re-broadcasts the roster and still has us as not ready. */
    send_lobby_state(k_roster, 2);

    mine = row_with_wire_id(SATURN_UID);
    CUI_ASSERT_GE(mine, 0);
    CUI_ASSERT_FALSE(coup_lobby_row_ready(st(), mine));
}

CUI_TEST(a_lobby_state_that_confirms_my_ready_is_shown_as_ready)
{
    roster_row_t confirmed[2];
    int mine;

    saturn_joins_lobby();
    press(CUI_INPUT_CONFIRM);

    /* The other direction, so the fix cannot be "never show self ready". */
    confirmed[0] = k_roster[0];
    confirmed[1] = k_roster[1];
    confirmed[1].ready = true;
    send_lobby_state(confirmed, 2);

    mine = row_with_wire_id(SATURN_UID);
    CUI_ASSERT_GE(mine, 0);
    CUI_ASSERT_TRUE(coup_lobby_row_ready(st(), mine));
}

CUI_TEST(the_ready_press_actually_reaches_the_wire)
{
    int i;
    bool found = false;

    saturn_joins_lobby();
    g_link.tx_len = 0;
    press(CUI_INPUT_CONFIRM);

    /* If READY never left the Saturn, symptom 4 would be a send bug rather
     * than a display bug, and the fix would be somewhere else entirely.
     * Frame is [LEN_HI][LEN_LO][0x10]. */
    for (i = 0; i + 2 < g_link.tx_len; i++) {
        if (g_link.tx[i] == 0 && g_link.tx[i + 1] == 1 &&
            g_link.tx[i + 2] == COUP_MSG_READY) {
            found = true;
            break;
        }
    }
    CUI_ASSERT_TRUE(found);
}

/* ==========================================================================
 * THE ROOT CAUSE - the two id spaces overlap.
 * ========================================================================== */

CUI_TEST(is_self_marks_exactly_one_row_in_a_plain_lobby)
{
    saturn_joins_lobby();
    CUI_ASSERT_EQ(1, count_self_rows());
    CUI_ASSERT_TRUE(st()->players[row_with_wire_id(SATURN_UID)].is_self);
}

CUI_TEST(is_self_lands_on_the_right_row_when_the_two_id_spaces_OVERLAP)
{
    /* THE test. Everything else in this file can pass on broken code if the
     * numbers happen not to collide.
     *
     * Build the collision on purpose:
     *   - the wire roster holds user_ids 1 and 2;
     *   - a game starts and coup_start_game() stamps SEAT INDICES 0 and 1
     *     over players[].id, and sets my_id to the seat index 1;
     *   - a LOBBY_STATE then arrives - which really happens, _end_game()
     *     clears game_active and broadcasts before the player has confirmed
     *     the game-over screen (server.py:1690,1713) - refilling
     *     players[].id with the WIRE ids 1 and 2.
     *
     * Seat index 1 and wire user_id 1 are now the same number belonging to
     * two different people. is_self = (players[i].id == my_id) therefore
     * matches the WEB player's row. Exactly one row is marked, so a count
     * alone would pass; it is the WRONG row that matters. */
    int mine;

    saturn_joins_lobby();

    /* Seat 1 is the Saturn, matching its position in the roster. */
    CUI_ASSERT_EQ(1, row_with_wire_id(SATURN_UID));
    coup_start_game(31337u, 1u);
    CUI_ASSERT_EQ(1, (int)st()->my_id);
    CUI_ASSERT_EQ(0, (int)st()->players[0].id);
    CUI_ASSERT_EQ(1, (int)st()->players[1].id);

    /* Back to the lobby roster, wire ids restored. */
    send_lobby_state(k_roster, 2);

    CUI_ASSERT_EQ(1, count_self_rows());

    mine = row_with_wire_id(SATURN_UID);
    CUI_ASSERT_GE(mine, 0);
    CUI_ASSERT_TRUE(st()->players[mine].is_self);
    CUI_ASSERT_FALSE(st()->players[row_with_wire_id(WEB_UID)].is_self);
}

CUI_TEST(ready_still_marks_my_row_after_the_overlap)
{
    /* The consequence the player actually sees: with is_self stuck on
     * somebody else's seat, the next READY press paints the wrong chair.
     *
     * Walked the way it really happens, because the screen matters - the
     * roster lands while the game-over screen is still up (_end_game clears
     * game_active and broadcasts before the player has confirmed), and only
     * then does the player return to the lobby and press A. */
    coup_state_t* m;
    int mine, theirs;

    saturn_joins_lobby();
    coup_start_game(31337u, 1u);        /* seat indices stamped over the ids */

    m = st_mut();
    m->screen = COUP_SCREEN_GAME_OVER;
    send_lobby_state(k_roster, 2);      /* wire ids back, my_id still a seat */

    press(CUI_INPUT_CONFIRM);           /* leave game over -> lobby */
    CUI_ASSERT_EQ((int)COUP_SCREEN_LOBBY, (int)st()->screen);

    mine   = row_with_wire_id(SATURN_UID);
    theirs = row_with_wire_id(WEB_UID);
    CUI_ASSERT_GE(mine, 0);
    CUI_ASSERT_GE(theirs, 0);

    press(CUI_INPUT_CONFIRM);           /* toggle ready */

    CUI_ASSERT_TRUE(coup_lobby_row_ready(st(), mine));
    CUI_ASSERT_FALSE(coup_lobby_row_ready(st(), theirs));
}

CUI_TEST(returning_to_the_lobby_restores_the_wire_identity)
{
    /* coup_game.c restores my_id from server_user_id when the player
     * confirms the game-over screen, but nothing recomputed is_self, so a
     * stale mark could outlive the restore until the next roster arrived -
     * and in a quiet lobby the next roster may never arrive. */
    coup_state_t* m;

    saturn_joins_lobby();
    coup_start_game(31337u, 1u);

    m = st_mut();
    m->screen = COUP_SCREEN_GAME_OVER;
    press(CUI_INPUT_CONFIRM);

    CUI_ASSERT_EQ((int)COUP_SCREEN_LOBBY, (int)st()->screen);
    CUI_ASSERT_EQ((int)SATURN_UID, (int)st()->my_id);
    CUI_ASSERT_EQ(1, count_self_rows());
}

/* ==========================================================================
 * SYMPTOM 1 - a mid-match joiner must not land in the lobby.
 * ========================================================================== */

CUI_TEST(a_mid_game_joiner_is_a_spectator_not_a_lobby_member)
{
    /* The client learns a game is in progress from GAME_START carrying the
     * spectator sentinel pid 0xFF (server.py:1121). It must then be in the
     * spectator state and OFF the lobby screen. */
    saturn_joins_lobby();
    CUI_ASSERT_FALSE(st()->is_spectator);

    coup_start_game(31337u, 0xFFu);

    CUI_ASSERT_TRUE(st()->is_spectator);
    CUI_ASSERT_TRUE(st()->screen != COUP_SCREEN_LOBBY);
}

CUI_TEST(a_spectator_owns_no_seat)
{
    /* 0xFF is not a seat. Nothing may claim to be the local player, or the
     * spectator sees its own name on somebody else's chair - which is
     * exactly what ac3e356 was reported as. */
    saturn_joins_lobby();
    coup_start_game(31337u, 0xFFu);

    CUI_ASSERT_EQ(0, count_self_rows());
}
