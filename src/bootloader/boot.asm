BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Enable A20 early
    in  al, 0x92
    or  al, 2
    out 0x92, al

    ; Verify INT 13h extensions present
    mov ax, 0x4100
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    cmp bx, 0xAA55
    jne disk_error
    test cx, 1
    jz disk_error

    ; Load stage1.5 to 0x00008000 (ES:BX = 0x0800:0000)
    mov ax, 0x0800
    mov es, ax
    xor bx, bx

    ; DAP: read STAGE15_SECTORS from LBA 1
    mov word  [dap_count], STAGE15_SECTORS
    mov word  [dap_off],   0x0000
    mov word  [dap_seg],   0x0800
    mov dword [dap_lba_low],  1
    mov dword [dap_lba_high], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Jump to stage1.5 at 0x00008000
    jmp 0x0800:0x0000

disk_error:
    mov si, err
.print:
    lodsb
    test al, al
    jz $
    mov ah, 0x0E
    int 0x10
    jmp .print

boot_drive db 0
err db "Boot1 disk error", 0

dap:
    db 0x10, 0x00
dap_count:   dw 0
dap_off:     dw 0
dap_seg:     dw 0
dap_lba_low: dd 0
dap_lba_high:dd 0

TIMES 510-($-$$) db 0
DW 0xAA55
