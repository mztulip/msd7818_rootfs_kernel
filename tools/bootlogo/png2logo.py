#!/usr/bin/env python3
"""Convert any image (PNG, JPG, ...) into a boot logo for the MStar MBoot.

MBoot shows a baseline JPEG, 720x576, YCbCr 4:4:4, read from SPI flash at
0x70000 (64 KB slot), stretched to the whole 16:9 screen. The input is
treated as a 16:9 picture: it is fitted (letterboxed on --bg) into 16:9 and
squashed to 720x576, so it looks right on the TV. The JPEG quality is
lowered until the file fits in the slot.

Usage: png2logo.py input.png [-o logo.jpg] [--bg 000000] [--stretch]
Prints the values needed in MBoot (LOGO_BIN_SIZE and the CRC32 of the
64 KB block), see docs/logo_startowe.md.
"""
import argparse
import io
import zlib

from PIL import Image

SLOT = 0x10000
W, H = 720, 576


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("-o", "--output", default="logo.jpg")
    ap.add_argument("--bg", default="000000", help="letterbox colour, hex RRGGBB")
    ap.add_argument("--stretch", action="store_true",
                    help="ignore the aspect ratio, just resize to 720x576")
    a = ap.parse_args()

    src = Image.open(a.input).convert("RGBA")
    bg = tuple(int(a.bg[i:i + 2], 16) for i in (0, 2, 4))
    if a.stretch:
        canvas = Image.new("RGB", src.size, bg)
        canvas.paste(src, (0, 0), src)
    else:
        # fit into a 16:9 canvas, then squash it to 720x576
        cw = max(src.width, round(src.height * 16 / 9))
        ch = max(src.height, round(cw * 9 / 16))
        canvas = Image.new("RGB", (cw, ch), bg)
        canvas.paste(src, ((cw - src.width) // 2, (ch - src.height) // 2), src)
    img = canvas.resize((W, H), Image.LANCZOS)

    for q in range(92, 39, -3):
        buf = io.BytesIO()
        img.save(buf, "JPEG", quality=q, subsampling=0, progressive=False, optimize=False)
        data = buf.getvalue()
        if len(data) < SLOT:
            break
    else:
        raise SystemExit("does not fit in 64 KB even at quality 40, simplify the picture")

    open(a.output, "wb").write(data)
    block = data + bytes(SLOT - len(data))
    print(f"{a.output}: {len(data)} bytes, quality {q}")
    print(f"MBoot: setenv LOGO_BIN_SIZE {len(data):X}")
    print(f"CRC32 of the JPEG:            {zlib.crc32(data):08X}")
    print(f"CRC32 of the 64 KB flash block: {zlib.crc32(block):08X}")


if __name__ == "__main__":
    main()
