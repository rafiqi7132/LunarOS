/**
 * syscall.c - LunarOS System Call Handler (ARM64)
 *
 * Dipanggil dari gic.c::syscall_handler() yang
 * dipanggil dari isr.S saat instruksi SVC #0.
 *
 * Konvensi:
 *   x8      = nomor syscall
 *   x0–x5   = argumen 0–5
 *   x0      = return value (ditulis balik ke regs->x[0])
 *
 * Lokasi: src/process/syscall.c
 */

#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"
#include "../drivers/arm_timer.h"
#include "../mm/heap.h"
#include "../mm/vmm.h"
#include "../fs/vfs.h"
#include "../security/auth.h"
#include "../security/acl.h"

/* ─────────────────────────────────────────────
   Tabel syscall handler
   ───────────────────────────────────────────── */
typedef int64_t (*syscall_fn_t)(registers_t *regs);
static syscall_fn_t syscall_table[SYSCALL_MAX];

/* ─────────────────────────────────────────────
   Helper: ambil argumen dari register
   x0–x5 di dalam regs->x[0]–regs->x[5]
   ───────────────────────────────────────────── */
#define ARG0(regs)  ((regs)->x[0])
#define ARG1(regs)  ((regs)->x[1])
#define ARG2(regs)  ((regs)->x[2])
#define ARG3(regs)  ((regs)->x[3])
#define ARG4(regs)  ((regs)->x[4])
#define ARG5(regs)  ((regs)->x[5])

/* Helper: pointer user-space — validasi dulu */
static int validate_user_ptr(const void *ptr, uint64_t len) {
    uint64_t addr = (uint64_t)ptr;
    if (addr == 0) return 0;
    if (addr < USER_SPACE_START) return 0;
    if (addr + len > USER_SPACE_END) return 0;
    return 1;
}

/* ─────────────────────────────────────────────
   IMPLEMENTASI SYSCALL
   ───────────────────────────────────────────── */

/* ── SYS_EXIT (1) ── */
static int64_t sys_exit(registers_t *regs) {
    int code = (int)ARG0(regs);
    pl011_printf("[SYSCALL] exit(%d) pid=%u\n",
                 code, process_get_pid());
    process_exit(code);
    return 0;   /* tidak pernah sampai sini */
}

/* ── SYS_GETPID (3) ── */
static int64_t sys_getpid(registers_t *regs) {
    (void)regs;
    return (int64_t)process_get_pid();
}

/* ── SYS_GETPPID (4) ── */
static int64_t sys_getppid(registers_t *regs) {
    (void)regs;
    process_t *p = process_get_current();
    return p ? (int64_t)p->ppid : EINVAL;
}

/* ── SYS_SLEEP (5) ── */
static int64_t sys_sleep(registers_t *regs) {
    uint32_t ms = (uint32_t)ARG0(regs);
    process_sleep(ms);
    return ESUCCESS;
}

/* ── SYS_YIELD (6) ── */
static int64_t sys_yield(registers_t *regs) {
    (void)regs;
    scheduler_yield();
    return ESUCCESS;
}

/* ── SYS_KILL (9) ── */
static int64_t sys_kill(registers_t *regs) {
    uint32_t  pid  = (uint32_t)ARG0(regs);
    process_t *cur = process_get_current();

    /* Hanya root (uid=0) atau parent yang bisa kill */
    if (cur && cur->uid != 0 && cur->pid != pid) {
        return EPERM;
    }

    int r = process_destroy(pid);
    return r == 0 ? ESUCCESS : ESRCH;
}

/* ── SYS_SETPRIORITY (10) ── */
static int64_t sys_setpriority(registers_t *regs) {
    uint32_t  pid      = (uint32_t)ARG0(regs);
    uint32_t  priority = (uint32_t)ARG1(regs);
    process_t *cur     = process_get_current();

    /* Hanya root yang bisa ubah priority proses lain */
    if (pid != process_get_pid() && cur && cur->uid != 0) {
        return EPERM;
    }

    process_t *p = process_get(pid);
    if (!p) return ESRCH;

    p->priority = priority > 255 ? 255 : priority;
    return ESUCCESS;
}

/* ── SYS_BRK (22) — perluas heap user ── */
static int64_t sys_brk(registers_t *regs) {
    uint64_t   new_end = ARG0(regs);
    process_t *p       = process_get_current();
    if (!p) return EINVAL;

    if (new_end == 0) return (int64_t)p->heap_end;

    if (new_end < p->heap_start) return EINVAL;
    if (new_end > p->heap_start + (64 * 1024 * 1024)) return ENOMEM;

    /* Map halaman baru jika perlu */
    uint64_t old_end = PAGE_ALIGN(p->heap_end);
    uint64_t target  = PAGE_ALIGN(new_end);

    while (old_end < target) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return ENOMEM;
        vmm_map_page(p->page_table, old_end, phys, VMM_FLAG_USER_RW);
        old_end += PAGE_SIZE;
    }

    p->heap_end = new_end;
    return (int64_t)new_end;
}

/* ── SYS_OPEN (30) ── */
static int64_t sys_open(registers_t *regs) {
    const char *path  = (const char *)ARG0(regs);
    uint32_t    flags = (uint32_t)ARG1(regs);

    if (!validate_user_ptr(path, 1)) return EINVAL;

    /* Cek ACL */
    process_t *p = process_get_current();
    if (p && !acl_check(path, p->uid, ACL_READ)) return EACCES;

    int fd = vfs_open(path, flags);
    return fd < 0 ? ENOENT : (int64_t)fd;
}

