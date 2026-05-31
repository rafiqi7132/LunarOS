/**
 * crypto.c - LunarOS Cryptography Implementation
 *
 * Implementasi kriptografi dari scratch (tanpa libc/openssl).
 * Berisi: SHA-256, AES-128, dan utility fungsi crypto.
 *
 * Lokasi: src/security/crypto.c
 */

#include "crypto.h"
#include "../lib/string.h"

/* ═══════════════════════════════════════════════════════════
   BAGIAN 1 — UTILITY
   ═══════════════════════════════════════════════════════════ */

/* Rotate right 32-bit */
static inline uint32_t rotr32(uint32_t x, uint8_t n) {
    return (x >> n) | (x << (32 - n));
}

/* Byte swap 32-bit (little-endian <-> big-endian) */
static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >>  8) |
           ((x & 0x0000FF00) <<  8) |
           ((x & 0x000000FF) << 24);
}

/* Salin memori dengan constant-time (tidak bocor lewat timing) */
static void ct_memcpy(uint8_t *dst, const uint8_t *src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/* Bandingkan memori constant-time — penting untuk hash comparison!
 * Versi biasa (memcmp) bisa bocor info via timing attack */
int crypto_memcmp_ct(const uint8_t *a, const uint8_t *b, uint32_t len) {
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0 ? 0 : 1;   /* 0 = sama, 1 = berbeda */
}

/* Nol-kan buffer (untuk hapus data sensitif dari memori) */
void crypto_zero(void *buf, uint32_t len) {
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len--) *p++ = 0;
}


/* ═══════════════════════════════════════════════════════════
   BAGIAN 2 — SHA-256
   Referensi: FIPS PUB 180-4
   ═══════════════════════════════════════════════════════════ */

/* Konstanta K — 64 nilai dari akar kubik bilangan prima pertama */
static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Nilai awal H — dari akar kuadrat bilangan prima pertama */
static const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/* Inisialisasi context SHA-256 */
void sha256_init(sha256_ctx_t *ctx) {
    ctx->datalen  = 0;
    ctx->bitlen   = 0;
    for (int i = 0; i < 8; i++) {
        ctx->state[i] = SHA256_H0[i];
    }
}

/* Proses satu blok 512-bit (64 byte) */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *data) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2, m[64];

    /* Buat message schedule W[0..63] */
    for (int i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4]     << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] <<  8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(m[i-15],  7) ^ rotr32(m[i-15], 18) ^ (m[i-15] >>  3);
        uint32_t s1 = rotr32(m[i-2],  17) ^ rotr32(m[i-2],  19) ^ (m[i-2]  >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }

    /* Set working variables dari state saat ini */
    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    /* 64 putaran kompresi */
    for (int i = 0; i < 64; i++) {
        uint32_t S1    = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch    = (e & f) ^ (~e & g);
        t1             = h + S1 + ch + SHA256_K[i] + m[i];

        uint32_t S0    = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        t2             = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    /* Tambahkan ke state */
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

/* Update — proses data input (bisa dipanggil berkali-kali) */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

/* Final — hasilkan digest 32 byte */
void sha256_final(sha256_ctx_t *ctx, uint8_t *digest) {
    uint32_t i = ctx->datalen;

    /* Padding */
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        for (int j = 0; j < 56; j++) ctx->data[j] = 0;
    }

    /* Append panjang pesan dalam bit (big-endian, 64-bit) */
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    /* Ubah state ke byte output (big-endian) */
    for (int j = 0; j < 4; j++) {
        digest[j]      = (ctx->state[0] >> (24 - j * 8)) & 0xFF;
        digest[j +  4] = (ctx->state[1] >> (24 - j * 8)) & 0xFF;
        digest[j +  8] = (ctx->state[2] >> (24 - j * 8)) & 0xFF;
        digest[j + 12] = (ctx->state[3] >> (24 - j * 8)) & 0xFF;
        digest[j + 16] = (ctx->state[4] >> (24 - j * 8)) & 0xFF;
        digest[j + 20] = (ctx->state[5] >> (24 - j * 8)) & 0xFF;
        digest[j + 24] = (ctx->state[6] >> (24 - j * 8)) & 0xFF;
        digest[j + 28] = (ctx->state[7] >> (24 - j * 8)) & 0xFF;
    }

    /* Hapus state dari memori setelah selesai */
    crypto_zero(ctx, sizeof(sha256_ctx_t));
}

