/**
 * coup_protocol.h - Coup Game Network Protocol
 *
 * Binary protocol extending SNCP framing for the Coup card game.
 * Uses the same [LEN_HI][LEN_LO][PAYLOAD...] framing as the chat protocol.
 * Reuses SNCP auth handshake (CONNECT/WELCOME) for player authentication.
 *
 * Header-only: all functions are static inline.
 */

#ifndef COUP_PROTOCOL_H
#define COUP_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include "cui_transport.h"

/*============================================================================
 * Reuse SNCP Auth Messages (from chat protocol)
 *============================================================================*/

#define SNCP_MSG_CONNECT           0x01
#define SNCP_MSG_SET_USERNAME      0x02
#define SNCP_MSG_HEARTBEAT         0x04
#define SNCP_MSG_DISCONNECT        0x05

#define SNCP_MSG_USERNAME_REQUIRED 0x81
#define SNCP_MSG_WELCOME           0x82
#define SNCP_MSG_WELCOME_BACK      0x83
#define SNCP_MSG_USERNAME_TAKEN    0x84

#define SNCP_UUID_LEN              36

/*============================================================================
 * Coup Client -> Server Messages (0x10 - 0x1F)
 *============================================================================*/

#define COUP_MSG_READY             0x10  /* Toggle ready state */
#define COUP_MSG_ACTION            0x11  /* Declare action [action:1][target:1] */
#define COUP_MSG_RESPONSE          0x12  /* Response [resp:1] (pass/challenge/block) */
#define COUP_MSG_BLOCK_CLAIM       0x13  /* Block claim [character:1] */
#define COUP_MSG_LOSE_INFLUENCE    0x14  /* Choose card to lose [index:1] */
#define COUP_MSG_EXCHANGE_CHOICE   0x15  /* Exchange pick [keep0:1][keep1:1] */
#define COUP_MSG_START_GAME_REQ   0x16  /* Host requests game start (no payload) */
#define COUP_MSG_ADD_BOT           0x17  /* Add bot [difficulty:1] */
#define COUP_MSG_REMOVE_BOT        0x18  /* Request remove bot (no payload) */
#define COUP_MSG_SET_BOT_DIFFICULTY 0x19 /* Set bot difficulty [bot_index:1][difficulty:1] */
#define COUP_MSG_RESYNC_REQ        0x1A  /* Request resync [last_seen_seq:2 BE] */

/*============================================================================
 * Coup Server -> Client Messages (0xA0 - 0xBF)
 *============================================================================*/

#define COUP_MSG_LOBBY_STATE       0xA0  /* [count:1][{id:1,name:LP,ready:1,is_bot:1,difficulty:1}...] */
#define COUP_MSG_GAME_START        0xA1  /* [seed:4 BE][my_engine_pid:1] */
#define COUP_MSG_YOUR_TURN         0xA2  /* [player_id:1][valid_actions:1] */
#define COUP_MSG_ACTION_DECLARED   0xA3  /* [actor:1][action:1][target:1] */
#define COUP_MSG_CHALLENGE_PROMPT  0xA4  /* [defender_id:1][claimed_char:1][timeout:1] */
#define COUP_MSG_BLOCK_PROMPT      0xA5  /* [target_only:1][block_chars:1][timeout:1] */
#define COUP_MSG_BLOCK_CHALLENGE   0xA6  /* [blocker:1][block_char:1][timeout:1] */
#define COUP_MSG_CHALLENGE_RESULT  0xA7  /* [challenger:1][defender:1][success:1][char:1] */
#define COUP_MSG_LOSE_PROMPT       0xA8  /* [player_id:1][timeout:1] */
#define COUP_MSG_EXCHANGE_OFFER    0xA9  /* [player_id:1][c0:1][c1:1][c2:1][c3:1] */
#define COUP_MSG_PLAYER_UPDATE     0xAA  /* [id:1][coins:1][card0:1][card1:1][alive:1] */
#define COUP_MSG_HAND_UPDATE       0xAB  /* [player_id:1][card0:1][card1:1] */
#define COUP_MSG_ELIMINATED        0xAC  /* [id:1] */
#define COUP_MSG_GAME_OVER         0xAD  /* [winner_id:1] */
#define COUP_MSG_LOG               0xAE  /* [len:1][text:N] */
#define COUP_MSG_COINS_UPDATE      0xAF  /* [id:1][coins:1] */
#define COUP_MSG_BLOCK_DECLARED    0xB0  /* [blocker:1][block_char:1] */
#define COUP_MSG_ROUND_UPDATE      0xB1  /* [round:1] */
#define COUP_MSG_INPUT_RELAY       0xB2  /* [seq:2 BE][input_type:1][player_id:1][data...] */
#define COUP_MSG_RESYNC            0xB3  /* [count:1][{entry_len:1,seq:2 BE,type:1,pid:1,data...}...] */
#define COUP_MSG_RESYNC_FULL       0xB4  /* [seed:4 BE][my_pid:1][total:2 BE] */
#define COUP_MSG_ACTION_REJECTED   0xB5  /* [current_seq:2 BE][phase:1] */

