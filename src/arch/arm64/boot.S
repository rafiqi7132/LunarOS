/* boot.S - LunarOS ARM64 Boot Entry
 * Lokasi: src/arch/arm64/boot.S
 *
 * Di ARM64 tidak ada GRUB/Multiboot.
 * Bootloader (U-Boot / UEFI) langsung lompat ke sini.
 */

.section ".text.boot"
.global _start

_start:
    /* ── Pastikan hanya core 0 yang boot, sisanya tidur ── */
    mrs  x0, mpidr_el1        /* baca CPU ID */
    and  x0, x0, #0xFF        /* ambil core number */
    cbnz x0, .park_core       /* kalau bukan core 0, parkir */

    /* ── Setup stack pointer ── */
    ldr  x0, =stack_top
    mov  sp, x0

    /* ── Nol-kan BSS section ── */
    ldr  x0, =__bss_start
    ldr  x1, =__bss_end
    mov  x2, #0
.zero_bss:
    cmp  x0, x1
    bge  .bss_done
    str  x2, [x0], #8
    b    .zero_bss
.bss_done:

    /* ── Setup exception vector (pengganti IDT di ARM64) ── */
    ldr  x0, =vector_table
    msr  vbar_el1, x0         /* VBAR_EL1 = Vector Base Address Register */
    isb

    /* ── Masuk ke kernel ── */
    bl   kernel_main

    /* Seharusnya tidak pernah sampai sini */
    b    .hang

/* Core non-0 diparkir di sini (WFE loop) */
.park_core:
    wfe
    b    .park_core

/* Hang jika kernel_main return */
.hang:
    wfi
    b    .hang

/* ── Stack (8 KB) ── */
.section ".bss"
.align 4
stack_bottom:
    .space 8192
stack_top:
