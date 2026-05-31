/**
 * process.h - LunarOS Process Management Header (ARM64)
 * Lokasi: src/process/process.h
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "../arch/arm64/gic.h"   /* registers_t */
#include "../security/sandbox.h"

/* ─────────────────────────────────────────────
   Konstanta
   ───────────────────────────────────────────── */
#define MAX_PROCESSES       64
#define MAX_THREADS         8       /* thread per proses */
#define PROCESS_NAME_LEN    32
#define KERNEL_STACK_SIZE   8192    /* 8 KB stack per thread */
#define USER_STACK_SIZE     65536   /* 64 KB stack user */
#define INVALID_PID         0xFFFFFFFF

/* ─────────────────────────────────────────────
   State proses
   ───────────────────────────────────────────── */
typedef enum {
    PROC_UNUSED    = 0,   /* slot kosong */
    PROC_CREATED,         /* baru dibuat, belum jalan */
    PROC_RUNNING,         /* sedang jalan di CPU */
    PROC_READY,           /* siap jalan, tunggu giliran */
    PROC_SLEEPING,        /* tidur sampai wake_tick */
    PROC_WAITING,         /* tunggu event (I/O, mutex, dll) */
    PROC_ZOMBIE,          /* sudah selesai, tunggu parent wait() */
    PROC_DEAD             /* sudah di-cleanup */
} proc_state_t;

/* ─────────────────────────────────────────────
   Privilege level
   ───────────────────────────────────────────── */
typedef enum {
    PRIV_KERNEL = 0,    /* EL1 — kernel */
    PRIV_USER   = 1     /* EL0 — user app */
} proc_priv_t;

/* ─────────────────────────────────────────────
   CPU context ARM64
   Disimpan saat context switch
   Hanya callee-saved registers yang perlu disimpan
   (ARM64 ABI: x19–x28, x29=FP, x30=LR, SP)
   ───────────────────────────────────────────── */
typedef struct {
    /* Callee-saved general purpose */
    uint64_t x19, x20, x21, x22;
    uint64_t x23, x24, x25, x26;
    uint64_t x27, x28;
    uint64_t x29;       /* frame pointer */
    uint64_t x30;       /* link register — return address */
    uint64_t sp;        /* stack pointer */
    uint64_t pc;        /* program counter — next instruction */
    uint64_t spsr;      /* saved program status (EL0/EL1) */
} cpu_context_t;

/* ─────────────────────────────────────────────
   Process Control Block (PCB)
   ───────────────────────────────────────────── */
typedef struct process {
    /* Identitas */
    uint32_t        pid;
    uint32_t        ppid;           /* parent PID */
    char            name[PROCESS_NAME_LEN];

    /* State */
    proc_state_t    state;
    proc_priv_t     privilege;
    int             exit_code;

    /* CPU context — disimpan saat di-switch out */
    cpu_context_t   context;

    /* Memory */
    uint64_t        *page_table;    /* PGD milik proses ini */
    uint64_t        heap_start;
    uint64_t        heap_end;
    uint64_t        stack_top;

    /* Stack kernel (untuk syscall / exception) */
    uint64_t        kernel_stack;
    uint64_t        kernel_stack_top;

    /* Scheduling */
    uint32_t        priority;       /* 0=tertinggi, 255=terendah */
    uint32_t        time_slice;     /* jatah tick per giliran */
    uint32_t        ticks_used;     /* tick yang sudah dipakai */
    uint64_t        wake_tick;      /* kapan bangun dari SLEEPING */
    uint64_t        total_ticks;    /* total tick sejak lahir */

    /* Keamanan */
    uint32_t        uid;            /* user ID */
    sandbox_t       *sandbox;       /* sandbox milik proses ini */

    /* Linked list untuk scheduler */
    struct process  *next;
    struct process  *prev;
} process_t;

/* ─────────────────────────────────────────────
   Fungsi publik
   ───────────────────────────────────────────── */
int        process_init(void);

process_t *process_create(const char *name,
                           void (*entry)(void),
                           proc_priv_t privilege,
                           uint32_t priority);

process_t *process_create_kernel(const char *name,
                                  void (*entry)(void));

int        process_destroy(uint32_t pid);
int        process_exit(int code);

process_t *process_get(uint32_t pid);
process_t *process_get_current(void);
uint32_t   process_get_pid(void);

void       process_set_state(uint32_t pid, proc_state_t state);
void       process_sleep(uint32_t ms);
void       process_wake(uint32_t pid);

/* Context switch (dipanggil dari scheduler) */
void       context_switch(cpu_context_t *old_ctx,
                           cpu_context_t *new_ctx);

/* Dipanggil dari assembly */
extern void context_switch_asm(cpu_context_t *old_ctx,
                                cpu_context_t *new_ctx);

#endif /* PROCESS_H */