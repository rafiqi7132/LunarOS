<div align="center">

# 🌙 LunarOS

**A lightweight operating system inspired by iOS, built from scratch for ARM64**

![Version](https://img.shields.io/badge/version-0.1.0-blue?style=flat-square)
![Architecture](https://img.shields.io/badge/arch-ARM64%20%2F%20AArch64-orange?style=flat-square)
![License](https://img.shields.io/badge/license-GPL--2.0-green?style=flat-square)
![Status](https://img.shields.io/badge/status-in%20development-yellow?style=flat-square)

<br>

*"Crescent"* — v0.1.0

</div>

---

## 📖 About

LunarOS is a hobby operating system built from scratch, targeting the **ARM64 (AArch64)** architecture. Inspired by the clean design philosophy of iOS and the XNU kernel, LunarOS aims to implement core OS concepts including memory management, process scheduling, a virtual filesystem, and a security subsystem — all without relying on any existing library or runtime.

> ⚠️ LunarOS is currently in early development. It is not intended for production use.

---

## ✨ Features

### 🏗️ Architecture
- ARM64 (AArch64) — targets QEMU `virt` machine
- Custom boot sequence via `boot.S` (no GRUB)
- Exception vector table via `VBAR_EL1` (replaces x86 IDT)
- GIC v2 interrupt controller (replaces x86 PIC 8259)
- 4-level ARM64 page tables (PGD → PUD → PMD → PTE)

### 🧠 Memory Management
- **PMM** — Physical Memory Manager using bitmap allocation
- **VMM** — Virtual Memory Manager with ARM64 MMU
- **Heap** — Kernel heap with `kmalloc` / `kfree` / `krealloc`
- Auto-expand heap, split & coalesce free blocks
- Double-free and corruption detection

### ⚙️ Process Management
- Process Control Block (PCB) with full CPU context
- Round-Robin scheduler with priority queue
- Context switch via callee-saved registers (ARM64 ABI)
- Sleep, wake, yield, and process lifecycle management
- System calls via `SVC #0` instruction

### 🔒 Security
- **SHA-256** — implemented from scratch (FIPS 180-4)
- **AES-128-CBC** — full implementation with S-Box
- **HMAC-SHA256** — for message authentication
- **Password hashing** — salted + iterated (PBKDF-like)
- **Authentication** — login system with lockout after failed attempts
- **ACL** — Access Control List for files and resources
- **Sandbox** — per-process memory and path isolation (inspired by iOS App Sandbox)
- **Secure Boot** — kernel image hash verification
- Constant-time memory comparison (timing attack prevention)

### 📁 File System
- **VFS** — Virtual File System abstraction layer
- **LunarFS** — custom in-memory filesystem with:
  - Hierarchical directory tree
  - File read/write with auto-resize buffers
  - Permission bits (owner/group/other)
  - Timestamps (created / modified)
  - CRC32 data integrity

### 🖥️ Drivers
- **PL011 UART** — serial output + console input (replaces x86 VGA + PS/2)
- **Framebuffer** — 800×600 pixel display with built-in 8×16 bitmap font
- **ARM Generic Timer** — 100 Hz tick for scheduling (replaces x86 PIT)
- Themed console output inspired by iOS color palette

### 📚 Libraries
- `string.c` — memcpy, memset, strcmp, strncpy, itoa, atoi, and more
- `stdio.c` — kprintf with full format specifier support
- `math.c` — integer math, bit manipulation, CRC32, FNV-1a hash

---

## 🚀 Building & Running

### Prerequisites

```bash
# ARM64 cross-compiler
sudo apt install gcc-aarch64-linux-gnu

# Or download ARM toolchain from:
# [https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)

# QEMU for testing
sudo apt install qemu-system-aarch64
```

# Clone the repository
```bash
git clone [https://github.com/rafiqi7132/LunarOS.git](https://github.com/rafiqi7132/LunarOS.git)
cd LunarOS

# Build kernel
make
```

# Run in QEMU 
```bash
# Boot LunarOS
make run

# Boot with GDB debug server (port 1234)
make run-debug

# In another terminal — connect GDB
make gdb
```
# Other build targets
```bash
make size      # show section sizes
make dump      # disassembly
make symbols   # list all symbols
make clean     # remove build output
make help      # show all targets
```

# Boot Sequence

```text
boot.S (_start)
  │
  ├─ Core 0 check (park other cores)
  ├─ Stack setup
  ├─ BSS zero
  ├─ VBAR_EL1 = vector_table
  └─ bl kernel_main()
       │
       └─ lunar_init()
            │
            ├─ [1] GIC init + unmask interrupts
            ├─ [2] ARM Timer (100 Hz) + UART + Framebuffer
            ├─ [3] PMM → VMM (MMU on) → Heap
            ├─ [4] Secure Boot → Auth → ACL → Sandbox
            ├─ [5] VFS → LunarFS mount → mkdir /sys /home /tmp /apps
            ├─ [6] Syscall table → Scheduler
            └─ [7] Login prompt → scheduler_start()
```

# 🔧 System Calls

User programs invoke system calls via SVC #0 with the syscall number in x8 and arguments in x0–x5. Return value is in x0.

1 = exit
Terminate process

3 = getpid
Get process ID

5 = sleep
Sleep for N milliseconds

6 = yield
Yield CPU voluntarily

9 = kill
Terminate another process

22 = brk
Expand process heap

30 = open
Open a file

31 = close
Close a file descriptor

32 = read
Read from file

33 = write
Write to file

37 = mkdir
Create directory

60 = getuid
Get user ID

71 = uptime
Get system uptime (ms)

72 = meminfo
Get memory statistics

73 = shutdown
Shutdown the system

# 🔒 Security Model

LunarOS implements a layered security model inspired by iOS:
```text
┌─────────────────────────────────────┐
│           User Application          │
├─────────────────────────────────────┤
│     Sandbox (memory + path ACL)     │
├─────────────────────────────────────┤
│     Authentication (uid + role)     │
├─────────────────────────────────────┤
│   Syscall validation + ACL check    │
├─────────────────────────────────────┤
│   ARM64 EL0/EL1 privilege levels    │
└─────────────────────────────────────┘
```

- Password Security: Passwords are never stored in plaintext — always hashed with salt + 10,000 iterations.

- Timing Attack Prevention: All hash comparisons use constant-time functions.

- Process Isolation: Every process runs in its own sandbox with explicit path allowlist.

- Memory Sanitization: Sensitive data (keys, hashes) are zeroed from memory after use.

# 🗺️ Roadmap
```text
[x] ARM64 boot sequence
[x] Interrupt handling (GIC)
[x] Physical & virtual memory management
[x] Kernel heap
[x] Process management & scheduler
[x] System calls
[x] Security subsystem (crypto, auth, sandbox)
[x] Virtual filesystem + LunarFS
[ ] Shell / command line interface
[ ] ELF loader (load user programs)
[ ] Inter-process communication (IPC)
[ ] Network stack (basic TCP/IP)
[ ] LunarFS persistence (block device storage)
[ ] SMP support (multi-core)
[ ] GUI layer
```

# 👤 Author
Muhammad Rafiqi H. <br>
GitHub: @rafiqi7132

# 📄 License
LunarOS is released under the GNU General Public License v2.0. See the LICENSE file for details.
```text
LunarOS — Copyright (C) 2026 Muhammad Rafiqi H.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
```
