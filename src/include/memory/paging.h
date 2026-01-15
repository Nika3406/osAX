#ifndef PAGING_H
#define PAGING_H

#include "../core/types.h"

#define PAGE_SIZE 4096
#define PAGE_DIRECTORY_INDEX(x) (((x) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)
#define PAGE_GET_PHYSICAL_ADDRESS(x) ((x) & 0xFFFFF000)

// Page table entry flags
#define PAGE_PRESENT        (1 << 0)
#define PAGE_WRITABLE       (1 << 1)
#define PAGE_USER           (1 << 2)
#define PAGE_WRITETHROUGH   (1 << 3)
#define PAGE_CACHE_DISABLE  (1 << 4)
#define PAGE_ACCESSED       (1 << 5)
#define PAGE_DIRTY          (1 << 6)
#define PAGE_GLOBAL         (1 << 8)

// Memory layout
#define KERNEL_VIRTUAL_BASE 0xC0000000  // 3GB mark
#define KERNEL_HEAP_START   0xC0100000  // Start after kernel code
#define KERNEL_HEAP_END     0xC8000000  // 112MB kernel heap
#define USER_SPACE_START    0x00000000
#define USER_SPACE_END      0xBFFFFFFF  // 3GB user space

typedef struct {
    uint32_t present    : 1;
    uint32_t rw         : 1;
    uint32_t user       : 1;
    uint32_t writethrough : 1;
    uint32_t cache_disable : 1;
    uint32_t accessed   : 1;
    uint32_t dirty      : 1;
    uint32_t zero       : 1;
    uint32_t global     : 1;
    uint32_t available  : 3;
    uint32_t frame      : 20;
} __attribute__((packed)) page_table_entry_t;

typedef struct {
    page_table_entry_t pages[1024];
} __attribute__((packed)) page_table_t;

typedef struct {
    uint32_t present    : 1;
    uint32_t rw         : 1;
    uint32_t user       : 1;
    uint32_t writethrough : 1;
    uint32_t cache_disable : 1;
    uint32_t accessed   : 1;
    uint32_t zero       : 1;
    uint32_t page_size  : 1;
    uint32_t ignored    : 1;
    uint32_t available  : 3;
    uint32_t table_addr : 20;
} __attribute__((packed)) page_directory_entry_t;

typedef struct {
    page_directory_entry_t tables[1024];
    uint32_t physical_addr;
} __attribute__((packed)) page_directory_t;

// Core paging functions
void paging_init(void);
void map_page(page_directory_t* dir, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
void unmap_page(page_directory_t* dir, uint32_t virtual_addr);
page_directory_t* get_kernel_page_dir(void);
void switch_page_directory(page_directory_t* dir);
void enable_paging(void);

// Page fault handler
void page_fault_handler(uint32_t error_code);

// Virtual memory allocation functions
void* kmalloc_virtual(uint32_t size);
void kfree_virtual(void* ptr, uint32_t size);
void* physical_to_virtual(uint32_t physical_addr);
void kernel_heap_init(void);

// Memory statistics
void paging_get_stats(uint32_t* total_virtual, uint32_t* used_virtual,
                      uint32_t* total_physical, uint32_t* used_physical);

#endif
