/**
 * init.c - LunarOS Kernel Initialization (ARM64)
 *
 * Versi ARM64 — perubahan dari x86:
 *   - Tidak ada multiboot_info_t, gdt_init(), idt_init()
 *   - GDT/IDT diganti gic_init() (Generic Interrupt Controller)
 *   - vga_* diganti fb_console_* (framebuffer)
 *   - serial_* diganti pl011_*   (UART PL011)
 *   - timer_init() pakai arm_timer_init()
 *   - keyboard_init() tidak ada — input via pl011_getc()
 *   - cli/hlt diganti daifset/wfi
 *   - pmm_init() tidak butuh multiboot, pakai RAM_BASE/SIZE
 *   - heap_init() pakai 3 parameter (virt, initial, max)
 *   - lunar_init() tidak ada parameter
 *
 * Lokasi: src/kernel/init.c
 */

#include "init.h"
#include "panic.h"

/* ARM64 arch */
#include "../arch/arm64/gic.h"

/* Memory */
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"

/* Drivers ARM64 */
#include "../drivers/pl011.h"
#include "../drivers/framebuffer.h"
#include "../drivers/arm_timer.h"

/* Security */
#include "../security/auth.h"
#include "../security/acl.h"
#include "../security/sandbox.h"
#include "../security/secure_boot.h"

/* Process */
#include "../process/scheduler.h"
#include "../process/syscall.h"

/* Filesystem */
#include "../fs/vfs.h"

/* ─────────────────────────────────────────────
   Status inisialisasi
   ───────────────────────────────────────────── */
typedef enum {
    INIT_NOT_STARTED = 0,
    INIT_IN_PROGRESS,
    INIT_DONE,
    INIT_FAILED
} init_status_t;

static init_status_t system_status = INIT_NOT_STARTED;

/* ─────────────────────────────────────────────
   Helper log — output ke framebuffer + UART
   ───────────────────────────────────────────── */
static void init_log(const char *step, int success) {
    /* Warna di framebuffer */
    if (success) {
        fb_console_set_color(LUNAR_SUCCESS, LUNAR_BG);
        fb_console_puts("  [ OK ]  ");
    } else {
        fb_console_set_color(LUNAR_ERROR, LUNAR_BG);
        fb_console_puts("  [FAIL]  ");
    }
    fb_console_set_color(LUNAR_FG, LUNAR_BG);
    fb_console_puts(step);
    fb_console_puts("\n");

    /* Mirror ke UART */
    pl011_puts(success ? "  [ OK ]  " : "  [FAIL]  ");
    pl011_puts(step);
    pl011_puts("\n");
}

static void init_section_header(const char *title) {
    fb_console_set_color(LUNAR_ACCENT, LUNAR_BG);
    fb_console_puts("\n  >> ");
    fb_console_puts(title);
    fb_console_puts("\n");
    fb_console_set_color(LUNAR_FG, LUNAR_BG);

    pl011_puts("\n  >> ");
    pl011_puts(title);
    pl011_puts("\n");
}

/* ─────────────────────────────────────────────
   Banner boot
   ───────────────────────────────────────────── */
static void print_boot_banner(void) {
    fb_console_clear();
    fb_console_set_color(LUNAR_ACCENT, LUNAR_BG);

    fb_console_puts("\n");
    fb_console_puts("  ██╗     ██╗   ██╗███╗   ██╗ █████╗ ██████╗  ██████╗ ███████╗\n");
    fb_console_puts("  ██║     ██║   ██║████╗  ██║██╔══██╗██╔══██╗██╔═══██╗██╔════╝\n");
    fb_console_puts("  ██║     ██║   ██║██╔██╗ ██║███████║██████╔╝██║   ██║███████╗\n");
    fb_console_puts("  ██║     ██║   ██║██║╚██╗██║██╔══██║██╔══██╗██║   ██║╚════██║\n");
    fb_console_puts("  ███████╗╚██████╔╝██║ ╚████║██║  ██║██║  ██║╚██████╔╝███████║\n");
    fb_console_puts("  ╚══════╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝\n");

    fb_console_set_color(LUNAR_FG, LUNAR_BG);
    fb_console_puts("\n  LunarOS v");
    fb_console_puts(LUNAROS_VERSION_STR);
    fb_console_puts(" \"");
    fb_console_puts(LUNAROS_CODENAME);
    fb_console_puts("\" — ARM64 — Inspired by iOS\n");
    fb_console_puts("  ─────────────────────────────────────────────\n");

    pl011_puts("\n=== LunarOS v");
    pl011_puts(LUNAROS_VERSION_STR);
    pl011_puts(" ARM64 Boot Start ===\n");
}

