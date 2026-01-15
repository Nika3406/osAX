; src/bootloader/irq_asm.asm - Hardware interrupt handlers
[BITS 32]

extern keyboard_handler

; PIC ports
%define PIC1_COMMAND 0x20
%define PIC1_DATA    0x21
%define PIC2_COMMAND 0xA0
%define PIC2_DATA    0xA1

; IRQ0 - Timer (dummy handler for now)
global irq0_handler
irq0_handler:
    pusha
    mov al, 0x20
    out PIC1_COMMAND, al
    popa
    iret

; IRQ1 - Keyboard
global irq1_handler
irq1_handler:
    pusha
    
    ; Call C handler
    call keyboard_handler
    
    ; Send EOI to PIC
    mov al, 0x20
    out PIC1_COMMAND, al
    
    popa
    iret

; Initialize PICs (Programmable Interrupt Controllers)
global pic_init
pic_init:
    ; Save masks
    in al, PIC1_DATA
    mov cl, al
    in al, PIC2_DATA
    mov ch, al
    
    ; Start initialization sequence
    mov al, 0x11
    out PIC1_COMMAND, al
    out PIC2_COMMAND, al
    
    ; Set vector offsets
    mov al, 0x20        ; Master PIC starts at interrupt 32
    out PIC1_DATA, al
    mov al, 0x28        ; Slave PIC starts at interrupt 40
    out PIC2_DATA, al
    
    ; Configure cascade
    mov al, 0x04
    out PIC1_DATA, al
    mov al, 0x02
    out PIC2_DATA, al
    
    ; Set 8086 mode
    mov al, 0x01
    out PIC1_DATA, al
    out PIC2_DATA, al
    
    ; Mask all IRQs initially
    mov al, 0xFF
    out PIC1_DATA, al
    out PIC2_DATA, al
    
    ret
