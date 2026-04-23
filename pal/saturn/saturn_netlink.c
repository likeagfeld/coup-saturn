/**
 * saturn_netlink.c - Saturn NetLink implementation
 *
 * Wraps the XMP API for Jo Engine integration.
 * This file should only be compiled for Saturn target.
 */

#if defined(__SATURN__) && !defined(MOCK_NETWORK)

#include "saturn_netlink.h"
#include <string.h>
#include <stddef.h>

/* Include the actual XMP headers for Saturn build */
#include "XMPPub.h"

/*============================================================================
 * State
 *============================================================================*/

static saturn_netlink_state_t s_state = NETLINK_UNINITIALIZED;
static int32_t s_local_player_id = 0;
static char s_local_player_name[32] = "Player";

/* Session buffers */
#define SESSION_GAME_DATA_SIZE  256
#define SESSION_TX_BUFFER_SIZE  1024
#define SESSION_RX_BUFFER_SIZE  1024

static char s_tx_buffer[SESSION_TX_BUFFER_SIZE];
static char s_rx_buffer[SESSION_RX_BUFFER_SIZE];

/* Active socket */
static XBSocket* s_game_socket = NULL;

/* VBlank and frame tracking */
static uint32_t s_frame_count = 0;
static volatile uint32_t s_vblank_flag = 0;

/* Bounded packet processing */
static int s_packet_limit = 8;  /* Default: 8 packets per frame */
static int s_packets_this_frame = 0;

/* Statistics */
static saturn_netlink_stats_t s_stats = {0};
static uint32_t s_frame_start_ticks = 0;

/* Timer state */
static bool s_timer_initialized = false;

/*============================================================================
 * Player Enumeration Helper
 *============================================================================*/

typedef struct {
    saturn_netlink_player_callback_t callback;
    void* user_data;
} enum_context_t;

static enum_context_t s_enum_ctx;

/* XMP callback adapter */
static Boolean xmp_player_callback(
    void* context,
    long player_id,
    char* player_name,
    short player_flags)
{
    enum_context_t* ctx = (enum_context_t*)context;
    if (!ctx || !ctx->callback) {
        return 0;  /* Stop enumeration */
    }

    bool is_local = (player_flags & kPlayerIsSelf) != 0;

    if (is_local) {
        s_local_player_id = player_id;
        /* Copy name for later */
        int i = 0;
        while (player_name && player_name[i] && i < 31) {
            s_local_player_name[i] = player_name[i];
            i++;
        }
        s_local_player_name[i] = '\0';
    }

    return ctx->callback(player_id, player_name, is_local, ctx->user_data) ? 1 : 0;
}

/*============================================================================
 * Initialization
 *============================================================================*/

saturn_netlink_state_t saturn_netlink_init(void)
{
    if (s_state != NETLINK_UNINITIALIZED) {
        return s_state;
    }

    /* Initialize timer for frame timing */
    saturn_netlink_timer_init();

    /* Reset statistics */
    saturn_netlink_reset_stats();

    /* Initialize XMP */
    XBGameType game_type = XBInitXBAND();

    if (game_type == XBNetworkGame) {
        s_state = NETLINK_NETWORK_GAME;
    } else {
        s_state = NETLINK_LOCAL_GAME;
    }

    return s_state;
}

void saturn_netlink_shutdown(void)
{
    if (s_game_socket) {
        XBSocketClose(s_game_socket);
        s_game_socket = NULL;
    }

    XBCloseSession();
    s_state = NETLINK_UNINITIALIZED;
    s_local_player_id = 0;
}

saturn_netlink_state_t saturn_netlink_get_state(void)
{
    return s_state;
}

bool saturn_netlink_is_network_game(void)
{
    return s_state == NETLINK_NETWORK_GAME;
}

/*============================================================================
 * Frame Update
 *============================================================================*/

void saturn_netlink_service(void)
{
    if (s_state != NETLINK_NETWORK_GAME) {
        return;
    }

    /* These must be called every frame for proper networking */
    XBVBLTask();
    XBNetworkService();
}