/* ─────────────────────────────────────────────
   FASE 1: CPU Architecture — ARM64
   x86 : gdt_init() + idt_init() + sti
   ARM64: gic_init() + VBAR sudah diset di boot.S
   ───────────────────────────────────────────── */
static void init_arch(void) {
    init_section_header("CPU Architecture (ARM64)");

    /*
     * VBAR_EL1 sudah diset di boot.S sebelum kernel_main().
     * Yang perlu kita init di sini hanya GIC.
     */
    int r = gic_init();
    init_log("GIC (Generic Interrupt Controller)", r == 0);
    if (r != 0) PANIC("GIC initialization failed");

    /*
     * Unmask semua interrupt ARM64.
     * ARM64: msr daifclr, #0xF
     * (kebalikan dari daifset yang me-mask)
     * D=debug A=SError I=IRQ F=FIQ
     */
    __asm__ volatile("msr daifclr, #0xF");
    init_log("Interrupts unmasked (DAIF cleared)", 1);
}

/* ─────────────────────────────────────────────
   FASE 2: Early Drivers
   x86 : timer_init (PIT) + keyboard_init (PS/2)
   ARM64: arm_timer_init + pl011 sudah init di kernel_main
   ───────────────────────────────────────────── */
static void init_early_drivers(void) {
    init_section_header("Early Drivers (ARM64)");

    /* ARM Generic Timer — 100 Hz */
    int r = arm_timer_init(100);
    init_log("ARM Generic Timer (100 Hz)", r == 0);

    /*
     * Tidak ada keyboard_init() di ARM64.
     * Input karakter lewat UART PL011 (pl011_getc).
     * pl011 sudah di-init di kernel_main() sebelum
     * lunar_init() dipanggil.
     */
    init_log("UART PL011 console input (ready)", 1);

    /* Framebuffer display */
    r = fb_init();
    init_log("Framebuffer display (800x600)", r == 0);
}

/* ─────────────────────────────────────────────
   FASE 3: Memory Management
   x86 : pmm_init(mboot) — butuh multiboot
   ARM64: pmm_init(RAM_BASE, RAM_SIZE) — hardcoded QEMU
   ───────────────────────────────────────────── */
static void init_memory(void) {
    init_section_header("Memory Management");

    /* PMM */
    int r = pmm_init(RAM_BASE, RAM_SIZE);
    init_log("PMM (Physical Memory Manager)", r == 0);
    if (r != 0) PANIC("PMM initialization failed");

    /* Cetak info RAM */
    uint64_t total_mb = pmm_get_total_memory() / 1024 / 1024;
    uint64_t free_mb  = pmm_get_free_memory()  / 1024 / 1024;
    fb_console_puts("       RAM total: ");
    pl011_printf("       RAM total: %llu MB, free: %llu MB\n",
                 total_mb, free_mb);

    /* VMM — aktifkan MMU ARM64 */
    r = vmm_init();
    init_log("VMM + MMU (ARM64 4-level page table)", r == 0);
    if (r != 0) PANIC("VMM / MMU initialization failed");

    /* Heap */
    r = heap_init(KERNEL_HEAP_VIRT_START,
                  KERNEL_HEAP_INITIAL_SIZE,
                  KERNEL_HEAP_MAX_SIZE);
    init_log("Kernel Heap (kmalloc/kfree)", r == 0);
    if (r != 0) PANIC("Kernel heap initialization failed");
}

/* ─────────────────────────────────────────────
   FASE 4: Keamanan — tidak berubah dari x86
   Semua kode security pure C, portable
   ───────────────────────────────────────────── */
static void init_security(void) {
    init_section_header("Security Subsystem");

    /* Secure Boot */
    int r = secure_boot_verify();
    init_log("Secure Boot verification", r == 0);
    if (r != 0) {
        fb_console_set_color(LUNAR_WARNING, LUNAR_BG);
        fb_console_puts("  [WARN] Secure Boot bypassed (dev mode)\n");
        fb_console_set_color(LUNAR_FG, LUNAR_BG);
        pl011_puts("  [WARN] Secure Boot bypassed (dev mode)\n");
    }

    /* Auth */
    r = auth_init();
    init_log("Authentication system", r == 0);
    if (r != 0) PANIC("Auth init failed");

    auth_add_user("root",  "lunarOS_root", ROLE_ADMIN);
    auth_add_user("guest", "guest",        ROLE_GUEST);
    init_log("Default users created (root, guest)", 1);

    /* ACL */
    r = acl_init();
    init_log("Access Control List (ACL)", r == 0);

    /* Sandbox */
    r = sandbox_init();
    init_log("App Sandbox engine", r == 0);
}

