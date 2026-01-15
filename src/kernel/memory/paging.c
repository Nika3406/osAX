// src/kernel/paging.c - FIXED VERSION - Extended identity mapping
#include "types.h"
#include "paging.h"
#include "physical_mm.h"
#include "kstring.h"
#include "memory.h"
#include "serial.h"

extern uint32_t framebuffer_address;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;

// Global kernel page directory
static page_directory_t* kernel_page_dir = 0;
static page_table_t* kernel_page_tables[1024] = {0};

// Current page directory
page_directory_t* current_directory = 0;

// Kernel heap manager with free list support
typedef struct free_region {
    uint32_t start;
    uint32_t size;
    struct free_region* next;
} free_region_t;

static uint32_t kernel_heap_next = KERNEL_HEAP_START;
static free_region_t* free_list = NULL;

// Allocate a small pool for free list nodes (stored separately from freed memory)
#define MAX_FREE_REGIONS 64
static free_region_t free_region_pool[MAX_FREE_REGIONS];
static uint32_t free_region_pool_used = 0;

extern void load_page_directory(uint32_t);
extern void enable_paging_asm(void);

// Helper: Get physical address from virtual address
static uint32_t get_physical_address(page_directory_t* dir, uint32_t virtual_addr) {
    (void)dir;
    uint32_t page_dir_idx = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t page_tbl_idx = PAGE_TABLE_INDEX(virtual_addr);

    page_table_t* table = kernel_page_tables[page_dir_idx];
    if (!table || !table->pages[page_tbl_idx].present) {
        return 0;  // Not mapped
    }

    return (table->pages[page_tbl_idx].frame << 12) | (virtual_addr & 0xFFF);
}

void switch_page_directory(page_directory_t* dir) {
    current_directory = dir;
    uintptr_t dir_physical = (uintptr_t)&dir->tables;
    load_page_directory((uint32_t)dir_physical);
}

static void paging_map_framebuffer(void) {
    // CRITICAL: Validate framebuffer address before mapping
    if (framebuffer_address == 0 || 
        framebuffer_address == 0xFFFFFFFF ||
        framebuffer_address == 0xB8000) {
        kprintf("PAGING: No graphics framebuffer (using VGA text mode)\n");
        return;
    }
    
    // Additional validation: check if address is in valid range
    if (framebuffer_address < 0xA0000 || framebuffer_address > 0xFFFFFFFF) {
        kprintf("PAGING: Invalid framebuffer address 0x%08x\n", framebuffer_address);
        return;
    }
    
    kprintf("PAGING: Mapping framebuffer at 0x%08x...\n", framebuffer_address);
    
    // Calculate size
    uint32_t fb_size = framebuffer_pitch * framebuffer_height;
    uint32_t fb_pages = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    kprintf("PAGING: Framebuffer: %dx%d, pitch=%d, size=%d KB (%d pages)\n",
            framebuffer_width, framebuffer_height, framebuffer_pitch,
            fb_size / 1024, fb_pages);
    
    // Align to page boundary
    uint32_t fb_start = framebuffer_address & ~0xFFF;
    
    // Identity map all framebuffer pages
    for (uint32_t i = 0; i < fb_pages; i++) {
        uint32_t phys_addr = fb_start + (i * PAGE_SIZE);
        uint32_t virt_addr = phys_addr;  // Identity mapping
        
        map_page(kernel_page_dir, virt_addr, phys_addr, PAGE_WRITABLE);
    }
    
    kprintf("PAGING: Framebuffer mapped successfully\n");
}

