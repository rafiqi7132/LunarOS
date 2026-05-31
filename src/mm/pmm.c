/**
 * pmm.c - LunarOS Physical Memory Manager
 *
 * Mengelola frame fisik RAM menggunakan bitmap.
 * Setiap bit = satu frame (4 KB).
 * Bit 0 = bebas, Bit 1 = terpakai.
 *
 * Lokasi: src/mm/pmm.c
 */

#include "pmm.h"
#include "../kernel/panic.h"
#include "../drivers/pl011.h"

/* ─────────────────────────────────────────────
   Konstanta
   ───────────────────────────────────────────── */
#define FRAME_SIZE       4096           /* 4 KB per frame */
#define BITS_PER_ENTRY   64             /* uint64_t = 64 bit */

/* ─────────────────────────────────────────────
   State PMM
   ───────────────────────────────────────────── */
static uint64_t *bitmap      = NULL;    /* array bitmap */
static uint64_t  total_frames = 0;      /* total frame di RAM */
static uint64_t  free_frames  = 0;      /* frame yang masih bebas */
static uint64_t  bitmap_size  = 0;      /* ukuran bitmap dalam uint64_t */

/* Simbol dari linker.ld */
extern char __kernel_start[];
extern char __kernel_end[];

/* ─────────────────────────────────────────────
   Bitmap helpers
   ───────────────────────────────────────────── */
static inline void bitmap_set(uint64_t frame) {
    bitmap[frame / BITS_PER_ENTRY] |= (1ULL << (frame % BITS_PER_ENTRY));
}

static inline void bitmap_clear(uint64_t frame) {
    bitmap[frame / BITS_PER_ENTRY] &= ~(1ULL << (frame % BITS_PER_ENTRY));
}

static inline int bitmap_test(uint64_t frame) {
    return (bitmap[frame / BITS_PER_ENTRY] >> (frame % BITS_PER_ENTRY)) & 1;
}

/* ─────────────────────────────────────────────
   pmm_init()
   Dipanggil dari init.c dengan info RAM dari
   device tree atau hardcoded untuk QEMU
   ───────────────────────────────────────────── */
int pmm_init(uint64_t ram_base, uint64_t ram_size) {
    total_frames = ram_size / FRAME_SIZE;
    bitmap_size  = (total_frames + BITS_PER_ENTRY - 1) / BITS_PER_ENTRY;

    /* Taruh bitmap tepat setelah akhir kernel */
    bitmap = (uint64_t *)((uint64_t)__kernel_end);

    /* Align ke 8 byte */
    uint64_t bmap_addr = (uint64_t)bitmap;
    if (bmap_addr % 8 != 0) {
        bmap_addr = (bmap_addr + 7) & ~7ULL;
        bitmap = (uint64_t *)bmap_addr;
    }

    /* Awalnya tandai SEMUA frame sebagai terpakai */
    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFFFFFFFFFFFFFFFFULL;
    }
    free_frames = 0;

    /* Bebaskan region RAM yang tersedia
     * Untuk QEMU virt: RAM mulai 0x40000000
     * Kita bebaskan semua kecuali:
     *   - 0x00000000 – 0x3FFFFFFF (device MMIO)
     *   - kernel image
     *   - bitmap itu sendiri
     */
    uint64_t ram_end      = ram_base + ram_size;
    uint64_t kernel_start = (uint64_t)__kernel_start;
    uint64_t kernel_end   = (uint64_t)__kernel_end;
    uint64_t bitmap_end   = (uint64_t)bitmap + (bitmap_size * 8);

    /* Align semua ke frame boundary */
    uint64_t free_start = (bitmap_end + FRAME_SIZE - 1) & ~(uint64_t)(FRAME_SIZE - 1);
    uint64_t free_end   = ram_end & ~(uint64_t)(FRAME_SIZE - 1);

    /* Bebaskan frame yang aman */
    for (uint64_t addr = free_start; addr < free_end; addr += FRAME_SIZE) {
        uint64_t frame = (addr - ram_base) / FRAME_SIZE;
        if (frame < total_frames) {
            bitmap_clear(frame);
            free_frames++;
        }
    }

    pl011_puts("[PMM] Initialized\n");
    pl011_puts("[PMM] Total frames : ");
    pl011_print_uint(total_frames);
    pl011_puts("\n[PMM] Free frames  : ");
    pl011_print_uint(free_frames);
    pl011_puts("\n[PMM] Free memory  : ");
    pl011_print_uint(free_frames * FRAME_SIZE / 1024 / 1024);
    pl011_puts(" MB\n");

    return 0;
}

