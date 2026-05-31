/**
 * heap.c - LunarOS Kernel Heap (kmalloc / kfree)
 *
 * Implementasi heap dengan free list + header per blok.
 * Sederhana tapi cukup untuk kernel.
 *
 * Layout satu blok:
 *   [ heap_block_t header | ... data ... ]
 *
 * Lokasi: src/mm/heap.c
 */

#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"

/* ─────────────────────────────────────────────
   Konstanta
   ───────────────────────────────────────────── */
#define HEAP_MAGIC_FREE  0xFEEBDAEDUL   /* magic blok bebas */
#define HEAP_MAGIC_USED  0xC0DEBEEFUL   /* magic blok terpakai */
#define HEAP_MIN_SPLIT   32             /* minimum sisa untuk split blok */
#define HEAP_ALIGN       16             /* alignment semua alokasi (ARM64) */

/* ─────────────────────────────────────────────
   Header tiap blok heap
   ───────────────────────────────────────────── */
typedef struct heap_block {
    uint32_t          magic;    /* HEAP_MAGIC_FREE atau HEAP_MAGIC_USED */
    uint32_t          size;     /* ukuran DATA (tidak termasuk header) */
    struct heap_block *prev;    /* blok sebelumnya */
    struct heap_block *next;    /* blok berikutnya */
} heap_block_t;

#define HEADER_SIZE   sizeof(heap_block_t)

/* ─────────────────────────────────────────────
   State heap
   ───────────────────────────────────────────── */
static heap_block_t *heap_head  = NULL;   /* blok pertama */
static uint64_t      heap_start = 0;
static uint64_t      heap_end   = 0;
static uint64_t      heap_max   = 0;

/* Statistik */
static uint64_t stat_alloc_count = 0;
static uint64_t stat_free_count  = 0;
static uint64_t stat_used_bytes  = 0;

/* ─────────────────────────────────────────────
   Helper: align ke kelipatan HEAP_ALIGN
   ───────────────────────────────────────────── */
static inline uint64_t align_up(uint64_t val) {
    return (val + HEAP_ALIGN - 1) & ~(uint64_t)(HEAP_ALIGN - 1);
}

/* ─────────────────────────────────────────────
   heap_expand()
   Minta frame baru dari PMM jika heap habis
   ───────────────────────────────────────────── */
static int heap_expand(uint64_t min_size) {
    if (heap_end >= heap_max) return -1;

    /* Hitung berapa frame yang dibutuhkan */
    uint64_t needed  = align_up(min_size + HEADER_SIZE);
    uint64_t n_pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < n_pages; i++) {
        if (heap_end + PAGE_SIZE > heap_max) return -1;

        uint64_t phys = pmm_alloc_frame();
        if (phys == 0) return -1;

        /* Map halaman baru ke heap virtual address */
        vmm_map_page(vmm_get_kernel_pgd(),
                     heap_end, phys, VMM_FLAG_KERNEL);
        heap_end += PAGE_SIZE;
    }

    /* Buat blok bebas baru dari halaman yang baru di-map */
    heap_block_t *new_block = (heap_block_t *)(heap_end - n_pages * PAGE_SIZE);
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->size  = n_pages * PAGE_SIZE - HEADER_SIZE;
    new_block->prev  = NULL;
    new_block->next  = NULL;

    /* Cari blok terakhir dan sambungkan */
    if (heap_head == NULL) {
        heap_head = new_block;
    } else {
        heap_block_t *cur = heap_head;
        while (cur->next) cur = cur->next;
        cur->next       = new_block;
        new_block->prev = cur;
    }

    return 0;
}

/* ─────────────────────────────────────────────
   heap_init()
   Dipanggil dari init.c setelah VMM siap
   ───────────────────────────────────────────── */
int heap_init(uint64_t virt_start, uint64_t initial_size, uint64_t max_size) {
    heap_start = virt_start;
    heap_end   = virt_start;
    heap_max   = virt_start + max_size;

    /* Alokasi halaman awal */
    uint64_t n_pages = (initial_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < n_pages; i++) {
        uint64_t phys = pmm_alloc_frame();
        if (phys == 0) PANIC("heap_init: tidak cukup frame fisik");

        vmm_map_page(vmm_get_kernel_pgd(),
                     heap_end, phys, VMM_FLAG_KERNEL);
        heap_end += PAGE_SIZE;
    }

    /* Setup satu blok bebas besar */
    heap_head        = (heap_block_t *)heap_start;
    heap_head->magic = HEAP_MAGIC_FREE;
    heap_head->size  = (heap_end - heap_start) - HEADER_SIZE;
    heap_head->prev  = NULL;
    heap_head->next  = NULL;

    pl011_puts("[HEAP] Initialized: ");
    pl011_print_uint(initial_size / 1024);
    pl011_puts(" KB awal, max ");
    pl011_print_uint(max_size / 1024 / 1024);
    pl011_puts(" MB\n");

    return 0;
}

