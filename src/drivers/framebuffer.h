/**
 * framebuffer.h - LunarOS Framebuffer Driver Header
 * Pengganti vga.c untuk ARM64
 * Lokasi: src/drivers/framebuffer.h
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Resolusi default QEMU virt
   ───────────────────────────────────────────── */
#define FB_WIDTH        800
#define FB_HEIGHT       600
#define FB_BPP          32      /* bit per pixel (ARGB8888) */
#define FB_PITCH        (FB_WIDTH * (FB_BPP / 8))

/* Base address framebuffer QEMU virtio-gpu
 * Di hardware nyata, baca dari device tree     */
#define FB_BASE         0x50000000ULL

/* ─────────────────────────────────────────────
   Warna — format ARGB 32-bit
   ───────────────────────────────────────────── */
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0066CC
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_ORANGE    0xFFFF9500
#define COLOR_PURPLE    0xFF9B59B6
#define COLOR_GREY      0xFF8E8E93
#define COLOR_DARK_GREY 0xFF1C1C1E

/* Warna tema LunarOS (terinspirasi iOS) */
#define LUNAR_BG        0xFF000000   /* hitam */
#define LUNAR_FG        0xFFFFFFFF   /* putih */
#define LUNAR_ACCENT    0xFF0A84FF   /* biru iOS */
#define LUNAR_PANEL     0xFF1C1C1E   /* panel gelap */
#define LUNAR_BORDER    0xFF38383A   /* border */
#define LUNAR_SUCCESS   0xFF30D158   /* hijau */
#define LUNAR_WARNING   0xFFFF9F0A   /* oranye */
#define LUNAR_ERROR     0xFFFF453A   /* merah */

/* ─────────────────────────────────────────────
   Font 8x16 (built-in bitmap font)
   ───────────────────────────────────────────── */
#define FONT_W          8
#define FONT_H          16

/* ─────────────────────────────────────────────
   Info framebuffer
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t *addr;         /* pointer ke buffer pixel */
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;        /* byte per baris */
    uint32_t  bpp;
} fb_info_t;

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */

/* Init */
int  fb_init(void);
void fb_get_info(fb_info_t *info);

/* Pixel */
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(uint32_t x, uint32_t y);

/* Shape */
void fb_fill_rect(uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h,
                  uint32_t color, uint32_t thickness);
void fb_draw_line(uint32_t x0, uint32_t y0,
                  uint32_t x1, uint32_t y1, uint32_t color);
void fb_fill_circle(uint32_t cx, uint32_t cy,
                    uint32_t r, uint32_t color);
void fb_fill_rounded_rect(uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h,
                           uint32_t r, uint32_t color);

/* Teks */
void fb_draw_char(uint32_t x, uint32_t y,
                  char c, uint32_t fg, uint32_t bg);
void fb_draw_string(uint32_t x, uint32_t y,
                    const char *str, uint32_t fg, uint32_t bg);
void fb_draw_string_scaled(uint32_t x, uint32_t y,
                            const char *str,
                            uint32_t fg, uint32_t bg,
                            uint32_t scale);

/* Screen */
void fb_clear(uint32_t color);
void fb_scroll_up(uint32_t lines);

/* Console mode (seperti VGA text mode tapi di framebuffer) */
void fb_console_init(void);
void fb_console_putc(char c);
void fb_console_puts(const char *str);
void fb_console_set_color(uint32_t fg, uint32_t bg);
void fb_console_clear(void);

#endif /* FRAMEBUFFER_H */