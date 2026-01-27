// src/kernel/core/process.c - x86_64 VERSION
#include "process.h"
#include "heap.h"
#include "kstring.h"
#include "serial.h"
#include "paging.h"

static process_t* process_list = NULL;
static process_t* current_process = NULL;
static uint32_t next_pid = 1;

// Process queue management
static process_t* ready_queue = NULL;

void process_init(void) {
    kprintf("PROCESS: Initializing process management...\n");

    // Create idle process (kernel process that runs when nothing else is ready)
    process_list = NULL;
    current_process = NULL;
    next_pid = 1;

    kprintf("PROCESS: Process management initialized\n");
}

process_t* process_create(const char* name, void (*entry_point)(void), uint32_t is_kernel) {
    kprintf("PROCESS: Creating process '%s'...\n", name);

    // Allocate PCB
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (!proc) {
        kprintf("PROCESS: Failed to allocate PCB\n");
        return NULL;
    }

    memset(proc, 0, sizeof(process_t));

    // Initialize basic fields
    proc->pid = next_pid++;
    for (int i = 0; i < 32 && name[i]; i++) {
        proc->name[i] = name[i];
    }
    proc->state = PROCESS_READY;
    proc->priority = 10;
    proc->time_slice = 10;

    // Create page directory
    if (!is_kernel) {
        proc->page_dir = (page_directory_t*)kmalloc_virtual(sizeof(page_directory_t));
        if (!proc->page_dir) {
            kprintf("PROCESS: Failed to allocate page directory\n");
            kfree(proc);
            return NULL;
        }

        // Copy kernel mappings to new page directory
        memcpy(proc->page_dir, get_kernel_page_dir(), sizeof(page_directory_t));
    } else {
        proc->page_dir = get_kernel_page_dir();
    }

    // Allocate kernel stack (8KB) - CHANGED to uint64_t
    proc->kernel_stack = (uint64_t)kmalloc_virtual(8192);
    if (!proc->kernel_stack) {
        kprintf("PROCESS: Failed to allocate kernel stack\n");
        if (!is_kernel) kfree_virtual(proc->page_dir, sizeof(page_directory_t));
        kfree(proc);
        return NULL;
    }
    proc->kernel_stack += 8192;  // Stack grows down

    // Allocate user stack (8KB) if user mode - CHANGED to uint64_t
    if (!is_kernel) {
        proc->user_stack = (uint64_t)kmalloc_virtual(8192);
        if (!proc->user_stack) {
            kprintf("PROCESS: Failed to allocate user stack\n");
            kfree_virtual((void*)(proc->kernel_stack - 8192), 8192);
            kfree_virtual(proc->page_dir, sizeof(page_directory_t));
            kfree(proc);
            return NULL;
        }
        proc->user_stack += 8192;  // Stack grows down
    }

    // Initialize context - CHANGED for 64-bit registers
    memset(&proc->context, 0, sizeof(cpu_context_t));
    proc->context.rip = (uint64_t)entry_point;      // CHANGED: eip -> rip
    proc->context.rsp = is_kernel ? proc->kernel_stack : proc->user_stack;  // CHANGED: esp -> rsp
    proc->context.rbp = proc->context.rsp;          // CHANGED: ebp -> rbp
    proc->context.rflags = 0x202;                   // CHANGED: eflags -> rflags (IF enabled)
    proc->context.cr3 = (uint64_t)proc->page_dir;   // CHANGED: 64-bit

    // Add to process list
    proc->next = process_list;
    process_list = proc;

    kprintf("PROCESS: Created process PID=%d '%s' at %p\n", proc->pid, proc->name, proc);

    return proc;
}

void process_destroy(process_t* proc) {
    if (!proc) return;

    kprintf("PROCESS: Destroying process PID=%d '%s'\n", proc->pid, proc->name);

    // Free stacks
    if (proc->kernel_stack) {
        kfree_virtual((void*)(proc->kernel_stack - 8192), 8192);
    }
    if (proc->user_stack) {
        kfree_virtual((void*)(proc->user_stack - 8192), 8192);
    }

    // Free page directory (if not kernel)
    if (proc->page_dir != get_kernel_page_dir()) {
        kfree_virtual(proc->page_dir, sizeof(page_directory_t));
    }

    // Remove from process list
    process_t** current = &process_list;
    while (*current) {
        if (*current == proc) {
            *current = proc->next;
            break;
        }
        current = &(*current)->next;
    }

    // Free PCB
    kfree(proc);
}

// Context switch assembly (simplified - you'd implement this in assembly)
extern void context_switch(cpu_context_t* old_context, cpu_context_t* new_context);

void process_switch(process_t* next) {
    if (!next || next == current_process) return;

    process_t* old = current_process;
    current_process = next;

    kprintf("PROCESS: Switching from PID=%d to PID=%d\n",
            old ? old->pid : 0, next->pid);

    // Update states
    if (old) old->state = PROCESS_READY;
    next->state = PROCESS_RUNNING;

    // Switch page directory
    switch_page_directory(next->page_dir);

    // Context switch (would be in assembly)
    // For now, just update RIP and continue
    // In real implementation, save/restore all registers
}

process_t* process_get_current(void) {
    return current_process;
}

// Simple round-robin scheduler
void scheduler_init(void) {
    kprintf("SCHEDULER: Initializing round-robin scheduler...\n");
    ready_queue = NULL;
}

void schedule(void) {
    // Find next ready process
    process_t* next = NULL;
    process_t* current = process_list;

    while (current) {
        if (current->state == PROCESS_READY) {
            next = current;
            break;
        }
        current = current->next;
    }

    if (next) {
        process_switch(next);
    }
}

void yield(void) {
    // Give up CPU voluntarily
    if (current_process) {
        current_process->state = PROCESS_READY;
        schedule();
    }
}

// Test process functions
void test_process_1(void) {
    kprintf("TEST_PROCESS_1: Starting (PID=%d)...\n",
            current_process ? current_process->pid : 0);

    for (int i = 0; i < 5; i++) {
        kprintf("TEST_PROCESS_1: Iteration %d\n", i);

        // Simulate work
        for (volatile int j = 0; j < 1000000; j++);
    }

    kprintf("TEST_PROCESS_1: Exiting\n");
}

void test_process_2(void) {
    kprintf("TEST_PROCESS_2: Starting (PID=%d)...\n",
            current_process ? current_process->pid : 0);

    for (int i = 0; i < 5; i++) {
        kprintf("TEST_PROCESS_2: Count %d\n", i);

        // Simulate work
        for (volatile int j = 0; j < 1000000; j++);
    }

    kprintf("TEST_PROCESS_2: Exiting\n");
}