/* ─────────────────────────────────────────────
   kmalloc()
   Alokasi memori di kernel heap
   ───────────────────────────────────────────── */
void *kmalloc(uint64_t size) {
    if (size == 0) return NULL;

    /* Align ukuran */
    size = align_up(size);

    /* Cari blok bebas yang cukup besar (first-fit) */
    heap_block_t *cur = heap_head;
    while (cur) {
        if (cur->magic == HEAP_MAGIC_FREE && cur->size >= size) {
            /* Cocok! Cek apakah bisa di-split */
            if (cur->size >= size + HEADER_SIZE + HEAP_MIN_SPLIT) {
                /* Split blok jadi dua */
                heap_block_t *new_free = (heap_block_t *)
                    ((uint64_t)(cur + 1) + size);

                new_free->magic = HEAP_MAGIC_FREE;
                new_free->size  = cur->size - size - HEADER_SIZE;
                new_free->prev  = cur;
                new_free->next  = cur->next;

                if (cur->next) cur->next->prev = new_free;
                cur->next = new_free;
                cur->size = size;
            }

            cur->magic = HEAP_MAGIC_USED;
            stat_alloc_count++;
            stat_used_bytes += cur->size;

            /* Return pointer ke data (setelah header) */
            return (void *)(cur + 1);
        }
        cur = cur->next;
    }

    /* Tidak ada blok yang cukup — expand heap */
    if (heap_expand(size) == 0) {
        return kmalloc(size);   /* coba lagi setelah expand */
    }

    pl011_puts("[HEAP] kmalloc GAGAL — out of memory!\n");
    return NULL;
}

/* ─────────────────────────────────────────────
   kzalloc()
   Alokasi + nol-kan (seperti calloc)
   ───────────────────────────────────────────── */
void *kzalloc(uint64_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (uint64_t i = 0; i < size; i++) p[i] = 0;
    }
    return ptr;
}

/* ─────────────────────────────────────────────
   kfree()
   Bebaskan memori yang di-kmalloc
   ───────────────────────────────────────────── */
void kfree(void *ptr) {
    if (ptr == NULL) return;

    /* Ambil header blok (tepat sebelum pointer) */
    heap_block_t *block = (heap_block_t *)ptr - 1;

    /* Validasi */
    if (block->magic == HEAP_MAGIC_FREE) {
        PANIC("kfree: double free terdeteksi!");
    }
    if (block->magic != HEAP_MAGIC_USED) {
        PANIC("kfree: pointer tidak valid atau heap corrupt!");
    }

    stat_free_count++;
    stat_used_bytes -= block->size;
    block->magic = HEAP_MAGIC_FREE;

    /* ── Coalesce: gabungkan dengan blok berikutnya jika bebas ── */
    if (block->next && block->next->magic == HEAP_MAGIC_FREE) {
        heap_block_t *next = block->next;
        block->size += HEADER_SIZE + next->size;
        block->next  = next->next;
        if (next->next) next->next->prev = block;
    }

    /* ── Coalesce: gabungkan dengan blok sebelumnya jika bebas ── */
    if (block->prev && block->prev->magic == HEAP_MAGIC_FREE) {
        heap_block_t *prev = block->prev;
        prev->size += HEADER_SIZE + block->size;
        prev->next  = block->next;
        if (block->next) block->next->prev = prev;
    }
}

/* ─────────────────────────────────────────────
   krealloc()
   Ubah ukuran alokasi
   ───────────────────────────────────────────── */
void *krealloc(void *ptr, uint64_t new_size) {
    if (ptr == NULL)      return kmalloc(new_size);
    if (new_size == 0)  { kfree(ptr); return NULL; }

    heap_block_t *block = (heap_block_t *)ptr - 1;
    if (block->magic != HEAP_MAGIC_USED) {
        PANIC("krealloc: pointer tidak valid");
    }

    /* Kalau blok sekarang sudah cukup besar */
    if (block->size >= align_up(new_size)) return ptr;

    /* Alokasi baru, copy, free lama */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    uint64_t copy_size = block->size < new_size ? block->size : new_size;
    for (uint64_t i = 0; i < copy_size; i++) dst[i] = src[i];

    kfree(ptr);
    return new_ptr;
}

/* ─────────────────────────────────────────────
   Info statistik heap
   ───────────────────────────────────────────── */
void heap_get_stats(heap_stats_t *stats) {
    if (!stats) return;
    stats->alloc_count = stat_alloc_count;
    stats->free_count  = stat_free_count;
    stats->used_bytes  = stat_used_bytes;
    stats->total_bytes = heap_end - heap_start;
    stats->free_bytes  = stats->total_bytes - stat_used_bytes;
}

void heap_dump_stats(void) {
    pl011_puts("[HEAP] alloc=");
    pl011_print_uint(stat_alloc_count);
    pl011_puts(" free=");
    pl011_print_uint(stat_free_count);
    pl011_puts(" used=");
    pl011_print_uint(stat_used_bytes / 1024);
    pl011_puts(" KB\n");
}
