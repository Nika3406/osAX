#ifndef DMA_H
#define DMA_H

#include "../core/types.h"

// Initialize DMA buffer pool (call after physical_mm_init)
void dma_init(void);

// Allocate DMA-capable buffer (guaranteed < 1MB for ISA compatibility)
// Size will be aligned to 4KB boundaries
void* dma_alloc(uint32_t size);

// Free DMA buffer
void dma_free(void* ptr);

// Get DMA statistics
void dma_get_stats(uint32_t* total, uint32_t* used, uint32_t* free);

#endif