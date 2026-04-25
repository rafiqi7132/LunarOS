/*
 * Lunar OS Kernel
 * Copyright (C) 2026 Muhammad Rafiqi H.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License version 2 as published by the
 * Free Software Foundation.
 */
#define VGA_ADDRESS 0xB8000
#define WHITE_ON_BLACK 0x0F

void print(const char* str) {
    unsigned short* vga = (unsigned short*) VGA_ADDRESS;
    int i = 0;
    while (str[i] != '\0') {
        vga[i] = (WHITE_ON_BLACK << 8) | str[i];
        i++;
    }
}

void kernel_main() {
    print("Welcome to Lunar OS");
}
