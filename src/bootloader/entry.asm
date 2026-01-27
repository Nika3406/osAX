; src/bootloader/entry.asm
; 64-bit kernel entry point. Expects:
;   RAX/EAX = detected memory size in MB (passed from stage1.5)

BITS 64
default rel

SECTION .text
GLOBAL _start
EXTERN c_main

; Exposed globals used by C code
GLOBAL detected_memory_size
GLOBAL framebuffer_address
GLOBAL framebuffer_width
GLOBAL framebuffer_height
GLOBAL framebuffer_pitch

_start:
    cli

    ; Set up a known-good stack (identity-mapped low memory)
    lea rsp, [rel stack_top]
    and rsp, -16

    ; Store detected memory (MB) into global expected by memory.c
    mov dword [rel detected_memory_size], eax

    ; No VBE/graphics framebuffer for now -> force VGA text path
    xor rax, rax
    mov qword [rel framebuffer_address], rax
    mov qword [rel framebuffer_width],   rax
    mov qword [rel framebuffer_height],  rax
    mov qword [rel framebuffer_pitch],   rax

    ; Enter C core init
    call c_main

.hang:
    hlt
    jmp .hang

SECTION .bss
ALIGN 16
detected_memory_size: resd 1

ALIGN 8
framebuffer_address: resq 1
framebuffer_width:   resq 1
framebuffer_height:  resq 1
framebuffer_pitch:   resq 1

ALIGN 16
stack_bottom: resb 65536
stack_top:
