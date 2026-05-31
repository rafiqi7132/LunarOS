/**
 * syscall.h - LunarOS System Call Header (ARM64)
 *
 * ARM64 syscall: user pakai instruksi SVC #0
 * Konvensi register:
 *   x8  = nomor syscall
 *   x0–x5 = argumen (maksimal 6)
 *   x0  = return value
 *
 * Lokasi: src/process/syscall.h
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "../arch/arm64/gic.h"

/* ─────────────────────────────────────────────
   Nomor syscall LunarOS
   Terinspirasi dari Linux ARM64 syscall numbers
   ───────────────────────────────────────────── */

/* Process */
#define SYS_EXIT            1
#define SYS_FORK            2
#define SYS_GETPID          3
#define SYS_GETPPID         4
#define SYS_SLEEP           5
#define SYS_YIELD           6
#define SYS_EXEC            7
#define SYS_WAIT            8
#define SYS_KILL            9
#define SYS_SETPRIORITY     10

/* Memory */
#define SYS_MMAP            20
#define SYS_MUNMAP          21
#define SYS_BRK             22

/* File System */
#define SYS_OPEN            30
#define SYS_CLOSE           31
#define SYS_READ            32
#define SYS_WRITE           33
#define SYS_SEEK            34
#define SYS_STAT            35
#define SYS_UNLINK          36
#define SYS_MKDIR           37
#define SYS_READDIR         38
#define SYS_GETCWD          39
#define SYS_CHDIR           40

/* I/O */
#define SYS_IOREAD          50
#define SYS_IOWRITE         51

/* Security */
#define SYS_GETUID          60
#define SYS_SETUID          61
#define SYS_LOGIN           62
#define SYS_LOGOUT          63
#define SYS_CHECKPERM       64

/* System info */
#define SYS_UNAME           70
#define SYS_UPTIME          71
#define SYS_MEMINFO         72
#define SYS_SHUTDOWN        73
#define SYS_REBOOT          74

#define SYSCALL_MAX         128

/* ─────────────────────────────────────────────
   Return values
   ───────────────────────────────────────────── */
#define ESUCCESS    0
#define EPERM      -1    /* permission denied */
#define ENOENT     -2    /* no such file */
#define ESRCH      -3    /* no such process */
#define EINVAL     -4    /* invalid argument */
#define ENOMEM     -5    /* out of memory */
#define EACCES     -6    /* access denied */
#define EBADF      -7    /* bad file descriptor */
#define ENOSYS     -38   /* syscall tidak diimplementasi */

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */
int      syscall_init(void);
int64_t  syscall_dispatch(uint64_t no, registers_t *regs);

#endif /* SYSCALL_H */