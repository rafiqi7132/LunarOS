/* isr.S - LunarOS ARM64 Exception Vector Table
 * Lokasi: src/arch/arm64/isr.S
 *
 * Di ARM64, exception vector bukan IDT tapi VBAR_EL1.
 * Ada 16 slot, masing-masing 128 byte (32 instruksi).
 * Format: 4 kelompok x 4 tipe exception.
 */

.section ".text"
.global vector_table

/* Macro: simpan semua register ke stack */
.macro SAVE_REGS
    sub  sp,  sp,  #272        /* 34 register x 8 byte */
    stp  x0,  x1,  [sp, #0]
    stp  x2,  x3,  [sp, #16]
    stp  x4,  x5,  [sp, #32]
    stp  x6,  x7,  [sp, #48]
    stp  x8,  x9,  [sp, #64]
    stp  x10, x11, [sp, #80]
    stp  x12, x13, [sp, #96]
    stp  x14, x15, [sp, #112]
    stp  x16, x17, [sp, #128]
    stp  x18, x19, [sp, #144]
    stp  x20, x21, [sp, #160]
    stp  x22, x23, [sp, #176]
    stp  x24, x25, [sp, #192]
    stp  x26, x27, [sp, #208]
    stp  x28, x29, [sp, #224]
    str  x30,      [sp, #240]  /* link register */
    mrs  x0, elr_el1           /* exception return address */
    mrs  x1, spsr_el1          /* saved program status */
    stp  x0,  x1,  [sp, #248]
    mrs  x0, esr_el1           /* exception syndrome */
    str  x0,       [sp, #264]
.endm

/* Macro: restore semua register dari stack */
.macro RESTORE_REGS
    ldp  x0,  x1,  [sp, #248]
    msr  elr_el1,  x0
    msr  spsr_el1, x1
    ldp  x0,  x1,  [sp, #0]
    ldp  x2,  x3,  [sp, #16]
    ldp  x4,  x5,  [sp, #32]
    ldp  x6,  x7,  [sp, #48]
    ldp  x8,  x9,  [sp, #64]
    ldp  x10, x11, [sp, #80]
    ldp  x12, x13, [sp, #96]
    ldp  x14, x15, [sp, #112]
    ldp  x16, x17, [sp, #128]
    ldp  x18, x19, [sp, #144]
    ldp  x20, x21, [sp, #160]
    ldp  x22, x23, [sp, #176]
    ldp  x24, x25, [sp, #192]
    ldp  x26, x27, [sp, #208]
    ldp  x28, x29, [sp, #224]
    ldr  x30,      [sp, #240]
    add  sp,  sp,  #272
.endm

/* Macro: satu slot vector (harus tepat 128 byte) */
.macro VECTOR_ENTRY label
    .align 7                   /* 2^7 = 128 byte alignment */
\label:
.endm

/* ─────────────────────────────────────────────
   Vector Table — harus align 2KB (2^11)
   ─────────────────────────────────────────────
   Struktur 4 kelompok:
     [0] Current EL, SP_EL0   (jarang dipakai)
     [1] Current EL, SP_ELx   ← kernel exception
     [2] Lower EL, AArch64    ← dari user space (64-bit)
     [3] Lower EL, AArch32    ← dari user space (32-bit, skip)
   Tiap kelompok punya 4 slot:
     Synchronous, IRQ, FIQ, SError
*/
.align 11
vector_table:

/* ── Kelompok 0: Current EL, SP_EL0 ── */
VECTOR_ENTRY vec_cur_sp0_sync
    SAVE_REGS
    mov  x0, sp
    mov  x1, #0x00            /* tipe: sync, SP_EL0 */
    bl   exception_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_cur_sp0_irq
    SAVE_REGS
    mov  x0, sp
    bl   irq_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_cur_sp0_fiq
    SAVE_REGS
    mov  x0, sp
    bl   fiq_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_cur_sp0_serror
    SAVE_REGS
    mov  x0, sp
    mov  x1, #0x03
    bl   exception_handler
    RESTORE_REGS
    eret

/* ── Kelompok 1: Current EL, SP_ELx (kernel) ── */
VECTOR_ENTRY vec_cur_spx_sync
    SAVE_REGS
    mov  x0, sp
    mov  x1, #0x10
    bl   exception_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_cur_spx_irq
    SAVE_REGS
    mov  x0, sp
    bl   irq_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_cur_spx_fiq
    SAVE_REGS
    mov  x0, sp
    bl   fiq_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_cur_spx_serror
    SAVE_REGS
    mov  x0, sp
    mov  x1, #0x13
    bl   exception_handler
    RESTORE_REGS
    eret

/* ── Kelompok 2: Lower EL, AArch64 (user space 64-bit) ── */
VECTOR_ENTRY vec_lower_a64_sync
    SAVE_REGS
    mov  x0, sp
    mrs  x1, esr_el1
    lsr  x1, x1, #26          /* EC field = exception class */
    cmp  x1, #0x15            /* 0x15 = SVC (system call) */
    beq  .syscall_entry
    mov  x1, #0x20
    bl   exception_handler
    b    .lower_a64_sync_done
.syscall_entry:
    bl   syscall_handler      /* panggil syscall handler C */
.lower_a64_sync_done:
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_lower_a64_irq
    SAVE_REGS
    mov  x0, sp
    bl   irq_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_lower_a64_fiq
    SAVE_REGS
    mov  x0, sp
    bl   fiq_handler
    RESTORE_REGS
    eret

VECTOR_ENTRY vec_lower_a64_serror
    SAVE_REGS
    mov  x0, sp
    mov  x1, #0x23
    bl   exception_handler
    RESTORE_REGS
    eret

/* ── Kelompok 3: Lower EL, AArch32 (tidak dipakai LunarOS) ── */
VECTOR_ENTRY vec_lower_a32_sync
    b    .hang32
VECTOR_ENTRY vec_lower_a32_irq
    b    .hang32
VECTOR_ENTRY vec_lower_a32_fiq
    b    .hang32
VECTOR_ENTRY vec_lower_a32_serror
.hang32:
    wfi
    b    .hang32
