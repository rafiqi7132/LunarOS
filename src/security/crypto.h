/**
 * crypto.h - LunarOS Cryptography Header
 *
 * Lokasi: src/security/crypto.h
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Konstanta ukuran
   ───────────────────────────────────────────── */
#define SHA256_DIGEST_SIZE   32    /* 256 bit = 32 byte */
#define SHA256_BLOCK_SIZE    64    /* 512 bit = 64 byte */

#define AES_BLOCK_SIZE       16    /* 128 bit = 16 byte */
#define AES128_KEY_SIZE      16    /* AES-128: key 16 byte */
#define AES128_ROUND_KEYS    176   /* 11 round keys x 16 byte */

#define PBKDF_ITERATIONS     10000 /* iterasi default untuk password hash */
#define SALT_SIZE            16    /* ukuran salt minimal */

/* ─────────────────────────────────────────────
   SHA-256 Context
   ───────────────────────────────────────────── */
typedef struct {
    uint8_t  data[64];      /* buffer input yang belum diproses */
    uint32_t datalen;       /* jumlah byte di buffer */
    uint64_t bitlen;        /* total panjang pesan dalam bit */
    uint32_t state[8];      /* 8 working variables (H0..H7) */
} sha256_ctx_t;

/* ─────────────────────────────────────────────
   SHA-256 — Fungsi publik
   ───────────────────────────────────────────── */

/** Inisialisasi context SHA-256 baru */
void sha256_init(sha256_ctx_t *ctx);

/** Update: tambah data ke context (bisa dipanggil berkali-kali) */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);

/** Final: hasilkan digest 32 byte dan nol-kan context */
void sha256_final(sha256_ctx_t *ctx, uint8_t *digest);

/**
 * sha256() — one-shot hashing (paling sering dipakai)
 *
 * Contoh:
 *   uint8_t hash[SHA256_DIGEST_SIZE];
 *   sha256((uint8_t*)"hello", 5, hash);
 */
void sha256(const uint8_t *data, uint32_t len, uint8_t *digest);

/**
 * hmac_sha256() — HMAC dengan SHA-256
 * Untuk verifikasi pesan dengan secret key (bukan password hashing)
 *
 * @param key       Secret key
 * @param key_len   Panjang key
 * @param data      Data yang di-authenticate
 * @param data_len  Panjang data
 * @param digest    Output HMAC (32 byte)
 */
void hmac_sha256(const uint8_t *key,  uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t *digest);

/* ─────────────────────────────────────────────
   AES-128 — Fungsi publik
   ───────────────────────────────────────────── */

/**
 * aes128_encrypt_block() — enkripsi satu blok 16 byte
 *
 * @param plaintext   Input 16 byte
 * @param key         Kunci 16 byte
 * @param ciphertext  Output 16 byte
 */
void aes128_encrypt_block(const uint8_t *plaintext,
                           const uint8_t *key,
                           uint8_t *ciphertext);

/** aes128_decrypt_block() — dekripsi satu blok 16 byte */
void aes128_decrypt_block(const uint8_t *ciphertext,
                           const uint8_t *key,
                           uint8_t *plaintext);

/**
 * aes128_cbc_encrypt() — enkripsi banyak blok (mode CBC)
 *
 * @param plaintext   Data asli (panjang harus kelipatan 16)
 * @param len         Panjang data
 * @param key         Kunci 16 byte
 * @param iv          Initialization vector 16 byte (harus unik tiap sesi)
 * @param ciphertext  Output (sama panjang dengan input)
 * @return  0 = sukses, -1 = len bukan kelipatan 16
 */
int aes128_cbc_encrypt(const uint8_t *plaintext,  uint32_t len,
                        const uint8_t *key,
                        const uint8_t *iv,
                        uint8_t *ciphertext);

/** aes128_cbc_decrypt() — dekripsi mode CBC */
int aes128_cbc_decrypt(const uint8_t *ciphertext, uint32_t len,
                        const uint8_t *key,
                        const uint8_t *iv,
                        uint8_t *plaintext);

/* ─────────────────────────────────────────────
   Password Hashing
   ───────────────────────────────────────────── */

/**
 * crypto_hash_password() — hash password dengan salt + key stretching
 *
 * JANGAN pakai sha256() langsung untuk password!
 * Gunakan fungsi ini karena ada salt dan iterasi.
 *
 * Contoh:
 *   uint8_t salt[SALT_SIZE] = { ... };  // random
 *   uint8_t hash[SHA256_DIGEST_SIZE];
 *   crypto_hash_password(
 *       (uint8_t*)"mypassword", 10,
 *       salt, SALT_SIZE,
 *       PBKDF_ITERATIONS,
 *       hash
 *   );
 */
void crypto_hash_password(const uint8_t *password, uint32_t pass_len,
                           const uint8_t *salt,     uint32_t salt_len,
                           uint32_t iterations,
                           uint8_t *out);

/**
 * crypto_verify_password() — verifikasi password (constant-time)
 *
 * @return  1 = password cocok, 0 = tidak cocok
 */
int crypto_verify_password(const uint8_t *password, uint32_t pass_len,
                             const uint8_t *salt,     uint32_t salt_len,
                             uint32_t iterations,
                             const uint8_t *stored_hash);

/* ─────────────────────────────────────────────
   Utility
   ───────────────────────────────────────────── */

/**
 * crypto_memcmp_ct() — bandingkan memori constant-time
 *
 * SELALU pakai ini untuk bandingkan hash/password/token!
 * Jangan pakai memcmp() biasa — rentan timing attack.
 *
 * @return  0 = sama, 1 = berbeda
 */
int crypto_memcmp_ct(const uint8_t *a, const uint8_t *b, uint32_t len);

/**
 * crypto_zero() — hapus data sensitif dari memori
 *
 * Gunakan ini setelah selesai pakai key/password/hash
 * supaya tidak bisa dibaca dari memory dump.
 */
void crypto_zero(void *buf, uint32_t len);

/**
 * crypto_bytes_to_hex() — ubah bytes ke hex string
 *
 * @param bytes    Input bytes
 * @param len      Jumlah byte
 * @param hex_out  Output string (butuh len*2 + 1 byte)
 */
void crypto_bytes_to_hex(const uint8_t *bytes, uint32_t len, char *hex_out);

#endif /* CRYPTO_H */