/*============================================================================
 * Player Management
 *============================================================================*/

void saturn_netlink_enumerate_players(
    saturn_netlink_player_callback_t callback,
    void* user_data)
{
    if (s_state == NETLINK_UNINITIALIZED) {
        return;
    }

    s_enum_ctx.callback = callback;
    s_enum_ctx.user_data = user_data;

    if (s_state == NETLINK_NETWORK_GAME) {
        /* Use XMP enumeration */
        XBEnumeratePlayerList(xmp_player_callback, &s_enum_ctx, 0);
    } else {
        /* Local game - just report local player */
        if (callback) {
            callback(1, s_local_player_name, true, user_data);
        }
    }
}

int32_t saturn_netlink_get_local_player_id(void)
{
    return s_local_player_id;
}

const char* saturn_netlink_get_player_name(int32_t player_id)
{
    if (player_id == s_local_player_id) {
        return s_local_player_name;
    }

    if (s_state == NETLINK_NETWORK_GAME) {
        return XBGetPlayerName(player_id);
    }

    return "Unknown";
}

/*============================================================================
 * Socket Operations
 *============================================================================*/

XBSocket* saturn_netlink_open_socket(int16_t port)
{
    if (s_state != NETLINK_NETWORK_GAME) {
        return NULL;
    }

    XBSocket* socket = XBSocketOpen(XBDatagramSocket);
    if (!socket) {
        return NULL;
    }

    XBErr err = XBSocketBind(socket, port);
    if (err != XBNoError) {
        XBSocketClose(socket);
        return NULL;
    }

    s_game_socket = socket;
    return socket;
}

void saturn_netlink_close_socket(XBSocket* socket)
{
    if (socket) {
        XBSocketClose(socket);
        if (socket == s_game_socket) {
            s_game_socket = NULL;
        }
    }
}

XBErr saturn_netlink_send(
    XBSocket* socket,
    const void* data,
    int32_t size,
    int32_t recipient_id,
    int16_t recipient_port)
{
    if (!socket || !data || size <= 0) {
        return XBBadPacket;
    }

    XBSender recipient;
    recipient.playerID = recipient_id;
    recipient.port = recipient_port;

    XBErr err = XBSocketSend(socket, (char*)data, size, &recipient);

    if (err == XBNoError) {
        s_stats.packets_sent++;
        s_stats.bytes_sent += size;
    }

    return err;
}

XBErr saturn_netlink_broadcast(
    XBSocket* socket,
    const void* data,
    int32_t size,
    int16_t port)
{
    /* Broadcast by setting player ID to 0 (kEntityIsBroadcast) */
    return saturn_netlink_send(socket, data, size, 0, port);
}

XBErr saturn_netlink_recv(
    XBSocket* socket,
    void* buffer,
    int32_t buffer_size,
    int32_t* out_size,
    cui_xb_sender_t* out_sender)
{
    if (!socket || !buffer || buffer_size <= 0) {
        return XBBadPacket;
    }

    long recv_size = buffer_size;
    XBSender sender;  /* Real XMP type from XMPPub.h */

    XBErr err = XBSocketRecv(socket, (char*)buffer, &recv_size, &sender);

    if (err == XBNoError) {
        if (out_size) *out_size = (int32_t)recv_size;
        if (out_sender) {
            out_sender->playerID = sender.playerID;
            out_sender->port = sender.port;
        }
    }

    return err;
}

/*============================================================================
 * Session Management
 *============================================================================*/

XBErr saturn_netlink_open_session(
    int32_t game_data_size,
    int32_t tx_buffer_size,
    int32_t rx_buffer_size)
{
    if (s_state != NETLINK_NETWORK_GAME) {
        return XBNoError;  /* OK for local games */
    }

    /* Clamp buffer sizes */
    if (tx_buffer_size > SESSION_TX_BUFFER_SIZE) {
        tx_buffer_size = SESSION_TX_BUFFER_SIZE;
    }
    if (rx_buffer_size > SESSION_RX_BUFFER_SIZE) {
        rx_buffer_size = SESSION_RX_BUFFER_SIZE;
    }

    return XBOpenSession(game_data_size, tx_buffer_size, rx_buffer_size);
}

