/**
 * stdio.c - LunarOS Kernel stdio
 *
 * Lapisan tipis di atas pl011 + framebuffer.
 * kprintf() = output ke BOTH UART dan framebuffer.
 * dbg_printf() = hanya UART (aman dipakai sebelum fb_init).
 *
 * Format yang didukung:
 *   %c  %s  %d  %i  %u  %x  %X  %p
 *   %ld %lu %lx  %lld %llu %llx
 *   %05d (zero-padding) %-10s (left-align) %10s (right-align)
 *
 * Lokasi: src/lib/stdio.c
 */

#include "stdio.h"
#include "string.h"
#include "../drivers/pl011.h"
#include "../drivers/framebuffer.h"

/* Warna default */
#define DEFAULT_FG  0xFFFFFFFF   /* putih */
#define DEFAULT_BG  0xFF000000   /* hitam */

static uint32_t current_fg = DEFAULT_FG;
static uint32_t current_bg = DEFAULT_BG;

/* ─────────────────────────────────────────────
   kvsnprintf — inti formatting ke buffer string
   ───────────────────────────────────────────── */
int kvsnprintf(char *buf, uint64_t size,
               const char *fmt, __builtin_va_list args)
{
    if (!buf || size == 0) return 0;

    uint64_t pos  = 0;
    int      total = 0;

#define PUT(c) do { \
    if (pos < size - 1) buf[pos++] = (c); \
    total++; \
} while (0)

    while (*fmt) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;   /* skip '%' */

        /* ── Flags ── */
        int flag_zero  = 0;
        int flag_left  = 0;
        if (*fmt == '0') { flag_zero = 1; fmt++; }
        if (*fmt == '-') { flag_left = 1; flag_zero = 0; fmt++; }

        /* ── Width ── */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* ── Length modifier ── */
        int is_long  = 0;
        int is_llong = 0;
        if (*fmt == 'l') {
            fmt++;
            is_long = 1;
            if (*fmt == 'l') { fmt++; is_llong = 1; }
        }

        /* ── Konversi ── */
        char tmp[66];
        const char *str = tmp;
        int  str_len    = 0;

        switch (*fmt) {

        /* Karakter */
        case 'c': {
            char c = (char)__builtin_va_arg(args, int);
            tmp[0] = c; tmp[1] = '\0';
            str_len = 1;
            break;
        }

        /* String */
        case 's': {
            str = __builtin_va_arg(args, const char *);
            if (!str) str = "(null)";
            str_len = (int)strlen(str);
            break;
        }

        /* Signed desimal */
        case 'd':
        case 'i': {
            int64_t val;
            if (is_llong)     val = __builtin_va_arg(args, int64_t);
            else if (is_long) val = (int64_t)__builtin_va_arg(args, long);
            else              val = (int64_t)__builtin_va_arg(args, int);
            itoa(val, tmp, 10);
            str_len = (int)strlen(tmp);
            break;
        }

        /* Unsigned desimal */
        case 'u': {
            uint64_t val;
            if (is_llong)     val = __builtin_va_arg(args, uint64_t);
            else if (is_long) val = (uint64_t)__builtin_va_arg(args, unsigned long);
            else              val = (uint64_t)__builtin_va_arg(args, unsigned int);
            utoa(val, tmp, 10);
            str_len = (int)strlen(tmp);
            break;
        }

        /* Hex lowercase */
        case 'x': {
            uint64_t val;
            if (is_llong)     val = __builtin_va_arg(args, uint64_t);
            else if (is_long) val = (uint64_t)__builtin_va_arg(args, unsigned long);
            else              val = (uint64_t)__builtin_va_arg(args, unsigned int);
            utoa(val, tmp, 16);
            str_len = (int)strlen(tmp);
            break;
        }

        /* Hex uppercase */
        case 'X': {
            uint64_t val;
            if (is_llong)     val = __builtin_va_arg(args, uint64_t);
            else if (is_long) val = (uint64_t)__builtin_va_arg(args, unsigned long);
            else              val = (uint64_t)__builtin_va_arg(args, unsigned int);
            utoa(val, tmp, 16);
            /* Ubah ke uppercase */
            for (int i = 0; tmp[i]; i++) {
                if (tmp[i] >= 'a' && tmp[i] <= 'f')
                    tmp[i] -= 32;
            }
            str_len = (int)strlen(tmp);
            break;
        }

        /* Pointer */
        case 'p': {
            uint64_t val = (uint64_t)__builtin_va_arg(args, void *);
            tmp[0] = '0'; tmp[1] = 'x';
            utoa(val, tmp + 2, 16);
            str_len = (int)strlen(tmp);
            break;
        }

        /* Persen literal */
        case '%':
            tmp[0] = '%'; tmp[1] = '\0';
            str_len = 1;
            break;

        case '\0':
            goto done;

        default:
            tmp[0] = '%'; tmp[1] = *fmt; tmp[2] = '\0';
            str_len = 2;
            break;
        }

        /* ── Padding ── */
        int pad = width - str_len;

        if (!flag_left && pad > 0) {
            char pc = flag_zero ? '0' : ' ';
            while (pad-- > 0) PUT(pc);
        }

        /* Tulis string hasil konversi */
        for (int i = 0; i < str_len; i++) PUT(str[i]);

        if (flag_left && pad > 0) {
            while (pad-- > 0) PUT(' ');
        }

        fmt++;
    }

