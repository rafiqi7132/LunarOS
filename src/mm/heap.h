/**
 * heap.h - LunarOS Kernel Heap Header
 * Lokasi: src/mm/heap.h
 */

#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>

/* Virtual address heap kernel (setelah kernel image + bitmap) */
#define HEAP_VIRT_START    0x41000000ULL         /* 1 MB setelah kernel base */
#define HEAP_INITIAL_SIZE  (4  * 1024 * 1024)    /* 4  MB awal */
#define HEAP_MAX_SIZE      (64 * 1024 * 1024)    /* 64 MB maksimum */

typedef struct {
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t total_bytes;
} heap_stats_t;

int   heap_init(uint64_t virt_start, uint64_t initial_size, uint64_t max_size);

void *kmalloc(uint64_t size);
void *kzalloc(uint64_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, uint64_t new_size);

void  heap_get_stats(heap_stats_t *stats);
void  heap_dump_stats(void);

#endif /* HEAP_H */