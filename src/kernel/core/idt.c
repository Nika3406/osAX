// src/kernel/core/idt.c - x86_64 VERSION
#include "idt.h"
#include "kstring.h"
#include "serial.h"
#include "paging.h"

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

// Set an IDT gate - CHANGED for 64-bit
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel;
    idt[num].ist = 0;  // Don't use IST
    idt[num].type_attr = flags | IDT_FLAG_PRESENT;
    idt[num].reserved = 0;
    idt[num].selector = KERNEL_CS;
}

void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m" (idt_ptr));
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base = (uint64_t)&idt;  // CHANGED: 64-bit base

    // Clear IDT
    memset(&idt, 0, sizeof(idt_entry_t) * IDT_ENTRIES);

    // Set up exception handlers (0-31)
    // CHANGED: Cast to uint64_t
    idt_set_gate(0, (uint64_t)isr0, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(1, (uint64_t)isr1, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(2, (uint64_t)isr2, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(3, (uint64_t)isr3, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(4, (uint64_t)isr4, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(5, (uint64_t)isr5, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(6, (uint64_t)isr6, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(7, (uint64_t)isr7, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(8, (uint64_t)isr8, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(9, (uint64_t)isr9, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(10, (uint64_t)isr10, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(11, (uint64_t)isr11, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(12, (uint64_t)isr12, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(13, (uint64_t)isr13, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(14, (uint64_t)isr14, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);  // Page fault
    idt_set_gate(15, (uint64_t)isr15, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(16, (uint64_t)isr16, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(17, (uint64_t)isr17, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(18, (uint64_t)isr18, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(19, (uint64_t)isr19, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(20, (uint64_t)isr20, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(21, (uint64_t)isr21, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(22, (uint64_t)isr22, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(23, (uint64_t)isr23, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(24, (uint64_t)isr24, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(25, (uint64_t)isr25, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(26, (uint64_t)isr26, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(27, (uint64_t)isr27, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(28, (uint64_t)isr28, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(29, (uint64_t)isr29, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(30, (uint64_t)isr30, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);
    idt_set_gate(31, (uint64_t)isr31, 0x08, IDT_GATE_INTERRUPT | IDT_FLAG_DPL0);

    // Load IDT
    idt_load();

    kprintf("IDT: Initialized with %d entries\n", IDT_ENTRIES);
}

// Exception names
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// Common ISR handler - CHANGED for 64-bit
void isr_handler(uint64_t int_no, uint64_t err_code) {
    // Special handling for page fault
    if (int_no == 14) {
        page_fault_handler(err_code);
        return;
    }

    // General exception handler
    kprintf("\n!!! EXCEPTION: %s !!!\n", exception_messages[int_no]);
    kprintf("Interrupt: %d, Error Code: 0x%llx\n", (uint32_t)int_no, err_code);

    // Halt
    for(;;) {
        __asm__ volatile("cli; hlt");
    }
}
