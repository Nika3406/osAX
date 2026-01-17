// src/kernel/memory.c - FIXED VERSION
#include "memory.h"
#include "serial.h"

// This variable is set by entry.asm before entering C
extern uint32_t detected_memory_size;

uint32_t detect_memory_mb(void) {
    kprintf("MEMORY: Detected %u MB RAM\n", detected_memory_size);
    return detected_memory_size;
}

// Wrapper for existing callers (stage2.c expects detect_memory())
uint32_t detect_memory(void) {
    return detect_memory_mb();
}
