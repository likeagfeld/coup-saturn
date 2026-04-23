/**
 * saturn_sci.h - SH-2 On-Chip Serial Communication Interface (SCI) Driver
 *
 * Provides access to the SH-2's built-in SCI peripheral at 0xFFFFFE00-05.
 * This connects to the Saturn's COMMUNICATION PORT (rear connector), NOT
 * the NetLink modem cartridge. Useful for serial debug links, loaders, etc.
 *
 * NOTE: The NetLink modem uses a 16550 UART on the cartridge bus (A-bus).
 *       For NetLink access, use saturn_uart16550.h instead.
 */

#ifndef SATURN_SCI_H
#define SATURN_SCI_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * SH-2 SCI Registers (CPU internal address space)
 * ========================================================================= */

#define SATURN_SCI_SMR  (*(volatile uint8_t*)0xFFFFFE00)  /* Serial Mode */
#define SATURN_SCI_BRR  (*(volatile uint8_t*)0xFFFFFE01)  /* Bit Rate */
#define SATURN_SCI_SCR  (*(volatile uint8_t*)0xFFFFFE02)  /* Serial Control */
#define SATURN_SCI_TDR  (*(volatile uint8_t*)0xFFFFFE03)  /* Transmit Data */
#define SATURN_SCI_SSR  (*(volatile uint8_t*)0xFFFFFE04)  /* Serial Status */
#define SATURN_SCI_RDR  (*(volatile uint8_t*)0xFFFFFE05)  /* Receive Data */

/* SMR bits */
#define SATURN_SCI_SMR_CA    0x80  /* 0=async, 1=clocked sync */
#define SATURN_SCI_SMR_CHR   0x40  /* 0=8bit, 1=7bit */
#define SATURN_SCI_SMR_PE    0x20  /* Parity enable */
#define SATURN_SCI_SMR_OE    0x10  /* 0=even, 1=odd parity */
#define SATURN_SCI_SMR_STOP  0x08  /* 0=1 stop bit, 1=2 stop bits */
#define SATURN_SCI_SMR_CKS1  0x02  /* Clock select bit 1 */
#define SATURN_SCI_SMR_CKS0  0x01  /* Clock select bit 0 */

/* SCR bits */
#define SATURN_SCI_SCR_TIE   0x80  /* TX interrupt enable */
#define SATURN_SCI_SCR_RIE   0x40  /* RX interrupt enable */
#define SATURN_SCI_SCR_TE    0x20  /* Transmit enable */
#define SATURN_SCI_SCR_RE    0x10  /* Receive enable */
#define SATURN_SCI_SCR_TEIE  0x04  /* TX end interrupt enable */
#define SATURN_SCI_SCR_CKE1  0x02  /* Clock enable bit 1 */
#define SATURN_SCI_SCR_CKE0  0x01  /* Clock enable bit 0 */

/* SSR bits */
#define SATURN_SCI_SSR_TDRE  0x80  /* TX data register empty */
#define SATURN_SCI_SSR_RDRF  0x40  /* RX data register full */
#define SATURN_SCI_SSR_ORER  0x20  /* Overrun error */
#define SATURN_SCI_SSR_FER   0x10  /* Framing error */
#define SATURN_SCI_SSR_PER   0x08  /* Parity error */
#define SATURN_SCI_SSR_TEND  0x04  /* TX end */

/* =========================================================================
 * Baud rate dividers for 28.636 MHz master clock, CKS=0
 * Formula: BRR = Phi / (32 * baud) - 1
 * ========================================================================= */

#define SATURN_SCI_BAUD_2400    185
#define SATURN_SCI_BAUD_4800    92
#define SATURN_SCI_BAUD_9600    46
#define SATURN_SCI_BAUD_19200   22
#define SATURN_SCI_BAUD_38400   11
#define SATURN_SCI_BAUD_57600   7
#define SATURN_SCI_BAUD_115200  3

/* =========================================================================
 * Functions
 * ========================================================================= */

/**
 * Initialize SCI for async 8N1 communication.
 */