/* INPUT_RELAY input type codes */
#define COUP_RELAY_START_GAME      0
#define COUP_RELAY_ACTION          1
#define COUP_RELAY_RESPONSE        2
#define COUP_RELAY_BLOCK_CLAIM     3
#define COUP_RELAY_LOSE_INFLUENCE  4
#define COUP_RELAY_EXCHANGE_CHOICE 5
#define COUP_RELAY_TIMEOUT         6

/*============================================================================
 * Buffer Sizes
 *============================================================================*/

#define COUP_RX_FRAME_SIZE  512    /* Largest: LOBBY_STATE with 8 players */
#define COUP_TX_FRAME_SIZE  64     /* Largest: SET_USERNAME */

/*============================================================================
 * Frame Send/Receive (reuse SNCP framing)
 *============================================================================*/

/**
 * Send a binary frame: [LEN_HI][LEN_LO][payload...]
 */
static inline void coup_send_frame(const cui_transport_t* transport,
                                    const uint8_t* payload, int payload_len)
{
    uint8_t hdr[2];
    hdr[0] = (uint8_t)((payload_len >> 8) & 0xFF);
    hdr[1] = (uint8_t)(payload_len & 0xFF);
    cui_transport_send(transport, hdr, 2);
    cui_transport_send(transport, payload, payload_len);
}

/**
 * Receive state machine (identical to SNCP).
 */
typedef struct {
    uint8_t* buf;
    int      buf_size;
    int      rx_pos;
    int      frame_len;
} coup_rx_state_t;

static inline void coup_rx_init(coup_rx_state_t* st, uint8_t* buf, int buf_size)
{
    st->buf = buf;
    st->buf_size = buf_size;
    st->rx_pos = 0;
    st->frame_len = -1;
}

/* Max UART bytes to process per poll call.  Bounds worst-case A-bus
 * stall time so audio refill and rendering aren't starved.  At 28800
 * baud the modem delivers ~48 bytes/frame; leftover bytes stay in the
 * UART FIFO and are drained on the next frame. */
#define COUP_RX_MAX_PER_POLL  48

static inline int coup_rx_poll(coup_rx_state_t* st,
                                const cui_transport_t* transport)
{
    int bytes_read = 0;
    while (bytes_read < COUP_RX_MAX_PER_POLL && cui_transport_rx_ready(transport)) {
        uint8_t b = cui_transport_rx_byte(transport);
        bytes_read++;

        if (st->frame_len < 0) {
            st->buf[st->rx_pos++] = b;
            if (st->rx_pos == 2) {
                st->frame_len = ((int)st->buf[0] << 8) | (int)st->buf[1];
                st->rx_pos = 0;
                if (st->frame_len > st->buf_size || st->frame_len == 0) {
                    st->frame_len = -1;
                    st->rx_pos = 0;
                    return -1;
                }
            }
        } else {
            st->buf[st->rx_pos++] = b;
            if (st->rx_pos >= st->frame_len) {
                int len = st->frame_len;
                st->frame_len = -1;
                st->rx_pos = 0;
                return len;
            }
        }
    }
    return 0;
}

/*============================================================================
 * Decode Helpers
 *============================================================================*/

static inline int coup_read_string(const uint8_t* p, int remaining,
                                    char* dst, int max)
{
    int slen, copy, i;
    if (remaining < 1) { dst[0] = '\0'; return -1; }
    slen = (int)p[0];
    if (remaining < 1 + slen) { dst[0] = '\0'; return -1; }
    copy = (slen < max - 1) ? slen : (max - 1);
    for (i = 0; i < copy; i++) dst[i] = (char)p[1 + i];
    dst[copy] = '\0';
    return 1 + slen;
}

/*============================================================================
 * Client -> Server Encode Functions
 *============================================================================*/

/** Encode CONNECT (new user, no UUID). Returns total frame size. */
static inline int coup_encode_connect(uint8_t* buf)
{
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = SNCP_MSG_CONNECT;
    return 3;
}

/** Encode CONNECT with UUID. Returns total frame size. */
static inline int coup_encode_connect_uuid(uint8_t* buf, const char* uuid)
{
    int i;
    buf[0] = 0x00;
    buf[1] = 37;
    buf[2] = SNCP_MSG_CONNECT;
    for (i = 0; i < SNCP_UUID_LEN; i++)
        buf[3 + i] = (uint8_t)uuid[i];
    return 3 + SNCP_UUID_LEN;
}

/** Encode SET_USERNAME. Returns total frame size. */
static inline int coup_encode_set_username(uint8_t* buf, const char* name)
{
    int nlen = 0;
    int payload_len;
    int i;
    while (name[nlen]) nlen++;
    payload_len = 1 + 1 + nlen;
    buf[0] = (uint8_t)((payload_len >> 8) & 0xFF);
    buf[1] = (uint8_t)(payload_len & 0xFF);
    buf[2] = SNCP_MSG_SET_USERNAME;
    buf[3] = (uint8_t)nlen;
    for (i = 0; i < nlen; i++)
        buf[4 + i] = (uint8_t)name[i];
    return 2 + payload_len;
}

/** Encode DISCONNECT. Returns total frame size. */
static inline int coup_encode_disconnect(uint8_t* buf)
{
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = SNCP_MSG_DISCONNECT;
    return 3;
}

/** Encode HEARTBEAT. Returns total frame size. */
static inline int coup_encode_heartbeat(uint8_t* buf)
{
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = SNCP_MSG_HEARTBEAT;
    return 3;
}

/** Encode READY toggle. Returns total frame size. */
static inline int coup_encode_ready(uint8_t* buf)
{
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = COUP_MSG_READY;
    return 3;
}

/** Encode START_GAME request (any player). Returns total frame size. */
static inline int coup_encode_start_game(uint8_t* buf)
{
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = COUP_MSG_START_GAME_REQ;
    return 3;
}

/** Encode ADD_BOT request with difficulty. Returns total frame size. */
static inline int coup_encode_add_bot(uint8_t* buf, uint8_t difficulty)
{
    buf[0] = 0x00;
    buf[1] = 0x02;
    buf[2] = COUP_MSG_ADD_BOT;
    buf[3] = difficulty;
    return 4;
}

/** Encode REMOVE_BOT request. Returns total frame size. */
static inline int coup_encode_remove_bot(uint8_t* buf)
{
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = COUP_MSG_REMOVE_BOT;
    return 3;
}

/** Encode SET_BOT_DIFFICULTY. Returns total frame size. */
static inline int coup_encode_set_bot_difficulty(uint8_t* buf,
                                                  uint8_t bot_index,
                                                  uint8_t difficulty)
{
    buf[0] = 0x00;
    buf[1] = 0x03;
    buf[2] = COUP_MSG_SET_BOT_DIFFICULTY;
    buf[3] = bot_index;
    buf[4] = difficulty;
    return 5;
}

/** Encode ACTION. Returns total frame size. */
static inline int coup_encode_action(uint8_t* buf, uint8_t action, uint8_t target)
{
    buf[0] = 0x00;
    buf[1] = 0x03;
    buf[2] = COUP_MSG_ACTION;
    buf[3] = action;
    buf[4] = target;
    return 5;
}

/** Encode RESPONSE (pass/challenge/block). Returns total frame size. */
static inline int coup_encode_response(uint8_t* buf, uint8_t response)
{
    buf[0] = 0x00;
    buf[1] = 0x02;
    buf[2] = COUP_MSG_RESPONSE;
    buf[3] = response;
    return 4;
}