/* ─────────────────────────────────────────────
   FASE 5: Filesystem — tidak berubah
   ───────────────────────────────────────────── */
static void init_filesystem(void) {
    init_section_header("File System");

    int r = vfs_init();
    init_log("VFS (Virtual File System)", r == 0);
    if (r != 0) PANIC("VFS init failed");

    r = vfs_mount("/", FS_TYPE_LUNFS, 0);
    init_log("Mount root filesystem (/)", r == 0);

    vfs_mkdir("/sys",  PERM_OWNER_RWX | PERM_GROUP_R  | PERM_OTHER_NONE);
    vfs_mkdir("/home", PERM_OWNER_RWX | PERM_GROUP_RX | PERM_OTHER_NONE);
    vfs_mkdir("/tmp",  PERM_OWNER_RWX | PERM_GROUP_RWX| PERM_OTHER_RWX);
    vfs_mkdir("/apps", PERM_OWNER_RWX | PERM_GROUP_RX | PERM_OTHER_RX);
    init_log("Base directories created (/sys /home /tmp /apps)", 1);
}

/* ─────────────────────────────────────────────
   FASE 6: Process & Scheduling — tidak berubah
   ───────────────────────────────────────────── */
static void init_process(void) {
    init_section_header("Process Management");

    int r = syscall_init();
    init_log("System Call table (SVC #0)", r == 0);
    if (r != 0) PANIC("Syscall init failed");

    r = scheduler_init();
    init_log("Task Scheduler", r == 0);
    if (r != 0) PANIC("Scheduler init failed");
}

/* ─────────────────────────────────────────────
   FASE 7: Selesai
   ───────────────────────────────────────────── */
static void init_done(void) {
    fb_console_set_color(LUNAR_SUCCESS, LUNAR_BG);
    fb_console_puts("\n  ✓ LunarOS booted successfully! (ARM64)\n\n");
    fb_console_set_color(LUNAR_FG, LUNAR_BG);
    fb_console_puts("  ─────────────────────────────────────────────\n");

    pl011_puts("=== LunarOS ARM64 Boot Complete ===\n");

    system_status = INIT_DONE;
}

/* ─────────────────────────────────────────────
   lunar_init() — entry point utama ARM64
   Tidak ada parameter (tidak ada multiboot)
   ───────────────────────────────────────────── */
void lunar_init(void) {
    system_status = INIT_IN_PROGRESS;

    /*
     * pl011 sudah di-init di kernel_main() — tidak perlu init ulang.
     * Langsung init framebuffer lewat init_early_drivers().
     */
    print_boot_banner();

    init_arch();
    init_early_drivers();
    init_memory();
    init_security();
    init_filesystem();
    init_process();
    init_done();

    /* Login prompt */
    auth_prompt_login();

    /* Mulai scheduler — tidak pernah return */
    scheduler_start();

    PANIC("scheduler_start() returned unexpectedly");
}

/* ─────────────────────────────────────────────
   Getter status
   ───────────────────────────────────────────── */
int init_is_done(void) {
    return system_status == INIT_DONE;
}

/* ─────────────────────────────────────────────
   lunar_shutdown() — shutdown bersih ARM64
   x86 : cli + hlt
   ARM64: daifset + wfi
   ───────────────────────────────────────────── */
void lunar_shutdown(void) {
    /* Mask semua interrupt */
    __asm__ volatile("msr daifset, #0xF");

    fb_console_set_color(LUNAR_FG, LUNAR_BG);
    fb_console_clear();
    fb_console_puts("\n  LunarOS sedang mematikan sistem...\n");
    pl011_puts("[SHUTDOWN] LunarOS shutting down...\n");

    /* Flush & unmount filesystem */
    vfs_sync();
    vfs_umount("/");
    init_log("Filesystem flushed & unmounted", 1);

    pl011_puts("[SHUTDOWN] Done. Safe to power off.\n");
    fb_console_puts("\n  Aman untuk mematikan perangkat.\n");

    /*
     * Tidak ada ACPI di ARM — untuk QEMU bisa pakai
     * PSCI (Power State Coordination Interface):
     *   hvc #0 dengan function ID PSCI_SYSTEM_OFF
     * Untuk sekarang, loop wfi.
     */
    while (1) {
        __asm__ volatile("wfi");
    }
}
