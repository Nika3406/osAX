; src/bootloader/stage15.asm
; Stage1.5: loads stage2 (kernel.bin) from disk using INT 13h Extensions,
;           bounce-buffering below 1MiB, then copies to 1MiB in protected mode,
;           then enters long mode and jumps to 0x00100000 with EAX=mem_MB.

BITS 16
ORG 0x7E00
default rel

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 168
%endif

%ifndef STAGE15_SECTORS
    ; Pass1 sizing may not define it; any value is OK there.
    %define STAGE15_SECTORS 64
%endif

; LBA layout:
; LBA 0      = MBR
; LBA 1..    = stage1.5 (STAGE15_SECTORS)
; next       = stage2
STAGE2_LBA       equ (1 + STAGE15_SECTORS)

; We cannot reliably DMA to 0x00100000 in real mode via DAP seg:off.
KERNEL_LOAD_PM   equ 0x00100000        ; final location expected by your linker
KERNEL_BOUNCE_RM equ 0x00010000        ; bounce buffer in low memory (<1MiB)
BOOTINFO_ADDR    equ 0x00009000
BOOTINFO_MEM_MB  equ BOOTINFO_ADDR

; Conservative BIOS read chunk.
MAX_SECTORS_PER_READ equ 0x007F        ; 127

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7E00
    sti

    mov [boot_drive], dl

    call enable_a20
    call detect_memory_e820
    call load_kernel_lba_bounce

    cli
    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE32_SEL:pm32_entry

; -------------------------
; Real mode helpers
; -------------------------
enable_a20:
    in  al, 0x92
    or  al, 00000010b
    out 0x92, al
    ret

; Store detected memory in MB at BOOTINFO_MEM_MB (E820 max-addr based)
detect_memory_e820:
    pushad
    xor ebx, ebx
    mov dword [BOOTINFO_MEM_MB], 16

    xor edi, edi          ; max_end_lo
    xor esi, esi          ; max_end_hi

.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov di, e820_buf
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done

    mov eax, [e820_buf + 16]   ; type
    cmp eax, 1                 ; usable
    jne .cont

    ; end = base + length (64-bit)
    mov eax, [e820_buf + 0]
    mov edx, [e820_buf + 4]
    add eax, [e820_buf + 8]
    adc edx, [e820_buf + 12]

    ; track max end address
    cmp edx, esi
    jb .cont
    ja .setmax
    cmp eax, edi
    jbe .cont
.setmax:
    mov edi, eax
    mov esi, edx

.cont:
    test ebx, ebx
    jnz .e820_next

.done:
    ; Convert max bytes -> MB (shift right 20)
    mov eax, edi
    mov edx, esi
    mov ecx, 20
.shift:
    shrd eax, edx, 1
    shr  edx, 1
    loop .shift

    cmp eax, 16
    jae .store
    mov eax, 16
.store:
    mov [BOOTINFO_MEM_MB], eax
    popad
    ret

e820_buf: times 24 db 0

; -------------------------
; Kernel load (bounce buffer) in RM, chunked reads
; -------------------------
load_kernel_lba_bounce:
    pushad

    mov dword [cur_lba_lo], STAGE2_LBA
    mov dword [cur_lba_hi], 0
    mov dword [cur_addr],   KERNEL_BOUNCE_RM
    mov cx, STAGE2_SECTORS

.read_loop:
    cmp cx, 0
    je .ok

    ; chunk = min(cx, 127)
    mov ax, cx
    cmp ax, MAX_SECTORS_PER_READ
    jbe .chunk_ok
    mov ax, MAX_SECTORS_PER_READ
.chunk_ok:
    mov [chunk_secs], ax

    ; Build DAP for this chunk at current addr
    mov byte  [dap_size], 0x10
    mov byte  [dap_res],  0

    mov ax, [chunk_secs]
    mov word  [dap_secs], ax

    ; Convert linear cur_addr -> seg:off
    mov eax, [cur_addr]
    mov bx, ax
    and bx, 0x000F
    mov word [dap_off], bx
    shr eax, 4
    mov word [dap_seg], ax

    mov eax, [cur_lba_lo]
    mov [dap_lba_lo], eax
    mov eax, [cur_lba_hi]
    mov [dap_lba_hi], eax

    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .fail

    ; Advance addr by chunk*512
    xor eax, eax
    mov ax, [chunk_secs]
    shl eax, 9
    add dword [cur_addr], eax

    ; Advance LBA by chunk
    movzx eax, word [chunk_secs]
    add dword [cur_lba_lo], eax
    adc dword [cur_lba_hi], 0

    ; Remaining sectors
    sub cx, [chunk_secs]
    jmp .read_loop

.ok:
    popad
    ret

.fail:
    mov si, err_kernel
    call print
.hang:
    hlt
    jmp .hang

chunk_secs dw 0
cur_addr   dd 0
cur_lba_lo dd 0
cur_lba_hi dd 0

; -------------------------
; Enter 32-bit PM, copy to 1MiB, then go long mode
; -------------------------
BITS 32
pm32_entry:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x9FC00

    ; Copy stage2 from bounce buffer -> 1MiB
    mov esi, KERNEL_BOUNCE_RM
    mov edi, KERNEL_LOAD_PM
    mov ecx, (STAGE2_SECTORS * 512) / 4
    rep movsd

    call setup_identity_paging

    ; PAE
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    mov eax, pml4
    mov cr3, eax

    ; PG
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp CODE64_SEL:lm64_entry

; Identity map first 1GiB using 2MiB pages (PML4->PDP->PD)
setup_identity_paging:
    mov edi, pml4
    mov ecx, (4096*3)/4
    xor eax, eax
    rep stosd

    mov eax, pdp
    or eax, 0x003
    mov [pml4 + 0], eax
    mov dword [pml4 + 4], 0

    mov eax, pd
    or eax, 0x003
    mov [pdp + 0], eax
    mov dword [pdp + 4], 0

    xor ecx, ecx
.fill_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x083
    mov [pd + ecx*8 + 0], eax
    mov dword [pd + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .fill_pd
    ret

ALIGN 4096
pml4: times 4096 db 0
pdp:  times 4096 db 0
pd:   times 4096 db 0

; -------------------------
; 64-bit entry: pass mem MB in EAX -> your entry.asm stores it
; -------------------------
BITS 64
lm64_entry:
    mov eax, dword [BOOTINFO_MEM_MB]
    mov rbx, KERNEL_LOAD_PM
    jmp rbx

; -------------------------
; Print (16-bit)
; -------------------------
BITS 16
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
err_kernel db "Boot2: kernel read failed", 0

; ONE DAP ONLY
dap:
dap_size   db 0
dap_res    db 0
dap_secs   dw 0
dap_off    dw 0
dap_seg    dw 0
dap_lba_lo dd 0
dap_lba_hi dd 0

ALIGN 8
gdt:
dq 0
dq 0x00CF9A000000FFFF
dq 0x00CF92000000FFFF
dq 0x00AF9A000000FFFF
dq 0x00AF92000000FFFF

gdt_desc:
dw gdt_end - gdt - 1
dd gdt
dd 0
gdt_end:

CODE32_SEL equ 0x08
DATA32_SEL equ 0x10
CODE64_SEL equ 0x18
DATA64_SEL equ 0x20
