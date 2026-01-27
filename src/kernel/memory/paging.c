// src/kernel/memory/paging.c - x86_64 VERSION (4-level paging)
#include "types.h"
#include "paging.h"
#include "physical_mm.h"
#include "kstring.h"
#include "memory.h"
#include "serial.h"

extern uint64_t framebuffer_address;
extern uint64_t framebuffer_width;
extern uint64_t framebuffer_height;
extern uint64_t framebuffer_pitch;

// Global kernel page directory (PML4)
static page_directory_t* kernel_page_dir = 0;

// Page tables for each level (we'll allocate as needed)
static page_table_t* kernel_page_tables[512][512] = {{0}};  // PDP and PD levels

// Current page directory
page_directory_t* current_directory = 0;

// Kernel heap manager with free list support
typedef struct free_region {
    uint64_t start;
    uint64_t size;
    struct free_region* next;
} free_region_t;

static uint64_t kernel_heap_next = KERNEL_HEAP_START;
static free_region_t* free_list = NULL;

// Allocate a small pool for free list nodes
#define MAX_FREE_REGIONS 64
static free_region_t free_region_pool[MAX_FREE_REGIONS];
static uint32_t free_region_pool_used = 0;

extern void load_page_directory(uint64_t);
extern void enable_paging_asm(void);

// Helper: Get physical address from virtual address
uint64_t get_physical_address(page_directory_t* pml4, uint64_t virtual_addr) {
    (void)pml4;  // We use kernel_page_dir for now
    
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);
    uint64_t pdp_idx = PDP_INDEX(virtual_addr);
    uint64_t pd_idx = PD_INDEX(virtual_addr);
    uint64_t pt_idx = PT_INDEX(virtual_addr);

    // Check PML4 entry
    if (!kernel_page_dir->entries[pml4_idx].present) {
        return 0;
    }

    // Get PDP table
    page_table_t* pdp = (page_table_t*)(kernel_page_dir->entries[pml4_idx].frame << 12);
    if (!pdp->entries[pdp_idx].present) {
        return 0;
    }

    // Get PD table
    page_table_t* pd = (page_table_t*)(pdp->entries[pdp_idx].frame << 12);
    if (!pd->entries[pd_idx].present) {
        return 0;
    }

    // Get PT table
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx].frame << 12);
    if (!pt->entries[pt_idx].present) {
        return 0;
    }

    return (pt->entries[pt_idx].frame << 12) | (virtual_addr & 0xFFF);
}

void switch_page_directory(page_directory_t* pml4) {
    current_directory = pml4;
    uint64_t pml4_physical = (uint64_t)pml4;
    load_page_directory(pml4_physical);
}

static void paging_map_framebuffer(void) {
    // CRITICAL: Validate framebuffer address before mapping
    if (framebuffer_address == 0 || 
        framebuffer_address == 0xFFFFFFFFFFFFFFFFULL ||
        framebuffer_address == 0xB8000) {
        kprintf("PAGING: No graphics framebuffer (using VGA text mode)\n");
        return;
    }
    
    kprintf("PAGING: Mapping framebuffer at 0x%llx...\n", framebuffer_address);
    
    // Calculate size
    uint64_t fb_size = framebuffer_pitch * framebuffer_height;
    uint64_t fb_pages = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    kprintf("PAGING: Framebuffer: %lldx%lld, pitch=%lld, size=%lld KB (%lld pages)\n",
            framebuffer_width, framebuffer_height, framebuffer_pitch,
            fb_size / 1024, fb_pages);
    
    // Align to page boundary
    uint64_t fb_start = framebuffer_address & ~0xFFF;
    
    // Identity map all framebuffer pages
    for (uint64_t i = 0; i < fb_pages; i++) {
        uint64_t phys_addr = fb_start + (i * PAGE_SIZE);
        uint64_t virt_addr = phys_addr;  // Identity mapping
        
        map_page(kernel_page_dir, virt_addr, phys_addr, PAGE_WRITABLE);
    }
    
    kprintf("PAGING: Framebuffer mapped successfully\n");
}

