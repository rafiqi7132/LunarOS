/**
 * string.h - LunarOS String Library Header
 * Lokasi: src/lib/string.h
 */

#ifndef STRING_H
#define STRING_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Memory
   ───────────────────────────────────────────── */
void    *memcpy (void *dst, const void *src, uint64_t n);
void    *memmove(void *dst, const void *src, uint64_t n);
void    *memset (void *dst, int val, uint64_t n);
int      memcmp (const void *a, const void *b, uint64_t n);
void    *memchr (const void *s, int c, uint64_t n);

/* ─────────────────────────────────────────────
   String
   ───────────────────────────────────────────── */
uint64_t strlen  (const char *s);
char    *strcpy  (char *dst, const char *src);
char    *strncpy (char *dst, const char *src, uint64_t n);
char    *strcat  (char *dst, const char *src);
char    *strncat (char *dst, const char *src, uint64_t n);
int      strcmp  (const char *a, const char *b);
int      strncmp (const char *a, const char *b, uint64_t n);
char    *strchr  (const char *s, int c);
char    *strrchr (const char *s, int c);
char    *strstr  (const char *haystack, const char *needle);
char    *strtok  (char *str, const char *delim);

/* ─────────────────────────────────────────────
   Konversi
   ───────────────────────────────────────────── */
int      atoi   (const char *s);
int64_t  atol   (const char *s);
uint64_t atoul  (const char *s);
char    *itoa   (int64_t val, char *buf, int base);
char    *utoa   (uint64_t val, char *buf, int base);

/* ─────────────────────────────────────────────
   Utility
   ───────────────────────────────────────────── */
int      isdigit (int c);
int      isalpha (int c);
int      isalnum (int c);
int      isspace (int c);
int      isupper (int c);
int      islower (int c);
int      toupper (int c);
int      tolower (int c);

#endif /* STRING_H */