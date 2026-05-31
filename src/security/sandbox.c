/**
 * sandbox.c - LunarOS App Sandbox (ARM64)
 *
 * Setiap proses punya sandbox sendiri — tidak bisa
 * akses memori atau file di luar batasnya.
 * Terinspirasi dari iOS App Sandbox.
 *
 * Struct sandbox_t dan deklarasi fungsi diambil
 * dari versi awal, diimplementasikan di sini.
 *
 * Lokasi: src/security/sandbox.c
 */

#include "sandbox.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"

/* ─────────────────────────────────────────────
   Tabel sandbox — satu per proses
   ───────────────────────────────────────────── */
#define MAX_SANDBOXES   64

static sandbox_t sandbox_table[MAX_SANDBOXES];
static int       sandbox_initialized = 0;

/* ─────────────────────────────────────────────
   Helper: strncmp sederhana (tanpa libc)
   ───────────────────────────────────────────── */
static int sb_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static int sb_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static void sb_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ─────────────────────────────────────────────
   sandbox_init()
   ───────────────────────────────────────────── */
int sandbox_init(void) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        sandbox_table[i].pid        = 0;
        sandbox_table[i].mem_base   = 0;
        sandbox_table[i].mem_limit  = 0;
        sandbox_table[i].path_count = 0;
        sandbox_table[i].can_network = 0;
        sandbox_table[i].can_ipc     = 0;
        sandbox_table[i].active      = 0;

        for (int j = 0; j < SANDBOX_MAX_PATHS; j++) {
            sandbox_table[i].allowed_paths[j][0] = '\0';
        }
    }

    sandbox_initialized = 1;
    pl011_puts("[SANDBOX] Initialized\n");
    return 0;
}

/* ─────────────────────────────────────────────
   Helper: cari sandbox berdasarkan PID
   ───────────────────────────────────────────── */
sandbox_t *sandbox_get(uint32_t pid) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (sandbox_table[i].active &&
            sandbox_table[i].pid == pid) {
            return &sandbox_table[i];
        }
    }
    return NULL;
}

static sandbox_t *sandbox_alloc(void) {
    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (!sandbox_table[i].active) {
            return &sandbox_table[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   sandbox_create()
   Buat sandbox baru untuk PID dengan batas memori
   ───────────────────────────────────────────── */
int sandbox_create(uint32_t pid,
                   uint64_t mem_base,
                   uint64_t mem_limit)
{
    if (!sandbox_initialized) return -1;

    /* Cek sudah ada sandbox untuk PID ini */
    if (sandbox_get(pid)) return -2;

    sandbox_t *sb = sandbox_alloc();
    if (!sb) {
        pl011_puts("[SANDBOX] ERROR: tabel penuh\n");
        return -3;
    }

    sb->pid         = pid;
    sb->mem_base    = mem_base;
    sb->mem_limit   = mem_limit;
    sb->path_count  = 0;
    sb->can_network = 0;
    sb->can_ipc     = 0;
    sb->active      = 1;

    /* Path default yang selalu diizinkan */
    sb_strncpy(sb->allowed_paths[0], "/tmp",  SANDBOX_PATH_LEN);
    sb->path_count = 1;

    pl011_printf("[SANDBOX] Created: pid=%u mem=0x%llx-0x%llx\n",
                 pid, mem_base, mem_limit);
    return 0;
}

/* ─────────────────────────────────────────────
   sandbox_destroy()
   Hapus sandbox saat proses mati
   ───────────────────────────────────────────── */
int sandbox_destroy(uint32_t pid) {
    sandbox_t *sb = sandbox_get(pid);
    if (!sb) return -1;

    sb->pid        = 0;
    sb->mem_base   = 0;
    sb->mem_limit  = 0;
    sb->path_count = 0;
    sb->active     = 0;

    for (int j = 0; j < SANDBOX_MAX_PATHS; j++) {
        sb->allowed_paths[j][0] = '\0';
    }

    pl011_printf("[SANDBOX] Destroyed: pid=%u\n", pid);
    return 0;
}

/* ─────────────────────────────────────────────
   sandbox_allow_path()
   Tambah path yang boleh diakses proses ini
   ───────────────────────────────────────────── */
int sandbox_allow_path(uint32_t pid, const char *path) {
    sandbox_t *sb = sandbox_get(pid);
    if (!sb)   return -1;
    if (!path) return -2;

    if (sb->path_count >= SANDBOX_MAX_PATHS) {
        pl011_puts("[SANDBOX] WARN: path list penuh\n");
        return -3;
    }

    sb_strncpy(sb->allowed_paths[sb->path_count],
               path, SANDBOX_PATH_LEN);
    sb->path_count++;

    return 0;
}

/* ─────────────────────────────────────────────
   sandbox_check_mem_access()
   Cek apakah alamat boleh diakses proses
   Return: 1 = boleh, 0 = tidak boleh
   ───────────────────────────────────────────── */
int sandbox_check_mem_access(uint32_t pid, uint64_t addr) {
    sandbox_t *sb = sandbox_get(pid);

    /* Kalau tidak ada sandbox, izinkan (kernel process) */
    if (!sb) return 1;

    if (addr >= sb->mem_base && addr < sb->mem_limit) {
        return 1;   /* dalam batas sandbox */
    }

    pl011_printf("[SANDBOX] VIOLATION: pid=%u addr=0x%llx "
                 "batas=0x%llx-0x%llx\n",
                 pid, addr, sb->mem_base, sb->mem_limit);
    return 0;
}

/* ─────────────────────────────────────────────
   sandbox_check_path()
   Cek apakah path boleh diakses proses
   Return: 1 = boleh, 0 = tidak boleh
   ───────────────────────────────────────────── */
int sandbox_check_path(uint32_t pid, const char *path) {
    sandbox_t *sb = sandbox_get(pid);

    /* Tidak ada sandbox = kernel process = izinkan semua */
    if (!sb) return 1;
    if (!path) return 0;

    int path_len = sb_strlen(path);

    for (int i = 0; i < sb->path_count; i++) {
        const char *allowed = sb->allowed_paths[i];
        int         al_len  = sb_strlen(allowed);

        if (al_len == 0) continue;

        /*
         * Cek prefix match:
         * allowed = "/apps/lunawatch"
         * path    = "/apps/lunawatch/data/save.db" → boleh
         * path    = "/apps/othersapp/hack" → tidak boleh
         */
        if (path_len >= al_len &&
            sb_strncmp(path, allowed, al_len) == 0) {
            /* Pastikan ini benar-benar subdirektori */
            if (path[al_len] == '/' || path[al_len] == '\0') {
                return 1;
            }
        }
    }

    pl011_printf("[SANDBOX] PATH BLOCKED: pid=%u path=%s\n",
                 pid, path);
    return 0;
}

/* ─────────────────────────────────────────────
   sandbox_set_network() / sandbox_set_ipc()
   ───────────────────────────────────────────── */
int sandbox_set_network(uint32_t pid, int allowed) {
    sandbox_t *sb = sandbox_get(pid);
    if (!sb) return -1;
    sb->can_network = allowed ? 1 : 0;
    return 0;
}

int sandbox_set_ipc(uint32_t pid, int allowed) {
    sandbox_t *sb = sandbox_get(pid);
    if (!sb) return -1;
    sb->can_ipc = allowed ? 1 : 0;
    return 0;
}