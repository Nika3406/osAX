; src/bootloader/entry.asm
BITS 32

; ---- entry code must be in TEXT ----
section .text.entry
align 16
global protected_entry
extern c_main
extern __bss_start
extern __bss_end

protected_entry:
    cli
    cld
    rep stosb	

    ; --- Setup segments ---
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; --- Stack (2MB is fine in QEMU, but 0x90000 is safer early-on) ---
    mov esp, 0x00090000

    ; --- Clear .bss ---
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    mov eax, [0x00000500]          ; RAM in MB
    mov [detected_memory_size], eax

    ; --- Call C kernel ---
    call c_main

.hang:
    cli
    hlt
    jmp .hang

; ---- data goes in DATA ----
section .data
align 4
global framebuffer_address, framebuffer_width, framebuffer_height, framebuffer_pitch, detected_memory_size

framebuffer_address:  dd 0
framebuffer_width:    dd 0
framebuffer_height:   dd 0
framebuffer_pitch:    dd 0
detected_memory_size: dd 0
