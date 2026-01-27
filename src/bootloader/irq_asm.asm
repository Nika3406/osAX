; src/bootloader/irq_asm.asm
BITS 64
SECTION .text

GLOBAL pic_init
GLOBAL irq1_handler

EXTERN keyboard_handler

%macro PUSH_REGS 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro POP_REGS 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
%endmacro

; void pic_init(void)
; Remap PIC IRQs to 0x20..0x2F
pic_init:
    ; ICW1
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    ; ICW2
    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al
    ; ICW3
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    ; ICW4
    mov al, 0x01
    out 0x21, al
    out 0xA1, al
    ret

; IRQ1 = keyboard (interrupt vector 33 after remap)
irq1_handler:
    PUSH_REGS

    call keyboard_handler

    ; Send EOI to PICs
    mov al, 0x20
    out 0x20, al

    POP_REGS
    iretq