/* ── SYS_CLOSE (31) ── */
static int64_t sys_close(registers_t *regs) {
    int fd = (int)ARG0(regs);
    int r  = vfs_close(fd);
    return r == 0 ? ESUCCESS : EBADF;
}

/* ── SYS_READ (32) ── */
static int64_t sys_read(registers_t *regs) {
    int      fd   = (int)ARG0(regs);
    void    *buf  = (void *)ARG1(regs);
    uint64_t size = ARG2(regs);

    if (!validate_user_ptr(buf, size)) return EINVAL;
    if (size == 0) return 0;

    int64_t r = vfs_read(fd, buf, size);
    return r < 0 ? EBADF : r;
}

/* ── SYS_WRITE (33) ── */
static int64_t sys_write(registers_t *regs) {
    int         fd   = (int)ARG0(regs);
    const void *buf  = (const void *)ARG1(regs);
    uint64_t    size = ARG2(regs);

    if (!validate_user_ptr(buf, size)) return EINVAL;
    if (size == 0) return 0;

    /* fd=1 (stdout) → UART */
    if (fd == 1 || fd == 2) {
        const char *s = (const char *)buf;
        for (uint64_t i = 0; i < size; i++) pl011_putc(s[i]);
        return (int64_t)size;
    }

    int64_t r = vfs_write(fd, buf, size);
    return r < 0 ? EBADF : r;
}

/* ── SYS_MKDIR (37) ── */
static int64_t sys_mkdir(registers_t *regs) {
    const char *path = (const char *)ARG0(regs);
    uint32_t    perm = (uint32_t)ARG1(regs);

    if (!validate_user_ptr(path, 1)) return EINVAL;

    process_t *p = process_get_current();
    if (p && !acl_check(path, p->uid, ACL_WRITE)) return EACCES;

    int r = vfs_mkdir(path, perm);
    return r == 0 ? ESUCCESS : EPERM;
}

/* ── SYS_GETUID (60) ── */
static int64_t sys_getuid(registers_t *regs) {
    (void)regs;
    process_t *p = process_get_current();
    return p ? (int64_t)p->uid : EINVAL;
}

/* ── SYS_UPTIME (71) ── */
static int64_t sys_uptime(registers_t *regs) {
    (void)regs;
    return (int64_t)arm_timer_get_uptime_ms();
}

/* ── SYS_MEMINFO (72) ── */
static int64_t sys_meminfo(registers_t *regs) {
    uint64_t *buf = (uint64_t *)ARG0(regs);
    if (!validate_user_ptr(buf, 16)) return EINVAL;

    buf[0] = pmm_get_total_memory();
    buf[1] = pmm_get_free_memory();
    return ESUCCESS;
}

/* ── SYS_SHUTDOWN (73) ── */
static int64_t sys_shutdown(registers_t *regs) {
    (void)regs;
    process_t *p = process_get_current();

    /* Hanya root yang bisa shutdown */
    if (!p || p->uid != 0) return EPERM;

    pl011_puts("[SYSCALL] shutdown requested\n");

    extern void lunar_shutdown(void);
    lunar_shutdown();

    return ESUCCESS;   /* tidak pernah sampai sini */
}

/* ── Syscall tidak diimplementasi ── */
static int64_t sys_enosys(registers_t *regs) {
    pl011_printf("[SYSCALL] unimplemented: no=%llu\n",
                 regs->x[8]);
    return ENOSYS;
}

/* ─────────────────────────────────────────────
   syscall_init() — isi tabel dengan semua handler
   ───────────────────────────────────────────── */
int syscall_init(void) {
    /* Default semua ke ENOSYS */
    for (int i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = sys_enosys;
    }

    /* Daftarkan handler */
    syscall_table[SYS_EXIT]         = sys_exit;
    syscall_table[SYS_GETPID]       = sys_getpid;
    syscall_table[SYS_GETPPID]      = sys_getppid;
    syscall_table[SYS_SLEEP]        = sys_sleep;
    syscall_table[SYS_YIELD]        = sys_yield;
    syscall_table[SYS_KILL]         = sys_kill;
    syscall_table[SYS_SETPRIORITY]  = sys_setpriority;
    syscall_table[SYS_BRK]          = sys_brk;
    syscall_table[SYS_OPEN]         = sys_open;
    syscall_table[SYS_CLOSE]        = sys_close;
    syscall_table[SYS_READ]         = sys_read;
    syscall_table[SYS_WRITE]        = sys_write;
    syscall_table[SYS_MKDIR]        = sys_mkdir;
    syscall_table[SYS_GETUID]       = sys_getuid;
    syscall_table[SYS_UPTIME]       = sys_uptime;
    syscall_table[SYS_MEMINFO]      = sys_meminfo;
    syscall_table[SYS_SHUTDOWN]     = sys_shutdown;

    pl011_printf("[SYSCALL] %d syscalls registered\n",
                 SYSCALL_MAX);
    return 0;
}

/* ─────────────────────────────────────────────
   syscall_dispatch() — dipanggil dari gic.c
   ───────────────────────────────────────────── */
int64_t syscall_dispatch(uint64_t no, registers_t *regs) {
    if (no >= SYSCALL_MAX) {
        pl011_printf("[SYSCALL] nomor invalid: %llu\n", no);
        return ENOSYS;
    }

    int64_t result = syscall_table[no](regs);

    /* Tulis return value ke x0 di struct registers
     * (gic.c akan restore ini ke register x0 user) */
    regs->x[0] = (uint64_t)result;

    return result;
}