/* One-shot SHA-256 — paling sering dipakai */
void sha256(const uint8_t *data, uint32_t len, uint8_t *digest) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* HMAC-SHA256 — untuk verifikasi pesan dengan secret key */
void hmac_sha256(const uint8_t *key,  uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t *digest)
{
    uint8_t k_ipad[64], k_opad[64];
    uint8_t inner[SHA256_DIGEST_SIZE];
    uint8_t actual_key[SHA256_DIGEST_SIZE];

    /* Jika key > 64 byte, hash dulu */
    if (key_len > 64) {
        sha256(key, key_len, actual_key);
        key     = actual_key;
        key_len = SHA256_DIGEST_SIZE;
    }

    /* Siapkan ipad dan opad */
    for (int i = 0; i < 64; i++) {
        uint8_t k = (i < (int)key_len) ? key[i] : 0;
        k_ipad[i] = k ^ 0x36;
        k_opad[i] = k ^ 0x5C;
    }

    /* Inner hash: H(ipad || data) */
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    /* Outer hash: H(opad || inner) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, digest);

    crypto_zero(k_ipad, 64);
    crypto_zero(k_opad, 64);
    crypto_zero(inner, SHA256_DIGEST_SIZE);
}


/* ═══════════════════════════════════════════════════════════
   BAGIAN 3 — AES-128
   Referensi: FIPS PUB 197
   ═══════════════════════════════════════════════════════════ */

/* S-Box AES */
static const uint8_t AES_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* Inverse S-Box (untuk dekripsi) */
static const uint8_t AES_INV_SBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

/* Konstanta Rcon untuk key expansion */
static const uint8_t AES_RCON[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

/* Galois Field multiply — dipakai oleh MixColumns */
static uint8_t gf_mul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    uint8_t hi_bit;
    for (int i = 0; i < 8; i++) {
        if (b & 1) result ^= a;
        hi_bit = a & 0x80;
        a <<= 1;
        if (hi_bit) a ^= 0x1b;   /* irreducible polynomial x^8+x^4+x^3+x+1 */
        b >>= 1;
    }
    return result;
}

/* Key Expansion — expand 16-byte key jadi 176 byte (11 round keys) */
static void aes_key_expansion(const uint8_t *key, uint8_t *round_keys) {
    ct_memcpy(round_keys, key, 16);

    for (int i = 4; i < 44; i++) {
        uint8_t temp[4];
        ct_memcpy(temp, &round_keys[(i-1) * 4], 4);

        if (i % 4 == 0) {
            /* RotWord */
            uint8_t t = temp[0];
            temp[0] = temp[1]; temp[1] = temp[2];
            temp[2] = temp[3]; temp[3] = t;
            /* SubWord */
            for (int j = 0; j < 4; j++) temp[j] = AES_SBOX[temp[j]];
            /* XOR Rcon */
            temp[0] ^= AES_RCON[i / 4];
        }

        for (int j = 0; j < 4; j++) {
            round_keys[i*4 + j] = round_keys[(i-4)*4 + j] ^ temp[j];
        }
    }
}

/* AddRoundKey */
static void aes_add_round_key(uint8_t state[4][4], const uint8_t *round_key) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            state[r][c] ^= round_key[c*4 + r];
}

