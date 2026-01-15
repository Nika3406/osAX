// src/kernel/heap.c - Unified heap supporting both physical and virtual memory
#include "heap.h"
#include "physical_mm.h"
#include "paging.h"
#include "kstring.h"
#include "serial.h"

#define HEAP_MAGIC 0xDEADBEEF
#define MIN_BLOCK_SIZE 32

static heap_block_t* heap_start = NULL;
static uint32_t heap_size = 0;
static int paging_enabled = 0;

// Initialize heap - works before or after paging
void heap_init(void) {
    // Allocate initial heap space (16MB for large allocations like exFAT disk)
    uint32_t initial_size = 16 * 1024 * 1024;
    uint32_t pages_needed = (initial_size + 4095) / 4096;

    // Allocate physical pages
    void* physical_base = alloc_pages(pages_needed);
    if (!physical_base) {
        kprintf("HEAP: Failed to allocate physical pages!\n");
        return;
    }

    // Use physical address directly (works before paging)
    heap_start = (heap_block_t*)physical_base;

    // Initialize first block
    heap_start->size = initial_size - sizeof(heap_block_t);
    heap_start->is_free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    heap_size = initial_size;
    paging_enabled = 0;

    kprintf("HEAP: Initialized at %x with %d MB (physical mode)\n",
            heap_start, initial_size / 1024 / 1024);
}

// Initialize heap after paging is enabled
void heap_init_virtual(void) {
    if (!heap_start) {
        kprintf("HEAP: Error - heap_init() must be called first!\n");
        return;
    }

    // The heap is already accessible via identity mapping (first 4MB)
    // We just need to mark that we're now in paging mode
    // The heap will continue to work at its current physical address
    // which is identity-mapped in the page tables

    paging_enabled = 1;

    kprintf("HEAP: Paging mode enabled\n");
    kprintf("  Heap remains at physical address %x (identity-mapped)\n", heap_start);
    kprintf("  Future expansions will use virtual memory\n");
}

// Expand heap if needed
static int expand_heap(size_t additional_size) {
    uint32_t pages_needed = (additional_size + PAGE_SIZE - 1) / PAGE_SIZE;

    if (paging_enabled) {
        // With paging enabled, allocate using virtual memory
        void* new_mem = kmalloc_virtual(pages_needed * PAGE_SIZE);
        if (!new_mem) {
            kprintf("HEAP: Failed to expand (virtual)\n");
            return -1;
        }

        // Create new block
        heap_block_t* new_block = (heap_block_t*)new_mem;
        new_block->size = (pages_needed * PAGE_SIZE) - sizeof(heap_block_t);
        new_block->is_free = 1;
        new_block->next = NULL;
        new_block->prev = NULL;

        // Link to existing heap
        heap_block_t* current = heap_start;
        while (current->next) {
            current = current->next;
        }
        current->next = new_block;
        new_block->prev = current;

        heap_size += pages_needed * PAGE_SIZE;
        kprintf("HEAP: Expanded by %d KB (virtual)\n", (pages_needed * PAGE_SIZE) / 1024);

    } else {
        // Without paging, allocate physical memory directly
        void* new_mem = alloc_pages(pages_needed);
        if (!new_mem) {
            kprintf("HEAP: Failed to expand (physical)\n");
            return -1;
        }

        heap_block_t* new_block = (heap_block_t*)new_mem;
        new_block->size = (pages_needed * PAGE_SIZE) - sizeof(heap_block_t);
        new_block->is_free = 1;
        new_block->next = NULL;
        new_block->prev = NULL;

        // Link to existing heap
        heap_block_t* current = heap_start;
        while (current->next) {
            current = current->next;
        }
        current->next = new_block;
        new_block->prev = current;

        heap_size += pages_needed * PAGE_SIZE;
        kprintf("HEAP: Expanded by %d KB (physical)\n", (pages_needed * PAGE_SIZE) / 1024);
    }

    return 0;
}

