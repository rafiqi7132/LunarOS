/**
 * string.c - LunarOS String Library
 *
 * Implementasi fungsi string dari scratch — tanpa libc.
 * Semua fungsi ini dipakai di seluruh kernel LunarOS.
 *
 * Lokasi: src/lib/string.c
 */

#include "string.h"

/* ═══════════════════════════════════════════════════════════
   MEMORY FUNCTIONS
   ═══════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────
   memcpy — salin n byte dari src ke dst
   src dan dst TIDAK boleh overlap
   ───────────────────────────────────────────── */
void *memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    /* Salin 8 byte sekaligus selagi bisa (lebih cepat) */
    while (n >= 8) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8; s += 8; n -= 8;
    }
    /* Sisa byte satu-satu */
    while (n--) *d++ = *s++;

    return dst;
}

/* ─────────────────────────────────────────────
   memmove — salin n byte, aman untuk overlap
   ───────────────────────────────────────────── */
void *memmove(void *dst, const void *src, uint64_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) return dst;

    if (d < s || d >= s + n) {
        /* Tidak overlap atau dst sebelum src — salin maju */
        while (n >= 8) {
            *(uint64_t *)d = *(const uint64_t *)s;
            d += 8; s += 8; n -= 8;
        }
        while (n--) *d++ = *s++;
    } else {
        /* Overlap dan dst > src — salin mundur */
        d += n; s += n;
        while (n >= 8) {
            d -= 8; s -= 8; n -= 8;
            *(uint64_t *)d = *(const uint64_t *)s;
        }
        while (n--) *--d = *--s;
    }

    return dst;
}

/* ─────────────────────────────────────────────
   memset — isi n byte dengan nilai val
   ───────────────────────────────────────────── */
void *memset(void *dst, int val, uint64_t n) {
    uint8_t  *d   = (uint8_t *)dst;
    uint8_t   v   = (uint8_t)val;

    /* Buat pola 8 byte untuk isi sekaligus */
    uint64_t pattern = (uint64_t)v;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    pattern |= pattern << 32;

    while (n >= 8) {
        *(uint64_t *)d = pattern;
        d += 8; n -= 8;
    }
    while (n--) *d++ = v;

    return dst;
}

/* ─────────────────────────────────────────────
   memcmp — bandingkan n byte
   Return: 0=sama, <0=a<b, >0=a>b
   ───────────────────────────────────────────── */
int memcmp(const void *a, const void *b, uint64_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;

    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

/* ─────────────────────────────────────────────
   memchr — cari byte c dalam n byte pertama
   ───────────────────────────────────────────── */
void *memchr(const void *s, int c, uint64_t n) {
    const uint8_t *p = (const uint8_t *)s;
    uint8_t        v = (uint8_t)c;

    while (n--) {
        if (*p == v) return (void *)p;
        p++;
    }
    return NULL;
}


/* ═══════════════════════════════════════════════════════════
   STRING FUNCTIONS
   ═══════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────
   strlen — hitung panjang string
   ───────────────────────────────────────────── */
uint64_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (uint64_t)(p - s);
}

/* ─────────────────────────────────────────────
   strcpy — salin string (tanpa batas panjang)
   ───────────────────────────────────────────── */
char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

/* ─────────────────────────────────────────────
   strncpy — salin string maksimal n karakter
   Jika src < n, sisa dst diisi '\0'
   ───────────────────────────────────────────── */
char *strncpy(char *dst, const char *src, uint64_t n) {
    char *d = dst;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n-- > 0) *d++ = '\0';
    return dst;
}

/* ─────────────────────────────────────────────
   strcat — sambung src ke belakang dst
   ───────────────────────────────────────────── */
char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;          /* loncat ke akhir dst */
    while ((*d++ = *src++)); /* salin src */
    return dst;
}

/* ─────────────────────────────────────────────
   strncat — sambung maksimal n karakter
   ───────────────────────────────────────────── */