/** Encode BLOCK_CLAIM. Returns total frame size. */
static inline int coup_encode_block_claim(uint8_t* buf, uint8_t character)
{
    buf[0] = 0x00;
    buf[1] = 0x02;
    buf[2] = COUP_MSG_BLOCK_CLAIM;
    buf[3] = character;
    return 4;
}

/** Encode LOSE_INFLUENCE. Returns total frame size. */
static inline int coup_encode_lose_influence(uint8_t* buf, uint8_t card_index)
{
    buf[0] = 0x00;
    buf[1] = 0x02;
    buf[2] = COUP_MSG_LOSE_INFLUENCE;
    buf[3] = card_index;
    return 4;
}

/** Encode EXCHANGE_CHOICE. Returns total frame size. */
static inline int coup_encode_exchange_choice(uint8_t* buf,
                                               uint8_t keep0, uint8_t keep1)
{
    buf[0] = 0x00;
    buf[1] = 0x03;
    buf[2] = COUP_MSG_EXCHANGE_CHOICE;
    buf[3] = keep0;
    buf[4] = keep1;
    return 5;
}

/*============================================================================
 * INPUT_RELAY Decode (wire -> coup_input_t)
 *============================================================================*/

#ifdef COUP_RULES_H  /* Only available when coup_rules.h is included first */

/**
 * Decode an INPUT_RELAY payload into a coup_input_t.
 * payload points to the frame data AFTER the message type byte.
 * remaining is the number of bytes in payload.
 * out_seq receives the relay sequence number (may be NULL).
 * Returns 0 on success, -1 on malformed message.
 *
 * Wire format: [seq_hi:1][seq_lo:1][input_type:1][player_id:1][data...]
 */
static inline int coup_decode_input_relay(const uint8_t* payload, int remaining,
                                           coup_input_t* out, uint16_t* out_seq)
{
    uint8_t input_type;
    if (remaining < 4) return -1;

    if (out_seq) *out_seq = (uint16_t)((payload[0] << 8) | payload[1]);
    input_type     = payload[2];
    out->player_id = payload[3];

    switch (input_type) {
    case COUP_RELAY_START_GAME:
        out->type = COUP_INPUT_START_GAME;
        return 0;

    case COUP_RELAY_ACTION:
        if (remaining < 6) return -1;
        out->type = COUP_INPUT_ACTION;
        out->data.action.action    = payload[4];
        out->data.action.target_id = payload[5];
        return 0;

    case COUP_RELAY_RESPONSE:
        if (remaining < 5) return -1;
        out->type = COUP_INPUT_RESPONSE;
        out->data.response.response = payload[4];
        return 0;

    case COUP_RELAY_BLOCK_CLAIM:
        if (remaining < 5) return -1;
        out->type = COUP_INPUT_BLOCK_CLAIM;
        out->data.block_claim.character = payload[4];
        return 0;

    case COUP_RELAY_LOSE_INFLUENCE:
        if (remaining < 5) return -1;
        out->type = COUP_INPUT_LOSE_INFLUENCE;
        out->data.lose_influence.card_idx = payload[4];
        return 0;

    case COUP_RELAY_EXCHANGE_CHOICE:
        if (remaining < 6) return -1;
        out->type = COUP_INPUT_EXCHANGE_CHOICE;
        out->data.exchange_choice.keep[0] = payload[4];
        out->data.exchange_choice.keep[1] = payload[5];
        return 0;

    case COUP_RELAY_TIMEOUT:
        out->type = COUP_INPUT_TIMEOUT;
        return 0;

    default:
        return -1;
    }
}

/** Encode RESYNC_REQ. Returns total frame size. */
static inline int coup_encode_resync_req(uint8_t* buf, uint16_t last_seen_seq)
{
    buf[0] = 0x00;
    buf[1] = 0x03;
    buf[2] = COUP_MSG_RESYNC_REQ;
    buf[3] = (uint8_t)((last_seen_seq >> 8) & 0xFF);
    buf[4] = (uint8_t)(last_seen_seq & 0xFF);
    return 5;
}

#endif /* COUP_RULES_H */

#endif /* COUP_PROTOCOL_H */
