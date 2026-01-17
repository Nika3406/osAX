/* src/bootloader/stage2.ld */
OUTPUT_FORMAT(elf32-i386)
ENTRY(protected_entry)

SECTIONS
{
    . = 0x00100000;

    .text : ALIGN(4K)
    {
        *(.text.entry)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }


    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
