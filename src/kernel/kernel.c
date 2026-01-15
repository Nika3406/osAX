// src/kernel/kernel.c
#include "kstring.h"

static void putstr_vga(const char *s, unsigned short row, unsigned short col) {
    volatile unsigned short *video = (volatile unsigned short *)0xB8000;
    unsigned short pos = row * 80 + col;
    while (*s) {
        char c = *s++;
        unsigned short val = (unsigned short)c | (0x07 << 8);
        video[pos++] = val;
    }
}

static void clear_screen() {
    volatile unsigned short *video = (volatile unsigned short *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        video[i] = (unsigned short)' ' | (0x07 << 8);
    }
}

void kmain(void) {
    clear_screen();
    putstr_vga("Kernel: Basic system OK!", 1, 10);
    putstr_vga("No paging or complex memory", 2, 10);

    // Hang successfully
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
