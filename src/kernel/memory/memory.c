// src/kernel/memory.c - FIXED VERSION
#include "memory.h"
#include "serial.h"

// This variable is set by entry.asm in REAL MODE before entering protected mode
extern uint32_t detected_memory_size;

uint32_t detect_memory(void) {
    kprintf("MEMORY: Using pre-detected RAM size...\n");
    kprintf("MEMORY: Detected %d MB RAM\n", detected_memory_size / 1024 / 1024);
    return detected_memory_size;
}
