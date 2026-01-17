// src/kernel/physical_mm.c - MB-based version (mem_size is MB)
#include "types.h"
#include "physical_mm.h"
#include "paging.h"
#include "serial.h"

static uint32_t* bitmap;
static uint32_t total_pages;
static uint32_t used_pages;
static uint32_t bitmap_size;

// Keep both forms so other code can ask for bytes safely
static uint32_t memory_size_mb;
static uint32_t memory_size_bytes;

// External symbols from linker script
extern char __bss_end;

static inline void mark_page_used(uint32_t page_idx) {
    if (page_idx >= total_pages) return;

    uint32_t word = page_idx / 32;
    uint32_t bit  = page_idx % 32;
    uint32_t mask = (1u << bit);

    if (!(bitmap[word] & mask)) {
        bitmap[word] |= mask;
        used_pages++;
    }
}

void physical_mm_init(uint32_t mem_mb) {
    memory_size_mb = mem_mb;
    memory_size_bytes = mem_mb * 1024u * 1024u;

    // 1 MB = 256 pages of 4KB
    total_pages = mem_mb * 256u;
    bitmap_size = (total_pages + 31u) / 32u;

    kprintf("PMM: Init start (mem=%u MB, pages=%u, bitmap_words=%u)\n",
            mem_mb, total_pages, bitmap_size);

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

    // Reserve pages that cover: kernel + bitmap storage itself
    uint32_t bitmap_end = (uint32_t)(uintptr_t)bitmap + (bitmap_size * 4u);
    uint32_t reserved_end = (bitmap_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t reserved_pages = reserved_end / PAGE_SIZE;

    for (uint32_t i = 0; i < reserved_pages; i++) {
        mark_page_used(i);
    }

    // Reserve DMA region (0x10000 - 0xA0000)
    uint32_t dma_start_page = 0x10000u / PAGE_SIZE;
    uint32_t dma_end_page   = 0xA0000u / PAGE_SIZE;

    for (uint32_t i = dma_start_page; i < dma_end_page; i++) {
        mark_page_used(i);
    }

    uint32_t free_pages = (used_pages <= total_pages) ? (total_pages - used_pages) : 0;
    uint32_t free_mb = (free_pages * PAGE_SIZE) / (1024u * 1024u);

    kprintf("PMM: Reserved DMA region (0x10000 - 0xA0000) for buffer pool\n");
    kprintf("PMM: Init complete! (reserved=%u pages, free=%u MB)\n",
            used_pages, free_mb);
}

void* alloc_page(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        uint32_t word = i / 32;
        uint32_t bit = i % 32;

        if (!(bitmap[word] & (1u << bit))) {
            bitmap[word] |= (1u << bit);
            used_pages++;
            return (void*)(uintptr_t)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void* alloc_pages(uint32_t count) {
    if (count == 0) return NULL;
    if (count == 1) return alloc_page();

    if (count > total_pages) return NULL;

    for (uint32_t i = 0; i <= total_pages - count; i++) {
        uint32_t consecutive_free = 0;

        for (uint32_t j = 0; j < count; j++) {
            uint32_t page_idx = i + j;
            uint32_t word = page_idx / 32;
            uint32_t bit  = page_idx % 32;

            if (bitmap[word] & (1u << bit)) break;
            consecutive_free++;
        }

        if (consecutive_free == count) {
            for (uint32_t j = 0; j < count; j++) {
                uint32_t page_idx = i + j;
                uint32_t word = page_idx / 32;
                uint32_t bit  = page_idx % 32;

                bitmap[word] |= (1u << bit);
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
    uint32_t bit  = page_num % 32;

    if (bitmap[word] & (1u << bit)) {
        bitmap[word] &= ~(1u << bit);
        used_pages--;
    }
}

// Keep API returning bytes so other code doesn’t break
uint32_t get_total_memory(void) { return memory_size_bytes; }
uint32_t get_free_memory(void)  { return (total_pages - used_pages) * PAGE_SIZE; }
uint32_t get_used_memory(void)  { return used_pages * PAGE_SIZE; }

// Optional convenience (if you want it)
uint32_t get_total_memory_mb(void) { return memory_size_mb; }
