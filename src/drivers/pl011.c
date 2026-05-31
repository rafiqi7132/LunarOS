/**
 * pl011.c - LunarOS UART PL011 Driver
 *
 * Driver untuk ARM PrimeCell UART (PL011).
 * Dipakai sebagai:
 *   - Serial debug output (pengganti serial.c x86)
 *   - Console input keyboard (pengganti keyboard.c x86)
 *
 * Lokasi: src/drivers/pl011.c
 */

#include "pl011.h"
#include "../arch/arm64/gic.h"

/* ─────────────────────────────────────────────
   MMIO helpers
   ───────────────────────────────────────────── */
static inline void uart_write(uint32_t reg, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)(PL011_BASE + reg);
    *p = val;
}

static inline uint32_t uart_read(uint32_t reg) {
    volatile uint32_t *p = (volatile uint32_t *)(PL011_BASE + reg);
    return *p;
}

/* ─────────────────────────────────────────────
   RX callback (untuk IRQ-driven input)
   ───────────────────────────────────────────── */
static void (*rx_callback)(char c) = NULL;

/* ─────────────────────────────────────────────
   pl011_init()
   ───────────────────────────────────────────── */
int pl011_init(void) {
    /* 1. Disable UART dulu */
    uart_write(UARTCR, 0);

    /* 2. Tunggu sampai tidak busy */
    while (uart_read(UARTFR) & FR_BUSY);

    /* 3. Flush TX FIFO */
    uart_write(UARTLCR_H, 0);

    /* 4. Set baud rate */
    uart_write(UARTIBRD, PL011_IBRD);
    uart_write(UARTFBRD, PL011_FBRD);

    /* 5. Set 8-bit, no parity, 1 stop bit, FIFO enable */
    uart_write(UARTLCR_H, LCR_WLEN_8 | LCR_FEN);

    /* 6. Mask semua interrupt dulu */
    uart_write(UARTIMSC, 0);

    /* 7. Clear pending interrupt */
    uart_write(UARTICR, 0x7FF);

    /* 8. Enable UART: TX + RX */
    uart_write(UARTCR, CR_UARTEN | CR_TXE | CR_RXE);

    return 0;
}

/* ─────────────────────────────────────────────
   Output — karakter tunggal
   ───────────────────────────────────────────── */
void pl011_putc(char c) {
    /* Tunggu TX FIFO tidak penuh */
    while (uart_read(UARTFR) & FR_TXFF);
    uart_write(UARTDR, (uint32_t)c);

    /* Auto newline: \n → \r\n supaya terminal rapi */
    if (c == '\n') {
        while (uart_read(UARTFR) & FR_TXFF);
        uart_write(UARTDR, '\r');
    }
}

/* ─────────────────────────────────────────────
   Output — string
   ───────────────────────────────────────────── */
void pl011_puts(const char *str) {
    if (!str) return;
    while (*str) pl011_putc(*str++);
}

/* ─────────────────────────────────────────────
   Output — angka unsigned decimal
   ───────────────────────────────────────────── */
void pl011_print_uint(uint64_t val) {
    if (val == 0) { pl011_putc('0'); return; }

    char buf[20];
    int  idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    /* Balik */
    for (int i = idx - 1; i >= 0; i--) pl011_putc(buf[i]);
}

/* ─────────────────────────────────────────────
   Output — angka signed decimal
   ───────────────────────────────────────────── */
void pl011_print_int(int64_t val) {
    if (val < 0) { pl011_putc('-'); val = -val; }
    pl011_print_uint((uint64_t)val);
}

/* ─────────────────────────────────────────────
   Output — hex (dengan prefix 0x)
   ───────────────────────────────────────────── */
void pl011_print_hex(uint64_t val) {
    const char h[] = "0123456789abcdef";
    pl011_puts("0x");

    /* Cetak dari nibble paling signifikan */
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        if (nibble || started || i == 0) {
            pl011_putc(h[nibble]);
            started = 1;
        }
    }
}

/* ─────────────────────────────────────────────
   pl011_printf() — printf sederhana untuk kernel
   Format yang didukung: %s %c %d %u %x %p %llu %lld
   ───────────────────────────────────────────── */
void pl011_printf(const char *fmt, ...) {
    /* Implementasi variadic sederhana tanpa <stdarg.h>
     * ARM64 ABI: argumen di x0–x7, sisanya di stack      */

    /* Pakai __builtin_va_list dari GCC */
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            pl011_putc(*fmt++);
            continue;
        }
        fmt++;   /* skip '%' */

        /* Cek modifier 'll' */
        int is_long = 0;
        if (*fmt == 'l') { fmt++; is_long = 1; }
        if (*fmt == 'l') { fmt++; }

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            pl011_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            pl011_putc((char)__builtin_va_arg(args, int));
            break;
        case 'd':
        case 'i': {
            int64_t v = is_long
                ? __builtin_va_arg(args, int64_t)
                : (int64_t)__builtin_va_arg(args, int);
            pl011_print_int(v);
            break;
        }
        case 'u': {
            uint64_t v = is_long
                ? __builtin_va_arg(args, uint64_t)
                : (uint64_t)__builtin_va_arg(args, unsigned int);
            pl011_print_uint(v);
            break;
        }
        case 'x':
        case 'X':
        case 'p': {
            uint64_t v = is_long
                ? __builtin_va_arg(args, uint64_t)
                : (uint64_t)__builtin_va_arg(args, unsigned int);
            pl011_print_hex(v);
            break;
        }
        case '%':
            pl011_putc('%');
            break;
        case '\0':
            goto done;
        default:
            pl011_putc('%');
            pl011_putc(*fmt);
            break;
        }
        fmt++;
    }

done:
    __builtin_va_end(args);
}

/* ─────────────────────────────────────────────
   Input — cek apakah ada karakter masuk
   ───────────────────────────────────────────── */
int pl011_getc_ready(void) {
    return !(uart_read(UARTFR) & FR_RXFE);
}

/* ─────────────────────────────────────────────
   Input — baca satu karakter (blocking)
   ───────────────────────────────────────────── */
char pl011_getc(void) {
    while (!pl011_getc_ready());
    return (char)(uart_read(UARTDR) & 0xFF);
}

/* ─────────────────────────────────────────────
   Input — non-blocking (return 0 jika kosong)
   ───────────────────────────────────────────── */
char pl011_getc_nowait(void) {
    if (!pl011_getc_ready()) return 0;
    return (char)(uart_read(UARTDR) & 0xFF);
}

/* ─────────────────────────────────────────────
   IRQ-driven input
   ───────────────────────────────────────────── */
static void pl011_irq_handler(uint32_t irq_id, registers_t *regs) {
    (void)irq_id; (void)regs;

    /* Baca semua karakter yang tersedia */
    while (pl011_getc_ready()) {
        char c = pl011_getc_nowait();
        if (c && rx_callback) rx_callback(c);
    }

    /* Clear RX interrupt */
    uart_write(UARTICR, INT_RX);
}

void pl011_enable_rx_irq(void) {
    uart_write(UARTIMSC, INT_RX);
    gic_register_irq(IRQ_UART0, pl011_irq_handler);
}

void pl011_set_rx_callback(void (*cb)(char c)) {
    rx_callback = cb;
}