done:
    buf[pos] = '\0';
    return total;

#undef PUT
}

/* ─────────────────────────────────────────────
   ksnprintf — format ke buffer dengan batas size
   ───────────────────────────────────────────── */
int ksnprintf(char *buf, uint64_t size, const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    int n = kvsnprintf(buf, size, fmt, args);
    __builtin_va_end(args);
    return n;
}

/* ─────────────────────────────────────────────
   kputc — output satu karakter
   ───────────────────────────────────────────── */
void kputc(char c) {
    pl011_putc(c);
    fb_console_putc(c);
}

/* ─────────────────────────────────────────────
   kputs — output string
   ───────────────────────────────────────────── */
void kputs(const char *str) {
    if (!str) return;
    while (*str) kputc(*str++);
}

/* ─────────────────────────────────────────────
   kprintf — output format ke UART + framebuffer
   ───────────────────────────────────────────── */
void kprintf(const char *fmt, ...) {
    char buf[512];

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);

    kputs(buf);
}

/* ─────────────────────────────────────────────
   dbg_printf — hanya ke UART
   Aman dipakai sebelum fb_init() atau saat panic
   ───────────────────────────────────────────── */
void dbg_printf(const char *fmt, ...) {
    char buf[512];

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);

    pl011_puts(buf);
}

/* ─────────────────────────────────────────────
   Input dari UART
   ───────────────────────────────────────────── */
char kgetc(void) {
    return pl011_getc();
}

/* kgets — baca satu baris, echo ke terminal */
int kgets(char *buf, int max) {
    if (!buf || max <= 0) return 0;

    int i = 0;
    while (i < max - 1) {
        char c = pl011_getc();

        if (c == '\r' || c == '\n') {
            kputc('\n');
            break;
        } else if (c == '\b' || c == 127) {
            /* Backspace */
            if (i > 0) {
                i--;
                kputs("\b \b");   /* hapus karakter di terminal */
            }
        } else if (c >= 32 && c < 127) {
            buf[i++] = c;
            kputc(c);   /* echo */
        }
    }

    buf[i] = '\0';
    return i;
}

/* ─────────────────────────────────────────────
   Warna console
   ───────────────────────────────────────────── */
void kset_color(uint32_t fg, uint32_t bg) {
    current_fg = fg;
    current_bg = bg;
    fb_console_set_color(fg, bg);
}

void kreset_color(void) {
    kset_color(DEFAULT_FG, DEFAULT_BG);
}