XBErr saturn_netlink_close_session(void)
{
    return XBCloseSession();
}

void saturn_netlink_game_over(int32_t scores[8])
{
    if (s_state != NETLINK_NETWORK_GAME) {
        return;
    }

    XBMultiplayerGameResults results;
    memset(&results, 0, sizeof(results));

    for (int i = 0; i < 8; i++) {
        results.scores[i] = scores[i];
    }

    /* Use a static workspace buffer */
    static char workspace[256];
    XBNetworkGameOver(&results, workspace);

    /* Signal ready to exit */
    XBReadyToExit();
}

/*============================================================================
 * VBlank Synchronization
 *============================================================================*/

void saturn_netlink_wait_vblank(void)
{
    /* Wait for VDP2 VBlank using Jo Engine's sync mechanism */
    /* Jo Engine handles this in jo_core_run(), but for explicit sync: */
    slSynch();  /* SGL VBlank sync */
    s_frame_count++;
}

bool saturn_netlink_is_vblank(void)
{
    /* Check VDP2 status register for VBlank flag */
    /* VDP2 status register at 0x25F80004, bit 3 = VBLANK */
    volatile uint16_t* vdp2_status = (volatile uint16_t*)0x25F80004;
    return (*vdp2_status & 0x0008) != 0;
}

uint32_t saturn_netlink_get_frame_count(void)
{
    return s_frame_count;
}

void saturn_netlink_delay_frames(int frames)
{
    for (int i = 0; i < frames; i++) {
        saturn_netlink_wait_vblank();
    }
}

/*============================================================================
 * Bounded Packet Processing
 *============================================================================*/

XBErr saturn_netlink_recv_bounded(
    XBSocket* socket,
    void* buffer,
    int32_t buffer_size,
    int32_t* out_size,
    cui_xb_sender_t* out_sender)
{
    /* Check if we've hit the per-frame limit */
    if (s_packet_limit > 0 && s_packets_this_frame >= s_packet_limit) {
        s_stats.packets_dropped++;
        return XBNoData;
    }

    XBErr err = saturn_netlink_recv(socket, buffer, buffer_size, out_size, out_sender);

    if (err == XBNoError) {
        s_packets_this_frame++;
        s_stats.packets_received++;
        if (out_size) {
            s_stats.bytes_received += *out_size;
        }
    }

    return err;
}

void saturn_netlink_set_packet_limit(int limit)
{
    s_packet_limit = (limit < 0) ? 0 : limit;
}

void saturn_netlink_reset_packet_counter(void)
{
    s_packets_this_frame = 0;
}

int saturn_netlink_get_packets_this_frame(void)
{
    return s_packets_this_frame;
}

/*============================================================================
 * High-Level Frame API
 *============================================================================*/

void saturn_netlink_frame_begin(void)
{
    /* Record frame start time */
    s_frame_start_ticks = saturn_netlink_get_timer_ticks();

    /* Service the network */
    saturn_netlink_service();

    /* Reset per-frame counters */
    saturn_netlink_reset_packet_counter();

    /* Increment frame count */
    s_stats.frames_total++;
}

void saturn_netlink_frame_end(void)
{
    /* Calculate frame time */
    uint32_t end_ticks = saturn_netlink_get_timer_ticks();
    uint32_t elapsed;

    if (end_ticks >= s_frame_start_ticks) {
        elapsed = end_ticks - s_frame_start_ticks;
    } else {
        /* Handle wrap-around (16-bit timer) */
        elapsed = (0xFFFF - s_frame_start_ticks) + end_ticks + 1;
    }

    /* Convert ticks to microseconds
     * FRT runs at 28.6MHz / 8 = 3.575MHz, so ~0.28us per tick
     * elapsed * 28 / 100 approximates microseconds */
    s_stats.frame_time_us = (elapsed * 28) / 100;

    /* Update rolling average (simple exponential moving average) */
    if (s_stats.avg_frame_time_us == 0) {
        s_stats.avg_frame_time_us = s_stats.frame_time_us;
    } else {
        /* avg = avg * 0.9 + new * 0.1 */
        s_stats.avg_frame_time_us = (s_stats.avg_frame_time_us * 9 + s_stats.frame_time_us) / 10;
    }
}

