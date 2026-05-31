/**
 * sandbox.h - LunarOS App Sandbox Header
 * Dibuat dari sandbox.c yang sudah ada
 * Lokasi: src/security/sandbox.h
 */

#ifndef SANDBOX_H
#define SANDBOX_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Struct sandbox_t
   Diambil langsung dari sandbox.c kamu,
   diupgrade ke ARM64 (uint64_t untuk alamat)
   ───────────────────────────────────────────── */
#define SANDBOX_MAX_PATHS   8
#define SANDBOX_PATH_LEN    128

typedef struct {
    uint32_t pid;
    uint64_t mem_base;               /* base address yang diizinkan (ARM64: 64-bit) */
    uint64_t mem_limit;              /* batas atas memori (ARM64: 64-bit) */
    char     allowed_paths[SANDBOX_MAX_PATHS][SANDBOX_PATH_LEN];
    int      path_count;             /* berapa path yang sudah didaftarkan */
    int      can_network;            /* boleh akses network? */
    int      can_ipc;                /* boleh IPC antar proses? */
    int      active;                 /* sandbox aktif atau tidak */
} sandbox_t;

/* ─────────────────────────────────────────────
   Fungsi publik
   Deklarasi dari sandbox.c kamu +
   tambahan yang dibutuhkan process.h
   ───────────────────────────────────────────── */
int  sandbox_init(void);

int  sandbox_create(uint32_t pid,
                    uint64_t mem_base,
                    uint64_t mem_limit);

int  sandbox_destroy(uint32_t pid);

int  sandbox_allow_path(uint32_t    pid,
                        const char *path);

int  sandbox_check_mem_access(uint32_t pid,
                               uint64_t addr);

int  sandbox_check_path(uint32_t    pid,
                        const char *path);

int  sandbox_set_network(uint32_t pid, int allowed);
int  sandbox_set_ipc(uint32_t pid,     int allowed);

sandbox_t *sandbox_get(uint32_t pid);

#endif /* SANDBOX_H */