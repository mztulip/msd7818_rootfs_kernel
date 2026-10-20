#!/bin/sh
# Build bootsel.bin (linked at 0x80F00400) and bootblk.img, the 64 KB flash
# block at 0x1D0000: logofix.bin at offset 0, bootsel.bin at 0x400.
# Test from RAM without touching the flash (MBoot):
#   run wdoff; usb start; mw.l 0x81000000 0 1; fatload usb 0 0x81000000 vmlinux.bin
#   run flashlogo; fatload usb 0 0x80F00400 bootsel.bin; go 0x80F00400
set -e
cd "$(dirname "$0")"
CC=mipsel-linux-gnu-gcc
CFLAGS="-Os -march=mips32r2 -EL -ffreestanding -nostdlib -fno-pic -mno-abicalls -G0 -fno-builtin -Wall -Wextra"
$CC $CFLAGS -c bootsel.c -o bootsel.o
$CC $CFLAGS -c lzma.c -o lzma.o
mipsel-linux-gnu-ld -EL -T bootsel.ld bootsel.o lzma.o -o bootsel.elf
mipsel-linux-gnu-objcopy -O binary -j .text -j .rodata -j .data bootsel.elf bootsel.bin
mipsel-linux-gnu-size bootsel.elf
python3 - <<'PY'
lf = open('../logofix/logofix.bin', 'rb').read()
bs = open('bootsel.bin', 'rb').read()
assert len(lf) <= 0x400 and 0x400 + len(bs) <= 0x10000
blk = lf + b'\xff' * (0x400 - len(lf)) + bs
open('bootblk.img', 'wb').write(blk + b'\xff' * (0x10000 - len(blk)))
print('bootsel.bin %d B, bootblk.img 64 KB (logofix %d B @0, bootsel @0x400)' % (len(bs), len(lf)))
PY
