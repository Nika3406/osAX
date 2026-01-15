; FIXED VESA with proper fallback and CORRECT STACK
BITS 16

global start
global detected_memory_size
global framebuffer_address
global framebuffer_width
global framebuffer_height
global framebuffer_pitch

start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00        ; Safe stack below bootloader

    ; DEBUG: Print 'E' to show we reached entry.asm
    mov ah, 0x0E
    mov al, 'E'
    int 0x10

    ; === SET GRAPHICS MODE WHILE IN REAL MODE ===
    call set_vesa_mode

    ; DEBUG: Print 'V' to show VESA attempted
    mov ah, 0x0E
    mov al, 'V'
    int 0x10

    ; === DETECT MEMORY WHILE IN REAL MODE ===
    call detect_memory_real_mode

    ; DEBUG: Print 'M' to show memory detected
    mov ah, 0x0E
    mov al, 'M'
    int 0x10

    ; Enable A20
    call enable_a20

    ; DEBUG: Print 'A' to show A20 enabled
    mov ah, 0x0E
    mov al, 'A'
    int 0x10

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Enter protected mode
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    jmp 0x08:protected_entry

; ---------------------
; Set VESA graphics mode (REAL MODE ONLY)
; ---------------------
set_vesa_mode:
    ; Initialize defaults (text mode) in case VESA fails
    mov dword [framebuffer_address_temp], 0xB8000
    mov word [framebuffer_width_temp], 80
    mov word [framebuffer_height_temp], 25
    mov word [framebuffer_pitch_temp], 160
    
    ; Check if VBE is supported
    mov ax, 0x4F00
    mov di, vbe_info_block
    int 0x10
    cmp ax, 0x004F
    jne .done  ; If not supported, keep text mode defaults
    
    ; Try to set 1024x768x24 (mode 0x118)
    mov ax, 0x4F02
    mov bx, 0x4118  ; Mode with LFB bit set
    int 0x10
    cmp ax, 0x004F
    je .get_mode_info
    
    ; Try 800x600x24 (mode 0x115)
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    je .get_mode_info
    
    ; Try 640x480x24 (mode 0x112)
    mov ax, 0x4F02
    mov bx, 0x4112
    int 0x10
    cmp ax, 0x004F
    je .get_mode_info
    
    ; All graphics modes failed - stay in text mode
    jmp .done
    
.get_mode_info:
    ; Get detailed mode information
    mov ax, 0x4F01
    mov cx, bx
    and cx, 0x3FFF  ; Clear LFB bit
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    jne .done  ; If query failed, keep text mode
    
    ; Validate framebuffer address
    mov eax, [vbe_mode_info + 40]
    cmp eax, 0  ; Check if zero
    je .done
    cmp eax, 0xB8000  ; Check if text mode address
    je .done
    
    ; Valid graphics mode - store info
    mov [framebuffer_address_temp], eax
    
    movzx eax, word [vbe_mode_info + 18]  ; Width
    mov [framebuffer_width_temp], ax
    
    movzx eax, word [vbe_mode_info + 20]  ; Height
    mov [framebuffer_height_temp], ax
    
    movzx eax, word [vbe_mode_info + 16]  ; Bytes per scanline
    mov [framebuffer_pitch_temp], ax
    
.done:
    ret

; ---------------------
; Detect memory in REAL MODE
; ---------------------
detect_memory_real_mode:
    ; Try INT 0x15, AX=0xE801
    mov ax, 0xE801
    int 0x15
    jc .try_0x88

    movzx eax, ax
    shl eax, 10
    movzx ebx, bx
    shl ebx, 16
    add eax, ebx
    add eax, 0x100000
    jmp .store_result

.try_0x88:
    mov ah, 0x88
    int 0x15
    jc .fallback_default

    movzx eax, ax
    shl eax, 10
    add eax, 0x100000
    jmp .store_result

.fallback_default:
    mov eax, 128 * 1024 * 1024

.store_result:
    mov [memory_size_temp], eax
    ret

; Temporary storage
memory_size_temp: dd 0
framebuffer_address_temp: dd 0
framebuffer_width_temp: dw 0
framebuffer_height_temp: dw 0
framebuffer_pitch_temp: dw 0

; VBE structures
vbe_info_block: times 512 db 0
vbe_mode_info: times 256 db 0

; ---------------------
; Protected mode entry
; ---------------------
BITS 32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00200000   ; Stack at 2MB (safe location after kernel)

    ; Copy values to BSS
    mov eax, [memory_size_temp]
    mov [detected_memory_size], eax
    
    mov eax, [framebuffer_address_temp]
    mov [framebuffer_address], eax
    
    movzx eax, word [framebuffer_width_temp]
    mov [framebuffer_width], eax
    
    movzx eax, word [framebuffer_height_temp]
    mov [framebuffer_height], eax
    
    movzx eax, word [framebuffer_pitch_temp]
    mov [framebuffer_pitch], eax

    ; Call C runtime
    extern c_main
    call c_main

.hang:
    cli
    hlt
    jmp .hang

; ---------------------
; Enable A20
; ---------------------
BITS 16
enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; ---------------------
; GDT
; ---------------------
ALIGN 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00CFFA000000FFFF
    dq 0x00CFF2000000FFFF
    dq 0x0000000000000000
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------------------
; BSS
; ---------------------
section .bss
detected_memory_size: resd 1
framebuffer_address: resd 1
framebuffer_width: resd 1
framebuffer_height: resd 1
framebuffer_pitch: resd 1
