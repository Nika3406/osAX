; boot.asm - Stage 1 bootloader with relocation to 1MB
BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Print loading message
    mov si, msg
.print_loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .print_loop
.done:

    ; Load stage2/kernel to 0x10000 (64KB)
    mov ax, 0x1000
    mov es, ax
    xor bx, bx              ; ES:BX = 0x1000:0x0000 = 0x10000

    mov ah, 0x02            ; BIOS read sectors
    mov al, STAGE2_SECTORS  ; Injected by Makefile
    mov ch, 0x00            ; Cylinder 0
    mov cl, 0x02            ; Sector 2 (after bootloader)
    mov dh, 0x00            ; Head 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Enable A20 line
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

disk_error:
    mov si, err
.err_loop:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .err_loop
.halt:
    hlt
    jmp .halt

BITS 32
protected_mode:
    ; Setup segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00200000     ; Stack at 2MB

    ; Relocate kernel: 0x10000 (64KB) → 0x100000 (1MB)
    mov esi, 0x00010000     ; Source
    mov edi, 0x00100000     ; Destination
    mov ecx, STAGE2_SECTORS
    shl ecx, 7              ; sectors * 512 / 4 = sectors * 128
    rep movsd               ; Copy in 4-byte chunks

    ; Jump to relocated kernel at 1MB
    jmp 0x08:0x00100000

; Data
msg db "Loading OSAX...", 13, 10, 0
err db "Disk error!", 13, 10, 0
boot_drive db 0

; GDT
align 8
gdt_start:
    dq 0x0000000000000000   ; Null descriptor
    dq 0x00CF9A000000FFFF   ; Code segment
    dq 0x00CF92000000FFFF   ; Data segment
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

TIMES 510-($-$$) db 0
DW 0xAA55