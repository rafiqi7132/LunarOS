/**
 * vmm.c - LunarOS Virtual Memory Manager (ARM64)
 *
 * Mengatur page table ARM64 (4-level: PGD→PUD→PMD→PTE).
 * Setiap proses punya page table sendiri (seperti iOS).
 *
 * Lokasi: src/mm/vmm.c
 */

#include "vmm.h"
#include "pmm.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"

/* ─────────────────────────────────────────────
   ARM64 Page Table Entry flags
   ───────────────────────────────────────────── */
#define PTE_VALID       (1ULL << 0)     /* entry valid */
#define PTE_TABLE       (1ULL << 1)     /* ini table entry (bukan block) */
#define PTE_PAGE        (1ULL << 1)     /* ini page entry di level 3 */
#define PTE_AF          (1ULL << 10)    /* access flag — wajib di-set */
#define PTE_SH_INNER    (3ULL << 8)     /* inner shareable */
#define PTE_AP_RW_EL1   (0ULL << 6)    /* RW kernel only */
#define PTE_AP_RW_ALL   (1ULL << 6)    /* RW kernel + user */
#define PTE_AP_RO_EL1   (2ULL << 6)    /* RO kernel only */
#define PTE_AP_RO_ALL   (3ULL << 6)    /* RO kernel + user */
#define PTE_UXN         (1ULL << 54)   /* user execute never */
#define PTE_PXN         (1ULL << 53)   /* privileged execute never */
#define PTE_ATTRINDX(n) ((uint64_t)(n) << 2)   /* memory attribute index */

/* Memory attribute indices (MAIR_EL1) */
#define MAIR_NORMAL     0   /* normal cacheable memory */
#define MAIR_DEVICE     1   /* device memory (MMIO) */

/* ─────────────────────────────────────────────
   Ukuran dan mask
   ───────────────────────────────────────────── */
#define VA_BITS         39              /* 39-bit virtual address */
#define PAGE_SHIFT      12              /* 4 KB page */
#define TABLE_SHIFT     9               /* 512 entry per table */
#define TABLE_SIZE      (1 << TABLE_SHIFT)   /* 512 */
#define TABLE_ENTRIES   512

/* Mask untuk ekstrak index dari virtual address */
#define PGD_SHIFT       30
#define PUD_SHIFT       21
#define PMD_SHIFT       12

#define VA_INDEX(va, shift) (((va) >> (shift)) & (TABLE_ENTRIES - 1))

/* Mask alamat fisik di PTE (bit 47:12) */
#define PTE_ADDR_MASK   0x0000FFFFFFFFF000ULL

/* ─────────────────────────────────────────────
   State VMM
   ───────────────────────────────────────────── */
static uint64_t *kernel_pgd = NULL;    /* Page Global Directory kernel */

/* ─────────────────────────────────────────────
   Helper: alokasi satu frame untuk page table
   (harus nol sebelum dipakai)
   ───────────────────────────────────────────── */
static uint64_t *alloc_table(void) {
    uint64_t phys = pmm_alloc_frame();
    if (phys == 0) PANIC("VMM: kehabisan frame untuk page table");

    uint64_t *table = (uint64_t *)phys;
    for (int i = 0; i < TABLE_ENTRIES; i++) table[i] = 0;
    return table;
}

/* ─────────────────────────────────────────────
   Setup MAIR_EL1 — Memory Attribute Indirection Register
   Mendefinisikan tipe memori (normal vs device)
   ───────────────────────────────────────────── */
static void vmm_setup_mair(void) {
    /*
     * Index 0: Normal memory, inner/outer write-back cacheable
     * Index 1: Device-nGnRnE (paling strict, untuk MMIO)
     */
    uint64_t mair = (0xFFULL << 0)   /* index 0: normal */
                  | (0x00ULL << 8);  /* index 1: device */
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    __asm__ volatile("isb");
}

/* ─────────────────────────────────────────────
   Setup TCR_EL1 — Translation Control Register
   ───────────────────────────────────────────── */
