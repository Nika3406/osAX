#ifndef IDT_H
#define IDT_H

#include "types.h"

#define IDT_ENTRIES 256

// IDT gate types
#define IDT_GATE_TASK       0x5
#define IDT_GATE_INTERRUPT  0xE
#define IDT_GATE_TRAP       0xF

// IDT flags
#define IDT_FLAG_PRESENT    0x80
#define IDT_FLAG_DPL0       0x00
#define IDT_FLAG_DPL3       0x60

typedef struct {
    uint16_t base_low;      // Lower 16 bits of handler address
    uint16_t selector;      // Kernel segment selector
    uint8_t always0;        // Always 0
    uint8_t flags;          // Gate type and attributes
    uint16_t base_high;     // Upper 16 bits of handler address
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;         // Size of IDT - 1
    uint32_t base;          // Address of IDT
} __attribute__((packed)) idt_ptr_t;

// Interrupt frame pushed by CPU
typedef struct {
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} __attribute__((packed)) interrupt_frame_t;

// Function declarations
void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_load(void);

// ISR handlers (defined in idt_asm.asm)
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);  // Page fault
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

#endif
