; src/bootloader/boot.asm
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

    ; ------------------------------------------------------------
    ; Detect memory via E820 and store TOTAL USABLE RAM in MB at 0x500
    ; 0x500 = detected_mem_mb (DWORD)
    ; ------------------------------------------------------------
    call detect_memory_e820_mb

    ; --- Enable A20 early (safe) ---
    in al, 0x92
    or al, 2
    out 0x92, al

    ; --- Load stage2 BELOW 1MB at 0x00010000 (ES:BX = 0x1000:0000) ---
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    ; --- Verify INT 13h extensions present ---
    mov ax, 0x4100
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    cmp bx, 0xAA55
    jne disk_error
    test cx, 1
    jz disk_error

    ; --- Prepare Disk Address Packet (DAP) for AH=42h ---
    ; stage2 begins at LBA 1 (boot sector is LBA 0)
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0
    mov word  [dap_off], 0x0000
    mov word  [dap_seg], 0x1000     ; buffer at 0x00010000

    mov cx, STAGE2_SECTORS          ; total sectors to read

.read_loop:
    mov ax, cx
    cmp ax, 127                     ; conservative per-call chunk size
    jbe .chunk_ok
    mov ax, 127
.chunk_ok:
    mov [dap_count], ax

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; advance buffer segment by (sectors * 512) / 16 = sectors * 32 paragraphs
    mov bx, [dap_seg]
    mov dx, [dap_count]
    shl dx, 5                       ; *32
    add bx, dx
    mov [dap_seg], bx

    ; advance LBA by sectors read
    movzx ebx, word [dap_count]
    add dword [dap_lba_low], ebx
    adc dword [dap_lba_high], 0

    sub cx, [dap_count]
    jnz .read_loop

    ; --- Load GDT and enter protected mode ---
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm32

; -------------------------
; 32-bit protected-mode stub
; -------------------------
BITS 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x00090000

    cld

    ; Copy stage2 from 0x00010000 -> 0x00100000
    mov esi, 0x00010000
    mov edi, 0x00100000
    mov ecx, (STAGE2_SECTORS * 512) / 4
    rep movsd

    ; Stage2 can read detected RAM MB from 0x500 and store into its global
    ; (do NOT touch detected_memory_size here; stage2 does it in entry.asm)

    ; Jump to stage2 entry (linked at 1MB)
    jmp 0x08:0x00100000

; -------------------------
; Error printing (real mode)
; -------------------------
BITS 16
disk_error:
    mov si, err
.print:
    lodsb
    test al, al
    jz $
    mov ah, 0x0E
    int 0x10
    jmp .print

; ------------------------------------------------------------
; E820: Store TOTAL USABLE RAM in MB at 0x00000500 (DWORD)
; ------------------------------------------------------------
detect_memory_e820_mb:
    pushad
    push ds
    xor ax, ax
    mov ds, ax
    push ds
    pop es

    ; total bytes = 0 (64-bit at mem_total_lo/hi)
    mov dword [mem_total_lo], 0
    mov dword [mem_total_hi], 0

    xor ebx, ebx                    ; continuation = 0

.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150             ; 'SMAP'
    mov ecx, 24
    mov di, e820_buf                ; ES:DI -> buffer in low memory
    int 0x15
    jc .calc_mb
    cmp eax, 0x534D4150
    jne .calc_mb

    ; if type == 1 usable, add length to total
    cmp dword [e820_buf + 16], 1
    jne .skip

    mov eax, [e820_buf + 8]         ; length low
    mov edx, [e820_buf + 12]        ; length high
    add [mem_total_lo], eax
    adc [mem_total_hi], edx

.skip:
    test ebx, ebx
    jnz .e820_next

.calc_mb:
    ; Convert total bytes (mem_total_hi:mem_total_lo) -> MB (floor)
    ; MB = bytes >> 20
    mov eax, [mem_total_lo]
    mov edx, [mem_total_hi]
    shrd eax, edx, 20               ; eax = (edx:eax) >> 20
    ; store MB at 0x500
    mov [0x0500], eax

    pop ds
    popad
    ret

; ---- Low-memory scratch for E820 totals/buffer ----
mem_total_lo: dd 0
mem_total_hi: dd 0

e820_buf:
    dq 0                            ; base
    dq 0                            ; length
    dd 0                            ; type
    dd 0                            ; ext attrs (ignored)

; -------------------------
; Data
; -------------------------
boot_drive db 0
err db "Disk error", 0

; ---- Disk Address Packet (DAP) ----
dap:
    db 0x10, 0x00
dap_count:   dw 0
dap_off:     dw 0
dap_seg:     dw 0
dap_lba_low: dd 0
dap_lba_high:dd 0

; ---- GDT ----
ALIGN 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF   ; code (0x08)
    dq 0x00CF92000000FFFF   ; data (0x10)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

TIMES 510-($-$$) db 0
DW 0xAA55
