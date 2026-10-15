#!/bin/sh
# Build stockprobe.bin (loaded by MBoot at 0xA3800000) and the 12-byte jump
# patch for the stock "Help" handler; print the MBoot mw.l commands.
set -e
cd "$(dirname "$0")"
mipsel-linux-gnu-gcc -Os -march=mips32r2 -EL -ffreestanding -nostdlib -fno-pic -mno-abicalls -G0 \
	-fno-builtin -ffunction-sections -Wall -c stockprobe.c -o stockprobe.o
mipsel-linux-gnu-ld -EL -T stockprobe.ld stockprobe.o -o stockprobe.elf
mipsel-linux-gnu-objcopy -O binary stockprobe.elf stockprobe.bin
ENTRY=$(mipsel-linux-gnu-nm stockprobe.elf | awk '$3=="probe"{print $1}')
echo "probe entry $ENTRY"
ls -l stockprobe.bin
python3 - "$ENTRY" <<'PY' > mw_cmds.txt
import struct, sys
entry = int(sys.argv[1], 16)
d = open('stockprobe.bin', 'rb').read()
d += b'\0' * (-len(d) % 4)
for i in range(0, len(d), 4):
    print('mw.l %08x %08x' % (0xa3800000 + i, struct.unpack('<I', d[i:i+4])[0]))
# Help handler (0x80142468): lui t9,hi(entry); ori t9,t9,lo(entry); jr t9; nop
for i, w in enumerate((0x3c190000 | (entry >> 16), 0x37390000 | (entry & 0xffff), 0x03200008, 0)):
    print('mw.l %08x %08x' % (0xa0142468 + 4 * i, w))
PY
wc -l mw_cmds.txt