// Find free block using first-fit strategy
static heap_block_t* find_free_block(size_t size) {
    heap_block_t* current = heap_start;

    while (current) {
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// Split block if it's too large
static void split_block(heap_block_t* block, size_t size) {
    if (block->size >= size + sizeof(heap_block_t) + MIN_BLOCK_SIZE) {
        heap_block_t* new_block = (heap_block_t*)((uint8_t*)block + sizeof(heap_block_t) + size);
        new_block->size = block->size - size - sizeof(heap_block_t);
        new_block->is_free = 1;
        new_block->next = block->next;
        new_block->prev = block;

        if (block->next) {
            block->next->prev = new_block;
        }

        block->next = new_block;
        block->size = size;
    }
}

// Merge adjacent free blocks
static void coalesce_blocks(heap_block_t* block) {
    // Merge with next block if it's free
    if (block->next && block->next->is_free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;

        if (block->next) {
            block->next->prev = block;
        }
    }

    // Merge with previous block if it's free
    if (block->prev && block->prev->is_free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;

        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // Align size to 8 bytes
    size = (size + 7) & ~7;

    // Find free block
    heap_block_t* block = find_free_block(size);

    // If no block found, try to expand heap
    if (!block) {
        if (expand_heap(size + sizeof(heap_block_t) + 4096) < 0) {
            kprintf("HEAP: Out of memory! Requested: %d bytes\n", size);
            return NULL;
        }
        block = find_free_block(size);
        if (!block) return NULL;
    }

    // Split block if needed
    split_block(block, size);

    // Mark as allocated
    block->is_free = 0;

    // Return pointer to usable memory (after header)
    return (void*)((uint8_t*)block + sizeof(heap_block_t));
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    size_t total_size = size + alignment + sizeof(heap_block_t);
    void* ptr = kmalloc(total_size);

    if (!ptr) return NULL;

    // Calculate aligned address
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);

    if (aligned == addr) {
        return ptr;
    }

    return ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Get block header
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));

    // Mark as free
    block->is_free = 1;

    // Coalesce with adjacent free blocks
    coalesce_blocks(block);
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    // Get current block
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));

    // If new size fits in current block, return same pointer
    if (block->size >= new_size) {
        return ptr;
    }

    // Allocate new block
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    // Copy data
    memcpy(new_ptr, ptr, block->size < new_size ? block->size : new_size);

    // Free old block
    kfree(ptr);

    return new_ptr;
}

void heap_get_stats(heap_stats_t* stats) {
    if (!stats) return;

    stats->total_size = 0;
    stats->used_size = 0;
    stats->free_size = 0;
    stats->num_blocks = 0;
    stats->num_free_blocks = 0;

    heap_block_t* current = heap_start;
    while (current) {
        stats->num_blocks++;
        stats->total_size += current->size + sizeof(heap_block_t);

        if (current->is_free) {
            stats->num_free_blocks++;
            stats->free_size += current->size;
        } else {
            stats->used_size += current->size;
        }

        current = current->next;
    }
}

void heap_debug_print(void) {
    kprintf("\n=== HEAP DEBUG ===\n");
    kprintf("Heap start: %x (%s mode)\n", heap_start,
            paging_enabled ? "virtual" : "physical");

    heap_block_t* current = heap_start;
    int block_num = 0;

    while (current && block_num < 20) {  // Limit output
        kprintf("Block %d: addr=%x size=%d %s\n",
                block_num++,
                current,
                current->size,
                current->is_free ? "FREE" : "USED");
        current = current->next;
    }

    heap_stats_t stats;
    heap_get_stats(&stats);

    kprintf("\nTotal blocks: %d\n", stats.num_blocks);
    kprintf("Free blocks: %d\n", stats.num_free_blocks);
    kprintf("Total size: %d MB\n", stats.total_size / 1024 / 1024);
    kprintf("Used size: %d KB\n", stats.used_size / 1024);
    kprintf("Free size: %d MB\n", stats.free_size / 1024 / 1024);
    kprintf("==================\n\n");
}
