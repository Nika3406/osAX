// src/kernel/memory/dma.c - DMA buffer management for low memory
#include "dma.h"
#include "physical_mm.h"
#include "kstring.h"
#include "serial.h"
#include "heap.h"

// DMA region: 0x10000 - 0x9FFFF (below 1MB for ISA compatibility)
#define DMA_START 0x00010000
#define DMA_END   0x000A0000
#define DMA_SIZE  (DMA_END - DMA_START)

typedef struct dma_block {
    uint32_t address;
    uint32_t size;
    int in_use;
    struct dma_block* next;
} dma_block_t;

static dma_block_t* dma_free_list = NULL;
static uint32_t dma_total_size = 0;
static uint32_t dma_used_size = 0;

void dma_init(void) {
    kprintf("DMA: Initializing buffer pool...\n");
    
    // Create initial free block covering entire DMA region
    dma_free_list = (dma_block_t*)kmalloc(sizeof(dma_block_t));
    if (!dma_free_list) {
        kprintf("DMA: Failed to allocate management structure!\n");
        return;
    }
    
    dma_free_list->address = DMA_START;
    dma_free_list->size = DMA_SIZE;
    dma_free_list->in_use = 0;
    dma_free_list->next = NULL;
    
    dma_total_size = DMA_SIZE;
    dma_used_size = 0;
    
    kprintf("DMA: Initialized %d KB buffer pool at 0x%x - 0x%x\n", 
            DMA_SIZE / 1024, DMA_START, DMA_END - 1);
    kprintf("DMA: Available for disk I/O, networking, audio, USB\n");
}

void* dma_alloc(uint32_t size) {
    if (size == 0) return NULL;
    
    // Align to 4KB boundaries (common DMA requirement)
    size = (size + 0xFFF) & ~0xFFF;
    
    dma_block_t* current = dma_free_list;
    while (current) {
        if (!current->in_use && current->size >= size) {
            // Split block if larger than needed
            if (current->size > size + sizeof(dma_block_t) + 0x1000) {
                dma_block_t* new_block = (dma_block_t*)kmalloc(sizeof(dma_block_t));
                if (new_block) {
                    new_block->address = current->address + size;
                    new_block->size = current->size - size;
                    new_block->in_use = 0;
                    new_block->next = current->next;
                    current->next = new_block;
                    current->size = size;
                }
            }
            
            current->in_use = 1;
            dma_used_size += size;
            
            kprintf("DMA: Allocated %d KB at 0x%x\n", size / 1024, current->address);
            return (void*)current->address;
        }
        current = current->next;
    }
    
    kprintf("DMA: Out of memory (requested %d KB, %d KB available)\n", 
            size / 1024, (dma_total_size - dma_used_size) / 1024);
    return NULL;
}

void dma_free(void* ptr) {
    if (!ptr) return;
    
    uint32_t addr = (uint32_t)ptr;
    
    // Validate address is in DMA range
    if (addr < DMA_START || addr >= DMA_END) {
        kprintf("DMA: Invalid free at 0x%x (outside DMA region)\n", addr);
        return;
    }
    
    dma_block_t* current = dma_free_list;
    while (current) {
        if (current->address == addr && current->in_use) {
            current->in_use = 0;
            dma_used_size -= current->size;
            
            kprintf("DMA: Freed %d KB at 0x%x\n", current->size / 1024, addr);
            
            // Coalesce with next block if free
            if (current->next && !current->next->in_use) {
                dma_block_t* next = current->next;
                current->size += next->size;
                current->next = next->next;
                kfree(next);
            }
            
            return;
        }
        current = current->next;
    }
    
    kprintf("DMA: Invalid free at 0x%x (not allocated)\n", addr);
}

void dma_get_stats(uint32_t* total, uint32_t* used, uint32_t* free) {
    if (total) *total = dma_total_size;
    if (used) *used = dma_used_size;
    if (free) *free = dma_total_size - dma_used_size;
}