void paging_init(void) {
    kprintf("PAGING: Initializing x86_64 4-level paging...\n");

    // Create kernel PML4 (top-level page directory)
    kernel_page_dir = (page_directory_t*)alloc_page();
    memset(kernel_page_dir, 0, sizeof(page_directory_t));

    // Identity map first 32MB using 2MB huge pages for simplicity
    kprintf("PAGING: Creating identity mapping for first 32MB...\n");

    // We need: PML4[0] -> PDP[0] -> PD[0..15] with 2MB pages
    // For 32MB: 16 x 2MB pages
    
    // Allocate PDP for first PML4 entry
    page_table_t* pdp = (page_table_t*)alloc_page();
    memset(pdp, 0, sizeof(page_table_t));
    
    kernel_page_dir->entries[0].present = 1;
    kernel_page_dir->entries[0].rw = 1;
    kernel_page_dir->entries[0].user = 0;
    kernel_page_dir->entries[0].frame = ((uint64_t)pdp) >> 12;
    
    // Allocate PD for first PDP entry
    page_table_t* pd = (page_table_t*)alloc_page();
    memset(pd, 0, sizeof(page_table_t));
    
    pdp->entries[0].present = 1;
    pdp->entries[0].rw = 1;
    pdp->entries[0].user = 0;
    pdp->entries[0].frame = ((uint64_t)pd) >> 12;
    
    // Create 16 x 2MB huge pages (32MB total)
    for (uint32_t i = 0; i < 16; i++) {
        pd->entries[i].present = 1;
        pd->entries[i].rw = 1;
        pd->entries[i].user = 0;
        pd->entries[i].pat = 1;  // Use as huge page bit
        pd->entries[i].frame = (i * 0x200000) >> 12;  // 2MB increments
    }

    kprintf("PAGING: Identity mapping complete (using 2MB pages)\n");
    
    // Map VGA buffer
    kprintf("PAGING: Mapping VGA buffer...\n");
    map_page(kernel_page_dir, 0xB8000, 0xB8000, PAGE_WRITABLE);

    // Map framebuffer (if present)
    paging_map_framebuffer();

    kprintf("PAGING: Enabling paging...\n");
    switch_page_directory(kernel_page_dir);

    enable_paging_asm();

    kprintf("PAGING: Virtual memory enabled successfully!\n");
}

void map_page(page_directory_t* pml4, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags) {
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);
    uint64_t pdp_idx = PDP_INDEX(virtual_addr);
    uint64_t pd_idx = PD_INDEX(virtual_addr);
    uint64_t pt_idx = PT_INDEX(virtual_addr);

    // Ensure PML4 entry exists
    if (!pml4->entries[pml4_idx].present) {
        page_table_t* pdp = (page_table_t*)alloc_page();
        memset(pdp, 0, sizeof(page_table_t));
        
        pml4->entries[pml4_idx].present = 1;
        pml4->entries[pml4_idx].rw = 1;
        pml4->entries[pml4_idx].user = (flags & PAGE_USER) ? 1 : 0;
        pml4->entries[pml4_idx].frame = ((uint64_t)pdp) >> 12;
    }

    // Get PDP
    page_table_t* pdp = (page_table_t*)(pml4->entries[pml4_idx].frame << 12);

    // Ensure PDP entry exists
    if (!pdp->entries[pdp_idx].present) {
        page_table_t* pd = (page_table_t*)alloc_page();
        memset(pd, 0, sizeof(page_table_t));
        
        pdp->entries[pdp_idx].present = 1;
        pdp->entries[pdp_idx].rw = 1;
        pdp->entries[pdp_idx].user = (flags & PAGE_USER) ? 1 : 0;
        pdp->entries[pdp_idx].frame = ((uint64_t)pd) >> 12;
    }

    // Get PD
    page_table_t* pd = (page_table_t*)(pdp->entries[pdp_idx].frame << 12);

    // Ensure PD entry exists
    if (!pd->entries[pd_idx].present) {
        page_table_t* pt = (page_table_t*)alloc_page();
        memset(pt, 0, sizeof(page_table_t));
        
        pd->entries[pd_idx].present = 1;
        pd->entries[pd_idx].rw = 1;
        pd->entries[pd_idx].user = (flags & PAGE_USER) ? 1 : 0;
        pd->entries[pd_idx].frame = ((uint64_t)pt) >> 12;
    }

    // Get PT and set final mapping
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx].frame << 12);
    
    pt->entries[pt_idx].present = 1;
    pt->entries[pt_idx].rw = (flags & PAGE_WRITABLE) ? 1 : 0;
    pt->entries[pt_idx].user = (flags & PAGE_USER) ? 1 : 0;
    pt->entries[pt_idx].frame = physical_addr >> 12;

    invlpg(virtual_addr);
}

void unmap_page(page_directory_t* pml4, uint64_t virtual_addr) {
    (void)pml4;
    uint64_t pt_idx = PT_INDEX(virtual_addr);
    uint64_t pd_idx = PD_INDEX(virtual_addr);
    uint64_t pdp_idx = PDP_INDEX(virtual_addr);
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);

    if (!kernel_page_dir->entries[pml4_idx].present) return;
    
    page_table_t* pdp = (page_table_t*)(kernel_page_dir->entries[pml4_idx].frame << 12);
    if (!pdp->entries[pdp_idx].present) return;
    
    page_table_t* pd = (page_table_t*)(pdp->entries[pdp_idx].frame << 12);
    if (!pd->entries[pd_idx].present) return;
    
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx].frame << 12);
    pt->entries[pt_idx].present = 0;
    
    invlpg(virtual_addr);
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