static void vmm_setup_tcr(void) {
    uint64_t tcr =
        (0ULL  << 37) |   /* TBI0: tag bits tidak dipakai */
        (1ULL  << 36) |   /* AS: 16-bit ASID */
        (0ULL  << 32) |   /* IPS: 32-bit PA (4 GB) */
        (0b10ULL << 30) | /* TG1: 4KB granule (TTBR1) */
        (0b11ULL << 28) | /* SH1: inner shareable */
        (0b01ULL << 26) | /* ORGN1: write-back cacheable */
        (0b01ULL << 24) | /* IRGN1: write-back cacheable */
        (0ULL  << 23) |   /* EPD1: TTBR1 aktif */
        (25ULL << 16) |   /* T1SZ: 64-39=25 (39-bit VA) */
        (0b00ULL << 14) | /* TG0: 4KB granule (TTBR0) */
        (0b11ULL << 12) | /* SH0: inner shareable */
        (0b01ULL << 10) | /* ORGN0: write-back cacheable */
        (0b01ULL << 8)  | /* IRGN0: write-back cacheable */
        (0ULL  << 7)    | /* EPD0: TTBR0 aktif */
        (25ULL << 0);     /* T0SZ: 39-bit VA */

    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));
    __asm__ volatile("isb");
}

/* ─────────────────────────────────────────────
   vmm_map_page()
   Petakan satu virtual page ke satu physical frame
   ───────────────────────────────────────────── */
void vmm_map_page(uint64_t *pgd,
                  uint64_t  virt,
                  uint64_t  phys,
                  uint64_t  flags)
{
    /* Level 0 — PGD (Page Global Directory) */
    uint64_t pgd_idx = VA_INDEX(virt, PGD_SHIFT);
    uint64_t *pud;

    if (!(pgd[pgd_idx] & PTE_VALID)) {
        pud = alloc_table();
        pgd[pgd_idx] = (uint64_t)pud | PTE_VALID | PTE_TABLE;
    } else {
        pud = (uint64_t *)(pgd[pgd_idx] & PTE_ADDR_MASK);
    }

    /* Level 1 — PUD (Page Upper Directory) */
    uint64_t pud_idx = VA_INDEX(virt, PUD_SHIFT);
    uint64_t *pmd;

    if (!(pud[pud_idx] & PTE_VALID)) {
        pmd = alloc_table();
        pud[pud_idx] = (uint64_t)pmd | PTE_VALID | PTE_TABLE;
    } else {
        pmd = (uint64_t *)(pud[pud_idx] & PTE_ADDR_MASK);
    }

    /* Level 2 — PMD (Page Middle Directory) */
    uint64_t pmd_idx = VA_INDEX(virt, PMD_SHIFT);
    uint64_t *pte_table;

    if (!(pmd[pmd_idx] & PTE_VALID)) {
        pte_table = alloc_table();
        pmd[pmd_idx] = (uint64_t)pte_table | PTE_VALID | PTE_TABLE;
    } else {
        pte_table = (uint64_t *)(pmd[pmd_idx] & PTE_ADDR_MASK);
    }

    /* Level 3 — PTE (Page Table Entry) */
    uint64_t pte_idx = VA_INDEX(virt, PAGE_SHIFT) & (TABLE_ENTRIES - 1);
    pte_table[pte_idx] = (phys & PTE_ADDR_MASK) | flags | PTE_VALID | PTE_PAGE;

    /* Invalidate TLB untuk alamat ini */
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(virt >> PAGE_SHIFT));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/* ─────────────────────────────────────────────
   vmm_unmap_page()
   Hapus mapping satu virtual page
   ───────────────────────────────────────────── */
