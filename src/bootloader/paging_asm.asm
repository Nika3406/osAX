; src/bootloader/paging_asm.asm
section .text
global load_page_directory
global enable_paging_asm
global enable_paging

load_page_directory:
    mov eax, [esp + 4]  ; Get the page directory pointer (uint32_t)
    mov cr3, eax        ; Load page directory base register
    ret

enable_paging_asm:
    mov eax, cr0
    or eax, 0x80000000  ; Set PG bit
    mov cr0, eax
    ret

enable_paging:
    mov eax, cr0
    or eax, 0x80000000  ; Set PG bit
    mov cr0, eax
    ret
