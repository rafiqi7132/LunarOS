/**
 * stdio.h - LunarOS Kernel stdio Header
 * Lokasi: src/lib/stdio.h
 */

#ifndef STDIO_H
#define STDIO_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Output ke framebuffer + UART sekaligus
   ───────────────────────────────────────────── */
void kprintf(const char *fmt, ...);
void kputs  (const char *str);
void kputc  (char c);

/* Output hanya ke UART (untuk debug awal / panic) */
void dbg_printf(const char *fmt, ...);

/* ─────────────────────────────────────────────
   Input dari UART
   ───────────────────────────────────────────── */
char    kgetc (void);
int     kgets (char *buf, int max);

/* ─────────────────────────────────────────────
   String formatting
   ───────────────────────────────────────────── */
int  ksnprintf(char *buf, uint64_t size, const char *fmt, ...);
int  kvsnprintf(char *buf, uint64_t size,
                const char *fmt, __builtin_va_list args);

/* ─────────────────────────────────────────────
   Warna output (framebuffer console)
   ───────────────────────────────────────────── */
void kset_color(uint32_t fg, uint32_t bg);
void kreset_color(void);

#endif /* STDIO_H */