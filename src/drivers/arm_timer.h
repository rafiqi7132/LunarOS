/**
 * arm_timer.h - LunarOS ARM64 Generic Timer Header
 * Pengganti timer.c (PIT x86) untuk ARM64
 * Lokasi: src/drivers/arm_timer.h
 */

#ifndef ARM_TIMER_H
#define ARM_TIMER_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Frekuensi timer QEMU virt = 62.5 MHz
   (dibaca dari CNTFRQ_EL0)
   ───────────────────────────────────────────── */
#define TIMER_DEFAULT_HZ    100     /* 100 tick per detik = 10ms per tick */

/* ─────────────────────────────────────────────
   Struct info timer
   ───────────────────────────────────────────── */
typedef struct {
    uint64_t tick_count;        /* total tick sejak boot */
    uint64_t uptime_ms;         /* uptime dalam milidetik */
    uint64_t freq_hz;           /* frekuensi counter hardware */
    uint32_t tick_rate_hz;      /* tick rate yang diminta */
} timer_info_t;

/* ─────────────────────────────────────────────
   Tipe callback timer
   ───────────────────────────────────────────── */
typedef void (*timer_callback_t)(void);

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */
int      arm_timer_init(uint32_t hz);

/* Tick & waktu */
uint64_t arm_timer_get_ticks(void);
uint64_t arm_timer_get_uptime_ms(void);
uint64_t arm_timer_get_uptime_sec(void);
void     arm_timer_get_info(timer_info_t *info);

/* Delay */
void     arm_timer_sleep_ms(uint64_t ms);
void     arm_timer_sleep_us(uint64_t us);

/* Callback periodik */
void     arm_timer_set_callback(timer_callback_t cb);

/* Dipanggil dari GIC IRQ handler */
void     arm_timer_irq_handler(uint32_t irq_id, registers_t *regs);

#endif /* ARM_TIMER_H */