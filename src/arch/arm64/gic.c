/* gic.c - LunarOS ARM64 Generic Interrupt Controller
 * Lokasi: src/arch/arm64/gic.c
 */

#include "gic.h"
#include "../../kernel/panic.h"
#include "../../drivers/serial.h"

/* ─────────────────────────────────────────────
   MMIO read/write — akses register GIC
   ─────────────────────────────────────────────*/
static inline void mmio_write32(uint64_t addr, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    *p = val;
}

static inline uint32_t mmio_read32(uint64_t addr) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    return *p;
}

/* ─────────────────────────────────────────────
   Tabel handler — max 1020 IRQ (GICv2 limit)
   ─────────────────────────────────────────────*/
#define GIC_MAX_IRQ   1020
static irq_handler_t irq_handlers[GIC_MAX_IRQ];

/* ─────────────────────────────────────────────
   gic_init()
   ─────────────────────────────────────────────*/
int gic_init(void) {
    /* Baca jumlah IRQ yang didukung hardware */
    uint32_t typer    = mmio_read32(GICD_TYPER);
    uint32_t irq_lines = ((typer & 0x1F) + 1) * 32;

    /* Nol-kan tabel handler */
    for (uint32_t i = 0; i < GIC_MAX_IRQ; i++) irq_handlers[i] = 0;

    /* ── Setup Distributor ── */
    /* Disable dulu sebelum konfigurasi */
    mmio_write32(GICD_CTLR, 0);

    /* Disable semua IRQ */
    for (uint32_t i = 0; i < irq_lines; i += 32) {
        mmio_write32(GICD_ICENABLER + (i / 32) * 4, 0xFFFFFFFF);
    }

    /* Clear semua pending */
    for (uint32_t i = 0; i < irq_lines; i += 32) {
        mmio_write32(GICD_ICPENDR + (i / 32) * 4, 0xFFFFFFFF);
    }

    /* Set prioritas semua IRQ ke terendah (0xA0) */
    for (uint32_t i = 0; i < irq_lines; i += 4) {
        mmio_write32(GICD_IPRIORITYR + i, 0xA0A0A0A0);
    }

    /* Target semua IRQ ke CPU 0 */
    for (uint32_t i = 0; i < irq_lines; i += 4) {
        mmio_write32(GICD_ITARGETSR + i, 0x01010101);
    }

    /* Level-triggered untuk semua SPI */
    for (uint32_t i = 32; i < irq_lines; i += 16) {
        mmio_write32(GICD_ICFGR + (i / 16) * 4, 0x00000000);
    }

    /* Enable Distributor */
    mmio_write32(GICD_CTLR, 1);

    /* ── Setup CPU Interface ── */
    /* Priority mask: izinkan semua prioritas */
    mmio_write32(GICC_PMR, GIC_PRIORITY_ALL);

    /* Binary point: tidak ada preemption */
    mmio_write32(GICC_BPR, 0x07);

    /* Enable CPU interface */
    mmio_write32(GICC_CTLR, 1);

    serial_puts("[GIC] Initialized, IRQ lines: ");
    /* serial_print_int(irq_lines); */
    serial_puts("\n");

    return 0;
}

/* ─────────────────────────────────────────────
   Register / unregister handler
   ─────────────────────────────────────────────*/
void gic_register_irq(uint32_t irq_id, irq_handler_t handler) {
    if (irq_id >= GIC_MAX_IRQ) return;
    irq_handlers[irq_id] = handler;
    gic_enable_irq(irq_id);
}

void gic_unregister_irq(uint32_t irq_id) {
    if (irq_id >= GIC_MAX_IRQ) return;
    irq_handlers[irq_id] = 0;
    gic_disable_irq(irq_id);
}

/* ─────────────────────────────────────────────
   Enable / disable IRQ
   ─────────────────────────────────────────────*/
void gic_enable_irq(uint32_t irq_id) {
    uint32_t reg = irq_id / 32;
    uint32_t bit = irq_id % 32;
    mmio_write32(GICD_ISENABLER + reg * 4, 1U << bit);
}

void gic_disable_irq(uint32_t irq_id) {
    uint32_t reg = irq_id / 32;
    uint32_t bit = irq_id % 32;
    mmio_write32(GICD_ICENABLER + reg * 4, 1U << bit);
}

void gic_set_priority(uint32_t irq_id, uint8_t priority) {
    uint32_t reg    = irq_id / 4;
    uint32_t offset = (irq_id % 4) * 8;
    uint32_t val    = mmio_read32(GICD_IPRIORITYR + reg * 4);
    val &= ~(0xFF << offset);
    val |= ((uint32_t)priority << offset);
    mmio_write32(GICD_IPRIORITYR + reg * 4, val);
}

void gic_send_eoi(uint32_t irq_id) {
    mmio_write32(GICC_EOIR, irq_id);
}

/* ─────────────────────────────────────────────
   IRQ handler — dipanggil dari isr.S
   ─────────────────────────────────────────────*/
void irq_handler(registers_t *regs) {
    /* Baca IRQ ID dari CPU interface */
    uint32_t irq_id = mmio_read32(GICC_IAR) & 0x3FF;

    /* Spurious interrupt — abaikan */
    if (irq_id == GIC_SPURIOUS_IRQ) return;

    /* Panggil handler yang terdaftar */
    if (irq_id < GIC_MAX_IRQ && irq_handlers[irq_id]) {
        irq_handlers[irq_id](irq_id, regs);
    }

    /* Kirim EOI */
    gic_send_eoi(irq_id);
}

/* ─────────────────────────────────────────────
   FIQ handler (Fast IRQ — prioritas sangat tinggi)
   ─────────────────────────────────────────────*/
void fiq_handler(registers_t *regs) {
    (void)regs;
    /* LunarOS belum pakai FIQ — abaikan */
}

/* ─────────────────────────────────────────────
   Exception handler — dipanggil dari isr.S
   Untuk sync exception: page fault, undefined, dll
   ─────────────────────────────────────────────*/
void exception_handler(registers_t *regs, uint64_t type) {
    uint32_t ec = ESR_EC(regs->esr);

    /* SVC (syscall) ditangani langsung di isr.S, tidak masuk sini */

    /* Data abort / instruction abort = page fault */
    if (ec == EC_DATA_ABORT || ec == EC_INST_ABORT) {
        /* Baca FAR_EL1 = fault address */
        uint64_t far;
        __asm__ volatile("mrs %0, far_el1" : "=r"(far));
        serial_puts("[EXCEPTION] Page Fault at address: ");
        /* serial_print_hex(far); */
        serial_puts("\n");
    }

    /* Semua exception yang tidak di-handle → kernel panic */
    PANIC("Unhandled ARM64 exception");
}

/* ─────────────────────────────────────────────
   Syscall handler — dipanggil dari isr.S
   Konvensi ARM64: x8 = nomor syscall
                   x0–x5 = argumen
                   x0 = return value
   ─────────────────────────────────────────────*/
void syscall_handler(registers_t *regs) {
    uint64_t syscall_no = regs->x[8];

    /* Dispatch ke syscall table */
    extern int64_t syscall_dispatch(uint64_t no, registers_t *regs);
    int64_t result = syscall_dispatch(syscall_no, regs);

    /* Simpan return value ke x0 */
    regs->x[0] = (uint64_t)result;
}
