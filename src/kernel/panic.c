/**
 * panic.c - LunarOS Kernel Panic Handler (ARM64)
 *
 * Versi ARM64 — semua referensi x86 dihapus:
 *   - Tidak ada vga_*, diganti fb_console_* + pl011_*
 *   - Tidak ada cli (x86), diganti daif mask (ARM64)
 *   - Tidak ada CR2, diganti FAR_EL1
 *   - Register dump pakai x0–x30, ELR, SPSR, ESR
 *   - Stack trace pakai x29 (frame pointer ARM64)
 *   - Halt pakai wfi bukan hlt
 *
 * Lokasi: src/kernel/panic.c
 */

#include "panic.h"
#include "../drivers/framebuffer.h"
#include "../drivers/pl011.h"

/* Buffer pesan panic terakhir */
static char last_panic_message[256];
static int  panic_in_progress = 0;

/* ─────────────────────────────────────────────
   Helper output — tulis ke BOTH framebuffer + UART
   ───────────────────────────────────────────── */
static void panic_print(const char *str) {
    fb_console_puts(str);
    pl011_puts(str);
}

static void panic_print_hex64(uint64_t val) {
    const char h[] = "0123456789ABCDEF";
    char buf[19];   /* "0x" + 16 digit + '\0' */
    buf[0]  = '0';
    buf[1]  = 'x';
    buf[18] = '\0';
    for (int i = 17; i >= 2; i--) {
        buf[i] = h[val & 0xF];
        val >>= 4;
    }
    panic_print(buf);
}

static void panic_print_int(int val) {
    if (val < 0) { panic_print("-"); val = -val; }
    char buf[12];
    int  idx = 10;
    buf[11] = '\0';
    buf[10] = '0';
    if (val == 0) { panic_print("0"); return; }
    while (val > 0 && idx >= 0) {
        buf[idx--] = '0' + (val % 10);
        val /= 10;
    }
    panic_print(&buf[idx + 1]);
}

/* ─────────────────────────────────────────────
   Decode ESR_EL1 — jelaskan penyebab exception
   ───────────────────────────────────────────── */
static const char *esr_ec_name(uint32_t ec) {
    switch (ec) {
    case 0x00: return "Unknown reason";
    case 0x01: return "WFI/WFE trapped";
    case 0x07: return "SVE/SIMD/FP access";
    case 0x0E: return "Illegal execution state";
    case 0x15: return "SVC (system call) AArch64";
    case 0x18: return "MRS/MSR trapped";
    case 0x20: return "Instruction Abort (lower EL)";
    case 0x21: return "Instruction Abort (same EL)";
    case 0x22: return "PC alignment fault";
    case 0x24: return "Data Abort (lower EL)";
    case 0x25: return "Data Abort (same EL)";
    case 0x26: return "SP alignment fault";
    case 0x28: return "FP exception (AArch32)";
    case 0x2C: return "FP exception (AArch64)";
    case 0x2F: return "SError interrupt";
    case 0x30: return "Breakpoint (lower EL)";
    case 0x31: return "Breakpoint (same EL)";
    case 0x32: return "Software step (lower EL)";
    case 0x33: return "Software step (same EL)";
    case 0x34: return "Watchpoint (lower EL)";
    case 0x35: return "Watchpoint (same EL)";
    case 0x3C: return "BRK instruction";
    default:   return "Reserved/Unknown EC";
    }
}

/* ─────────────────────────────────────────────
   Dump register ARM64
   ───────────────────────────────────────────── */
static void panic_dump_registers(registers_t *regs) {
    if (!regs) return;

    panic_print("\n  [ARM64 Registers]\n");

    /* x0–x7 */
    for (int i = 0; i < 8; i++) {
        panic_print("  x");
        panic_print_int(i);
        if (i < 10) panic_print(" =");
        else        panic_print("=");
        panic_print_hex64(regs->x[i]);
        if (i % 2 == 1) panic_print("\n");
        else            panic_print("  ");
    }

    /* x8–x15 */
    for (int i = 8; i < 16; i++) {
        panic_print("  x");
        panic_print_int(i);
        panic_print("=");
        panic_print_hex64(regs->x[i]);
        if (i % 2 == 1) panic_print("\n");
        else            panic_print("  ");
    }

    /* x16–x29 */
    for (int i = 16; i < 30; i++) {
        panic_print("  x");
        panic_print_int(i);
        panic_print("=");
        panic_print_hex64(regs->x[i]);
        if (i % 2 == 1) panic_print("\n");
        else            panic_print("  ");
    }

    /* x30 = Link Register */
    panic_print("  x30(LR) =");
    panic_print_hex64(regs->x[30]);
    panic_print("\n");

    /* ELR — Exception Link Register (return address) */
    panic_print("  ELR_EL1 =");
    panic_print_hex64(regs->elr);
    panic_print("  (exception return addr)\n");

    /* SPSR */
    panic_print("  SPSR_EL1=");
    panic_print_hex64(regs->spsr);
    panic_print("\n");

    /* ESR — Exception Syndrome Register */
    uint32_t ec  = ESR_EC(regs->esr);
    uint32_t iss = ESR_ISS(regs->esr);
    panic_print("  ESR_EL1 =");
    panic_print_hex64(regs->esr);
    panic_print("\n");
    panic_print("  EC=0x");
    panic_print_hex64(ec);
    panic_print(" → ");
    panic_print(esr_ec_name(ec));
    panic_print("\n");
    panic_print("  ISS=0x");
    panic_print_hex64(iss);
    panic_print("\n");

    /* FAR_EL1 — Fault Address Register (pengganti CR2 x86) */
    uint64_t far;
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    panic_print("  FAR_EL1 =");
    panic_print_hex64(far);
    panic_print("  (fault address)\n");
}

