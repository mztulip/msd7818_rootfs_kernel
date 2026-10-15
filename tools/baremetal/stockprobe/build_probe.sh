#!/bin/sh
# Build a probe for the stock firmware: NAME.c with an entry function NAME()
# -> NAME.bin linked at 0xA3800000 + mw_cmds_NAME.txt (MBoot mw.l commands:
# the probe code, then the 16-byte jump patch over the demo console "Help"
# handler at 0x80142468). Load with tools/serial/run_stockprobe.py.
# Usage: ./build_probe.sh NAME        (see docs/sondy_stock.md)
set -e
cd "$(dirname "$0")"
N=${1:?usage: build_probe.sh NAME}
cat > $N.ld <<LD
ENTRY($N)
SECTIONS
{
	. = 0xA3800000;
	.text : { *(.text.$N) *(.text*) *(.rodata*) *(.data*) *(.sdata*) *(.bss*) *(.sbss*) }
	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
LD
mipsel-linux-gnu-gcc -Os -march=mips32r2 -EL -ffreestanding -nostdlib -fno-pic -mno-abicalls -G0 \
	-fno-builtin -ffunction-sections -Wall -c $N.c -o $N.o
mipsel-linux-gnu-ld -EL -T $N.ld $N.o -o $N.elf
mipsel-linux-gnu-objcopy -O binary $N.elf $N.bin
ENTRY=$(mipsel-linux-gnu-nm $N.elf | awk -v n=$N '$3==n{print $1}')
echo "$N entry $ENTRY, $(stat -c %s $N.bin) B"
python3 - "$ENTRY" $N <<'PY' > mw_cmds_$N.txt
import struct, sys
entry, n = int(sys.argv[1], 16), sys.argv[2]
d = open(n + '.bin', 'rb').read()
d += b'\0' * (-len(d) % 4)
for i in range(0, len(d), 4):
    print('mw.l %08x %08x' % (0xa3800000 + i, struct.unpack('<I', d[i:i+4])[0]))
# Help handler (0x80142468, written through KSEG1 0xa0142468):
# lui t9,hi(entry); ori t9,t9,lo(entry); jr t9; nop
for i, w in enumerate((0x3c190000 | (entry >> 16), 0x37390000 | (entry & 0xffff), 0x03200008, 0)):
    print('mw.l %08x %08x' % (0xa0142468 + 4 * i, w))
PY
wc -l < mw_cmds_$N.txt | sed 's/$/ mw.l commands in mw_cmds_'$N'.txt/'
