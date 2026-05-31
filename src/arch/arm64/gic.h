/* gic.h - LunarOS ARM64 Generic Interrupt Controller
 * Lokasi: src/arch/arm64/gic.h
 *
 * GIC adalah pengganti IDT + PIC 8259 di ARM64.
 * LunarOS pakai GICv2 (kompatibel QEMU virt, RPi 4, dll)
 */

#ifndef GIC_H
#define GIC_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Alamat base GICv2 (QEMU ARM virt machine)
   Untuk hardware nyata, baca dari Device Tree
   ─────────────────────────────────────────────
   QEMU: -machine virt → GIC di alamat ini
   Raspberry Pi 4: berbeda, perlu device tree
*/
#define GICD_BASE    0x08000000UL   /* Distributor */
#define GICC_BASE    0x08010000UL   /* CPU Interface */

/* ── GIC Distributor Register (GICD) ── */
#define GICD_CTLR        (GICD_BASE + 0x000)  /* control */
#define GICD_TYPER       (GICD_BASE + 0x004)  /* type info */
#define GICD_ISENABLER   (GICD_BASE + 0x100)  /* interrupt set-enable */
#define GICD_ICENABLER   (GICD_BASE + 0x180)  /* interrupt clear-enable */
#define GICD_ISPENDR     (GICD_BASE + 0x200)  /* set-pending */
#define GICD_ICPENDR     (GICD_BASE + 0x280)  /* clear-pending */
#define GICD_IPRIORITYR  (GICD_BASE + 0x400)  /* priority */
#define GICD_ITARGETSR   (GICD_BASE + 0x800)  /* target CPU */
#define GICD_ICFGR       (GICD_BASE + 0xC00)  /* config (level/edge) */

/* ── GIC CPU Interface Register (GICC) ── */
#define GICC_CTLR        (GICC_BASE + 0x000)  /* control */
#define GICC_PMR         (GICC_BASE + 0x004)  /* priority mask */
#define GICC_BPR         (GICC_BASE + 0x008)  /* binary point */
#define GICC_IAR         (GICC_BASE + 0x00C)  /* interrupt acknowledge */
#define GICC_EOIR        (GICC_BASE + 0x010)  /* end of interrupt */
#define GICC_RPR         (GICC_BASE + 0x014)  /* running priority */
#define GICC_HPPIR       (GICC_BASE + 0x018)  /* highest pending */

/* ── Konstanta ── */
#define GIC_SPURIOUS_IRQ   1023    /* nilai IAR jika tidak ada interrupt */
#define GIC_PRIORITY_MAX   0x00    /* prioritas tertinggi */
#define GIC_PRIORITY_MIN   0xFF    /* prioritas terendah */
#define GIC_PRIORITY_ALL   0xFF    /* mask: izinkan semua prioritas */

/* Nomor IRQ umum di QEMU virt */
#define IRQ_TIMER_VIRT     27      /* ARM virtual timer (PPI) */
#define IRQ_UART0          33      /* UART PL011 */
#define IRQ_VIRTIO_BASE    48      /* VirtIO devices */

/* ─────────────────────────────────────────────
   Struct registers ARM64
   Disimpan oleh SAVE_REGS macro di isr.S
   ─────────────────────────────────────────────*/
typedef struct {
    uint64_t x[31];      /* x0–x30 (x30 = link register) */
    uint64_t elr;        /* Exception Link Register — return address */
    uint64_t spsr;       /* Saved Program Status Register */
    uint64_t esr;        /* Exception Syndrome Register */
} __attribute__((packed)) registers_t;

/* Decode ESR_EL1 — exception class (bit [31:26]) */
#define ESR_EC(esr)      (((esr) >> 26) & 0x3F)
#define ESR_ISS(esr)     ((esr) & 0x1FFFFFF)

/* Exception Class values */
#define EC_UNKNOWN       0x00
#define EC_SVC_A64       0x15    /* SVC = system call */
#define EC_INST_ABORT    0x21    /* instruction abort (= page fault) */
#define EC_DATA_ABORT    0x25    /* data abort (= page fault) */
#define EC_SERROR        0x2F    /* system error */

/* ─────────────────────────────────────────────
   Tipe handler
   ─────────────────────────────────────────────*/
typedef void (*irq_handler_t)(uint32_t irq_id, registers_t *regs);

/* ─────────────────────────────────────────────
   Fungsi publik
   ─────────────────────────────────────────────*/

/** Inisialisasi GIC Distributor + CPU Interface */
int  gic_init(void);

/** Daftarkan handler untuk IRQ tertentu */
void gic_register_irq(uint32_t irq_id, irq_handler_t handler);

/** Hapus handler */
void gic_unregister_irq(uint32_t irq_id);

/** Enable / disable satu IRQ */
void gic_enable_irq(uint32_t irq_id);
void gic_disable_irq(uint32_t irq_id);

/** Set prioritas IRQ (0=tertinggi, 0xFF=terendah) */
void gic_set_priority(uint32_t irq_id, uint8_t priority);

/** Kirim End of Interrupt setelah handler selesai */
void gic_send_eoi(uint32_t irq_id);

/* Dipanggil dari isr.S */
void exception_handler(registers_t *regs, uint64_t type);
void irq_handler(registers_t *regs);
void fiq_handler(registers_t *regs);
void syscall_handler(registers_t *regs);

#endif /* GIC_H */
