; src/bootloader/idt_asm.asm
[BITS 32]

extern isr_handler

; Macro for ISRs without error codes
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push dword 0        ; Dummy error code
    push dword %1       ; Interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISRs with error codes (CPU pushes error code automatically)
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    push dword %1       ; Interrupt number (error code already pushed by CPU)
    jmp isr_common_stub
%endmacro

; Define all 32 CPU exception ISRs
ISR_NOERRCODE 0     ; Divide by zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound range exceeded
ISR_NOERRCODE 6     ; Invalid opcode
ISR_NOERRCODE 7     ; Device not available
ISR_ERRCODE   8     ; Double fault (has error code)
ISR_NOERRCODE 9     ; Coprocessor segment overrun
ISR_ERRCODE   10    ; Invalid TSS (has error code)
ISR_ERRCODE   11    ; Segment not present (has error code)
ISR_ERRCODE   12    ; Stack-segment fault (has error code)
ISR_ERRCODE   13    ; General protection fault (has error code)
ISR_ERRCODE   14    ; Page fault (has error code)
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU error
ISR_ERRCODE   17    ; Alignment check (has error code)
ISR_NOERRCODE 18    ; Machine check
ISR_NOERRCODE 19    ; SIMD floating-point exception
ISR_NOERRCODE 20    ; Virtualization exception
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_NOERRCODE 30    ; Reserved
ISR_NOERRCODE 31    ; Reserved

; Common ISR stub - saves context and calls C handler
isr_common_stub:
    ; Save all general purpose registers
    pusha                   ; Pushes EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI

    ; Save segment registers
    push ds
    push es
    push fs
    push gs

    ; Load kernel data segment
    mov ax, 0x10           ; Kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Call C interrupt handler
    ; Stack at this point has:
    ; [esp + 0]  = GS
    ; [esp + 4]  = FS
    ; [esp + 8]  = ES
    ; [esp + 12] = DS
    ; [esp + 16] = EDI
    ; [esp + 20] = ESI
    ; [esp + 24] = EBP
    ; [esp + 28] = ESP (useless value)
    ; [esp + 32] = EBX
    ; [esp + 36] = EDX
    ; [esp + 40] = ECX
    ; [esp + 44] = EAX
    ; [esp + 48] = Interrupt number
    ; [esp + 52] = Error code

    ; Push error code and interrupt number for C handler
    mov eax, [esp + 52]     ; Error code
    push eax
    mov eax, [esp + 52]     ; Interrupt number (adjusted for previous push)
    push eax

    call isr_handler

    ; Clean up pushed arguments
    add esp, 8

    ; Restore segment registers
    pop gs
    pop fs
    pop es
    pop ds

    ; Restore general purpose registers
    popa

    ; Clean up error code and interrupt number
    add esp, 8

    ; Return from interrupt
    sti
    iret
