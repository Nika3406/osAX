; src/bootloader/idt_asm.asm
BITS 64
SECTION .text

EXTERN isr_handler

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

; Common handler expects stack layout:
;  [rsp + 0]  = int_no
;  [rsp + 8]  = err_code (0 if none)
isr_common:
    PUSH_REGS

    ; After pushing regs, int_no/err_code are deeper on stack
    ; 15 regs * 8 = 120 bytes
    mov rdi, [rsp + 120 + 0]   ; int_no
    mov rsi, [rsp + 120 + 8]   ; err_code
    call isr_handler

    POP_REGS
    add rsp, 16                ; pop int_no + err_code
    iretq

%macro ISR_NOERR 1
GLOBAL isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
GLOBAL isr%1
isr%1:
    ; CPU already pushed err_code
    push qword %1
    jmp isr_common
%endmacro

; Exceptions with error code in x86:
; 8, 10, 11, 12, 13, 14, 17
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
