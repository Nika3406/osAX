/* stage2.ld - Link kernel to run at 1MB (after relocation) */
OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

SECTIONS
{
  . = 0x00100000;  /* Kernel runs at 1MB after relocation */

  .text : {
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  }

  .data : {
    *(.data)
    *(.data.*)
  }

  .bss : {
    __bss_start = .;
    *(COMMON)
    *(.bss)
    *(.bss.*)
    __bss_end = .;
  }

  /DISCARD/ : {
    *(.comment)
    *(.note)
    *(.eh_frame)
  }
}