/* ─────────────────────────────────────────────
   Stack trace ARM64
   Pakai x29 (frame pointer) chain
   ARM64 ABI: [x29] = prev FP, [x29+8] = return addr
   ───────────────────────────────────────────── */
static void panic_print_stack_trace(void) {
    panic_print("\n  [Stack Trace]\n");

    uint64_t *fp;
    __asm__ volatile("mov %0, x29" : "=r"(fp));

    int depth = 0;
    while (fp && depth < 8) {
        uint64_t ret_addr = *(fp + 1);
        if (ret_addr == 0) break;

        panic_print("  #");
        panic_print_int(depth);
        panic_print("  ");
        panic_print_hex64(ret_addr);
        panic_print("\n");

        fp = (uint64_t *)*fp;
        depth++;
    }

    if (depth == 0) {
        panic_print("  (stack trace tidak tersedia)\n");
    }
}

/* ─────────────────────────────────────────────
   kernel_panic() — fungsi utama
   ───────────────────────────────────────────── */
void kernel_panic(const char *message, const char *file,
                  int line, registers_t *regs)
{
    /*
     * Masking semua interrupt ARM64.
     * ARM64 tidak pakai cli — pakai MSR DAIF
     * D=debug, A=SError, I=IRQ, F=FIQ
     */
    __asm__ volatile("msr daifset, #0xF");

    /* Cegah panic rekursif */
    if (panic_in_progress) {
        pl011_puts("\n*** DOUBLE PANIC — SYSTEM HALTED ***\n");
        goto halt;
    }
    panic_in_progress = 1;

    /* Salin pesan ke buffer */
    int i = 0;
    while (message[i] && i < 255) {
        last_panic_message[i] = message[i];
        i++;
    }
    last_panic_message[i] = '\0';

    /* Set warna merah di framebuffer */
    fb_console_set_color(LUNAR_FG, LUNAR_ERROR);
    fb_console_clear();

    /* ── Header ── */
    panic_print("══════════════════════════════════════════════════\n");
    panic_print("           LunarOS — KERNEL PANIC                \n");
    panic_print("           ARM64 / AArch64                       \n");
    panic_print("══════════════════════════════════════════════════\n\n");

    /* ── Pesan ── */
    panic_print("  Pesan  : ");
    panic_print(message);
    panic_print("\n");

    panic_print("  File   : ");
    panic_print(file ? file : "(unknown)");
    panic_print("\n");

    panic_print("  Baris  : ");
    panic_print_int(line);
    panic_print("\n");

    /* ── Register dump ── */
    panic_dump_registers(regs);

    /* ── Stack trace ── */
    panic_print_stack_trace();

    /* ── Footer ── */
    panic_print("\n══════════════════════════════════════════════════\n");
    panic_print("  Sistem dihentikan. Silakan restart perangkat.\n");
    panic_print("══════════════════════════════════════════════════\n");

    /* Log ke UART */
    pl011_puts("\n[KERNEL PANIC] ");
    pl011_puts(message);
    pl011_puts(" at ");
    pl011_puts(file ? file : "?");
    pl011_puts(":");
    pl011_print_int(line);
    pl011_puts("\n");

halt:
    /* Halt CPU ARM64 — wfi dalam infinite loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ─────────────────────────────────────────────
   Dipanggil dari exception_handler() di gic.c
   ───────────────────────────────────────────── */
void panic_exception_handler(registers_t *regs) {
    uint32_t ec = ESR_EC(regs->esr);
    kernel_panic(esr_ec_name(ec), __FILE__, __LINE__, regs);
}

/* ─────────────────────────────────────────────
   Getter pesan terakhir
   ───────────────────────────────────────────── */
const char *panic_get_last_message(void) {
    return last_panic_message;
}