// In paging_init(), add the call:
void paging_init(void) {
    kprintf("PAGING: Initializing virtual memory system...\n");

    // Create kernel page directory
    kernel_page_dir = (page_directory_t*)alloc_page();
    memset(kernel_page_dir, 0, sizeof(page_directory_t));

    // Identity map first 32MB (8 page tables)
    kprintf("PAGING: Creating identity mapping for first 32MB...\n");

    for (uint32_t i = 0; i < 8; i++) {
        page_table_t* table = (page_table_t*)alloc_page();
        memset(table, 0, sizeof(page_table_t));

        for (uint32_t j = 0; j < 1024; j++) {
            uint32_t physical_addr = (i * 1024 + j) * PAGE_SIZE;
            table->pages[j].present = 1;
            table->pages[j].rw = 1;
            table->pages[j].user = 0;
            table->pages[j].frame = physical_addr >> 12;
        }

        kernel_page_dir->tables[i].present = 1;
        kernel_page_dir->tables[i].rw = 1;
        kernel_page_dir->tables[i].user = 0;
        kernel_page_dir->tables[i].table_addr = ((uintptr_t)table) >> 12;

        kernel_page_tables[i] = table;
    }

    kprintf("PAGING: Mapping kernel to higher half (0xC0000000)...\n");

    // Map kernel to higher half
    uint32_t kernel_physical_start = 0x100000;
    uint32_t kernel_virtual_start = KERNEL_VIRTUAL_BASE;
    uint32_t kernel_size_pages = (0x400000 + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = 0; i < kernel_size_pages; i++) {
        uint32_t virtual_addr = kernel_virtual_start + i * PAGE_SIZE;
        uint32_t physical_addr = kernel_physical_start + i * PAGE_SIZE;
        map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
    }

    kprintf("PAGING: Mapping VGA buffer...\n");
    map_page(kernel_page_dir, 0xC00B8000, 0xB8000, PAGE_WRITABLE);

    // NEW: Map framebuffer (if present)
    paging_map_framebuffer();

    kprintf("PAGING: Enabling paging...\n");
    switch_page_directory(kernel_page_dir);

    enable_paging_asm();

    kprintf("PAGING: Virtual memory enabled successfully!\n");
}

void map_page(page_directory_t* dir, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t page_dir_idx = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t page_tbl_idx = PAGE_TABLE_INDEX(virtual_addr);

    page_table_t* table = kernel_page_tables[page_dir_idx];
    if (!table) {
        table = (page_table_t*)alloc_page();
        memset(table, 0, sizeof(page_table_t));

        dir->tables[page_dir_idx].present = 1;
        dir->tables[page_dir_idx].rw = (flags & PAGE_WRITABLE) ? 1 : 0;
        dir->tables[page_dir_idx].user = (flags & PAGE_USER) ? 1 : 0;
        dir->tables[page_dir_idx].table_addr = ((uintptr_t)table) >> 12;

        kernel_page_tables[page_dir_idx] = table;
    }

    table->pages[page_tbl_idx].present = 1;
    table->pages[page_tbl_idx].rw = (flags & PAGE_WRITABLE) ? 1 : 0;
    table->pages[page_tbl_idx].user = (flags & PAGE_USER) ? 1 : 0;
    table->pages[page_tbl_idx].frame = physical_addr >> 12;

    __asm__ volatile ("invlpg (%0)" : : "r" (virtual_addr));
}

void unmap_page(page_directory_t* dir, uint32_t virtual_addr) {
    (void)dir;
    uint32_t page_dir_idx = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t page_tbl_idx = PAGE_TABLE_INDEX(virtual_addr);

    page_table_t* table = kernel_page_tables[page_dir_idx];
    if (table) {
        table->pages[page_tbl_idx].present = 0;
        __asm__ volatile ("invlpg (%0)" : : "r" (virtual_addr));
    }
}

page_directory_t* get_kernel_page_dir(void) {
    return kernel_page_dir;
}

// Allocate a free region node from the pool
static free_region_t* alloc_free_region_node(void) {
    if (free_region_pool_used >= MAX_FREE_REGIONS) {
        kprintf("WARNING: Free region pool exhausted!\n");
        return NULL;
    }
    return &free_region_pool[free_region_pool_used++];
}

