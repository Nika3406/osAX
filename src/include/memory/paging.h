// src/include/memory/paging.h - x86_64 VERSION (4-level paging)
#ifndef PAGING_H
#define PAGING_H

#include "../core/types.h"

// Page size (same as 32-bit)
#define PAGE_SIZE 4096

// Virtual memory layout for x86_64
// Canonical addresses: only 48 bits are used
// Bits 48-63 must match bit 47 (sign-extended)

// User space (lower half - canonical)
#define USER_SPACE_START    0x0000000000000000ULL
#define USER_SPACE_END      0x00007FFFFFFFFFFFULL

// Kernel space (higher half - canonical)
#define KERNEL_SPACE_START  0xFFFF800000000000ULL
#define KERNEL_VIRTUAL_BASE 0x0000000000100000ULL  // Currently at 1MB (can move to higher half later)
#define KERNEL_HEAP_START   0x0000000000400000ULL  // After 4MB kernel
#define KERNEL_HEAP_END     0x0000000040000000ULL  // 1GB heap

// Page table indices (9 bits each for 512 entries)
#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FF)
#define PDP_INDEX(addr)   (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)    (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)    (((addr) >> 12) & 0x1FF)
#define PAGE_OFFSET(addr) ((addr) & 0xFFF)

// Legacy macros for compatibility (map to new names)
#define PAGE_DIRECTORY_INDEX(addr) PD_INDEX(addr)
#define PAGE_TABLE_INDEX(addr)     PT_INDEX(addr)

// Page flags
#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITETHROUGH   (1ULL << 3)
#define PAGE_CACHE_DISABLE  (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_HUGE           (1ULL << 7)   // 2MB or 1GB pages
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NX             (1ULL << 63)  // No-execute bit

// Page table entry (64-bit)
typedef struct {
    uint64_t present    : 1;   // Present in memory
    uint64_t rw         : 1;   // Read/Write
    uint64_t user       : 1;   // User/Supervisor
    uint64_t pwt        : 1;   // Page Write-Through
    uint64_t pcd        : 1;   // Page Cache Disable
    uint64_t accessed   : 1;   // Accessed
    uint64_t dirty      : 1;   // Dirty (written to)
    uint64_t pat        : 1;   // Page Attribute Table
    uint64_t global     : 1;   // Global page
    uint64_t available1 : 3;   // Available for OS use
    uint64_t frame      : 40;  // Physical frame address (bits 12-51)
    uint64_t available2 : 11;  // Available for OS use
    uint64_t nx         : 1;   // No-execute
} __attribute__((packed)) page_table_entry_t;

// Page table (512 entries)
typedef struct {
    page_table_entry_t entries[512];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

// All levels use the same structure in x86_64
typedef page_table_t page_directory_t;
typedef page_table_t page_directory_pointer_t;
typedef page_table_t pml4_t;

// For compatibility with existing code
typedef struct {
    page_table_entry_t tables[1024];  // Legacy structure (will be replaced)
} old_page_directory_t;

// Functions
void paging_init(void);
void switch_page_directory(page_directory_t* pml4);

// Map a virtual address to physical address
void map_page(page_directory_t* pml4, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
void unmap_page(page_directory_t* pml4, uint64_t virtual_addr);

// Get physical address from virtual
uint64_t get_physical_address(page_directory_t* pml4, uint64_t virtual_addr);

// Kernel page directory
page_directory_t* get_kernel_page_dir(void);

// Virtual memory allocator
void* kmalloc_virtual(size_t size);
void kfree_virtual(void* ptr, size_t size);

// Map physical device memory to virtual space
void* physical_to_virtual(uint64_t physical_addr, size_t size);

// Page fault handler
void page_fault_handler(uint64_t error_code);

// Kernel heap
void kernel_heap_init(void);

// Statistics
void paging_get_stats(uint64_t* total_virtual, uint64_t* used_virtual, 
                      uint64_t* total_physical, uint64_t* used_physical);

// Inline assembly helpers
static inline void invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline void load_cr3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline uint64_t read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

// Check if address is canonical (bits 48-63 must match bit 47)
static inline bool is_canonical(uint64_t addr) {
    uint64_t bit47 = (addr >> 47) & 1;
    uint64_t upper_bits = addr >> 48;
    
    if (bit47) {
        return upper_bits == 0xFFFF;  // Should be all 1s
    } else {
        return upper_bits == 0x0000;  // Should be all 0s
    }
}

#endif // PAGING_H
