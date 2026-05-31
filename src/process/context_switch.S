/* context_switch.S - ARM64 Context Switch
 * Lokasi: src/process/context_switch.S
 *
 * Simpan callee-saved registers proses lama,
 * restore callee-saved registers proses baru.
 *
 * void context_switch_asm(cpu_context_t *old, cpu_context_t *new)
 *   x0 = pointer ke cpu_context_t proses lama
 *   x1 = pointer ke cpu_context_t proses baru
 *
 * Layout cpu_context_t (offset dalam byte):
 *   0  : x19
 *   8  : x20
 *   16 : x21
 *   24 : x22
 *   32 : x23
 *   40 : x24
 *   48 : x25
 *   56 : x26
 *   64 : x27
 *   72 : x28
 *   80 : x29 (FP)
 *   88 : x30 (LR)
 *   96 : sp
 *   104: pc
 *   112: spsr
 */

.section .text
.global context_switch_asm

context_switch_asm:
    /* ── Simpan context proses LAMA (x0) ── */
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #80]   /* FP dan LR */
    mov  x9,  sp
    str  x9,       [x0, #96]   /* SP */
    adr  x9,  .ret_old
    str  x9,       [x0, #104]  /* PC = setelah stp ini */
    mrs  x9,  spsr_el1
    str  x9,       [x0, #112]  /* SPSR */

    /* ── Restore context proses BARU (x1) ── */
    ldp  x19, x20, [x1, #0]
    ldp  x21, x22, [x1, #16]
    ldp  x23, x24, [x1, #32]
    ldp  x25, x26, [x1, #48]
    ldp  x27, x28, [x1, #64]
    ldp  x29, x30, [x1, #80]   /* FP dan LR */
    ldr  x9,       [x1, #96]
    mov  sp,  x9               /* SP */
    ldr  x9,       [x1, #112]
    msr  spsr_el1, x9          /* SPSR */
    ldr  x9,       [x1, #104]  /* PC */
    br   x9                    /* lompat ke proses baru */

.ret_old:
    ret
