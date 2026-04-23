/**
 * saturn_xmp_stubs.c - Stub implementations for XMP API
 *
 * The original XMPLib.ar is in COFF format (Hitachi toolchain) but
 * Jo Engine uses GCC which produces ELF format. These are incompatible.
 *
 * These stubs allow the code to compile and link. On real hardware,
 * the game will fall back to local mode without NetLink networking.
 *
 * TODO: To enable real NetLink networking, either:
 * 1. Find/create an ELF-format XMP library
 * 2. Use objcopy to convert COFF to ELF (may not preserve all info)
 * 3. Rebuild XMP from source with GCC if sources available
 */

#ifdef __SATURN__

#include "saturn_netlink.h"

/* Player enumeration flag */
const short kPlayerIsSelf = 0x0001;

/*============================================================================
 * XMP Stub Implementations
 *============================================================================*/

/* Initialize XBAND - returns local game since we have no real networking */
unsigned char XBInitXBAND(void)
{
    return 0;  /* XBLocalGame - no network */
}

/* Open session - no-op for stubs */
short XBOpenSession(long gameDataSize, long txBufferSize, long rxBufferSize)
{
    (void)gameDataSize;
    (void)txBufferSize;
    (void)rxBufferSize;
    return 0;  /* XBNoError */
}

/* Close session - no-op */
short XBCloseSession(void)
{
    return 0;
}

/* VBL task - no-op */
void XBVBLTask(void)
{
}

/* Network service - no-op */
short XBNetworkService(void)
{
    return 0;
}

/* Enumerate players - just report local player */
void XBEnumeratePlayerList(
    int (*callback)(void* context, long playerID, char* name, short flags),
    void* context,
    short flags)
{
    (void)flags;
    if (callback) {
        callback(context, 1, "Player 1", kPlayerIsSelf);
    }
}

/* Get player name */
char* XBGetPlayerName(long playerID)
{
    (void)playerID;
    return "Player";
}

/* Socket operations - return NULL/error */
struct XBSocket* XBSocketOpen(short socketType)
{
    (void)socketType;
    return (void*)0;  /* NULL - no socket */
}

short XBSocketBind(struct XBSocket* socket, short port)
{
    (void)socket;
    (void)port;
    return -1;  /* Error */
}

short XBSocketClose(struct XBSocket* socket)
{
    (void)socket;
    return 0;
}

short XBSocketConnect(struct XBSocket* socket, long playerID, short port, long timeout)
{
    (void)socket;
    (void)playerID;
    (void)port;
    (void)timeout;
    return -1;  /* Error */
}

short XBSocketSend(struct XBSocket* socket, char* buffer, long size, void* recipient)
{
    (void)socket;
    (void)buffer;
    (void)size;
    (void)recipient;
    return -1;  /* Error */
}

short XBSocketRecv(struct XBSocket* socket, char* buffer, long* size, void* sender)
{
    (void)socket;
    (void)buffer;
    (void)size;
    (void)sender;
    return -602;  /* XBNoData */
}

/* Game over */
void XBNetworkGameOver(void* results, char* workspace)
{
    (void)results;
    (void)workspace;
}

void XBReadyToExit(void)
{
}

#endif /* __SATURN__ */
