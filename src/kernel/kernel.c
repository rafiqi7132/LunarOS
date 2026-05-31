/*
 * kernel.c - LunarOS Kernel Entry Point
 * Copyright (C) 2026 Muhammad Rafiqi H.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Lokasi: src/kernel/kernel.c
 *
 * ARM64: dipanggil dari boot.S setelah stack siap
 * dan BSS sudah di-nol-kan.
 */

#include "init.h"
#include "panic.h"
#include "../drivers/pl011.h"
#include "../drivers/framebuffer.h"

/*
 * kernel_main() — entry point pertama dari C
 *
 * Dipanggil oleh boot.S via:
 *     bl kernel_main
 *
 * Tidak menerima parameter apapun dari bootloader.
 * Info RAM dan hardware dibaca langsung dari
 * konstanta di init.h (untuk QEMU) atau device tree
 * (untuk hardware nyata).
 */
void kernel_main(void) {
    /*
     * Inisialisasi UART pertama kali — SEBELUM apapun.
     * Kalau ada yang crash sebelum fb_init(), masih bisa
     * lihat outputnya lewat serial/UART.
     */
    pl011_init();
    pl011_puts("\n[BOOT] LunarOS kernel_main() reached\n");

    /*
     * Serahkan semua ke lunar_init().
     * lunar_init() tidak pernah return — dia akan
     * masuk ke scheduler_start() setelah semua siap.
     */
    lunar_init();

    /* Seharusnya tidak pernah sampai sini */
    PANIC("kernel_main: lunar_init() returned unexpectedly");
}