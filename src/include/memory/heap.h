// src/include/heap.h
#ifndef HEAP_H
#define HEAP_H

#include "../core/types.h"

// Heap block header structure
typedef struct heap_block {
    uint32_t size;              // Size of block (excluding header)
    uint32_t is_free;           // 1 if free, 0 if allocated
    struct heap_block* next;    // Next block in list
    struct heap_block* prev;    // Previous block in list
} heap_block_t;

// Heap statistics
typedef struct {
    uint32_t total_size;
    uint32_t used_size;
    uint32_t free_size;
    uint32_t num_blocks;
    uint32_t num_free_blocks;
} heap_stats_t;

// Initialize kernel heap (call before paging)
void heap_init(void);

// Convert heap to virtual addressing (call after paging is enabled)
void heap_init_virtual(void);

// Allocate memory from kernel heap
void* kmalloc(size_t size);

// Allocate aligned memory
void* kmalloc_aligned(size_t size, size_t alignment);

// Free memory
void kfree(void* ptr);

// Reallocate memory
void* krealloc(void* ptr, size_t new_size);

// Get heap statistics
void heap_get_stats(heap_stats_t* stats);

// Debug: print heap state
void heap_debug_print(void);

#endif // HEAP_H
