/**
 * scheduler.h - LunarOS Scheduler Header (ARM64)
 * Lokasi: src/process/scheduler.h
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "process.h"

/* Algoritma: Round-Robin dengan priority */
int  scheduler_init(void);
void scheduler_start(void) __attribute__((noreturn));
void scheduler_tick(void);      /* dipanggil tiap timer IRQ */
void scheduler_yield(void);     /* proses menyerahkan CPU sukarela */
void scheduler_add(process_t *proc);
void scheduler_remove(uint32_t pid);

/* Statistik */
uint64_t scheduler_get_total_ticks(void);
uint32_t scheduler_get_proc_count(void);

#endif /* SCHEDULER_H */