#ifndef PHYSICAL_MM_H
#define PHYSICAL_MM_H

#include "../core/types.h"

#define PAGE_SIZE 4096
#define KERNEL_PHYSICAL_START 0x100000  // 1MB

void physical_mm_init(uint32_t memory_size);
void* alloc_page(void);
void* alloc_pages(uint32_t count);
void free_page(void* page);
uint32_t get_total_memory(void);
uint32_t get_free_memory(void);
uint32_t get_used_memory(void);

#endif
