/* Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic). MIT. */
OUTPUT_FORMAT(binary)
ENTRY(_start)
SECTIONS
{
    . = 0x100;
    .text   : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.got*) }
}
