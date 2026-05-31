/**
 * arm_timer.c - LunarOS ARM64 Generic Timer Driver
 *
 * Pakai ARM Generic Timer (built-in di semua Cortex-A).
 * Register yang dipakai:
 *   CNTFRQ_EL0  — frekuensi counter (read-only, diset firmware)
 *   CNTP_CTL_EL0 — control (enable/disable/mask)
 *   CNTP_TVAL_EL0 — nilai countdown (tulis untuk set interval)
 *   CNTPCT_EL0  — counter value saat ini (read-only)
 *
 * Lokasi: src/drivers/arm_timer.c
 */

#include "arm_timer.h"
#include "pl011.h"
#include "../arch/arm64/gic.h"
#include "../kernel/panic.h"

/* ─────────────────────────────────────────────
   CNTP_CTL_EL0 bits
   ───────────────────────────────────────────── */
#define TIMER_CTL_ENABLE    (1 << 0)   /* enable timer */
#define TIMER_CTL_IMASK     (1 << 1)   /* mask interrupt (1=masked) */
#define TIMER_CTL_ISTATUS   (1 << 2)   /* interrupt status (read-only) */

/* ─────────────────────────────────────────────
   State
   ───────────────────────────────────────────── */
static uint64_t timer_freq     = 0;    /* CNTFRQ_EL0 */
static uint64_t timer_interval = 0;    /* ticks per interrupt */
static uint64_t tick_count     = 0;
static uint32_t tick_rate_hz   = 0;

static timer_callback_t user_callback = NULL;

/* ─────────────────────────────────────────────
   Helper: baca/tulis system register timer
   ───────────────────────────────────────────── */
static inline uint64_t read_cntfrq(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_cntpct(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline void write_cntp_tval(uint64_t val) {
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(val));
}

static inline void write_cntp_ctl(uint64_t val) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(val));
}

static inline uint64_t read_cntp_ctl(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    return val;
}

/* ─────────────────────────────────────────────
   arm_timer_init()
   ───────────────────────────────────────────── */
int arm_timer_init(uint32_t hz) {
    if (hz == 0) hz = TIMER_DEFAULT_HZ;
    tick_rate_hz = hz;

    /* Baca frekuensi hardware dari firmware */
    timer_freq = read_cntfrq();
    if (timer_freq == 0) {
        /* QEMU kadang return 0, paksa ke 62.5 MHz */
        timer_freq = 62500000ULL;
    }

    /* Hitung interval: berapa counter-tick per timer-tick */
    timer_interval = timer_freq / hz;

    /* Disable dulu */
    write_cntp_ctl(0);

    /* Set interval pertama */
    write_cntp_tval(timer_interval);

    /* Daftarkan IRQ handler ke GIC
     * IRQ 27 = ARM virtual timer PPI (Private Peripheral Interrupt) */
    gic_register_irq(IRQ_TIMER_VIRT, arm_timer_irq_handler);

    /* Enable timer, unmask interrupt */
    write_cntp_ctl(TIMER_CTL_ENABLE);

    pl011_puts("[TIMER] ARM Generic Timer: ");
    pl011_print_uint(timer_freq / 1000000);
    pl011_puts(" MHz, ");
    pl011_print_uint(hz);
    pl011_puts(" Hz tick\n");

    return 0;
}

/* ─────────────────────────────────────────────
   IRQ handler — dipanggil GIC setiap timer tick
   ───────────────────────────────────────────── */
void arm_timer_irq_handler(uint32_t irq_id, registers_t *regs) {
    (void)irq_id; (void)regs;

    tick_count++;

    /* Set interval berikutnya */
    write_cntp_tval(timer_interval);

    /* Panggil callback (scheduler, dll) */
    if (user_callback) user_callback();
}

/* ─────────────────────────────────────────────
   Getter
   ───────────────────────────────────────────── */
uint64_t arm_timer_get_ticks(void) {
    return tick_count;
}

uint64_t arm_timer_get_uptime_ms(void) {
    return tick_count * 1000 / tick_rate_hz;
}

uint64_t arm_timer_get_uptime_sec(void) {
    return tick_count / tick_rate_hz;
}

void arm_timer_get_info(timer_info_t *info) {
    if (!info) return;
    info->tick_count    = tick_count;
    info->uptime_ms     = arm_timer_get_uptime_ms();
    info->freq_hz       = timer_freq;
    info->tick_rate_hz  = tick_rate_hz;
}

/* ─────────────────────────────────────────────
   Delay — blocking (pakai hardware counter)
   ───────────────────────────────────────────── */
void arm_timer_sleep_ms(uint64_t ms) {
    uint64_t target = read_cntpct() + (timer_freq * ms / 1000);
    while (read_cntpct() < target) {
        __asm__ volatile("wfe");   /* hemat power sambil nunggu */
    }
}

void arm_timer_sleep_us(uint64_t us) {
    uint64_t target = read_cntpct() + (timer_freq * us / 1000000);
    while (read_cntpct() < target);
}

/* ─────────────────────────────────────────────
   Set callback periodik
   ───────────────────────────────────────────── */
void arm_timer_set_callback(timer_callback_t cb) {
    user_callback = cb;
}
