/**
 * scheduler.c - LunarOS Task Scheduler (ARM64)
 *
 * Algoritma: Round-Robin dengan priority queue sederhana.
 * Timer ARM64 (100 Hz) memanggil scheduler_tick() tiap 10ms.
 *
 * Lokasi: src/process/scheduler.c
 */

#include "scheduler.h"
#include "process.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"
#include "../drivers/arm_timer.h"
#include "../mm/vmm.h"

/* ─────────────────────────────────────────────
   Ready queue — linked list proses siap jalan
   ───────────────────────────────────────────── */
static process_t *ready_head  = NULL;    /* head antrian */
static process_t *ready_tail  = NULL;    /* tail antrian */
static process_t *idle_proc   = NULL;    /* proses idle */
static uint64_t   total_ticks = 0;
static uint32_t   sched_count = 0;       /* jumlah proses di queue */

/* Akses ke current_proc (didefinisikan di process.c) */
extern process_t *current_proc;

/* ─────────────────────────────────────────────
   Idle task — jalan kalau tidak ada proses lain
   ───────────────────────────────────────────── */
static void idle_task(void) {
    while (1) {
        __asm__ volatile("wfi");   /* hemat power */
    }
}

/* ─────────────────────────────────────────────
   scheduler_init()
   ───────────────────────────────────────────── */
int scheduler_init(void) {
    ready_head  = NULL;
    ready_tail  = NULL;
    total_ticks = 0;
    sched_count = 0;

    /* Inisialisasi tabel proses */
    process_init();

    /* Buat idle process — PID 0, prioritas terendah */
    idle_proc = process_create_kernel("idle", idle_task);
    if (!idle_proc) PANIC("scheduler_init: gagal buat idle process");

    idle_proc->pid      = 0;
    idle_proc->priority = 255;

    /* Daftarkan timer callback */
    arm_timer_set_callback(scheduler_tick);

    pl011_puts("[SCHED] Scheduler initialized (Round-Robin + Priority)\n");
    return 0;
}

/* ─────────────────────────────────────────────
   scheduler_add() — tambah proses ke ready queue
   Diurutkan berdasarkan priority (kecil = duluan)
   ───────────────────────────────────────────── */
void scheduler_add(process_t *proc) {
    if (!proc) return;
    proc->state = PROC_READY;
    proc->next  = NULL;
    proc->prev  = NULL;

    if (!ready_head) {
        /* Queue kosong */
        ready_head = proc;
        ready_tail = proc;
    } else if (proc->priority <= ready_head->priority) {
        /* Masuk di depan */
        proc->next        = ready_head;
        ready_head->prev  = proc;
        ready_head        = proc;
    } else {
        /* Cari posisi yang tepat */
        process_t *cur = ready_head;
        while (cur->next && cur->next->priority <= proc->priority) {
            cur = cur->next;
        }
        proc->next = cur->next;
        proc->prev = cur;
        if (cur->next) cur->next->prev = proc;
        else           ready_tail      = proc;
        cur->next = proc;
    }

    sched_count++;
}

/* ─────────────────────────────────────────────
   scheduler_remove() — hapus dari ready queue
   ───────────────────────────────────────────── */
void scheduler_remove(uint32_t pid) {
    process_t *cur = ready_head;
    while (cur) {
        if (cur->pid == pid) {
            if (cur->prev) cur->prev->next = cur->next;
            else           ready_head      = cur->next;
            if (cur->next) cur->next->prev = cur->prev;
            else           ready_tail      = cur->prev;
            cur->next = NULL;
            cur->prev = NULL;
            sched_count--;
            return;
        }
        cur = cur->next;
    }
}

/* ─────────────────────────────────────────────
   pick_next() — pilih proses berikutnya
   ───────────────────────────────────────────── */
static process_t *pick_next(void) {
    uint64_t now = arm_timer_get_ticks();

    /* Bangunkan proses yang sudah waktunya */
    process_t *cur = ready_head;
    while (cur) {
        process_t *next = cur->next;
        if (cur->state == PROC_SLEEPING && cur->wake_tick <= now) {
            cur->state     = PROC_READY;
            cur->wake_tick = 0;
        }
        cur = next;
    }

    /* Ambil yang pertama di queue (prioritas tertinggi) */
    process_t *chosen = ready_head;
    while (chosen && chosen->state != PROC_READY) {
        chosen = chosen->next;
    }

    /* Kalau tidak ada, pakai idle */
    if (!chosen) chosen = idle_proc;

    return chosen;
}

/* ─────────────────────────────────────────────
   do_switch() — lakukan context switch
   ───────────────────────────────────────────── */
static void do_switch(process_t *next) {
    if (!next || next == current_proc) return;

    process_t *prev = current_proc;

    /* Update state */
    if (prev && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
    }
    next->state      = PROC_RUNNING;
    next->ticks_used = 0;
    current_proc     = next;

    /* Ganti page table jika beda proses */
    if (prev && prev->page_table != next->page_table) {
        vmm_switch_pgd(next->page_table);
    }

    /* Context switch */
    if (prev) {
        context_switch(&prev->context, &next->context);
    } else {
        /* Pertama kali — tidak ada prev context */
        extern void context_switch_asm(cpu_context_t*, cpu_context_t*);
        cpu_context_t dummy = {0};
        context_switch_asm(&dummy, &next->context);
    }
}

/* ─────────────────────────────────────────────
   scheduler_tick() — dipanggil tiap timer IRQ (100 Hz)
   ───────────────────────────────────────────── */
void scheduler_tick(void) {
    total_ticks++;

    if (!current_proc) return;

    current_proc->ticks_used++;
    current_proc->total_ticks++;

    /* Cek apakah time slice habis */
    if (current_proc->ticks_used >= current_proc->time_slice) {
        /* Pindahkan ke belakang queue (round-robin) */
        if (current_proc != idle_proc) {
            scheduler_remove(current_proc->pid);
            scheduler_add(current_proc);
        }

        process_t *next = pick_next();
        do_switch(next);
    }
}

/* ─────────────────────────────────────────────
   scheduler_yield() — serahkan CPU sukarela
   ───────────────────────────────────────────── */
void scheduler_yield(void) {
    if (current_proc && current_proc != idle_proc) {
        if (current_proc->state == PROC_RUNNING) {
            current_proc->state = PROC_READY;
        }
        scheduler_remove(current_proc->pid);
        if (current_proc->state == PROC_READY) {
            scheduler_add(current_proc);
        }
    }

    process_t *next = pick_next();
    do_switch(next);
}

/* ─────────────────────────────────────────────
   scheduler_start() — mulai scheduling, tidak pernah return
   ───────────────────────────────────────────── */
void scheduler_start(void) {
    pl011_puts("[SCHED] Scheduler starting...\n");

    if (!ready_head) {
        pl011_puts("[SCHED] Tidak ada proses — jalankan idle\n");
    }

    /* Set idle sebagai current supaya context switch pertama benar */
    current_proc       = idle_proc;
    idle_proc->state   = PROC_RUNNING;

    /* Pilih proses pertama yang nyata */
    process_t *first = pick_next();
    if (first == idle_proc && ready_head) {
        first = ready_head;
    }

    do_switch(first);

    /* Tidak pernah sampai sini */
    PANIC("scheduler_start: do_switch returned");
}

/* ─────────────────────────────────────────────
   Statistik
   ───────────────────────────────────────────── */
uint64_t scheduler_get_total_ticks(void) { return total_ticks; }
uint32_t scheduler_get_proc_count(void)  { return sched_count; }