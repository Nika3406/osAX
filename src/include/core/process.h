// src/include/core/process.h - x86_64 VERSION
#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "../memory/paging.h"

#define MAX_PROCESSES 256

// Process states
typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

// CPU context saved during context switch (64-bit)
typedef struct {
    // General purpose registers (64-bit)
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    
    // Extended registers (new in x86_64)
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    
    // Special registers
    uint64_t rip;           // Instruction pointer
    uint64_t rflags;        // Flags register
    uint64_t cr3;           // Page directory (PML4 address)
    
    // Segment registers (still 16-bit but stored in 64-bit for alignment)
    uint64_t cs, ds, es, fs, gs, ss;
} cpu_context_t;

// Process Control Block
typedef struct process {
    uint32_t pid;                    // Process ID
    char name[32];                   // Process name
    process_state_t state;           // Current state

    cpu_context_t context;           // Saved CPU context
    page_directory_t* page_dir;      // Process page directory (PML4)

    uint64_t kernel_stack;           // Kernel stack pointer (64-bit)
    uint64_t user_stack;             // User stack pointer (64-bit)

    uint32_t priority;               // Scheduling priority
    uint32_t time_slice;             // Remaining time slice

    struct process* next;            // Next process in queue
} process_t;

// Function declarations
void process_init(void);
process_t* process_create(const char* name, void (*entry_point)(void), uint32_t is_kernel);
void process_destroy(process_t* proc);
void process_switch(process_t* next);
process_t* process_get_current(void);
void scheduler_init(void);
void schedule(void);
void yield(void);

// Test functions
void test_process_1(void);
void test_process_2(void);

#endif // PROCESS_H
