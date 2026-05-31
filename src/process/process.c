/**
 * process.c - LunarOS Process Management (ARM64)
 *
 * Mengelola Process Control Block (PCB):
 *   - Buat / hapus proses
 *   - Alokasi stack + page table per proses
 *   - Context switch via callee-saved registers
 *   - Sleep / wake
 *
 * Lokasi: src/process/process.c
 */

#include "process.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"
#include "../drivers/arm_timer.h"

/* ─────────────────────────────────────────────
   Tabel semua proses
   ───────────────────────────────────────────── */
static process_t  proc_table[MAX_PROCESSES];
static process_t *current_proc = NULL;
static uint32_t   next_pid     = 1;     /* PID 0 = idle */
static uint32_t   proc_count   = 0;

/* ─────────────────────────────────────────────
   process_init()
   ───────────────────────────────────────────── */
int process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid   = INVALID_PID;
        proc_table[i].next  = NULL;
        proc_table[i].prev  = NULL;
    }
    proc_count   = 0;
    current_proc = NULL;
    next_pid     = 1;

    pl011_puts("[PROC] Process table initialized\n");
    return 0;
}

/* ─────────────────────────────────────────────
   Helper: cari slot PCB kosong
   ───────────────────────────────────────────── */
static process_t *alloc_pcb(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            return &proc_table[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   Helper: salin string
   ───────────────────────────────────────────── */
static void proc_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ─────────────────────────────────────────────
   process_create()
   Buat proses baru dengan stack dan page table sendiri
   ───────────────────────────────────────────── */
process_t *process_create(const char *name,
                           void (*entry)(void),
                           proc_priv_t privilege,
                           uint32_t priority)
{
    /* Ambil slot PCB */
    process_t *proc = alloc_pcb();
    if (!proc) {
        pl011_puts("[PROC] ERROR: tidak ada slot PCB kosong\n");
        return NULL;
    }

    /* Identitas */
    proc->pid   = next_pid++;
    proc->ppid  = current_proc ? current_proc->pid : 0;
    proc_strncpy(proc->name, name, PROCESS_NAME_LEN);

    /* State */
    proc->state     = PROC_CREATED;
    proc->privilege = privilege;
    proc->exit_code = 0;

    /* Priority & time slice */
    proc->priority   = priority;
    proc->time_slice = (priority < 10) ? 10 :
                       (priority < 50) ? 5  : 2;
    proc->ticks_used  = 0;
    proc->total_ticks = 0;
    proc->wake_tick   = 0;

    /* ── Alokasi kernel stack ── */
    uint64_t kstack = pmm_alloc_frames(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!kstack) PANIC("process_create: gagal alokasi kernel stack");

    /* Map kernel stack ke page table kernel */
    for (uint64_t off = 0; off < KERNEL_STACK_SIZE; off += PAGE_SIZE) {
        vmm_map_page(vmm_get_kernel_pgd(),
                     kstack + off, kstack + off,
                     VMM_FLAG_KERNEL);
    }
    proc->kernel_stack     = kstack;
    proc->kernel_stack_top = kstack + KERNEL_STACK_SIZE;

    /* ── Buat page table sendiri untuk user process ── */
    if (privilege == PRIV_USER) {
        proc->page_table = vmm_create_pgd();

        /* Alokasi user stack */
        uint64_t ustack_phys = pmm_alloc_frames(USER_STACK_SIZE / PAGE_SIZE);
        if (!ustack_phys) PANIC("process_create: gagal alokasi user stack");

        uint64_t ustack_virt = USER_SPACE_END - USER_STACK_SIZE;
        for (uint64_t off = 0; off < USER_STACK_SIZE; off += PAGE_SIZE) {
            vmm_map_page(proc->page_table,
                         ustack_virt + off,
                         ustack_phys + off,
                         VMM_FLAG_USER_RW);
        }
        proc->stack_top = USER_SPACE_END;
    } else {
        /* Kernel process — pakai page table kernel */
        proc->page_table = vmm_get_kernel_pgd();
        proc->stack_top  = proc->kernel_stack_top;
    }

    /* ── Setup CPU context ARM64 ── */
    /*
     * Saat pertama kali di-schedule, CPU akan:
     *   1. Restore x19–x29, x30, sp dari context
     *   2. Lompat ke x30 (LR) — jadi x30 = entry point
     *
     * SPSR menentukan EL saat kembali (EL0 untuk user, EL1 untuk kernel)
     */
    for (int i = 0; i < sizeof(cpu_context_t) / 8; i++) {
        ((uint64_t*)&proc->context)[i] = 0;
    }
    proc->context.pc   = (uint64_t)entry;
    proc->context.x30  = (uint64_t)entry;       /* LR = entry */
    proc->context.sp   = proc->stack_top - 16;  /* ARM64: SP harus align 16 */

    if (privilege == PRIV_USER) {
        /* SPSR untuk EL0: M[4:0] = 0b00000 = EL0t */
        proc->context.spsr = 0x00000000;
    } else {
        /* SPSR untuk EL1: M[4:0] = 0b00100 = EL1t */
        proc->context.spsr = 0x00000004;
    }

    /* Keamanan */
    proc->uid     = (privilege == PRIV_KERNEL) ? 0 : 1000;
    proc->sandbox = NULL;   /* diisi oleh sandbox_create() nanti */

    proc_count++;

    pl011_printf("[PROC] Created: pid=%u name=%s priv=%s\n",
                 proc->pid, proc->name,
                 (privilege == PRIV_KERNEL) ? "kernel" : "user");

    return proc;
}

/* ─────────────────────────────────────────────
   process_create_kernel() — shortcut untuk kernel thread
   ───────────────────────────────────────────── */
process_t *process_create_kernel(const char *name, void (*entry)(void)) {
    return process_create(name, entry, PRIV_KERNEL, 0);
}

/* ─────────────────────────────────────────────
   process_destroy() — hapus proses dan bebaskan resource
   ───────────────────────────────────────────── */
int process_destroy(uint32_t pid) {
    process_t *proc = process_get(pid);
    if (!proc) return -1;
    if (proc->state == PROC_RUNNING) return -2;  /* tidak bisa hapus yang sedang jalan */

    /* Bebaskan kernel stack */
    pmm_free_frames(proc->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);

    /* Bebaskan page table (hanya user process) */
    if (proc->privilege == PRIV_USER && proc->page_table) {
        vmm_destroy_pgd(proc->page_table);
    }

    /* Bebaskan sandbox */
    if (proc->sandbox) {
        /* sandbox_destroy(proc->sandbox); */
        proc->sandbox = NULL;
    }

    pl011_printf("[PROC] Destroyed: pid=%u name=%s\n",
                 proc->pid, proc->name);

    /* Reset slot */
    proc->state = PROC_UNUSED;
    proc->pid   = INVALID_PID;
    proc->next  = NULL;
    proc->prev  = NULL;
    proc_count--;

    return 0;
}

/* ─────────────────────────────────────────────
   process_exit() — proses keluar sendiri
   ───────────────────────────────────────────── */
int process_exit(int code) {
    if (!current_proc) return -1;

    current_proc->exit_code = code;
    current_proc->state     = PROC_ZOMBIE;

    pl011_printf("[PROC] Exit: pid=%u code=%d\n",
                 current_proc->pid, code);

    /* Yield ke scheduler */
    extern void scheduler_yield(void);
    scheduler_yield();

    /* Tidak pernah sampai sini */
    return 0;
}

/* ─────────────────────────────────────────────
   Getter
   ───────────────────────────────────────────── */
process_t *process_get(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_UNUSED &&
            proc_table[i].pid   == pid) {
            return &proc_table[i];
        }
    }
    return NULL;
}