void vmm_unmap_page(uint64_t *pgd, uint64_t virt) {
    uint64_t pgd_idx = VA_INDEX(virt, PGD_SHIFT);
    if (!(pgd[pgd_idx] & PTE_VALID)) return;
    uint64_t *pud = (uint64_t *)(pgd[pgd_idx] & PTE_ADDR_MASK);

    uint64_t pud_idx = VA_INDEX(virt, PUD_SHIFT);
    if (!(pud[pud_idx] & PTE_VALID)) return;
    uint64_t *pmd = (uint64_t *)(pud[pud_idx] & PTE_ADDR_MASK);

    uint64_t pmd_idx = VA_INDEX(virt, PMD_SHIFT);
    if (!(pmd[pmd_idx] & PTE_VALID)) return;
    uint64_t *pte_table = (uint64_t *)(pmd[pmd_idx] & PTE_ADDR_MASK);

    uint64_t pte_idx = VA_INDEX(virt, PAGE_SHIFT) & (TABLE_ENTRIES - 1);
    pte_table[pte_idx] = 0;

    __asm__ volatile("tlbi vaae1is, %0" :: "r"(virt >> PAGE_SHIFT));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/* ─────────────────────────────────────────────
   vmm_create_pgd()
   Buat page table baru untuk proses baru
   ───────────────────────────────────────────── */
uint64_t *vmm_create_pgd(void) {
    return alloc_table();
}

/* ─────────────────────────────────────────────
   vmm_destroy_pgd()
   Hapus semua page table milik satu proses
   ───────────────────────────────────────────── */
void vmm_destroy_pgd(uint64_t *pgd) {
    for (int i = 0; i < TABLE_ENTRIES; i++) {
        if (!(pgd[i] & PTE_VALID)) continue;
        uint64_t *pud = (uint64_t *)(pgd[i] & PTE_ADDR_MASK);
        for (int j = 0; j < TABLE_ENTRIES; j++) {
            if (!(pud[j] & PTE_VALID)) continue;
            uint64_t *pmd = (uint64_t *)(pud[j] & PTE_ADDR_MASK);
            for (int k = 0; k < TABLE_ENTRIES; k++) {
                if (!(pmd[k] & PTE_VALID)) continue;
                pmm_free_frame(pmd[k] & PTE_ADDR_MASK);
            }
            pmm_free_frame((uint64_t)pmd);
        }
        pmm_free_frame((uint64_t)pud);
    }
    pmm_free_frame((uint64_t)pgd);
}

/* ─────────────────────────────────────────────
   vmm_switch_pgd()
   Ganti page table aktif (context switch)
   Tulis ke TTBR0_EL1 (user space page table)
   ───────────────────────────────────────────── */
void vmm_switch_pgd(uint64_t *pgd) {
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)pgd));
    __asm__ volatile("isb");
    /* Flush TLB semua entry user space */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/* ─────────────────────────────────────────────
   vmm_init()
   Setup paging untuk kernel, aktifkan MMU
   ───────────────────────────────────────────── */
int vmm_init(void) {
    vmm_setup_mair();
    vmm_setup_tcr();

    /* Buat page table kernel */
    kernel_pgd = alloc_table();

    /* Map kernel image — identity map (VA = PA) */
    extern char __kernel_start[];
    extern char __kernel_end[];

    uint64_t kstart = (uint64_t)__kernel_start;
    uint64_t kend   = PAGE_ALIGN((uint64_t)__kernel_end);

    uint64_t kernel_flags = PTE_AF | PTE_SH_INNER |
                            PTE_AP_RW_EL1 | PTE_UXN |
                            PTE_ATTRINDX(MAIR_NORMAL);

    for (uint64_t addr = kstart; addr < kend; addr += PAGE_SIZE) {
        vmm_map_page(kernel_pgd, addr, addr, kernel_flags);
    }

    /* Map GIC MMIO region sebagai device memory */
    uint64_t device_flags = PTE_AF | PTE_AP_RW_EL1 |
                             PTE_UXN | PTE_PXN |
                             PTE_ATTRINDX(MAIR_DEVICE);

    /* GIC Distributor */
    for (uint64_t addr = 0x08000000; addr < 0x08001000; addr += PAGE_SIZE) {
        vmm_map_page(kernel_pgd, addr, addr, device_flags);
    }
    /* GIC CPU Interface */
    for (uint64_t addr = 0x08010000; addr < 0x08020000; addr += PAGE_SIZE) {
        vmm_map_page(kernel_pgd, addr, addr, device_flags);
    }
    /* UART PL011 */
    for (uint64_t addr = 0x09000000; addr < 0x09001000; addr += PAGE_SIZE) {
        vmm_map_page(kernel_pgd, addr, addr, device_flags);
    }

    /* Pasang kernel page table ke TTBR1_EL1 (kernel space) */
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"((uint64_t)kernel_pgd));
    __asm__ volatile("isb");

    /* Aktifkan MMU via SCTLR_EL1 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);    /* M  — enable MMU */
    sctlr |= (1ULL << 2);    /* C  — enable data cache */
    sctlr |= (1ULL << 12);   /* I  — enable instruction cache */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");

    pl011_puts("[VMM] MMU aktif, kernel mapped\n");
    return 0;
}

/* ─────────────────────────────────────────────
   Getter kernel PGD
   ───────────────────────────────────────────── */
uint64_t *vmm_get_kernel_pgd(void) {
    return kernel_pgd;
}