void* kmalloc_virtual(uint32_t size) {
    uint32_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t total_size = pages_needed * PAGE_SIZE;

    kprintf("KMALLOC_VIRTUAL: Allocating %d bytes (%d pages)\n", size, pages_needed);
    
    // Try to find space in free list first
    free_region_t** current = &free_list;
    while (*current) {
        if ((*current)->size >= total_size) {
            uint32_t virtual_start = (*current)->start;

            // Remove from free list or shrink
            if ((*current)->size == total_size) {
                *current = (*current)->next;
            } else {
                (*current)->start += total_size;
                (*current)->size -= total_size;
            }

            // Allocate physical pages and map
            uintptr_t physical = (uintptr_t)alloc_pages(pages_needed);
            if (!physical) return NULL;

            for (uint32_t i = 0; i < pages_needed; i++) {
                uint32_t virtual_addr = virtual_start + i * PAGE_SIZE;
                uint32_t physical_addr = (uint32_t)(physical + i * PAGE_SIZE);
                map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
            }

            return (void*)(uintptr_t)virtual_start;
        }
        current = &(*current)->next;
    }

    // No suitable free region, allocate from heap end
    if (kernel_heap_next + total_size > KERNEL_HEAP_END) {
        kprintf("KMALLOC: Out of kernel heap space!\n");
        return NULL;
    }

    uintptr_t physical = (uintptr_t)alloc_pages(pages_needed);
    if (!physical) {
        kprintf("KMALLOC: Out of physical memory!\n");
        return NULL;
    }

    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t virtual_addr = kernel_heap_next + i * PAGE_SIZE;
        uint32_t physical_addr = (uint32_t)(physical + i * PAGE_SIZE);
        map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
        
        // Debug: print first few mappings
        if (i < 3 || i == pages_needed - 1) {
            kprintf("  Mapped page %d: virt=0x%08x -> phys=0x%08x\n", 
                    i, virtual_addr, physical_addr);
        }
    }
    
    void* result = (void*)(uintptr_t)kernel_heap_next;
    kernel_heap_next += total_size;
    
    kprintf("KMALLOC_VIRTUAL: Returning 0x%08x\n", (uint32_t)result);

    return result;
}

void kfree_virtual(void* ptr, uint32_t size) {
    if (!ptr) return;

    uint32_t pages_freed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t virtual_start = (uint32_t)(uintptr_t)ptr;

    // Free physical pages FIRST, then unmap
    for (uint32_t i = 0; i < pages_freed; i++) {
        uint32_t virtual_addr = virtual_start + i * PAGE_SIZE;
        uint32_t physical_addr = get_physical_address(kernel_page_dir, virtual_addr);

        if (physical_addr) {
            free_page((void*)(uintptr_t)(physical_addr & 0xFFFFF000));
        }
    }

    // Now unmap the pages
    for (uint32_t i = 0; i < pages_freed; i++) {
        uint32_t virtual_addr = virtual_start + i * PAGE_SIZE;
        unmap_page(kernel_page_dir, virtual_addr);
    }

    // Add to free list using separate pool
    free_region_t* region = alloc_free_region_node();
    if (region) {
        region->start = virtual_start;
        region->size = pages_freed * PAGE_SIZE;
        region->next = free_list;
        free_list = region;
    }
}

void* physical_to_virtual(uint32_t physical_addr) {
    static uint32_t device_virtual_next = 0xD0000000;

    uint32_t virtual_addr = device_virtual_next;
    device_virtual_next += PAGE_SIZE;

    map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
    return (void*)(uintptr_t)virtual_addr;
}

void page_fault_handler(uint32_t error_code) {
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

    kprintf("\n!!! PAGE FAULT !!!\n");
    kprintf("Faulting address: %x\n", faulting_address);
    kprintf("Error code: %x\n", error_code);
    kprintf("  Present: %d\n", error_code & 0x1);
    kprintf("  Write: %d\n", (error_code & 0x2) >> 1);
    kprintf("  User: %d\n", (error_code & 0x4) >> 2);
    kprintf("  Reserved: %d\n", (error_code & 0x8) >> 3);
    kprintf("  Instruction fetch: %d\n", (error_code & 0x10) >> 4);

    for(;;) __asm__ volatile("cli; hlt");
}

void kernel_heap_init(void) {
    kernel_heap_next = KERNEL_HEAP_START;
    free_list = NULL;
    free_region_pool_used = 0;
    kprintf("HEAP: Kernel heap initialized at %x\n", KERNEL_HEAP_START);
}

void paging_get_stats(uint32_t* total_virtual, uint32_t* used_virtual, uint32_t* total_physical, uint32_t* used_physical) {
    if (total_virtual) *total_virtual = USER_SPACE_END - USER_SPACE_START;
    if (used_virtual) *used_virtual = kernel_heap_next - KERNEL_HEAP_START;
    if (total_physical) *total_physical = get_total_memory();
    if (used_physical) *used_physical = get_used_memory();
}
