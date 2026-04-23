/**
 * main_saturn.c - Saturn Entry Point for Coup Card Game
 *
 * Saturn-specific initialization: SGL, SMPC, UART/modem, CUI PAL.
 * All game logic is in the shared coup_game.c.
 *
 * Architecture:
 *   Saturn + NetLink  --phone cable-->  DreamPi
 *       --TCP-->  tools/coup_server/server.py
 *
 * VDP1 rendering strategy:
 *   SGL's slSynch() overwrites VDP1 VRAM offset 0x40 with its own
 *   END command. Our VDP1 commands are buffered in RAM during rendering,
 *   then flushed to VRAM after slSynch() returns (during VBLANK).
 *   This ensures VDP1 sees our polygon/sprite commands next frame.
 */

#include <stdint.h>
#include <stdbool.h>

/* Saturn UART and modem */
#include "saturn_uart16550.h"
#include "modem.h"

/* Saturn PAL + CUI */
#include "saturn_pal.h"
#include "cui_layout.h"

/* SGL declarations */
#include "../../pal/saturn/sgl_defs.h"

/* Coup game */
#include "coup.h"
#include "coup_protocol.h"

/* Sprite loading */
#include "coup_sprite_loader.h"

/* Game over background */
#include "coup_gameover_loader.h"

/* Animated character sprites */
#include "coup_anim_loader.h"

/*============================================================================
 * Configuration
 *============================================================================*/

#define COUP_DIAL_NUMBER   "199401"
#define COUP_DIAL_TIMEOUT  180000000          /* ~60 seconds at 28.6MHz */

/*============================================================================
 * Saturn Transport (wraps UART for cui_transport_t)
 *============================================================================*/

static saturn_uart16550_t uart;
static bool modem_detected = false;

static bool saturn_transport_rx_ready(void* ctx)
{
    return saturn_uart_rx_ready((saturn_uart16550_t*)ctx);
}

static uint8_t saturn_transport_rx_byte(void* ctx)
{
    saturn_uart16550_t* u = (saturn_uart16550_t*)ctx;
    return (uint8_t)saturn_uart_reg_read(u, SATURN_UART_RBR);
}

static int saturn_transport_send(void* ctx, const uint8_t* data, int len)
{
    saturn_uart16550_t* u = (saturn_uart16550_t*)ctx;
    int i;
    for (i = 0; i < len; i++) {
        if (!saturn_uart_putc(u, data[i])) return i;
    }
    return len;
}

static cui_transport_t saturn_transport = {
    .rx_ready     = saturn_transport_rx_ready,
    .rx_byte      = saturn_transport_rx_byte,
    .send         = saturn_transport_send,
    .is_connected = NULL,
    .ctx          = NULL,
};

/*============================================================================
 * Render + sync helper (flushes VDP1 buffer after SGL sync)
 *============================================================================*/

static void render_and_sync(void)
{
    coup_render_now();
    cui_saturn_vdp1_flush_cmds();
    slSynch();
    cui_saturn_vdp1_activate();
}

/*============================================================================
 * Modem Connection (called when user selects ONLINE from title menu)
 *============================================================================*/

#define set_connect_stage(s, m) coup_set_connect_stage((s), (m))

static bool do_connect(void)
{
    modem_result_t result;

    set_connect_stage(0, "Probing modem...");
    coup_log("Probing modem...");
    render_and_sync();

    if (modem_probe(&uart) != MODEM_OK) {
        coup_log("No modem response");
        return false;
    }

    set_connect_stage(1, "Initializing modem...");
    coup_log("Modem detected - initializing");
    render_and_sync();

    if (modem_init(&uart) != MODEM_OK) {
        coup_log("Modem init failed");
        return false;
    }

    coup_log("Modem ready");
    render_and_sync();

    set_connect_stage(2, "Dialing server...");
    coup_log("Dialing " COUP_DIAL_NUMBER "...");
    render_and_sync();

    result = modem_dial(&uart, COUP_DIAL_NUMBER, COUP_DIAL_TIMEOUT);
    switch (result) {
    case MODEM_CONNECT:
        set_connect_stage(3, "Connected!");
        coup_log("Connection established!");
        modem_flush_input(&uart);
        render_and_sync();
        return true;
    case MODEM_NO_CARRIER:
        coup_log("NO CARRIER - Check cable");
        return false;
    case MODEM_BUSY:
        coup_log("LINE BUSY - Try again");
        return false;
    case MODEM_NO_DIALTONE:
        coup_log("NO DIALTONE - Check line");
        return false;
    case MODEM_NO_ANSWER:
        coup_log("NO ANSWER - Server down?");
        return false;
    case MODEM_TIMEOUT_ERR:
        coup_log("TIMEOUT - Server offline?");
        return false;
    default:
        coup_log("Dial failed - Unknown error");
        return false;
    }
}