saturn_netlink_stats_t saturn_netlink_get_stats(void)
{
    return s_stats;
}

void saturn_netlink_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

/*============================================================================
 * Timer Utilities
 *============================================================================*/

void saturn_netlink_timer_init(void)
{
    if (s_timer_initialized) {
        return;
    }

    /* Configure FRT (Free-Running Timer) on SH-2
     * TIER (Timer Interrupt Enable Register) at 0xFFFFFE10
     * FTCSR (Free-running Timer Control/Status) at 0xFFFFFE11
     * FRC (Free-Running Counter) at 0xFFFFFE12-13
     * TCR (Timer Control Register) at 0xFFFFFE16 */

    volatile uint8_t* TIER  = (volatile uint8_t*)0xFFFFFE10;
    volatile uint8_t* FTCSR = (volatile uint8_t*)0xFFFFFE11;
    volatile uint8_t* TCR   = (volatile uint8_t*)0xFFFFFE16;

    /* Disable interrupts and clear flags */
    *TIER = 0x00;
    *FTCSR = 0x00;

    /* Set clock divider: internal clock / 8 */
    *TCR = 0x01;  /* CKS = 01: internal clock / 8 */

    s_timer_initialized = true;
}

uint32_t saturn_netlink_get_timer_ticks(void)
{
    /* Read FRC (16-bit counter) */
    volatile uint16_t* FRC = (volatile uint16_t*)0xFFFFFE12;
    return *FRC;
}

void saturn_netlink_delay_ms(uint32_t ms)
{
    /* At 28.6MHz / 8 = 3.575MHz, each tick is ~0.28us
     * 1ms = ~3575 ticks */
    uint32_t ticks_per_ms = 3575;
    uint32_t target_ticks = ms * ticks_per_ms;

    /* For delays longer than ~18ms (65535 ticks), loop */
    while (target_ticks > 0) {
        uint32_t wait_ticks = (target_ticks > 60000) ? 60000 : target_ticks;
        uint32_t start = saturn_netlink_get_timer_ticks();

        while (1) {
            uint32_t now = saturn_netlink_get_timer_ticks();
            uint32_t elapsed;

            if (now >= start) {
                elapsed = now - start;
            } else {
                elapsed = (0xFFFF - start) + now + 1;
            }

            if (elapsed >= wait_ticks) {
                break;
            }
        }

        target_ticks -= wait_ticks;
    }
}

#else /* !__SATURN__ || MOCK_NETWORK */

/*============================================================================
 * Stub Implementation for Non-Saturn Builds or Mock Mode
 *============================================================================*/

#include "saturn_netlink.h"
#include <stddef.h>
#include <string.h>

saturn_netlink_state_t saturn_netlink_init(void) {
    return NETLINK_LOCAL_GAME;
}

void saturn_netlink_shutdown(void) {}

saturn_netlink_state_t saturn_netlink_get_state(void) {
    return NETLINK_LOCAL_GAME;
}

bool saturn_netlink_is_network_game(void) {
    return false;
}

void saturn_netlink_service(void) {}

void saturn_netlink_enumerate_players(
    saturn_netlink_player_callback_t callback,
    void* user_data) {
    if (callback) {
        callback(1, "Player", true, user_data);
    }
}

int32_t saturn_netlink_get_local_player_id(void) {
    return 1;
}

const char* saturn_netlink_get_player_name(int32_t player_id) {
    (void)player_id;
    return "Player";
}