void* kmalloc_virtual(size_t size) {
    uint64_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t total_size = pages_needed * PAGE_SIZE;

    kprintf("KMALLOC_VIRTUAL: Allocating %lld bytes (%lld pages)\n", (uint64_t)size, pages_needed);
    
    // Try to find space in free list first
    free_region_t** current = &free_list;
    while (*current) {
        if ((*current)->size >= total_size) {
            uint64_t virtual_start = (*current)->start;

            // Remove from free list or shrink
            if ((*current)->size == total_size) {
                *current = (*current)->next;
            } else {
                (*current)->start += total_size;
                (*current)->size -= total_size;
            }

            // Allocate physical pages and map
            void* physical = alloc_pages(pages_needed);
            if (!physical) return NULL;

            for (uint64_t i = 0; i < pages_needed; i++) {
                uint64_t virtual_addr = virtual_start + i * PAGE_SIZE;
                uint64_t physical_addr = (uint64_t)physical + i * PAGE_SIZE;
                map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
            }

            return (void*)virtual_start;
        }
        current = &(*current)->next;
    }

    // No suitable free region, allocate from heap end
    if (kernel_heap_next + total_size > KERNEL_HEAP_END) {
        kprintf("KMALLOC: Out of kernel heap space!\n");
        return NULL;
    }

    void* physical = alloc_pages(pages_needed);
    if (!physical) {
        kprintf("KMALLOC: Out of physical memory!\n");
        return NULL;
    }

    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t virtual_addr = kernel_heap_next + i * PAGE_SIZE;
        uint64_t physical_addr = (uint64_t)physical + i * PAGE_SIZE;
        map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
    }
    
    void* result = (void*)kernel_heap_next;
    kernel_heap_next += total_size;
    
    kprintf("KMALLOC_VIRTUAL: Returning 0x%llx\n", (uint64_t)result);

    return result;
}

void kfree_virtual(void* ptr, size_t size) {
    if (!ptr) return;

    uint64_t pages_freed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t virtual_start = (uint64_t)ptr;

    // Free physical pages FIRST, then unmap
    for (uint64_t i = 0; i < pages_freed; i++) {
        uint64_t virtual_addr = virtual_start + i * PAGE_SIZE;
        uint64_t physical_addr = get_physical_address(kernel_page_dir, virtual_addr);

        if (physical_addr) {
            free_page((void*)(physical_addr & 0xFFFFFFFFFFFFF000ULL));
        }
    }

    // Now unmap the pages
    for (uint64_t i = 0; i < pages_freed; i++) {
        uint64_t virtual_addr = virtual_start + i * PAGE_SIZE;
        unmap_page(kernel_page_dir, virtual_addr);
    }

    // Add to free list
    free_region_t* region = alloc_free_region_node();
    if (region) {
        region->start = virtual_start;
        region->size = pages_freed * PAGE_SIZE;
        region->next = free_list;
        free_list = region;
    }
}

void* physical_to_virtual(uint64_t physical_addr, size_t size) {
    (void)size;  // Not used in simple implementation
    static uint64_t device_virtual_next = 0xFFFFFF8000000000ULL;  // High address for devices

    uint64_t virtual_addr = device_virtual_next;
    device_virtual_next += PAGE_SIZE;

    map_page(kernel_page_dir, virtual_addr, physical_addr, PAGE_WRITABLE);
    return (void*)virtual_addr;
}

void page_fault_handler(uint64_t error_code) {
    uint64_t faulting_address = read_cr2();

    kprintf("\n!!! PAGE FAULT !!!\n");
    kprintf("Faulting address: 0x%llx\n", faulting_address);
    kprintf("Error code: 0x%llx\n", error_code);
    kprintf("  Present: %d\n", (int)(error_code & 0x1));
    kprintf("  Write: %d\n", (int)((error_code & 0x2) >> 1));
    kprintf("  User: %d\n", (int)((error_code & 0x4) >> 2));
    kprintf("  Reserved: %d\n", (int)((error_code & 0x8) >> 3));
    kprintf("  Instruction fetch: %d\n", (int)((error_code & 0x10) >> 4));

    for(;;) __asm__ volatile("cli; hlt");
}

void kernel_heap_init(void) {
    kernel_heap_next = KERNEL_HEAP_START;
    free_list = NULL;
    free_region_pool_used = 0;
    kprintf("HEAP: Kernel heap initialized at 0x%llx\n", KERNEL_HEAP_START);
}

void paging_get_stats(uint64_t* total_virtual, uint64_t* used_virtual, 
                      uint64_t* total_physical, uint64_t* used_physical) {
    if (total_virtual) *total_virtual = KERNEL_HEAP_END - KERNEL_HEAP_START;
    if (used_virtual) *used_virtual = kernel_heap_next - KERNEL_HEAP_START;
    if (total_physical) *total_physical = get_total_memory();
    if (used_physical) *used_physical = get_used_memory();
}
