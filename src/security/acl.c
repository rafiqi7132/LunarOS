/**
 * acl.c - LunarOS Access Control List
 *
 * Mengatur siapa boleh akses resource apa.
 * Struct acl_entry_t dan deklarasi fungsi diambil
 * dari versi awal, diimplementasikan di sini.
 *
 * Lokasi: src/security/acl.c
 */

#include "acl.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"

/* ─────────────────────────────────────────────
   Tabel ACL
   ───────────────────────────────────────────── */
static acl_entry_t acl_table[ACL_MAX_ENTRIES];
static int         acl_initialized = 0;

/* ─────────────────────────────────────────────
   String helpers (tanpa libc)
   ───────────────────────────────────────────── */
static int acl_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void acl_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int acl_strlen(const char *s) {
    int i = 0; while (s[i]) i++; return i;
}

/* ─────────────────────────────────────────────
   acl_init()
   ───────────────────────────────────────────── */
int acl_init(void) {
    for (int i = 0; i < ACL_MAX_ENTRIES; i++) {
        acl_table[i].active          = 0;
        acl_table[i].resource_path[0]= '\0';
        acl_table[i].owner_uid       = 0;
        acl_table[i].perms_owner     = 0;
        acl_table[i].perms_group     = 0;
        acl_table[i].perms_other     = 0;
    }

    /*
     * Set ACL default untuk path-path sistem.
     * root (uid=0) punya akses penuh ke semua,
     * user biasa dibatasi sesuai kebutuhan.
     */

    /* /sys — hanya root */
    acl_set("/sys",  0, ACL_RWX, ACL_NONE, ACL_NONE);

    /* /home — owner RWX, other tidak bisa */
    acl_set("/home", 0, ACL_RWX, ACL_NONE, ACL_NONE);

    /* /tmp — semua bisa RWX */
    acl_set("/tmp",  0, ACL_RWX, ACL_RWX,  ACL_RWX);

    /* /apps — semua bisa baca dan exec, tidak bisa tulis */
    acl_set("/apps", 0, ACL_RWX, ACL_RX,   ACL_RX);

    acl_initialized = 1;
    pl011_puts("[ACL] Initialized with default entries\n");
    return 0;
}

/* ─────────────────────────────────────────────
   Helper: cari entry untuk path
   ───────────────────────────────────────────── */
static acl_entry_t *acl_find(const char *path) {
    for (int i = 0; i < ACL_MAX_ENTRIES; i++) {
        if (acl_table[i].active &&
            acl_strcmp(acl_table[i].resource_path, path) == 0) {
            return &acl_table[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   Helper: cari entry parent path
   Contoh: path="/home/rafiqi/file.txt"
   Cek "/home/rafiqi" dulu, lalu "/home", lalu "/"
   ───────────────────────────────────────────── */
static acl_entry_t *acl_find_best(const char *path) {
    /* Cari exact match dulu */
    acl_entry_t *exact = acl_find(path);
    if (exact) return exact;

    /* Cari parent path terpanjang yang cocok */
    char   parent[ACL_PATH_LEN];
    int    plen = acl_strlen(path);
    acl_entry_t *best = NULL;
    int    best_len   = -1;

    for (int i = 0; i < ACL_MAX_ENTRIES; i++) {
        if (!acl_table[i].active) continue;

        const char *ep  = acl_table[i].resource_path;
        int         elen = acl_strlen(ep);

        if (elen > plen) continue;

        /* Cek apakah ep adalah prefix dari path */
        int match = 1;
        for (int j = 0; j < elen; j++) {
            if (ep[j] != path[j]) { match = 0; break; }
        }

        /* Pastikan batas tepat di '/' atau end of string */
        if (match && (path[elen] == '/' || path[elen] == '\0')) {
            if (elen > best_len) {
                best     = &acl_table[i];
                best_len = elen;
            }
        }
    }

    return best;
}

/* ─────────────────────────────────────────────
   acl_check()
   ───────────────────────────────────────────── */
int acl_check(const char *path, uint32_t uid, uint8_t required_perm) {
    if (!path) return 0;

    /* Root (uid=0) selalu boleh */
    if (uid == 0) return 1;

    acl_entry_t *entry = acl_find_best(path);
    if (!entry) {
        /* Tidak ada ACL entry — default izinkan untuk user biasa */
        return 1;
    }

    uint8_t effective_perm;

    if (uid == entry->owner_uid) {
        effective_perm = entry->perms_owner;
    } else {
        /* Untuk sekarang semua non-owner dianggap "other" */
        effective_perm = entry->perms_other;
    }

    int allowed = (effective_perm & required_perm) == required_perm;

    if (!allowed) {
        pl011_printf("[ACL] DENIED: uid=%u path=%s perm=0x%x\n",
                     uid, path, required_perm);
    }

    return allowed;
}

/* ─────────────────────────────────────────────
   acl_set()
   ───────────────────────────────────────────── */
int acl_set(const char *path, uint32_t uid,
            uint8_t owner_p, uint8_t group_p, uint8_t other_p)
{
    if (!path) return -1;

    /* Update jika sudah ada */
    acl_entry_t *existing = acl_find(path);
    if (existing) {
        existing->owner_uid   = uid;
        existing->perms_owner = owner_p;
        existing->perms_group = group_p;
        existing->perms_other = other_p;
        return 0;
    }

    /* Cari slot kosong */
    for (int i = 0; i < ACL_MAX_ENTRIES; i++) {
        if (!acl_table[i].active) {
            acl_strncpy(acl_table[i].resource_path, path, ACL_PATH_LEN);
            acl_table[i].owner_uid   = uid;
            acl_table[i].perms_owner = owner_p;
            acl_table[i].perms_group = group_p;
            acl_table[i].perms_other = other_p;
            acl_table[i].active      = 1;
            return 0;
        }
    }

    pl011_puts("[ACL] ERROR: tabel penuh\n");
    return -1;
}

/* ─────────────────────────────────────────────
   acl_remove()
   ───────────────────────────────────────────── */
int acl_remove(const char *path) {
    acl_entry_t *entry = acl_find(path);
    if (!entry) return -1;

    entry->active           = 0;
    entry->resource_path[0] = '\0';
    return 0;
}

/* ─────────────────────────────────────────────
   acl_dump() — debug
   ───────────────────────────────────────────── */
void acl_dump(void) {
    pl011_puts("\n[ACL] Table:\n");
    pl011_puts("  PATH                         UID  OWNER GROUP OTHER\n");
    pl011_puts("  ─────────────────────────────────────────────────────\n");

    for (int i = 0; i < ACL_MAX_ENTRIES; i++) {
        if (!acl_table[i].active) continue;

        pl011_printf("  %-28s  %3u    %c%c%c   %c%c%c   %c%c%c\n",
            acl_table[i].resource_path,
            acl_table[i].owner_uid,
            (acl_table[i].perms_owner & ACL_READ)  ? 'r' : '-',
            (acl_table[i].perms_owner & ACL_WRITE) ? 'w' : '-',
            (acl_table[i].perms_owner & ACL_EXEC)  ? 'x' : '-',
            (acl_table[i].perms_group & ACL_READ)  ? 'r' : '-',
            (acl_table[i].perms_group & ACL_WRITE) ? 'w' : '-',
            (acl_table[i].perms_group & ACL_EXEC)  ? 'x' : '-',
            (acl_table[i].perms_other & ACL_READ)  ? 'r' : '-',
            (acl_table[i].perms_other & ACL_WRITE) ? 'w' : '-',
            (acl_table[i].perms_other & ACL_EXEC)  ? 'x' : '-'
        );
    }
    pl011_puts("\n");
}