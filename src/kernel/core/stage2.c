// src/kernel/core/stage2.c
#include "kstring.h"
#include "physical_mm.h"
#include "memory.h"
#include "paging.h"
#include "serial.h"
#include "idt.h"
#include "heap.h"
#include "exfat.h"
#include "dma.h"  // NEW

extern uint32_t framebuffer_address;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;

void c_main(void) {
    serial_init();
    kprintf("OSAX: Booting from 1MB (relocated from 64KB)...\n");
    
    // IDT
    idt_init();
    kprintf("  IDT initialized\n");
    
    // Memory detection
    uint32_t mem_mb = detect_memory();
    kprintf("  Memory: %d MB\n", mem_mb);
    
    // Physical memory manager (now reserves DMA region)
    physical_mm_init(mem_mb);
    kprintf("  Physical memory manager ready\n");
    
    // Heap (physical mode)
    heap_init();
    kprintf("  Heap initialized\n");
    
    // NEW: Initialize DMA buffer pool (BEFORE paging)
    dma_init();
    kprintf("  DMA buffers ready\n");
    
    // Enable paging
    paging_init();
    heap_init_virtual();
    kernel_heap_init();
    exfat_set_paging_mode();
    kprintf("  Virtual memory enabled\n");
    
    // Disk buffer
    exfat_init_disk(10);
    kprintf("  Disk buffer ready\n");
    
    kprintf("OSAX: Core initialization complete\n");
    kprintf("  Memory layout:\n");
    kprintf("    0x00010000 - 0x0009FFFF: DMA buffers (576 KB)\n");
    kprintf("    0x00100000+           : Kernel\n");
    kprintf("    0x00200000+           : Stack/Heap\n\n");
    
    // Transfer control to main OS
    extern void os_main(void);
    os_main();
    
    for(;;) __asm__ volatile("hlt");
}
