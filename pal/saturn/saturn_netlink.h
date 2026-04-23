/**
 * saturn_netlink.h - Saturn NetLink integration for Jo Engine
 *
 * Wraps the XMP (X-Band Multiplayer) API for use with Jo Engine.
 * Provides socket-based networking for 2-8 player games.
 */

#ifndef CUI_SATURN_NETLINK_H
#define CUI_SATURN_NETLINK_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * XMP Types
 *
 * These are compatible with the real XMP SDK types in XMPPub.h.
 * We define our own to avoid multiple definition issues (XMPPub.h uses
 * const short globals which can't be safely included in multiple TUs).
 *============================================================================*/

typedef int16_t  XBErr;

/* XMP Error codes (from XMPPub.h) */
#define XB_NO_ERROR              0
#define XB_SESSION_CLOSED       -405
#define XB_NO_DIALTONE          -416
#define XB_CONNECTION_LOST      -421
#define XB_TIMEOUT              -426
#define XB_NO_DATA              -602
#define XB_BAD_PACKET           -603

#ifdef __SATURN__
/* Forward declare the opaque XBSocket struct (defined in XMPPub.h) */
struct XBSocket;
typedef struct XBSocket XBSocket;
#else
/* Non-Saturn: use void pointer */
typedef struct XBSocket_stub XBSocket;
#endif

/* XBSender structure - matches XMPPub.h layout */
typedef struct cui_xb_sender {
    long    playerID;
    short   port;
} cui_xb_sender_t;

/*============================================================================
 * NetLink State
 *============================================================================*/

typedef enum saturn_netlink_state {
    NETLINK_UNINITIALIZED = 0,
    NETLINK_LOCAL_GAME,         /* No NetLink, local play only */
    NETLINK_NETWORK_GAME,       /* Connected via NetLink */
    NETLINK_ERROR
} saturn_netlink_state_t;

/*============================================================================
 * Player Enumeration
 *============================================================================*/

/**
 * Callback for player enumeration.
 *
 * @param player_id Unique network player ID
 * @param player_name Player's display name
 * @param is_local True if this is the local player
 * @param user_data Context passed to enumerate function
 * @return True to continue enumeration, false to stop
 */
typedef bool (*saturn_netlink_player_callback_t)(
    int32_t player_id,
    const char* player_name,
    bool is_local,
    void* user_data
);

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * Initialize the NetLink subsystem.
 * Must be called before any other NetLink functions.
 *
 * @return Current NetLink state
 */
saturn_netlink_state_t saturn_netlink_init(void);

/**
 * Shutdown the NetLink subsystem.
 * Closes all sockets and cleans up resources.
 */
void saturn_netlink_shutdown(void);

/**
 * Get current NetLink state.
 *
 * @return Current state
 */
saturn_netlink_state_t saturn_netlink_get_state(void);

/**
 * Check if we're in a network game.
 *
 * @return True if connected via NetLink
 */
bool saturn_netlink_is_network_game(void);

/*============================================================================
 * Frame Update
 *============================================================================*/

/**
 * Service the NetLink subsystem (call every frame).
 * This calls XBVBLTask() and XBNetworkService() internally.
 */
void saturn_netlink_service(void);

/*============================================================================
 * Player Management
 *============================================================================*/

/**
 * Enumerate all players in the current game.
 *
 * @param callback Function called for each player
 * @param user_data Context passed to callback
 */
void saturn_netlink_enumerate_players(
    saturn_netlink_player_callback_t callback,
    void* user_data
);

/**
 * Get the local player's ID.
 *
 * @return Local player ID, or 0 if not available
 */
int32_t saturn_netlink_get_local_player_id(void);

/**
 * Get a player's name by ID.
 *
 * @param player_id Player ID to look up
 * @return Player name string (static buffer, do not free)
 */
const char* saturn_netlink_get_player_name(int32_t player_id);

/*============================================================================
 * Socket Operations
 *============================================================================*/

/**
 * Open a datagram socket for game communication.
 *
 * @param port Port number to bind to
 * @return Socket handle, or NULL on failure
 */
XBSocket* saturn_netlink_open_socket(int16_t port);

/**
 * Close a socket.
 *
 * @param socket Socket to close
 */
void saturn_netlink_close_socket(XBSocket* socket);

/**
 * Send data to a specific player.
 *
 * @param socket Socket to send from
 * @param data Data buffer to send
 * @param size Size of data in bytes
 * @param recipient_id Target player ID (0 for broadcast)
 * @param recipient_port Target port number
 * @return XB_NO_ERROR on success
 */
XBErr saturn_netlink_send(
    XBSocket* socket,
    const void* data,
    int32_t size,
    int32_t recipient_id,
    int16_t recipient_port
);

/**
 * Broadcast data to all players.
 *
 * @param socket Socket to send from
 * @param data Data buffer to send
 * @param size Size of data in bytes
 * @param port Target port number
 * @return XB_NO_ERROR on success
 */
XBErr saturn_netlink_broadcast(
    XBSocket* socket,
    const void* data,
    int32_t size,
    int16_t port
);

/**
 * Receive data from the socket.
 * Non-blocking - returns XB_NO_DATA if nothing available.
 *
 * @param socket Socket to receive from
 * @param buffer Buffer to receive into
 * @param buffer_size Maximum bytes to receive
 * @param out_size Actual bytes received
 * @param out_sender Sender information (player ID and port)
 * @return XB_NO_ERROR if data received, XB_NO_DATA if none available
 */
XBErr saturn_netlink_recv(
    XBSocket* socket,
    void* buffer,
    int32_t buffer_size,
    int32_t* out_size,
    cui_xb_sender_t* out_sender
);

/*============================================================================
 * Session Management
 *============================================================================*/

/**
 * Open a network session.
 * Must be called after saturn_netlink_init() if in network mode.
 *
 * @param game_data_size Size of per-player game data
 * @param tx_buffer_size Transmit buffer size
 * @param rx_buffer_size Receive buffer size
 * @return XB_NO_ERROR on success
 */
XBErr saturn_netlink_open_session(
    int32_t game_data_size,
    int32_t tx_buffer_size,
    int32_t rx_buffer_size
);

/**
 * Close the current session.
 *
 * @return XB_NO_ERROR on success
 */
XBErr saturn_netlink_close_session(void);

/**
 * Signal that the game is over.
 * Call this before saturn_netlink_shutdown().
 *
 * @param scores Array of 8 player scores
 */
void saturn_netlink_game_over(int32_t scores[8]);

/*============================================================================
 * VBlank Synchronization
 *
 * These functions provide proper 60Hz timing to prevent frame waste and
 * ensure consistent frame rates. Use these instead of busy-wait loops.
 *============================================================================*/

/**
 * Wait for the next VBlank interrupt.
 * Blocks until VBlank occurs, providing proper 60Hz timing.
 * This is the preferred way to sync to display refresh.
 */
void saturn_netlink_wait_vblank(void);

/**
 * Check if currently in VBlank period.
 *
 * @return True if in VBlank, false otherwise
 */
bool saturn_netlink_is_vblank(void);

/**
 * Get the current frame count since initialization.
 * Increments once per VBlank.
 *
 * @return Frame count (wraps at UINT32_MAX)
 */
uint32_t saturn_netlink_get_frame_count(void);

/**
 * Delay for a specified number of frames.
 * Uses VBlank sync for accurate timing.
 *
 * @param frames Number of frames to delay (1 frame = ~16.67ms at 60Hz)
 */
void saturn_netlink_delay_frames(int frames);

/*============================================================================
 * Bounded Packet Processing
 *
 * These functions limit packet processing per frame to prevent frame drops
 * under heavy network load. Default limit is 8 packets per frame.
 *============================================================================*/

/**
 * Receive data from the socket with automatic per-frame limiting.
 * Returns XB_NO_DATA when the per-frame limit is reached.
 * Call saturn_netlink_frame_begin() to reset the counter each frame.
 *
 * @param socket Socket to receive from
 * @param buffer Buffer to receive into
 * @param buffer_size Maximum bytes to receive
 * @param out_size Actual bytes received
 * @param out_sender Sender information (player ID and port)
 * @return XB_NO_ERROR if data received, XB_NO_DATA if none or limit reached
 */
XBErr saturn_netlink_recv_bounded(
    XBSocket* socket,
    void* buffer,
    int32_t buffer_size,
    int32_t* out_size,
    cui_xb_sender_t* out_sender
);

/**
 * Set the maximum packets to process per frame.
 * Default is 8. Set to 0 for unlimited (not recommended).
 *
 * @param limit Maximum packets per frame
 */
void saturn_netlink_set_packet_limit(int limit);

/**
 * Reset the per-frame packet counter.
 * Called automatically by saturn_netlink_frame_begin().
 */
void saturn_netlink_reset_packet_counter(void);

/**
 * Get the number of packets processed this frame.
 *
 * @return Packet count since last reset
 */
int saturn_netlink_get_packets_this_frame(void);

/*============================================================================
 * High-Level Frame API
 *
 * These functions provide a clean per-frame workflow:
 *   saturn_netlink_frame_begin();
 *   // ... process input, send/recv packets ...
 *   saturn_netlink_frame_end();
 *============================================================================*/

/**
 * Statistics structure for performance monitoring.
 */
typedef struct saturn_netlink_stats {
    uint32_t frames_total;          /* Total frames since init */
    uint32_t packets_sent;          /* Total packets sent */
    uint32_t packets_received;      /* Total packets received */
    uint32_t packets_dropped;       /* Packets dropped (limit exceeded) */
    uint32_t bytes_sent;            /* Total bytes sent */
    uint32_t bytes_received;        /* Total bytes received */
    uint32_t frame_time_us;         /* Last frame time in microseconds */
    uint32_t avg_frame_time_us;     /* Rolling average frame time */
} saturn_netlink_stats_t;

/**
 * Begin frame processing.
 * This should be called at the start of each game frame:
 * - Calls saturn_netlink_service() (XBVBLTask + XBNetworkService)
 * - Resets the per-frame packet counter
 * - Updates frame statistics
 */
void saturn_netlink_frame_begin(void);

/**
 * End frame processing.
 * Optional cleanup at end of frame. Currently:
 * - Finalizes frame timing statistics
 */
void saturn_netlink_frame_end(void);

/**
 * Get current performance statistics.
 *
 * @return Copy of current statistics
 */
saturn_netlink_stats_t saturn_netlink_get_stats(void);

/**
 * Reset all statistics to zero.
 */
void saturn_netlink_reset_stats(void);

/*============================================================================
 * Timer Utilities
 *
 * Low-level timer access for precise timing measurements.
 * Uses the Saturn's Free-Running Timer (FRT).
 *============================================================================*/

/**
 * Initialize the hardware timer.
 * Called automatically by saturn_netlink_init().
 */
void saturn_netlink_timer_init(void);

/**
 * Get the current timer tick count.
 * Resolution depends on timer configuration (~0.035us per tick at 28.6MHz/8).
 *
 * @return Current tick count (16-bit, wraps at 65535)
 */
uint32_t saturn_netlink_get_timer_ticks(void);

/**
 * Delay for a specified number of milliseconds.
 * Uses busy-wait with timer. Prefer saturn_netlink_delay_frames() for
 * longer delays as it doesn't waste CPU cycles.
 *
 * @param ms Milliseconds to delay (max ~2300ms before overflow)
 */
void saturn_netlink_delay_ms(uint32_t ms);

#endif /* CUI_SATURN_NETLINK_H */
