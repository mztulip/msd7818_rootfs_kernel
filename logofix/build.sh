#!/bin/sh
# Build logofix.bin, linked to run at 0x80F00000 from MBoot:
#   usb start; fatload usb 0 0x80F00000 logofix.bin; go 0x80F00000
set -e
cd "$(dirname "$0")"
CC=mipsel-linux-gnu-gcc
$CC -O2 -march=mips32r2 -EL -ffreestanding -nostdlib -fno-pic -mno-abicalls -G0 \
	-fno-builtin -Wall -c logofix.c -o logofix.o
mipsel-linux-gnu-ld -EL -T logofix.ld logofix.o -o logofix.elf
mipsel-linux-gnu-objcopy -O binary logofix.elf logofix.bin
mipsel-linux-gnu-objdump -d logofix.elf | head -8
ls -la logofix.bin
