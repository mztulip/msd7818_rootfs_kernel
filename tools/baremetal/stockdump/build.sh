#!/bin/sh
# Build the stock firmware register-dump patch and print MBoot mw.l commands.
set -e
cd "$(dirname "$0")"
mipsel-linux-gnu-as -march=mips32r2 -EL -o stockdump.o stockdump.S
mipsel-linux-gnu-ld -EL -T stockdump.ld -o stockdump.elf stockdump.o
mipsel-linux-gnu-objcopy -O binary -j .text stockdump.elf text.bin
ls -l text.bin; [ $(stat -c %s text.bin) -le 204 ] || { echo 'too big (max 204 B)'; exit 1; }
python3 - <<'PY' > mw_cmds.txt
import struct
for f, base in (('text.bin', 0x80142468),):
    d = open(f, 'rb').read()
    d += b'\0' * (-len(d) % 4)
    for i in range(0, len(d), 4):
        # uncached KSEG1 alias so the write goes straight to RAM
        print('mw.l %08x %08x' % (base - 0x80000000 + 0xa0000000 + i, struct.unpack('<I', d[i:i+4])[0]))
PY
wc -l mw_cmds.txt
