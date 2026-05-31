/**
 * math.h - LunarOS Kernel Math Library Header
 * Lokasi: src/lib/math.h
 */

#ifndef MATH_H
#define MATH_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Macro — tidak butuh fungsi
   ───────────────────────────────────────────── */
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define ABS(x)          ((x) < 0 ? -(x) : (x))
#define CLAMP(x, lo, hi) MIN(MAX((x), (lo)), (hi))

#define KB(n)   ((n) * 1024ULL)
#define MB(n)   ((n) * 1024ULL * 1024ULL)
#define GB(n)   ((n) * 1024ULL * 1024ULL * 1024ULL)

#define IS_POWER_OF_2(n)    ((n) != 0 && ((n) & ((n)-1)) == 0)
#define ALIGN_UP(x, a)      (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))
#define DIV_ROUND_UP(x, d)  (((x) + (d) - 1) / (d))

/* ─────────────────────────────────────────────
   Integer math
   ───────────────────────────────────────────── */
int64_t  kabs  (int64_t x);
uint64_t kpow  (uint64_t base, uint32_t exp);
uint64_t ksqrt (uint64_t n);
uint64_t kgcd  (uint64_t a, uint64_t b);
uint64_t klcm  (uint64_t a, uint64_t b);

/* ─────────────────────────────────────────────
   Bit manipulation
   ───────────────────────────────────────────── */
uint32_t popcount32 (uint32_t x);
uint32_t popcount64 (uint64_t x);
uint32_t clz32      (uint32_t x);   /* count leading zeros */
uint32_t clz64      (uint64_t x);
uint32_t ctz32      (uint32_t x);   /* count trailing zeros */
uint32_t ctz64      (uint64_t x);
uint32_t next_pow2  (uint32_t x);   /* next power of 2 >= x */
uint32_t log2_floor (uint64_t x);   /* floor(log2(x)) */

/* ─────────────────────────────────────────────
   Checksum & hashing (non-crypto, untuk FS/data)
   ───────────────────────────────────────────── */
uint32_t crc32     (const uint8_t *data, uint64_t len);
uint32_t fnv1a_32  (const uint8_t *data, uint64_t len);
uint64_t fnv1a_64  (const uint8_t *data, uint64_t len);

/* ─────────────────────────────────────────────
   Fixed-point (untuk timer/scheduling tanpa float)
   Skala: 1 unit = 1/1000
   ───────────────────────────────────────────── */
typedef int64_t fixed_t;
#define FIXED_SCALE     1000LL
#define INT_TO_FIXED(x) ((x) * FIXED_SCALE)
#define FIXED_TO_INT(x) ((x) / FIXED_SCALE)
#define FIXED_MUL(a,b)  ((a) * (b) / FIXED_SCALE)
#define FIXED_DIV(a,b)  ((a) * FIXED_SCALE / (b))

#endif /* MATH_H */