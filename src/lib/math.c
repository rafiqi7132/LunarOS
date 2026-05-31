/**
 * math.c - LunarOS Kernel Math Library
 *
 * Fungsi matematika tanpa FPU / tanpa libm.
 * ARM64 kernel tidak boleh pakai floating point
 * kecuali sudah save/restore SIMD registers.
 * Semua operasi di sini integer-only.
 *
 * Lokasi: src/lib/math.c
 */

#include "math.h"

/* ═══════════════════════════════════════════════════════════
   INTEGER MATH
   ═══════════════════════════════════════════════════════════ */

int64_t kabs(int64_t x) {
    return x < 0 ? -x : x;
}

/* ─────────────────────────────────────────────
   kpow — base^exp (integer)
   ───────────────────────────────────────────── */
uint64_t kpow(uint64_t base, uint32_t exp) {
    uint64_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

/* ─────────────────────────────────────────────
   ksqrt — integer square root (floor)
   Pakai algoritma Newton-Raphson integer
   ───────────────────────────────────────────── */
uint64_t ksqrt(uint64_t n) {
    if (n == 0) return 0;
    if (n == 1) return 1;

    uint64_t x = n;
    uint64_t y = (x + 1) / 2;

    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* ─────────────────────────────────────────────
   kgcd — Greatest Common Divisor (Euclidean)
   ───────────────────────────────────────────── */
uint64_t kgcd(uint64_t a, uint64_t b) {
    while (b != 0) {
        uint64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/* ─────────────────────────────────────────────
   klcm — Least Common Multiple
   ───────────────────────────────────────────── */
uint64_t klcm(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) return 0;
    return (a / kgcd(a, b)) * b;
}


/* ═══════════════════════════════════════════════════════════
   BIT MANIPULATION
   ═══════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────
   popcount — hitung jumlah bit yang set (=1)
   ARM64 punya instruksi CNT tapi pakai intrinsic
   ───────────────────────────────────────────── */
uint32_t popcount32(uint32_t x) {
    /* Algoritma Hamming weight */
    x = x - ((x >> 1) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2) & 0x33333333U);
    x = (x + (x >> 4)) & 0x0F0F0F0FU;
    return (x * 0x01010101U) >> 24;
}

uint32_t popcount64(uint64_t x) {
    return popcount32((uint32_t)x) + popcount32((uint32_t)(x >> 32));
}

/* ─────────────────────────────────────────────
   clz — count leading zeros
   ARM64: instruksi CLZ
   ───────────────────────────────────────────── */
uint32_t clz32(uint32_t x) {
    if (x == 0) return 32;
    uint32_t n = 0;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n +=  8; x <<=  8; }
    if (x <= 0x0FFFFFFF) { n +=  4; x <<=  4; }
    if (x <= 0x3FFFFFFF) { n +=  2; x <<=  2; }
    if (x <= 0x7FFFFFFF) { n +=  1; }
    return n;
}

uint32_t clz64(uint64_t x) {
    if (x == 0) return 64;
    uint32_t hi = (uint32_t)(x >> 32);
    if (hi) return clz32(hi);
    return 32 + clz32((uint32_t)x);
}

/* ─────────────────────────────────────────────
   ctz — count trailing zeros
   ───────────────────────────────────────────── */
uint32_t ctz32(uint32_t x) {
    if (x == 0) return 32;
    /* x & -x isolasi bit paling kanan */
    return popcount32((x & (uint32_t)(-(int32_t)x)) - 1);
}

uint32_t ctz64(uint64_t x) {
    if (x == 0) return 64;
    uint32_t lo = (uint32_t)x;
    if (lo) return ctz32(lo);
    return 32 + ctz32((uint32_t)(x >> 32));
}

/* ─────────────────────────────────────────────
   next_pow2 — bilangan pangkat 2 terkecil >= x
   Dipakai oleh heap.c untuk sizing buffer
   ───────────────────────────────────────────── */
uint32_t next_pow2(uint32_t x) {
    if (x == 0) return 1;
    if (IS_POWER_OF_2(x)) return x;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

/* ─────────────────────────────────────────────
   log2_floor — floor(log2(x))
   Dipakai untuk menentukan order page allocator
   ───────────────────────────────────────────── */
uint32_t log2_floor(uint64_t x) {
    if (x == 0) return 0;
    return 63 - clz64(x);
}


/* ═══════════════════════════════════════════════════════════
   CHECKSUM & HASHING
   ═══════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────
   CRC32 — untuk verifikasi data filesystem
   Polynomial: 0xEDB88320 (IEEE 802.3)
   ───────────────────────────────────────────── */
static uint32_t crc32_table[256];
static int      crc32_initialized = 0;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

uint32_t crc32(const uint8_t *data, uint64_t len) {
    if (!crc32_initialized) crc32_init_table();

    uint32_t crc = 0xFFFFFFFFU;
    while (len--) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ *data++) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ─────────────────────────────────────────────
   FNV-1a — hash cepat untuk hash table
   Lebih cepat dari CRC32, cocok untuk nama file
   ───────────────────────────────────────────── */
uint32_t fnv1a_32(const uint8_t *data, uint64_t len) {
    uint32_t hash = 0x811C9DC5U;   /* FNV offset basis 32-bit */
    while (len--) {
        hash ^= (uint32_t)*data++;
        hash *= 0x01000193U;       /* FNV prime 32-bit */
    }
    return hash;
}

uint64_t fnv1a_64(const uint8_t *data, uint64_t len) {
    uint64_t hash = 0xCBF29CE484222325ULL;   /* FNV offset basis 64-bit */
    while (len--) {
        hash ^= (uint64_t)*data++;
        hash *= 0x00000100000001B3ULL;        /* FNV prime 64-bit */
    }
    return hash;
}
