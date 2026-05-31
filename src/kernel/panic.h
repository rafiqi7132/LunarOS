/**
 * panic.h - LunarOS Kernel Panic Header
 *
 * Lokasi: src/kernel/panic.h
 */

#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/*
 * Import registers_t dari gic.h (ARM64).
 * Di ARM64 tidak ada idt.h — struct registers
 * didefinisikan di gic.h bersama exception handler.
 */
#include "../arch/arm64/gic.h"

/* ─────────────────────────────────────────────
   Macro PANIC — otomatis isi file & baris
   Contoh pemakaian:
       PANIC("GIC init failed");
   ───────────────────────────────────────────── */
#define PANIC(msg) \
    kernel_panic((msg), __FILE__, __LINE__, NULL)

/* Versi dengan register dump (dari exception handler) */
#define PANIC_REGS(msg, regs) \
    kernel_panic((msg), __FILE__, __LINE__, (regs))

/* Assert — panic jika kondisi false */
#define KASSERT(cond) \
    do { \
        if (!(cond)) \
            kernel_panic("Assertion failed: " #cond, \
                         __FILE__, __LINE__, NULL); \
    } while (0)

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */

/**
 * kernel_panic() — hentikan sistem dan tampilkan info error
 *
 * @param message  Pesan error
 * @param file     Nama file (__FILE__)
 * @param line     Nomor baris (__LINE__)
 * @param regs     Register ARM64 saat panic (boleh NULL)
 *
 * Fungsi ini TIDAK pernah return.
 */
void kernel_panic(const char *message, const char *file,
                  int line, registers_t *regs)
    __attribute__((noreturn));

/**
 * panic_exception_handler() — dipanggil dari exception_handler()
 * di gic.c saat terjadi sync exception yang tidak di-handle.
 *
 * @param regs  Register ARM64 yang disimpan oleh isr.S
 */
void panic_exception_handler(registers_t *regs)
    __attribute__((noreturn));

/**
 * panic_get_last_message() — ambil pesan panic terakhir
 */
const char *panic_get_last_message(void);

#endif /* PANIC_H */