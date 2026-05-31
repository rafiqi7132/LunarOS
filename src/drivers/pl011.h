/**
 * pl011.h - LunarOS UART PL011 Header
 * Pengganti serial.c untuk ARM64
 * Lokasi: src/drivers/pl011.h
 */

#ifndef PL011_H
#define PL011_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Base address PL011 di QEMU virt machine
   ───────────────────────────────────────────── */
#define PL011_BASE      0x09000000ULL

/* ─────────────────────────────────────────────
   Register offset (dari base)
   ───────────────────────────────────────────── */
#define UARTDR          0x000   /* Data Register */
#define UARTRSR         0x004   /* Receive Status / Error Clear */
#define UARTFR          0x018   /* Flag Register */
#define UARTIBRD        0x024   /* Integer Baud Rate Divisor */
#define UARTFBRD        0x028   /* Fractional Baud Rate Divisor */
#define UARTLCR_H       0x02C   /* Line Control Register */
#define UARTCR          0x030   /* Control Register */
#define UARTIMSC        0x038   /* Interrupt Mask Set/Clear */
#define UARTMIS         0x040   /* Masked Interrupt Status */
#define UARTICR         0x044   /* Interrupt Clear Register */

/* ─────────────────────────────────────────────
   Flag Register bits (UARTFR)
   ───────────────────────────────────────────── */
#define FR_TXFF         (1 << 5)    /* TX FIFO full */
#define FR_RXFE         (1 << 4)    /* RX FIFO empty */
#define FR_BUSY         (1 << 3)    /* UART busy */

/* ─────────────────────────────────────────────
   Control Register bits (UARTCR)
   ───────────────────────────────────────────── */
#define CR_UARTEN       (1 << 0)    /* UART enable */
#define CR_TXE          (1 << 8)    /* TX enable */
#define CR_RXE          (1 << 9)    /* RX enable */

/* ─────────────────────────────────────────────
   Line Control Register bits (UARTLCR_H)
   ───────────────────────────────────────────── */
#define LCR_FEN         (1 << 4)    /* FIFO enable */
#define LCR_WLEN_8      (3 << 5)    /* 8-bit word */

/* ─────────────────────────────────────────────
   Interrupt bits (UARTIMSC)
   ───────────────────────────────────────────── */
#define INT_RX          (1 << 4)    /* RX interrupt */
#define INT_TX          (1 << 5)    /* TX interrupt */

/* ─────────────────────────────────────────────
   Baud rate — asumsi clock 24 MHz di QEMU
   Formula: IBRD = clock / (16 * baud)
            FBRD = ((clock / (16 * baud)) - IBRD) * 64
   Untuk 115200 baud, 24 MHz clock:
     IBRD = 13, FBRD = 1
   ───────────────────────────────────────────── */
#define PL011_CLOCK     24000000UL
#define PL011_BAUD      115200UL
#define PL011_IBRD      (PL011_CLOCK / (16 * PL011_BAUD))
#define PL011_FBRD      (((PL011_CLOCK % (16 * PL011_BAUD)) * 64 \
                          + (8 * PL011_BAUD)) / (16 * PL011_BAUD))

/* ─────────────────────────────────────────────
   Warna ANSI untuk output serial
   ───────────────────────────────────────────── */
#define ANSI_RESET      "\033[0m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"
#define ANSI_BOLD       "\033[1m"

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */
int     pl011_init(void);

/* Output */
void    pl011_putc(char c);
void    pl011_puts(const char *str);
void    pl011_print_uint(uint64_t val);
void    pl011_print_int(int64_t val);
void    pl011_print_hex(uint64_t val);
void    pl011_printf(const char *fmt, ...);

/* Input */
int     pl011_getc_ready(void);     /* 1 jika ada karakter di RX buffer */
char    pl011_getc(void);           /* baca satu karakter (blocking) */
char    pl011_getc_nowait(void);    /* baca satu karakter (non-blocking, 0 jika kosong) */

/* IRQ-driven input */
void    pl011_enable_rx_irq(void);
void    pl011_set_rx_callback(void (*cb)(char c));

#endif /* PL011_H */