static inline void saturn_sci_init(uint8_t baud_divider) {
    SATURN_SCI_SCR = 0x00;              /* Disable all */
    SATURN_SCI_SMR = 0x00;              /* Async, 8N1, clock/1 */
    SATURN_SCI_BRR = baud_divider;

    /* Delay for baud rate generator to stabilize */
    for (volatile int i = 0; i < 1000; i++);

    SATURN_SCI_SCR = SATURN_SCI_SCR_TE | SATURN_SCI_SCR_RE;
}

/**
 * Reinitialize SCI with new baud rate.
 */
static inline void saturn_sci_reinit(uint8_t baud_divider) {
    SATURN_SCI_SCR = 0x00;

    for (volatile int i = 0; i < 1000; i++);

    SATURN_SCI_SMR = 0x00;
    SATURN_SCI_BRR = baud_divider;

    for (volatile int i = 0; i < 1000; i++);

    SATURN_SCI_SSR &= ~(SATURN_SCI_SSR_ORER | SATURN_SCI_SSR_FER |
                         SATURN_SCI_SSR_PER  | SATURN_SCI_SSR_RDRF);

    SATURN_SCI_SCR = SATURN_SCI_SCR_TE | SATURN_SCI_SCR_RE;
}

/**
 * Check if TX buffer is ready for data.
 */
static inline bool saturn_sci_tx_ready(void) {
    return (SATURN_SCI_SSR & SATURN_SCI_SSR_TDRE) != 0;
}

/**
 * Check if RX data is available.
 */
static inline bool saturn_sci_rx_ready(void) {
    return (SATURN_SCI_SSR & SATURN_SCI_SSR_RDRF) != 0;
}

/**
 * Check for receive errors.
 */
static inline bool saturn_sci_rx_error(void) {
    return (SATURN_SCI_SSR & (SATURN_SCI_SSR_ORER | SATURN_SCI_SSR_FER |
                              SATURN_SCI_SSR_PER)) != 0;
}

/**
 * Clear receive errors.
 */
static inline void saturn_sci_clear_errors(void) {
    SATURN_SCI_SSR &= ~(SATURN_SCI_SSR_ORER | SATURN_SCI_SSR_FER |
                         SATURN_SCI_SSR_PER);
}

/**
 * Send a single byte (blocking with timeout).
 * @return true if sent, false on timeout
 */
static inline bool saturn_sci_putc(uint8_t c) {
    uint32_t timeout = 100000;
    while (!saturn_sci_tx_ready()) {
        if (--timeout == 0) return false;
    }
    SATURN_SCI_TDR = c;
    SATURN_SCI_SSR &= ~SATURN_SCI_SSR_TDRE;
    return true;
}

/**
 * Receive a single byte (blocking).
 */
static inline uint8_t saturn_sci_getc(void) {
    while (!saturn_sci_rx_ready());
    uint8_t c = SATURN_SCI_RDR;
    SATURN_SCI_SSR &= ~SATURN_SCI_SSR_RDRF;
    return c;
}

/**
 * Receive a byte with timeout.
 * @return received byte, or -1 on timeout
 */
static inline int saturn_sci_getc_timeout(uint32_t timeout) {
    while (timeout--) {
        if (saturn_sci_rx_ready()) {
            uint8_t c = SATURN_SCI_RDR;
            SATURN_SCI_SSR &= ~SATURN_SCI_SSR_RDRF;
            return c;
        }
    }
    return -1;
}

/**
 * Send a null-terminated string.
 * @return true if all sent, false on timeout
 */
static inline bool saturn_sci_puts(const char* str) {
    while (*str) {
        if (!saturn_sci_putc(*str++)) return false;
    }
    return true;
}

/**
 * Check if SCI hardware is accessible (TDRE should be set after init).
 * NOTE: This only confirms the CPU register is accessible, NOT that an
 * external device is connected to the communication port.
 */
static inline bool saturn_sci_detect_hardware(void) {
    for (volatile int i = 0; i < 10000; i++);
    return (SATURN_SCI_SSR & SATURN_SCI_SSR_TDRE) != 0;
}

/**
 * Read raw SSR value (for diagnostics).
 */
static inline uint8_t saturn_sci_read_ssr(void) {
    return SATURN_SCI_SSR;
}

#endif /* SATURN_SCI_H */