process_t *process_get_current(void) {
    return current_proc;
}

uint32_t process_get_pid(void) {
    return current_proc ? current_proc->pid : INVALID_PID;
}

void process_set_state(uint32_t pid, proc_state_t state) {
    process_t *p = process_get(pid);
    if (p) p->state = state;
}

/* ─────────────────────────────────────────────
   process_sleep() — tidurkan proses N milidetik
   ───────────────────────────────────────────── */
void process_sleep(uint32_t ms) {
    if (!current_proc) return;

    uint64_t ticks_per_sec = 100;   /* sesuai arm_timer_init(100) */
    uint64_t sleep_ticks   = (ms * ticks_per_sec) / 1000;
    if (sleep_ticks == 0) sleep_ticks = 1;

    current_proc->wake_tick = arm_timer_get_ticks() + sleep_ticks;
    current_proc->state     = PROC_SLEEPING;

    extern void scheduler_yield(void);
    scheduler_yield();
}

/* ─────────────────────────────────────────────
   process_wake() — bangunkan proses yang tidur
   ───────────────────────────────────────────── */
void process_wake(uint32_t pid) {
    process_t *p = process_get(pid);
    if (p && p->state == PROC_SLEEPING) {
        p->state     = PROC_READY;
        p->wake_tick = 0;
    }
}

/* ─────────────────────────────────────────────
   context_switch()
   Wrapper C untuk context_switch_asm()
   ───────────────────────────────────────────── */
void context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx) {
    context_switch_asm(old_ctx, new_ctx);
}