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

// CPU context saved during context switch
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, esp, ebp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cr3;  // Page directory
} cpu_context_t;

// Process Control Block
typedef struct process {
    uint32_t pid;                    // Process ID
    char name[32];                   // Process name
    process_state_t state;           // Current state

    cpu_context_t context;           // Saved CPU context
    page_directory_t* page_dir;      // Process page directory

    uint32_t kernel_stack;           // Kernel stack pointer
    uint32_t user_stack;             // User stack pointer

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

#endif
