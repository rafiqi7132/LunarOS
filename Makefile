# ═══════════════════════════════════════════════════════════
# Makefile - LunarOS ARM64
# ═══════════════════════════════════════════════════════════

# ─────────────────────────────────────────────
# Toolchain ARM64
# Install: sudo apt install gcc-aarch64-linux-gnu
# ─────────────────────────────────────────────
CROSS   := aarch64-none-elf-
CC      := $(CROSS)gcc
AS      := $(CROSS)as
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
NM      := $(CROSS)nm

# ─────────────────────────────────────────────
# Output
# ─────────────────────────────────────────────
TARGET     := lunar
BUILD_DIR  := build
KERNEL_ELF := $(BUILD_DIR)/$(TARGET).elf
KERNEL_BIN := $(BUILD_DIR)/$(TARGET).bin
KERNEL_MAP := $(BUILD_DIR)/$(TARGET).map

# ─────────────────────────────────────────────
# Flags
# ─────────────────────────────────────────────
CFLAGS := \
    -std=c11               \
    -ffreestanding         \
    -nostdlib              \
    -nostdinc              \
    -fno-builtin           \
    -fno-stack-protector   \
    -fno-pie               \
    -fno-pic               \
    -march=armv8-a         \
    -mtune=cortex-a57      \
    -mstrict-align         \
    -Wall                  \
    -Wextra                \
    -Werror                \
    -O2                    \
    -g                     \
    -Isrc                  \
    -DARCH_ARM64           \
    -DLUNAROS_VERSION=\"0.1.0\"

ASFLAGS := \
    -march=armv8-a

LDFLAGS := \
    -T linker.ld           \
    -nostdlib              \
    -Map=$(KERNEL_MAP)     \
    --no-undefined

# ─────────────────────────────────────────────
# Source files
# ─────────────────────────────────────────────

# Assembly (boot + interrupt)
ASM_SRCS := \
    src/arch/arm64/boot.S  \
    src/arch/arm64/isr.S

# C sources
C_SRCS := \
    src/kernel/kernel.c        \
    src/kernel/panic.c         \
    src/kernel/init.c          \
    src/arch/arm64/gic.c       \
    src/mm/pmm.c               \
    src/mm/vmm.c               \
    src/mm/heap.c              \
    src/drivers/pl011.c        \
    src/drivers/framebuffer.c  \
    src/drivers/arm_timer.c    \
    src/security/crypto.c      \
    src/security/auth.c        \
    src/security/acl.c         \
    src/security/sandbox.c     \
    src/security/secure_boot.c \
    src/process/scheduler.c    \
    src/process/process.c      \
    src/process/syscall.c      \
    src/fs/vfs.c               \
    src/fs/lunfs.c             \
    src/lib/string.c           \
    src/lib/stdio.c            \
    src/lib/math.c

# Object files (semua masuk build/)
ASM_OBJS := $(patsubst src/%.S,   $(BUILD_DIR)/%.o, $(ASM_SRCS))
C_OBJS   := $(patsubst src/%.c,   $(BUILD_DIR)/%.o, $(C_SRCS))
ALL_OBJS := $(ASM_OBJS) $(C_OBJS)

# ─────────────────────────────────────────────
# Rules utama
# ─────────────────────────────────────────────
.PHONY: all clean run run-debug dump symbols

all: $(KERNEL_BIN)
	@echo ""
	@echo "  ✓ Build selesai!"
	@echo "    ELF : $(KERNEL_ELF)"
	@echo "    BIN : $(KERNEL_BIN)"
	@echo "    MAP : $(KERNEL_MAP)"
	@echo ""

# Link semua object jadi ELF
$(KERNEL_ELF): $(ALL_OBJS)
	@echo "[LD]  $@"
	@mkdir -p $(dir $@)
	@$(LD) $(LDFLAGS) -o $@ $^

# Strip jadi raw binary (untuk QEMU / flash ke hardware)
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "[BIN] $@"
	@$(OBJCOPY) -O binary $< $@

# ─────────────────────────────────────────────
# Compile C
# ─────────────────────────────────────────────
$(BUILD_DIR)/%.o: src/%.c
	@echo "[CC]  $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ─────────────────────────────────────────────
# Compile Assembly
# ─────────────────────────────────────────────
$(BUILD_DIR)/%.o: src/%.S
	@echo "[AS]  $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ─────────────────────────────────────────────
# QEMU — test tanpa hardware
# ─────────────────────────────────────────────
QEMU      := qemu-system-aarch64
QEMU_MACH := virt
QEMU_CPU  := cortex-a57
QEMU_MEM  := 256M

QEMU_FLAGS := \
    -machine $(QEMU_MACH)  \
    -cpu $(QEMU_CPU)        \
    -m $(QEMU_MEM)          \
    -kernel $(KERNEL_BIN)   \
    -serial stdio           \
    -display none           \
    -no-reboot

# Jalankan LunarOS di QEMU
run: $(KERNEL_BIN)
	@echo "[QEMU] Menjalankan LunarOS..."
	@$(QEMU) $(QEMU_FLAGS)

# Jalankan dengan GDB server (port 1234)
run-debug: $(KERNEL_BIN)
	@echo "[QEMU] Debug mode — tunggu GDB di port 1234"
	@$(QEMU) $(QEMU_FLAGS) -s -S

# GDB client (jalankan di terminal lain setelah run-debug)
gdb: $(KERNEL_ELF)
	@aarch64-none-elf-gdb \
	    -ex "target remote :1234" \
	    -ex "symbol-file $(KERNEL_ELF)" \
	    $(KERNEL_ELF)

# ─────────────────────────────────────────────
# Debugging tools
# ─────────────────────────────────────────────

# Disassembly kernel
dump: $(KERNEL_ELF)
	@$(OBJDUMP) -d $(KERNEL_ELF) | less

# Daftar semua simbol
symbols: $(KERNEL_ELF)
	@$(NM) -n $(KERNEL_ELF)

# Tampilkan ukuran setiap section
size: $(KERNEL_ELF)
	@$(CROSS)size $(KERNEL_ELF)

# ─────────────────────────────────────────────
# Clean
# ─────────────────────────────────────────────
clean:
	@echo "[CLN] Menghapus build..."
	@rm -rf $(BUILD_DIR)
	@echo "  ✓ Bersih!"

# Tampilkan semua target yang tersedia
help:
	@echo ""
	@echo "  LunarOS ARM64 — Makefile targets:"
	@echo ""
	@echo "    make          → build kernel (ELF + BIN)"
	@echo "    make run      → build + jalankan di QEMU"
	@echo "    make run-debug→ build + QEMU dengan GDB server"
	@echo "    make gdb      → connect GDB ke QEMU"
	@echo "    make dump     → disassembly kernel"
	@echo "    make symbols  → daftar semua simbol"
	@echo "    make size     → ukuran tiap section"
	@echo "    make clean    → hapus semua build output"
	@echo ""
