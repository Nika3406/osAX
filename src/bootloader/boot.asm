; src/bootloader/boot.asm
; MBR: loads 64 sectors of stage1.5 from LBA 1 into 0x7E00 then jumps.

BITS 16
ORG 0x7C00
default rel

STAGE15_LOAD  equ 0x7E00
STAGE15_LBA   equ 1
%ifndef STAGE15_SECTORS
    %define STAGE15_SECTORS 64
%endif

STAGE15_SECTS equ STAGE15_SECTORS


start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Fill DAP (correct layout: db/db/dw/dw/dw/dq)
    mov byte  [dap_size], 0x10
    mov byte  [dap_res],  0x00
    mov word  [dap_secs], STAGE15_SECTS
    mov word  [dap_off],  STAGE15_LOAD
    mov word  [dap_seg],  0x0000
    mov dword [dap_lba_lo], STAGE15_LBA
    mov dword [dap_lba_hi], 0

    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    jmp 0x0000:STAGE15_LOAD

disk_error:
    mov si, err_msg
    call print
.hang:
    hlt
    jmp .hang

print:
    mov ah, 0x0E
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

boot_drive db 0
err_msg db "Boot1: read stage1.5 failed", 0

; Disk Address Packet (DAP) for INT 13h Extensions
dap:
dap_size   db 0
dap_res    db 0
dap_secs   dw 0
dap_off    dw 0
dap_seg    dw 0
dap_lba_lo dd 0
dap_lba_hi dd 0

TIMES 510-($-$$) db 0
DW 0xAA55
