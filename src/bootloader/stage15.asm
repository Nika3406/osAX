; src/bootloader/stage15.asm
; Stage 1.5 loader (real mode) loaded by stage1 at 0x00008000.
; Responsibilities:
;  - E820 RAM detect -> store MB at 0x00000500
;  - VBE set LFB mode -> store fb info at 0x00000504..0x00000510
;  - Load stage2.bin to 0x00010000
;  - Enter protected mode, copy stage2 to 0x00100000, jump

BITS 16
ORG 0x8000

start15:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; DL = boot drive from stage1
    mov [boot_drive], dl

    ; ------------------------------------------------------------
    ; Detect memory via E820 and store TOTAL USABLE RAM in MB at 0x500
    ; 0x500 = detected_mem_mb (DWORD)
    ; ------------------------------------------------------------
    call detect_memory_e820_mb

    ; ------------------------------------------------------------
    ; Try set VBE LFB mode and store framebuffer info in mailbox:
    ; 0x504=addr, 0x508=width, 0x50C=height, 0x510=pitch
    ; ------------------------------------------------------------
    call vbe_try_set_lfb

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
    ; Stage2 begins AFTER stage1.5:
    ; LBA0 = stage1, LBA1.. = stage1.5, stage2 starts at LBA (1 + STAGE15_SECTORS)
    mov dword [dap_lba_low], (1 + STAGE15_SECTORS)
    mov dword [dap_lba_high], 0
    mov word  [dap_off], 0x0000
    mov word  [dap_seg], 0x1000     ; buffer at 0x00010000

    mov cx, STAGE2_SECTORS          ; total sectors to read

.read_loop:
    mov ax, cx
    cmp ax, 127                     ; per-call chunk size
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
    ; MB = bytes >> 20
    mov eax, [mem_total_lo]
    mov edx, [mem_total_hi]
    shrd eax, edx, 20               ; eax = (edx:eax) >> 20
    mov [0x0500], eax               ; store MB at 0x500

    pop ds
    popad
    ret

; ---- Scratch for E820 totals/buffer ----
mem_total_lo: dd 0
mem_total_hi: dd 0

e820_buf:
    dq 0                            ; base
    dq 0                            ; length
    dd 0                            ; type
    dd 0                            ; ext attrs (ignored)

; ------------------------------------------------------------
; VBE: Try to set a linear framebuffer mode and store:
; 0x504 = fb_addr (DWORD)
; 0x508 = width   (DWORD)
; 0x50C = height  (DWORD)
; 0x510 = pitch   (DWORD)
; If not available, zero them.
; ------------------------------------------------------------
vbe_try_set_lfb:
    pushad
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Default: no framebuffer (forces VGA text mode)
    mov dword [0x0504], 0
    mov dword [0x0508], 0
    mov dword [0x050C], 0
    mov dword [0x0510], 0

    ; Try a short list of common VBE modes.
    ; We'll accept ONLY 24bpp or 32bpp.
    mov si, vbe_mode_list

.try_next_mode:
    lodsw                   ; AX = mode
    test ax, ax
    jz .done                ; 0 terminator -> give up

    mov cx, ax              ; CX = mode for 4F01
    mov ax, 0x4F01
    mov di, 0x0600          ; mode info buffer in low memory
    int 0x10
    cmp ax, 0x004F
    jne .try_next_mode

    ; BitsPerPixel at offset 0x19
    mov al, [0x0600 + 0x19]
    cmp al, 24
    je .candidate_ok
    cmp al, 32
    jne .try_next_mode

.candidate_ok:
    ; PhysBasePtr at offset 0x28
    mov eax, [0x0600 + 0x28]
    test eax, eax
    jz .try_next_mode

    ; Set mode with Linear Framebuffer bit (bit 14)
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .try_next_mode

    ; Success: store mailbox info
    mov eax, [0x0600 + 0x28]        ; fb addr
    mov [0x0504], eax

    movzx eax, word [0x0600 + 0x12] ; Xres
    mov [0x0508], eax

    movzx eax, word [0x0600 + 0x14] ; Yres
    mov [0x050C], eax

    movzx eax, word [0x0600 + 0x10] ; Pitch
    mov [0x0510], eax

.done:
    pop es
    pop ds
    popad
    ret

vbe_mode_list:
    dw 0x11B     ; often 1280x1024x32 (not guaranteed)
    dw 0x118     ; often 1024x768x24/32 or 16 depending
    dw 0x117     ; often 1024x768
    dw 0x115     ; often 800x600
    dw 0x114     ; often 800x600
    dw 0x0000

; -------------------------
; Data
; -------------------------
boot_drive db 0
err db "Stage15 disk error", 0

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

; ------------------------------------------------------------
; Pad stage1.5 to an even number of sectors so stage2 starts
; exactly at LBA (1 + STAGE15_SECTORS)
; ------------------------------------------------------------
TIMES (STAGE15_SECTORS*512) - ($-$$) db 0