/* SubBytes */
static void aes_sub_bytes(uint8_t state[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            state[r][c] = AES_SBOX[state[r][c]];
}

/* InvSubBytes */
static void aes_inv_sub_bytes(uint8_t state[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            state[r][c] = AES_INV_SBOX[state[r][c]];
}

/* ShiftRows */
static void aes_shift_rows(uint8_t state[4][4]) {
    uint8_t t;
    /* Row 1: shift 1 */
    t=state[1][0]; state[1][0]=state[1][1]; state[1][1]=state[1][2]; state[1][2]=state[1][3]; state[1][3]=t;
    /* Row 2: shift 2 */
    t=state[2][0]; state[2][0]=state[2][2]; state[2][2]=t;
    t=state[2][1]; state[2][1]=state[2][3]; state[2][3]=t;
    /* Row 3: shift 3 */
    t=state[3][3]; state[3][3]=state[3][2]; state[3][2]=state[3][1]; state[3][1]=state[3][0]; state[3][0]=t;
}

/* InvShiftRows */
static void aes_inv_shift_rows(uint8_t state[4][4]) {
    uint8_t t;
    t=state[1][3]; state[1][3]=state[1][2]; state[1][2]=state[1][1]; state[1][1]=state[1][0]; state[1][0]=t;
    t=state[2][0]; state[2][0]=state[2][2]; state[2][2]=t;
    t=state[2][1]; state[2][1]=state[2][3]; state[2][3]=t;
    t=state[3][0]; state[3][0]=state[3][1]; state[3][1]=state[3][2]; state[3][2]=state[3][3]; state[3][3]=t;
}

/* MixColumns */
static void aes_mix_columns(uint8_t state[4][4]) {
    for (int c = 0; c < 4; c++) {
        uint8_t s0=state[0][c], s1=state[1][c], s2=state[2][c], s3=state[3][c];
        state[0][c] = gf_mul(0x02,s0)^gf_mul(0x03,s1)^s2^s3;
        state[1][c] = s0^gf_mul(0x02,s1)^gf_mul(0x03,s2)^s3;
        state[2][c] = s0^s1^gf_mul(0x02,s2)^gf_mul(0x03,s3);
        state[3][c] = gf_mul(0x03,s0)^s1^s2^gf_mul(0x02,s3);
    }
}

/* InvMixColumns */
static void aes_inv_mix_columns(uint8_t state[4][4]) {
    for (int c = 0; c < 4; c++) {
        uint8_t s0=state[0][c], s1=state[1][c], s2=state[2][c], s3=state[3][c];
        state[0][c] = gf_mul(0x0e,s0)^gf_mul(0x0b,s1)^gf_mul(0x0d,s2)^gf_mul(0x09,s3);
        state[1][c] = gf_mul(0x09,s0)^gf_mul(0x0e,s1)^gf_mul(0x0b,s2)^gf_mul(0x0d,s3);
        state[2][c] = gf_mul(0x0d,s0)^gf_mul(0x09,s1)^gf_mul(0x0e,s2)^gf_mul(0x0b,s3);
        state[3][c] = gf_mul(0x0b,s0)^gf_mul(0x0d,s1)^gf_mul(0x09,s2)^gf_mul(0x0e,s3);
    }
}

/* ─────────────────────────────────────────────
   AES-128 Enkripsi satu blok (16 byte)
   ───────────────────────────────────────────── */
void aes128_encrypt_block(const uint8_t *plaintext,
                           const uint8_t *key,
                           uint8_t *ciphertext)
{
    uint8_t round_keys[176];
    uint8_t state[4][4];

    aes_key_expansion(key, round_keys);

    /* Load plaintext ke state (column-major) */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            state[r][c] = plaintext[r + c*4];

    /* Round 0: AddRoundKey saja */
    aes_add_round_key(state, round_keys);

    /* Round 1–9 */
    for (int round = 1; round < 10; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, &round_keys[round * 16]);
    }

    /* Round 10: tanpa MixColumns */
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, &round_keys[160]);

    /* Salin state ke output */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            ciphertext[r + c*4] = state[r][c];

    crypto_zero(round_keys, 176);
    crypto_zero(state, 16);
}

/* ─────────────────────────────────────────────
   AES-128 Dekripsi satu blok (16 byte)
   ───────────────────────────────────────────── */
void aes128_decrypt_block(const uint8_t *ciphertext,
                           const uint8_t *key,
                           uint8_t *plaintext)
{
    uint8_t round_keys[176];
    uint8_t state[4][4];

    aes_key_expansion(key, round_keys);

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            state[r][c] = ciphertext[r + c*4];

    aes_add_round_key(state, &round_keys[160]);

    for (int round = 9; round >= 1; round--) {
        aes_inv_shift_rows(state);
        aes_inv_sub_bytes(state);
        aes_add_round_key(state, &round_keys[round * 16]);
        aes_inv_mix_columns(state);
    }

    aes_inv_shift_rows(state);
    aes_inv_sub_bytes(state);
    aes_add_round_key(state, round_keys);

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            plaintext[r + c*4] = state[r][c];

    crypto_zero(round_keys, 176);
    crypto_zero(state, 16);
}

/* ─────────────────────────────────────────────
   AES-128-CBC — enkripsi banyak blok
   IV (Initialization Vector) harus 16 byte acak
   ───────────────────────────────────────────── */
