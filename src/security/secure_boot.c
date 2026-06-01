/**
 * secure_boot.c - LunarOS Secure Boot Implementation
 *
 * Verifikasi integritas kernel image sebelum dijalankan.
 * Terinspirasi dari Secure Boot di iOS / UEFI.
 *
 * Fungsi secure_boot_verify_kernel() diambil dari
 * versi awal dan dilengkapi dengan implementasi penuh.
 *
 * Lokasi: src/security/secure_boot.c
 */

#include "secure_boot.h"
#include "crypto.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"

/* ─────────────────────────────────────────────
   Hash kernel yang diharapkan
   Di production: di-embed saat build via objcopy
   Di dev mode  : semua nol = bypass
   ───────────────────────────────────────────── */
const uint8_t expected_kernel_hash[SECURE_BOOT_HASH_SIZE] = {
    /* Dev mode — semua nol = selalu bypass */
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    /*
     * Untuk production, ganti dengan hash kernel yang benar.
     * Cara generate:
     *   sha256sum build/lunar.bin
     * Lalu isi nilai hex di sini.
     */
};

/* Mode saat ini */
static int sb_mode = SECURE_BOOT_DISABLED;

/* ─────────────────────────────────────────────
   Helper: cek apakah hash semua nol (dev mode)
   ───────────────────────────────────────────── */
static int hash_is_zero(const uint8_t *hash) {
    for (int i = 0; i < SECURE_BOOT_HASH_SIZE; i++) {
        if (hash[i] != 0) return 0;
    }
    return 1;
}

/* ─────────────────────────────────────────────
   Helper: print hash sebagai hex string
   ───────────────────────────────────────────── */
static void print_hash(const uint8_t *hash) {
    const char h[] = "0123456789abcdef";
    for (int i = 0; i < SECURE_BOOT_HASH_SIZE; i++) {
        pl011_putc(h[hash[i] >> 4]);
        pl011_putc(h[hash[i] & 0xF]);
        if (i == 15) pl011_putc('\n');   /* baris baru di tengah */
    }
    pl011_putc('\n');
}

/* ─────────────────────────────────────────────
   secure_boot_verify_kernel()
   Diambil langsung dari secure_boot.c yang sudah ada,
   diupgrade ke ARM64 (pakai uint64_t untuk size)
   ───────────────────────────────────────────── */
int secure_boot_verify_kernel(uint8_t  *kernel_image,
                               uint32_t  size,
                               uint8_t  *expected_hash)
{
    if (!kernel_image || size == 0 || !expected_hash) return 0;

    uint8_t actual_hash[SECURE_BOOT_HASH_SIZE];

    /* Hash kernel image pakai SHA-256 dari crypto.c */
    sha256(kernel_image, (uint32_t)size, actual_hash);

    /*
     * Bandingkan dengan constant-time comparison
     * dari crypto.c untuk cegah timing attack
     */
    return crypto_memcmp_ct(actual_hash,
                             expected_hash,
                             SECURE_BOOT_HASH_SIZE) == 0 ? 1 : 0;
}

/* ─────────────────────────────────────────────
   secure_boot_verify()
   Dipanggil dari init.c saat boot
   ───────────────────────────────────────────── */
int secure_boot_verify(void) {
    pl011_puts("[SECBOOT] Checking kernel integrity...\n");

    /* Simbol dari linker.ld */
    extern char __kernel_start[];
    extern char __kernel_end[];

    uint8_t  *image = (uint8_t *)__kernel_start;
    uint32_t  size  = (uint32_t)((uint64_t)__kernel_end -
                                  (uint64_t)__kernel_start);

    /* Dev mode — hash semua nol = bypass */
    if (hash_is_zero(expected_kernel_hash)) {
        pl011_puts("[SECBOOT] Dev mode — hash check bypassed\n");
        return -1;   /* -1 = bypass, bukan error fatal */
    }

    /* Hitung hash kernel yang sedang berjalan */
    uint8_t actual_hash[SECURE_BOOT_HASH_SIZE];
    sha256(image, size, actual_hash);

    pl011_puts("[SECBOOT] Kernel size : ");
    pl011_print_uint(size);
    pl011_puts(" bytes\n");

    pl011_puts("[SECBOOT] Expected    : ");
    print_hash(expected_kernel_hash);

    pl011_puts("[SECBOOT] Actual      : ");
    print_hash(actual_hash);

    /* Verifikasi */
    int valid = secure_boot_verify_kernel(image, size,
                                           (uint8_t *)expected_kernel_hash);

    if (valid) {
        pl011_puts("[SECBOOT] ✓ Kernel hash VALID\n");
        return 0;
    }

    /* Hash tidak cocok */
    pl011_puts("[SECBOOT] ✗ Kernel hash INVALID!\n");

    switch (sb_mode) {
    case SECURE_BOOT_DISABLED:
        /* Tidak pernah sampai sini karena hash nol = bypass */
        return -1;

    case SECURE_BOOT_PERMISSIVE:
        /* Warning tapi tetap boot */
        pl011_puts("[SECBOOT] WARN: mode permissive — lanjut boot\n");
        return -1;

    case SECURE_BOOT_ENFORCING:
        /* Hash salah = PANIC, tidak boleh boot */
        PANIC("Secure Boot: kernel image hash mismatch! "
              "Possible tampering detected.");
        break;
    }

    return -1;
}

/* ─────────────────────────────────────────────
   Getter / setter mode
   ───────────────────────────────────────────── */
int secure_boot_get_mode(void) {
    return sb_mode;
}

void secure_boot_set_mode(int mode) {
    if (mode < SECURE_BOOT_DISABLED ||
        mode > SECURE_BOOT_ENFORCING) return;
    sb_mode = mode;

    const char *names[] = { "DISABLED", "PERMISSIVE", "ENFORCING" };
    pl011_printf("[SECBOOT] Mode set to: %s\n", names[mode]);
}