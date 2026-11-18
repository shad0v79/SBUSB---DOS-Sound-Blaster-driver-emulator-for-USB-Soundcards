#!/bin/sh
# Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic). MIT.
set -e
CF="-m16 -march=i386 -mtune=i486 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-builtin -mno-sse -mno-mmx -mno-80387 -Wall -Wno-unused-function -Wno-array-bounds"
gcc $CF -c start.S -o start.o
gcc $CF -c main.c -o main.o
ld -m elf_i386 -T link.ld -nostdlib -o USBTONE.COM start.o main.o
ld -m elf_i386 -T link.ld -nostdlib --oformat elf32-i386 -o usbtone.elf start.o main.o 2>/dev/null || true
gcc $CF -c cputest.c -o cputest.o
ld -m elf_i386 -T link.ld -nostdlib -o CPUTEST.COM start.o cputest.o
gcc $CF -c sbtest.c -o sbtest.o
ld -m elf_i386 -T link.ld -nostdlib -o SBTEST.COM start.o sbtest.o
gcc $CF -c kbtest.c -o kbtest.o
ld -m elf_i386 -T link.ld -nostdlib -o KBTEST.COM start.o kbtest.o
gcc $CF -c usbrec.c -o usbrec.o
ld -m elf_i386 -T link.ld -nostdlib -o USBREC.COM start.o usbrec.o
gcc $CF -c sbmix.c -o sbmix.o
ld -m elf_i386 -T link.ld -nostdlib -o SBMIX.COM start.o sbmix.o
gcc $CF -c usbmix.c -o usbmix.o
ld -m elf_i386 -T link.ld -nostdlib -o USBMIX.COM start.o usbmix.o
gcc $CF -c pamiec.c -o pamiec.o
ld -m elf_i386 -T link.ld -nostdlib -o PAMIEC.COM start.o pamiec.o
gcc $CF -c strona.c -o strona.o
ld -m elf_i386 -T link.ld -nostdlib -o STRONA.COM start.o strona.o
gcc $CF -c install.c -o install.o
ld -m elf_i386 -T link.ld -nostdlib -o INSTALL.COM start.o install.o
gcc $CF -DLANG_EN -c install.c -o install_en.o
ld -m elf_i386 -T link.ld -nostdlib -o INSTALLE.COM start.o install_en.o