char *strncat(char *dst, const char *src, uint64_t n) {
    char *d = dst;
    while (*d) d++;
    while (n-- > 0 && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

/* ─────────────────────────────────────────────
   strcmp — bandingkan dua string
   ───────────────────────────────────────────── */
int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ─────────────────────────────────────────────
   strncmp — bandingkan maksimal n karakter
   ───────────────────────────────────────────── */
int strncmp(const char *a, const char *b, uint64_t n) {
    while (n-- > 0) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (*a == '\0') return 0;
        a++; b++;
    }
    return 0;
}

/* ─────────────────────────────────────────────
   strchr — cari karakter c pertama kali
   ───────────────────────────────────────────── */
char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

/* ─────────────────────────────────────────────
   strrchr — cari karakter c terakhir
   ───────────────────────────────────────────── */
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

/* ─────────────────────────────────────────────
   strstr — cari substring needle di haystack
   ───────────────────────────────────────────── */
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;

    uint64_t nlen = strlen(needle);

    while (*haystack) {
        if (*haystack == *needle &&
            strncmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   strtok — tokenize string
   ───────────────────────────────────────────── */
char *strtok(char *str, const char *delim) {
    static char *saved = NULL;
    if (str) saved = str;
    if (!saved || !*saved) return NULL;

    /* Skip delimiter di awal */
    while (*saved && strchr(delim, *saved)) saved++;
    if (!*saved) return NULL;

    char *token = saved;

    /* Cari delimiter berikutnya */
    while (*saved && !strchr(delim, *saved)) saved++;
    if (*saved) {
        *saved = '\0';
        saved++;
    }

    return token;
}


/* ═══════════════════════════════════════════════════════════
   KONVERSI
   ═══════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────
   atoi — string ke int
   ───────────────────────────────────────────── */
int atoi(const char *s) {
    return (int)atol(s);
}

/* ─────────────────────────────────────────────
   atol — string ke int64_t
   ───────────────────────────────────────────── */
int64_t atol(const char *s) {
    int64_t result = 0;
    int     sign   = 1;

    /* Skip whitespace */
    while (isspace((unsigned char)*s)) s++;

    /* Tanda */
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    /* Digit */
    while (isdigit((unsigned char)*s)) {
        result = result * 10 + (*s - '0');
        s++;
    }

    return sign * result;
}

/* ─────────────────────────────────────────────
   atoul — string ke uint64_t (support hex 0x)
   ───────────────────────────────────────────── */
uint64_t atoul(const char *s) {
    while (isspace((unsigned char)*s)) s++;

    /* Hex */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        uint64_t result = 0;
        while (1) {
            char c = *s++;
            if      (c >= '0' && c <= '9') result = result*16 + (c-'0');
            else if (c >= 'a' && c <= 'f') result = result*16 + (c-'a'+10);
            else if (c >= 'A' && c <= 'F') result = result*16 + (c-'A'+10);
            else break;
        }
        return result;
    }

    /* Desimal */
    uint64_t result = 0;
    while (isdigit((unsigned char)*s)) {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result;
}

/* ─────────────────────────────────────────────
   itoa — int64_t ke string
   base: 10 = desimal, 16 = hex, 2 = biner
   ───────────────────────────────────────────── */
char *itoa(int64_t val, char *buf, int base) {
    if (base < 2 || base > 36) { buf[0] = '\0'; return buf; }

    const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char  tmp[66];
    int   idx  = 0;
    int   neg  = 0;

    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }

    if (val < 0 && base == 10) { neg = 1; val = -val; }

    uint64_t uval = (uint64_t)val;
    while (uval > 0) {
        tmp[idx++] = digits[uval % base];
        uval /= base;
    }
    if (neg) tmp[idx++] = '-';

    /* Balik */
    int i = 0;
    while (idx > 0) buf[i++] = tmp[--idx];
    buf[i] = '\0';

    return buf;
}

/* ─────────────────────────────────────────────
   utoa — uint64_t ke string
   ───────────────────────────────────────────── */
char *utoa(uint64_t val, char *buf, int base) {
    if (base < 2 || base > 36) { buf[0] = '\0'; return buf; }

    const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char  tmp[66];
    int   idx = 0;

    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }

    while (val > 0) {
        tmp[idx++] = digits[val % base];
        val /= base;
    }

    int i = 0;
    while (idx > 0) buf[i++] = tmp[--idx];
    buf[i] = '\0';

    return buf;
}


/* ═══════════════════════════════════════════════════════════
   CHARACTER CLASSIFICATION
   ═══════════════════════════════════════════════════════════ */

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
int isalnum(int c) { return isdigit(c) || isalpha(c); }
int isspace(int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }