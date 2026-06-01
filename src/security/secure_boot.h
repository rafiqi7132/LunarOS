/**
 * secure_boot.h - LunarOS Secure Boot Header
 * Lokasi: src/security/secure_boot.h
 */

#ifndef SECURE_BOOT_H
#define SECURE_BOOT_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Mode Secure Boot
   ───────────────────────────────────────────── */
#define SECURE_BOOT_DISABLED    0   /* dev mode — bypass */
#define SECURE_BOOT_PERMISSIVE  1   /* warning saja, tidak halt */
#define SECURE_BOOT_ENFORCING   2   /* gagal = panic */

/* ─────────────────────────────────────────────
   Ukuran hash SHA-256
   ───────────────────────────────────────────── */
#define SECURE_BOOT_HASH_SIZE   32  /* SHA-256 = 32 byte */

/* ─────────────────────────────────────────────
   Hash kernel yang diharapkan
   Di production, nilai ini di-embed saat build
   dan tidak bisa diubah tanpa recompile
   ───────────────────────────────────────────── */
extern const uint8_t expected_kernel_hash[SECURE_BOOT_HASH_SIZE];

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */

/**
 * secure_boot_verify()
 * Verifikasi integritas kernel image saat boot.
 * Dipanggil dari init.c di awal init_security().
 *
 * @return  0 = OK, -1 = hash tidak cocok
 */
int secure_boot_verify(void);

/**
 * secure_boot_verify_kernel()
 * Verifikasi image kernel secara eksplisit.
 * Diambil dari secure_boot.c yang sudah ada.
 *
 * @param kernel_image   pointer ke kernel image di memori
 * @param size           ukuran image dalam byte
 * @param expected_hash  hash SHA-256 yang diharapkan (32 byte)
 * @return  1 = cocok (valid), 0 = tidak cocok (invalid)
 */
int secure_boot_verify_kernel(uint8_t  *kernel_image,
                               uint32_t  size,
                               uint8_t  *expected_hash);

/**
 * secure_boot_get_mode()
 * @return  mode saat ini (DISABLED / PERMISSIVE / ENFORCING)
 */
int secure_boot_get_mode(void);

/**
 * secure_boot_set_mode()
 * Ubah mode — hanya bisa dilakukan sebelum init selesai
 */
void secure_boot_set_mode(int mode);

#endif /* SECURE_BOOT_H */