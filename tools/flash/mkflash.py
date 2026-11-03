#!/usr/bin/env python3
"""Make the SPI flash images of the WIWA HD95 flash system (see build_manual.md).

  mkflash.py kernel vmlinux_mini.bin kernel.img   LZMA image for MBoot's LzmaDec
  mkflash.py pad    FILE OUT SIZE                 pad with 0xFF to SIZE (e.g. 0x200000)

LzmaDec unpacks an "LZMA alone" stream and checks a CRC32 (zlib) stored in
the last 4 bytes of the unpacked data; on a mismatch it rewrites the MBoot
environment and resets. The kernel command prints the compressed length,
which goes into the LzmaDec line of the flashboot variable.
"""
import lzma
import struct
import sys
import zlib

KERNEL_PART = 0x130000		# bytes read by flashboot (spi_rdc ... 0x130000)


def pad(data, size):
    if len(data) > size:
        sys.exit(f"{len(data)} bytes do not fit in {size:#x}")
    return data + b"\xff" * (size - len(data))


def kernel(src, dst):
    raw = open(src, "rb").read()
    payload = raw + struct.pack("<I", zlib.crc32(raw))
    c = bytearray(lzma.compress(payload, format=lzma.FORMAT_ALONE, filters=[
        {"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 8 << 20}]))
    c[5:13] = struct.pack("<Q", len(payload))	# known size, as in the stock image
    open(dst, "wb").write(pad(bytes(c), KERNEL_PART))
    print(f"{dst}: LZMA {len(c)} bytes = {len(c):#x} (LzmaDec size), unpacked {len(payload)}")


def main(a):
    if len(a) == 3 and a[0] == "kernel":
        kernel(a[1], a[2])
    elif len(a) == 4 and a[0] == "pad":
        open(a[2], "wb").write(pad(open(a[1], "rb").read(), int(a[3], 0)))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
