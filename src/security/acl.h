/**
 * acl.h - LunarOS Access Control List Header
 * Lokasi: src/security/acl.h
 */

#ifndef ACL_H
#define ACL_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Konstanta permission — bitmask
   Sama dengan yang dipakai di vfs.h
   ───────────────────────────────────────────── */
#define ACL_READ    0x4   /* 100 */
#define ACL_WRITE   0x2   /* 010 */
#define ACL_EXEC    0x1   /* 001 */
#define ACL_RW      0x6   /* 110 */
#define ACL_RX      0x5   /* 101 */
#define ACL_RWX     0x7   /* 111 */
#define ACL_NONE    0x0   /* 000 */

/* ─────────────────────────────────────────────
   Konstanta tabel
   ───────────────────────────────────────────── */
#define ACL_MAX_ENTRIES     128
#define ACL_PATH_LEN        128

/* ─────────────────────────────────────────────
   Struct acl_entry_t
   Diambil dari acl.c yang sudah ada
   ───────────────────────────────────────────── */
typedef struct {
    char     resource_path[ACL_PATH_LEN];
    uint32_t owner_uid;
    uint8_t  perms_owner;   /* bitmask: READ=4, WRITE=2, EXEC=1 */
    uint8_t  perms_group;
    uint8_t  perms_other;
    int      active;        /* slot ini terpakai? */
} acl_entry_t;

/* ─────────────────────────────────────────────
   Fungsi publik
   Deklarasi dari acl.c + tambahan yang
   dibutuhkan init.c dan syscall.c
   ───────────────────────────────────────────── */

/** Inisialisasi tabel ACL */
int  acl_init(void);

/**
 * acl_check() — cek apakah uid boleh akses path
 *
 * @param path          path resource yang diakses
 * @param uid           user ID yang mengakses
 * @param required_perm permission yang dibutuhkan (ACL_READ/WRITE/EXEC)
 * @return  1 = boleh, 0 = tidak boleh
 */
int  acl_check(const char *path, uint32_t uid, uint8_t required_perm);

/**
 * acl_set() — set permission untuk satu resource
 *
 * @param path      path resource
 * @param uid       owner user ID
 * @param owner_p   permission untuk owner
 * @param group_p   permission untuk group
 * @param other_p   permission untuk other
 * @return  0 = sukses, -1 = gagal
 */
int  acl_set(const char *path, uint32_t uid,
             uint8_t owner_p, uint8_t group_p, uint8_t other_p);

/** Hapus ACL entry untuk path tertentu */
int  acl_remove(const char *path);

/** Dump semua ACL entry (untuk debugging) */
void acl_dump(void);

#endif /* ACL_H */