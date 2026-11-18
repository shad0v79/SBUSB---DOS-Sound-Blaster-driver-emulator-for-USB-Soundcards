/* USBTONE - punkt wejscia programu .COM (tryb rzeczywisty, gcc -m16) */
/* Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic). MIT. */
        .code16gcc
        .section .text.start,"ax"
        .globl  _start
_start:
        cld
        movzwl  %sp, %esp          /* gornie bity ESP musza byc zerem      */
        xorl    %ebp, %ebp
        /* wyzeruj .bss */
        movw    $__bss_start, %di
        movw    $__bss_end, %cx
        subw    %di, %cx
        xorb    %al, %al
        rep stosb
        calll   main
        movb    $0x4c, %ah         /* kod wyjscia w AL */
        int     $0x21
1:      hlt
        jmp     1b