/* ─────────────────────────────────────────────
   pmm_alloc_frame()
   Alokasi satu frame fisik (4 KB)
   Return: alamat fisik frame, atau 0 jika habis
   ───────────────────────────────────────────── */
uint64_t pmm_alloc_frame(void) {
    if (free_frames == 0) return 0;

    /* Cari entry bitmap yang tidak penuh */
    for (uint64_t i = 0; i < bitmap_size; i++) {
        if (bitmap[i] == 0xFFFFFFFFFFFFFFFFULL) continue;

        /* Ada bit yang 0 (frame bebas) di entry ini */
        /* Cari bit pertama yang 0 dengan bit scan */
        uint64_t val = bitmap[i];
        uint64_t bit = 0;

        /* Brian Kernighan — cari trailing one */
        uint64_t inv = ~val;
        /* cari posisi bit pertama yang set di inv */
        for (bit = 0; bit < BITS_PER_ENTRY; bit++) {
            if (inv & (1ULL << bit)) break;
        }

        uint64_t frame = i * BITS_PER_ENTRY + bit;
        if (frame >= total_frames) return 0;

        bitmap_set(frame);
        free_frames--;

        /* Kembalikan alamat fisik */
        return frame * FRAME_SIZE;
    }

    return 0;   /* RAM habis */
}

/* ─────────────────────────────────────────────
   pmm_alloc_frames()
   Alokasi N frame yang BERURUTAN (contiguous)
   Dibutuhkan untuk DMA dan page table
   ───────────────────────────────────────────── */
uint64_t pmm_alloc_frames(uint64_t count) {
    if (count == 0 || free_frames < count) return 0;

    uint64_t consecutive = 0;
    uint64_t start_frame = 0;

    for (uint64_t frame = 0; frame < total_frames; frame++) {
        if (!bitmap_test(frame)) {
            if (consecutive == 0) start_frame = frame;
            consecutive++;
            if (consecutive == count) {
                /* Tandai semua sebagai terpakai */
                for (uint64_t i = start_frame; i < start_frame + count; i++) {
                    bitmap_set(i);
                }
                free_frames -= count;
                return start_frame * FRAME_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }

    return 0;   /* tidak ada blok berurutan yang cukup */
}

/* ─────────────────────────────────────────────
   pmm_free_frame()
   Bebaskan satu frame fisik
   ───────────────────────────────────────────── */
void pmm_free_frame(uint64_t phys_addr) {
    if (phys_addr == 0) return;
    if (phys_addr % FRAME_SIZE != 0) {
        PANIC("pmm_free_frame: alamat tidak aligned ke 4KB");
    }

    uint64_t frame = phys_addr / FRAME_SIZE;
    if (frame >= total_frames) {
        PANIC("pmm_free_frame: alamat di luar batas RAM");
    }
    if (!bitmap_test(frame)) {
        PANIC("pmm_free_frame: double free terdeteksi!");
    }

    bitmap_clear(frame);
    free_frames++;
}

/* ─────────────────────────────────────────────
   pmm_free_frames()
   Bebaskan N frame berurutan
   ───────────────────────────────────────────── */
void pmm_free_frames(uint64_t phys_addr, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        pmm_free_frame(phys_addr + i * FRAME_SIZE);
    }
}

/* ─────────────────────────────────────────────
   Info
   ───────────────────────────────────────────── */
uint64_t pmm_get_total_memory(void) { return total_frames * FRAME_SIZE; }
uint64_t pmm_get_free_memory(void)  { return free_frames  * FRAME_SIZE; }
uint64_t pmm_get_used_memory(void)  {
    return (total_frames - free_frames) * FRAME_SIZE;
}