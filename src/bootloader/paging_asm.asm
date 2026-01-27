; src/bootloader/paging_asm.asm
BITS 64
SECTION .text

GLOBAL load_page_directory
GLOBAL enable_paging_asm

; void load_page_directory(uint64_t pml4_phys)
load_page_directory:
    mov cr3, rdi
    ret

; void enable_paging_asm(void)
; In long mode paging is already enabled, but your kernel calls this.
; We keep it safe: ensure CR0.PG and CR0.WP are set.
enable_paging_asm:
    mov rax, cr0
    or rax, (1 << 31)          ; PG
    or rax, (1 << 16)          ; WP
    mov cr0, rax
    ret
