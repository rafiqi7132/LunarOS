/**
 * pmm.h - LunarOS Physical Memory Manager Header
 * Lokasi: src/mm/pmm.h
 */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PAGE_SIZE   4096ULL
#define PAGE_ALIGN(addr)  (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

/* QEMU virt RAM layout */
#define RAM_BASE    0x40000000ULL
#define RAM_SIZE    (256ULL * 1024 * 1024)   /* 256 MB */

int      pmm_init(uint64_t ram_base, uint64_t ram_size);

uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_frames(uint64_t count);
void     pmm_free_frame(uint64_t phys_addr);
void     pmm_free_frames(uint64_t phys_addr, uint64_t count);

uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_used_memory(void);

#endif /* PMM_H */