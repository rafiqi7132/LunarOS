/* ═══════════════════════════════════════════════════════════
   linker.ld - LunarOS ARM64 Linker Script
   
   Memory layout QEMU virt machine:
     0x40000000  → tempat kernel di-load oleh QEMU/U-Boot
     0x40000000  → .text.boot  (entry point boot.S)
     0x40001000  → .text        (kode kernel)
     ...         → .rodata      (string konstan, read-only)
     ...         → .data        (variabel global yang ada nilainya)
     ...         → .bss         (variabel global yang nol)
     ...         → stack        (tumbuh ke bawah)
     0x48000000  → batas atas kernel (128 MB dari base)
   ═══════════════════════════════════════════════════════════ */

/* Arsitektur ARM64 (AArch64, little-endian) */
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

/* Entry point — simbol pertama yang dijalankan CPU */
ENTRY(_start)

/* ─────────────────────────────────────────────
   Definisi memory region
   ─────────────────────────────────────────────
   QEMU -machine virt meletakkan RAM mulai 0x40000000
   Total 256 MB (sesuai -m 256M di Makefile)
*/
MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 256M
}

/* ─────────────────────────────────────────────
   Konstanta alamat penting
   Bisa dipakai dari C dengan: extern uint64_t __kernel_start;
   ─────────────────────────────────────────────*/
KERNEL_BASE = 0x40000000;

SECTIONS
{
    /* Mulai dari base address */
    . = KERNEL_BASE;

    /* ── Simbol awal kernel (untuk pmm.c) ── */
    __kernel_start = .;

    /* ════════════════════════════════════════
       .text.boot — harus PERTAMA
       Berisi _start dari boot.S
       Wajib di offset 0 dari kernel base
       ════════════════════════════════════════ */
    .text.boot : {
        KEEP(*(.text.boot))     /* KEEP: jangan di-strip meski tidak ada referensi */
    } > RAM

    /* ════════════════════════════════════════
       .text — kode kernel
       ════════════════════════════════════════ */
    . = ALIGN(4096);            /* align ke page boundary (4 KB) */
    __text_start = .;

    .text : {
        *(.text)
        *(.text.*)
    } > RAM

    __text_end = .;

    /* ════════════════════════════════════════
       .rodata — data read-only
       String literal, konstanta, lookup table
       ════════════════════════════════════════ */
    . = ALIGN(4096);
    __rodata_start = .;

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > RAM

    __rodata_end = .;

    /* ════════════════════════════════════════
       .data — variabel global yang ada nilainya
       Contoh: int x = 5;
       ════════════════════════════════════════ */
    . = ALIGN(4096);
    __data_start = .;

    .data : {
        *(.data)
        *(.data.*)
    } > RAM

    __data_end = .;

    /* ════════════════════════════════════════
       .bss — variabel global yang diinisialisasi nol
       Contoh: int y; atau static int z = 0;
       Tidak disimpan di binary, hanya dicatat ukurannya.
       boot.S yang nol-kan saat startup (zero_bss loop)
       ════════════════════════════════════════ */
    . = ALIGN(4096);
    __bss_start = .;

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } > RAM

    . = ALIGN(8);
    __bss_end = .;

    /* ════════════════════════════════════════
       Stack kernel
       8 KB, tumbuh ke bawah (stack_top > stack_bottom)
       Align 16 byte — wajib di ARM64!
       ════════════════════════════════════════ */
    . = ALIGN(4096);
    __stack_bottom = .;

    .stack : {
        . = . + 8192;           /* 8 KB stack */
    } > RAM

    . = ALIGN(16);              /* ARM64 wajib 16-byte aligned stack */
    __stack_top = .;

    /* ── Simbol akhir kernel ── */
    __kernel_end = .;

    /* ════════════════════════════════════════
       Heap — mulai setelah kernel, dikelola pmm.c
       Tidak didefinisikan ukurannya di sini,
       pmm.c yang menghitung dari __kernel_end
       sampai batas RAM
       ════════════════════════════════════════ */
    __heap_start = __kernel_end;

    /* ─────────────────────────────────────────
       Buang section yang tidak dibutuhkan
       (debug info, comment, dll)
       ─────────────────────────────────────────*/
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
    }
}

/*
   Ringkasan simbol yang bisa dipakai dari C:

   extern char __kernel_start[];   // awal kernel
   extern char __kernel_end[];     // akhir kernel
   extern char __text_start[];     // awal .text
   extern char __text_end[];       // akhir .text
   extern char __rodata_start[];
   extern char __rodata_end[];
   extern char __data_start[];
   extern char __data_end[];
   extern char __bss_start[];      // dipakai boot.S untuk zero BSS
   extern char __bss_end[];
   extern char __stack_top[];      // dipakai boot.S untuk setup SP
   extern char __heap_start[];     // dipakai pmm_init()

   Contoh di pmm.c:
       extern char __kernel_end[];
       extern char __heap_start[];
       uint64_t heap_base = (uint64_t)__heap_start;
       uint64_t ram_end   = KERNEL_BASE + (256 * 1024 * 1024);
       uint64_t free_mem  = ram_end - heap_base;
*/