int aes128_cbc_encrypt(const uint8_t *plaintext,  uint32_t len,
                        const uint8_t *key,
                        const uint8_t *iv,
                        uint8_t *ciphertext)
{
    if (len % AES_BLOCK_SIZE != 0) return -1;   /* harus kelipatan 16 */

    uint8_t prev[AES_BLOCK_SIZE];
    ct_memcpy(prev, iv, AES_BLOCK_SIZE);

    for (uint32_t i = 0; i < len; i += AES_BLOCK_SIZE) {
        /* XOR plaintext dengan blok sebelumnya (CBC chaining) */
        uint8_t block[AES_BLOCK_SIZE];
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            block[j] = plaintext[i + j] ^ prev[j];
        }
        aes128_encrypt_block(block, key, &ciphertext[i]);
        ct_memcpy(prev, &ciphertext[i], AES_BLOCK_SIZE);
    }

    crypto_zero(prev, AES_BLOCK_SIZE);
    return 0;
}

int aes128_cbc_decrypt(const uint8_t *ciphertext, uint32_t len,
                        const uint8_t *key,
                        const uint8_t *iv,
                        uint8_t *plaintext)
{
    if (len % AES_BLOCK_SIZE != 0) return -1;

    uint8_t prev[AES_BLOCK_SIZE];
    ct_memcpy(prev, iv, AES_BLOCK_SIZE);

    for (uint32_t i = 0; i < len; i += AES_BLOCK_SIZE) {
        uint8_t block[AES_BLOCK_SIZE];
        aes128_decrypt_block(&ciphertext[i], key, block);
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            plaintext[i + j] = block[j] ^ prev[j];
        }
        ct_memcpy(prev, &ciphertext[i], AES_BLOCK_SIZE);
    }

    crypto_zero(prev, AES_BLOCK_SIZE);
    return 0;
}


/* ═══════════════════════════════════════════════════════════
   BAGIAN 4 — Password Hashing (PBKDF2-like sederhana)
   Untuk hash password yang lebih aman dari SHA-256 biasa
   ═══════════════════════════════════════════════════════════ */

/**
 * crypto_hash_password()
 *
 * Gabungkan password + salt lalu hash berulang (key stretching).
 * Lebih lambat dari SHA-256 biasa — itu memang disengaja!
 * Makin lambat = makin susah di-brute-force.
 *
 * @param password     String password user
 * @param pass_len     Panjang password
 * @param salt         Data acak unik per user (minimal 16 byte)
 * @param salt_len     Panjang salt
 * @param iterations   Jumlah iterasi (minimal 1000, rekomendasi 10000)
 * @param out          Output hash (32 byte)
 */
void crypto_hash_password(const uint8_t *password, uint32_t pass_len,
                           const uint8_t *salt,     uint32_t salt_len,
                           uint32_t iterations,
                           uint8_t *out)
{
    /* Tahap 1: hash awal dari password+salt */
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, password, pass_len);
    sha256_update(&ctx, salt, salt_len);
    sha256_final(&ctx, out);

    /* Tahap 2: iterasi berulang (key stretching) */
    for (uint32_t i = 0; i < iterations; i++) {
        sha256(out, SHA256_DIGEST_SIZE, out);
    }
}

/* ─────────────────────────────────────────────
   Verifikasi password (constant-time)
   ───────────────────────────────────────────── */
int crypto_verify_password(const uint8_t *password, uint32_t pass_len,
                             const uint8_t *salt,     uint32_t salt_len,
                             uint32_t iterations,
                             const uint8_t *stored_hash)
{
    uint8_t computed[SHA256_DIGEST_SIZE];
    crypto_hash_password(password, pass_len, salt, salt_len,
                          iterations, computed);

    /* Gunakan constant-time compare — cegah timing attack! */
    int result = crypto_memcmp_ct(computed, stored_hash, SHA256_DIGEST_SIZE);
    crypto_zero(computed, SHA256_DIGEST_SIZE);
    return result == 0 ? 1 : 0;   /* 1 = cocok, 0 = tidak cocok */
}

/* ─────────────────────────────────────────────
   Ubah byte ke hex string (untuk display/log)
   ───────────────────────────────────────────── */
void crypto_bytes_to_hex(const uint8_t *bytes, uint32_t len, char *hex_out) {
    const char h[] = "0123456789abcdef";
    for (uint32_t i = 0; i < len; i++) {
        hex_out[i*2]     = h[bytes[i] >> 4];
        hex_out[i*2 + 1] = h[bytes[i] & 0x0F];
    }
    hex_out[len * 2] = '\0';
}