XBSocket* saturn_netlink_open_socket(int16_t port) {
    (void)port;
    return NULL;
}

void saturn_netlink_close_socket(XBSocket* socket) {
    (void)socket;
}

XBErr saturn_netlink_send(
    XBSocket* socket,
    const void* data,
    int32_t size,
    int32_t recipient_id,
    int16_t recipient_port) {
    (void)socket; (void)data; (void)size;
    (void)recipient_id; (void)recipient_port;
    return XB_NO_ERROR;
}

XBErr saturn_netlink_broadcast(
    XBSocket* socket,
    const void* data,
    int32_t size,
    int16_t port) {
    (void)socket; (void)data; (void)size; (void)port;
    return XB_NO_ERROR;
}

XBErr saturn_netlink_recv(
    XBSocket* socket,
    void* buffer,
    int32_t buffer_size,
    int32_t* out_size,
    cui_xb_sender_t* out_sender) {
    (void)socket; (void)buffer; (void)buffer_size;
    (void)out_size; (void)out_sender;
    return XB_NO_DATA;
}

XBErr saturn_netlink_open_session(
    int32_t game_data_size,
    int32_t tx_buffer_size,
    int32_t rx_buffer_size) {
    (void)game_data_size; (void)tx_buffer_size; (void)rx_buffer_size;
    return XB_NO_ERROR;
}

XBErr saturn_netlink_close_session(void) {
    return XB_NO_ERROR;
}

void saturn_netlink_game_over(int32_t scores[8]) {
    (void)scores;
}

/*============================================================================
 * VBlank Synchronization (Stubs)
 *============================================================================*/

static uint32_t s_stub_frame_count = 0;

void saturn_netlink_wait_vblank(void) {
    s_stub_frame_count++;
}

bool saturn_netlink_is_vblank(void) {
    return false;
}

uint32_t saturn_netlink_get_frame_count(void) {
    return s_stub_frame_count;
}

void saturn_netlink_delay_frames(int frames) {
    (void)frames;
}

/*============================================================================
 * Bounded Packet Processing (Stubs)
 *============================================================================*/

static int s_stub_packet_limit = 8;
static int s_stub_packets_this_frame = 0;

XBErr saturn_netlink_recv_bounded(
    XBSocket* socket,
    void* buffer,
    int32_t buffer_size,
    int32_t* out_size,
    cui_xb_sender_t* out_sender) {
    (void)socket; (void)buffer; (void)buffer_size;
    (void)out_size; (void)out_sender;

    if (s_stub_packet_limit > 0 && s_stub_packets_this_frame >= s_stub_packet_limit) {
        return XB_NO_DATA;
    }
    return XB_NO_DATA;
}

void saturn_netlink_set_packet_limit(int limit) {
    s_stub_packet_limit = (limit < 0) ? 0 : limit;
}

void saturn_netlink_reset_packet_counter(void) {
    s_stub_packets_this_frame = 0;
}

int saturn_netlink_get_packets_this_frame(void) {
    return s_stub_packets_this_frame;
}

/*============================================================================
 * High-Level Frame API (Stubs)
 *============================================================================*/

static saturn_netlink_stats_t s_stub_stats = {0};

void saturn_netlink_frame_begin(void) {
    saturn_netlink_reset_packet_counter();
    s_stub_stats.frames_total++;
}

void saturn_netlink_frame_end(void) {
    /* No-op in stub */
}

saturn_netlink_stats_t saturn_netlink_get_stats(void) {
    return s_stub_stats;
}

void saturn_netlink_reset_stats(void) {
    s_stub_stats = (saturn_netlink_stats_t){0};
}

/*============================================================================
 * Timer Utilities (Stubs)
 *============================================================================*/

void saturn_netlink_timer_init(void) {
    /* No-op in stub */
}

uint32_t saturn_netlink_get_timer_ticks(void) {
    return 0;
}

void saturn_netlink_delay_ms(uint32_t ms) {
    (void)ms;
}

#endif /* __SATURN__ */
