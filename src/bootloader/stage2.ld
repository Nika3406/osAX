/* src/bootloader/stage2.ld */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
  . = 0x00100000;

  __kernel_start = .;

  .text : ALIGN(16) {
    *(.text*)
  }

  .rodata : ALIGN(16) {
    *(.rodata*)
  }

  .data : ALIGN(16) {
    *(.data*)
  }

  __bss_start = .;
  .bss : ALIGN(16) {
    *(COMMON)
    *(.bss*)
  }
  __bss_end = .;

  __kernel_end = .;
}
