/**
 * init.h - LunarOS Kernel Initialization Header (ARM64)
 *
 * Lokasi: src/kernel/init.h
 */

#ifndef INIT_H
#define INIT_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Memory layout ARM64 — QEMU virt machine
   Berbeda total dari x86:
     x86 : kernel di 0x00100000 (1 MB)
     ARM64: kernel di 0x40000000 (1 GB) — QEMU virt
   ───────────────────────────────────────────── */
#define RAM_BASE                 0x40000000ULL   /* QEMU virt RAM start */
#define RAM_SIZE                 (256ULL * 1024 * 1024)  /* 256 MB */
#define RAM_END                  (RAM_BASE + RAM_SIZE)

#define KERNEL_LOAD_ADDR         0x40000000ULL   /* kernel di-load di sini */

/*
 * Heap virtual — mulai 16 MB setelah kernel base.
 * Nilai eksak kernel_end dibaca dari linker symbol
 * __kernel_end di pmm.c/heap.c, tapi kita perlu
 * konstanta ini untuk heap_init() di init.c.
 */
#define KERNEL_HEAP_VIRT_START   0x41000000ULL
#define KERNEL_HEAP_INITIAL_SIZE (4ULL  * 1024 * 1024)   /* 4  MB */
#define KERNEL_HEAP_MAX_SIZE     (64ULL * 1024 * 1024)   /* 64 MB */

/*
 * User space — pakai TTBR0_EL1 (0x0000... range)
 * Kernel space — pakai TTBR1_EL1 (0xFFFF... range)
 * Berbeda dari x86 yang flat 4 GB
 */
#define USER_SPACE_START         0x0000000000400000ULL
#define USER_SPACE_END           0x0000FFFFFFFFFFFFULL

/* ─────────────────────────────────────────────
   Versi OS
   ───────────────────────────────────────────── */
#define LUNAROS_VERSION_MAJOR  0
#define LUNAROS_VERSION_MINOR  1
#define LUNAROS_VERSION_PATCH  0
#define LUNAROS_VERSION_STR    "0.1.0"
#define LUNAROS_CODENAME       "Crescent"

/* ─────────────────────────────────────────────
   Flag fitur
   ───────────────────────────────────────────── */
#define FEATURE_SECURE_BOOT    0   /* 1=aktif, 0=dev mode */
#define FEATURE_SANDBOX        1
#define FEATURE_ACL            1
#define FEATURE_UART_LOG       1   /* ARM64: UART bukan serial x86 */
#define FEATURE_SMP            0   /* multi-core belum diimplementasi */

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */

/**
 * lunar_init() — entry point inisialisasi kernel ARM64
 *
 * Di ARM64 tidak ada multiboot_info_t dari GRUB.
 * Info RAM diambil dari konstanta RAM_BASE / RAM_SIZE
 * (untuk QEMU) atau device tree (hardware nyata).
 *
 * Dipanggil dari kernel_main() tanpa parameter.
 * Tidak pernah return.
 */
void lunar_init(void) __attribute__((noreturn));

/**
 * lunar_shutdown() — matikan sistem secara bersih
 */
void lunar_shutdown(void) __attribute__((noreturn));

/**
 * init_is_done() — cek apakah init sudah selesai
 */
int init_is_done(void);

#endif /* INIT_H */