/*============================================================================
 * Platform callback: attempt online connection
 * Called by coup_game.c when user selects ONLINE from title menu.
 *============================================================================*/

void coup_platform_try_connect(void)
{
    if (!modem_detected) {
        coup_log("No NetLink modem detected");
        coup_enter_offline();
        return;
    }

    coup_log("Connecting...");
    render_and_sync();

    if (do_connect()) {
        saturn_uart_reg_write(&uart, SATURN_UART_FCR,
            SATURN_UART_FCR_ENABLE | SATURN_UART_FCR_RXRESET);
        coup_set_transport(&saturn_transport);
        coup_on_connected();
    } else {
        coup_log("Connection failed");
        coup_enter_offline();
    }
}

/*============================================================================
 * Entry Point
 *============================================================================*/

void main(void)
{
    /* ---- SGL Initialization ---- */
    slInitSystem(TV_320x224, (TEXTURE*)0, 1);
    slInitSynch();

    slTVOff();
    slBack1ColSet((void*)(VDP2_VRAM_A1 + 0x1fffe), 0x0000);
    slScrAutoDisp(NBG0ON);
    slTVOn();

    /* ---- CUI Initialization ---- */
    cui_saturn_init();

    /* ---- Load sprite assets into VDP1 VRAM ---- */
    coup_sprites_load();
    coup_gameover_load();
    coup_anim_load();

    /* ---- Transport setup ---- */
    saturn_transport.ctx = &uart;

    /* ---- Init game ---- */
    coup_init();

    /* Initialize audio: loads M68K sound driver, starts CD-DA routing.
     * NOTE: We do NOT send SNDOFF (SMPC 0x07) here — the M68K must
     * remain running because slInitSound() loads our sound driver
     * into Sound RAM and starts the 68K to manage SCSP slots,
     * CD-DA routing, and PCM mixing. */
    coup_audio_init();

    /* ---- Start music on title screen ---- */
    coup_audio_start_music();

    /* ---- Show initial frame (title screen) ---- */
    render_and_sync();
    render_and_sync();

    /* ---- Detect modem hardware (non-blocking, does NOT dial) ---- */
    saturn_netlink_smpc_enable();
    {
        static const struct { uint32_t base; uint32_t stride; } addrs[] = {
            { 0x25895001, 4 },
            { 0x04895001, 4 },
        };
        int i;

        modem_detected = false;
        for (i = 0; i < 2; i++) {
            uart.base = addrs[i].base;
            uart.stride = addrs[i].stride;
            if (saturn_uart_detect(&uart)) {
                modem_detected = true;
                break;
            }
        }
    }

    /* Tell game whether modem hardware is present (for title menu) */
    coup_set_modem_available(modem_detected);

    /* ---- Main Loop ---- */
    while (1) {
        cui_input_action_t action;

        /* A+B+C+START soft reset (classic Saturn combo) */
        if (Per_Connect1) {
            uint16_t data = Smpc_Peripheral[0].data;
            uint16_t combo = PER_DGT_TA | PER_DGT_TB | PER_DGT_TC | PER_DGT_ST;
            if ((data & combo) == 0) {  /* All pressed (active-low) */
                coup_send_disconnect();
                coup_init();
                coup_set_modem_available(modem_detected);
                coup_audio_start_music();
                render_and_sync();
                continue;
            }
        }

        /* Feed raw pad data to audio debug menu BEFORE polling input.
         * The debug menu does its own edge detection and won't consume
         * normal game input unless the overlay is open. */
        coup_audio_debug_update(Smpc_Peripheral[0].data);
        action = cui_saturn_poll_input();

        coup_update(action);
        coup_audio_tick();   /* audio first — time-critical ring refill */
        coup_tick();
        coup_render();

        /* Debug overlay draws on top of everything */
        coup_audio_debug_render();

        /* Phase 1: Write draw commands to VDP1 VRAM while VDP1 is idle.
         * Waits for VDP1 draw completion (EDSR), then bulk-copies all
         * buffered commands to slots 4+.  Safe during active display
         * because VDP1 has finished drawing by the time we get here. */
        cui_saturn_vdp1_flush_cmds();

        slSynch();

        /* Phase 2: Activate commands by patching slot 2 with JUMP.
         * SGL just wrote END to slot 3.  We overwrite slot 2 with
         * LOCAL_COORD(0,0) + JP=ASSIGN → slot 4, skipping slot 3.
         * Only 4 writes (~200ns), well within vblank window. */
        cui_saturn_vdp1_activate();
    }
}
