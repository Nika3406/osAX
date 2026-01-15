// src/kernel/physical_mm.c - Clean version without VGA debug
#include "types.h"
#include "physical_mm.h"
#include "paging.h"
#include "serial.h"

static uint32_t* bitmap;
static uint32_t total_pages;
static uint32_t used_pages;
static uint32_t bitmap_size;
static uint32_t memory_size;

// External symbols from linker script
extern char __bss_end;

// Add after your existing physical_mm_init function
void physical_mm_init(uint32_t mem_size) {
    memory_size = mem_size;
    total_pages = mem_size / PAGE_SIZE;
    bitmap_size = (total_pages + 31) / 32;

    kprintf("PMM: Init start (pages=%d, bitmap_words=%d)\n", total_pages, bitmap_size);

    // Place bitmap AFTER kernel end
    uint32_t kernel_end = (uint32_t)(uintptr_t)&__bss_end;
    kernel_end = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    bitmap = (uint32_t*)(uintptr_t)kernel_end;
    used_pages = 0;

    kprintf("PMM: Bitmap at %x, clearing...\n", bitmap);

    // Clear bitmap
    for (uint32_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0;
    }

    kprintf("PMM: Bitmap cleared, marking reserved pages...\n");

    // Mark reserved regions as used
    uint32_t bitmap_end = (uint32_t)(uintptr_t)bitmap + (bitmap_size * 4);
    uint32_t reserved_end = (bitmap_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t reserved_pages = reserved_end / PAGE_SIZE;

    for (uint32_t i = 0; i < reserved_pages; i++) {
        bitmap[i / 32] |= (1 << (i % 32));
        used_pages++;
    }

    // NEW: Reserve DMA region (0x10000 - 0xA0000) for DMA buffer pool
    // This region is managed by dma.c, not the page allocator
    uint32_t dma_start_page = 0x10000 / PAGE_SIZE;
    uint32_t dma_end_page = 0xA0000 / PAGE_SIZE;
    
    for (uint32_t i = dma_start_page; i < dma_end_page; i++) {
        bitmap[i / 32] |= (1 << (i % 32));
        used_pages++;
    }

    kprintf("PMM: Reserved DMA region (0x10000 - 0xA0000) for buffer pool\n");
    kprintf("PMM: Init complete! (reserved=%d pages, free=%d MB)\n",
            used_pages, ((total_pages - used_pages) * PAGE_SIZE) / 1024 / 1024);
}

void* alloc_page(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        uint32_t word = i / 32;
        uint32_t bit = i % 32;

        if (!(bitmap[word] & (1 << bit))) {
            bitmap[word] |= (1 << bit);
            used_pages++;
            return (void*)(uintptr_t)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void* alloc_pages(uint32_t count) {
    if (count == 0) return NULL;
    if (count == 1) return alloc_page();

    for (uint32_t i = 0; i <= total_pages - count; i++) {
        uint32_t consecutive_free = 0;

        for (uint32_t j = 0; j < count; j++) {
            uint32_t page_idx = i + j;
            uint32_t word = page_idx / 32;
            uint32_t bit = page_idx % 32;

            if (page_idx >= total_pages || (bitmap[word] & (1 << bit))) {
                break;
            }
            consecutive_free++;
        }

        if (consecutive_free == count) {
            for (uint32_t j = 0; j < count; j++) {
                uint32_t page_idx = i + j;
                uint32_t word = page_idx / 32;
                uint32_t bit = page_idx % 32;
                bitmap[word] |= (1 << bit);
                used_pages++;
            }
            return (void*)(uintptr_t)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void free_page(void* page) {
    uint32_t page_num = (uint32_t)((uintptr_t)page / PAGE_SIZE);
    if (page_num >= total_pages) return;

    uint32_t word = page_num / 32;
    uint32_t bit = page_num % 32;

    if (bitmap[word] & (1 << bit)) {
        bitmap[word] &= ~(1 << bit);
        used_pages--;
    }
}

uint32_t get_total_memory(void) {
    return memory_size;
}

uint32_t get_free_memory(void) {
    return (total_pages - used_pages) * PAGE_SIZE;
}

uint32_t get_used_memory(void) {
    return used_pages * PAGE_SIZE;
}
