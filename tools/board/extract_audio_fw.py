#!/usr/bin/env python3
"""Generate the audio DSP firmware headers for mstar_audio.h from YOUR OWN
flash dump of a WIWA HD95 (MStar MSD7818 / Kronus, stock eCos firmware).

The DSP code and the audio init table belong to MStar, so they are not
distributed; this script takes them out of the stock application in the
flash dump:

  flash 0x8001C, 0x289977 bytes: LZMA ("alone") image of the eCos app,
  loaded at 0x80000180. Inside it:
    0x80371030  MDrv_AUDIO_WriteInitTable table (8-byte entries)
    0x804F6FB8  pau_info "K1 SE system"  (type 0x60)
    0x804F6FEC  pau_info "K1 DEC system" (type 0x50)
    0x804F725C  SE decoder table, 0x34 bytes per entry (type 0x31 = entry 1)
    0x804F7500  DEC decoder table, 0x34 bytes per entry (0x03 mpeg, 0x0b xpcm)
  pau_info = 13 words: cm_addr, cm_len, cm_src, pm_addr, pm_len, pm_src,
  x1_addr, x1_len, x1_src, x2_addr, x2_len, x2_src, name. Lengths are in
  BYTES (3 bytes per 24-bit DSP word).

Usage: extract_audio_fw.py FLASH_DUMP.BIN [OUTPUT_DIR]
Writes audio_init_table.h, dsp_fw.h, dec03_fw.h, se31_fw.h, xpcm_fw.h.
The addresses are for this box's firmware; another firmware version needs
them found again (docs/pl/audio.md).
"""
import lzma
import os
import struct
import sys

APP_OFF, APP_LEN, APP_BASE = 0x8001C, 0x289977, 0x80000180


def load_app(dump):
    data = open(dump, "rb").read()
    app = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(
        data[APP_OFF:APP_OFF + APP_LEN])
    if len(app) != 7324916:
        print("warning: unexpected app size %d (other firmware version?)" % len(app))
    return app


def word(app, addr):
    return struct.unpack("<I", app[addr - APP_BASE:addr - APP_BASE + 4])[0]


def blob(app, addr, n):
    return app[addr - APP_BASE:addr - APP_BASE + n]


def pau(app, addr):
    w = [word(app, addr + 4 * i) for i in range(13)]
    segs = {}
    for k, seg in enumerate(("cm", "pm", "x1", "x2")):
        a, n, src = w[3 * k:3 * k + 3]
        segs[seg] = (a, n, blob(app, src, n) if n else b"", src)
    return segs


def c_array(lines, name, data):
    lines.append("static const uint8_t %s[%d] = {" % (name, len(data)))
    for i in range(0, len(data), 16):
        lines.append("\t" + " ".join("0x%02x," % b for b in data[i:i + 16]))
    lines.append("};")


def segs_header(path, comment, prefix, arrname, segs, order, skip_empty=False):
    lines = ["/* " + comment[0]] + [" * " + c for c in comment[1:-1]] + [" * " + comment[-1] + " */",
                                                                          "#include <stdint.h>"]
    for seg in order:
        a, n, data, _ = segs[seg]
        if skip_empty and not n:
            continue
        S = seg.upper()
        lines.append("#define %s_%s_ADDR 0x%x" % (prefix, S, a))
        lines.append("#define %s_%s_LEN %d" % (prefix, S, n))
        if n:
            c_array(lines, "%s_%s" % (arrname, seg), data)
    open(path, "w").write("\n".join(lines) + "\n")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[2] if len(sys.argv) > 2 else "."
    app = load_app(sys.argv[1])
    P = lambda f: os.path.join(out, f)

    # init table: u32 reg, u8 mask, u8 val, 2 pad; ends with reg 0xffff mask 0
    lines = ["/* MDrv_AUDIO_WriteInitTable of the stock firmware (decompressed.bin 0x80371030).",
             " * reg: 0xAxxxx = RIU 0x00xxxx (PM), 0xBxxxx = 0x10xxxx, else + 0x110000 (audio);",
             " * 0xFFFF with mask 1 = delay of val ms. Generated, see docs/audio.md. */",
             "static const struct { unsigned int reg; unsigned char mask, val; } audio_init_table[] = {"]
    a = 0x80371030
    while True:
        reg, mask, val = struct.unpack("<IBB", blob(app, a, 6))
        if reg == 0xffff and mask == 0:
            break
        lines.append("\t{ 0x%05x, 0x%02x, 0x%02x }," % (reg, mask, val))
        a += 8
    lines.append("};")
    open(P("audio_init_table.h"), "w").write("\n".join(lines) + "\n")

    # SE + DEC system code in one header
    se, dec = pau(app, 0x804F6FB8), pau(app, 0x804F6FEC)
    lines = ["/* K1 SE/DEC system DSP firmware from the stock firmware (decompressed.bin),",
             " * pau_info tables at 0x804F6FB8 (SE, type 0x60) and 0x804F6FEC (DEC, 0x50).",
             " * Lengths are in BYTES (3 bytes per 24-bit DSP word). Generated, see docs/audio.md. */",
             "#include <stdint.h>"]
    for prefix, arr, segs in (("SE", "se", se), ("DEC", "dec", dec)):
        for seg in ("cm", "pm", "x1", "x2"):
            sa, n, data, _ = segs[seg]
            lines.append("#define %s_%s_ADDR 0x%x" % (prefix, seg.upper(), sa))
            lines.append("#define %s_%s_LEN %d" % (prefix, seg.upper(), n))
            if n:
                c_array(lines, "%s_%s" % (arr, seg), data)
    open(P("dsp_fw.h"), "w").write("\n".join(lines) + "\n")

    dec03 = pau(app, 0x804F7500 + 3 * 0x34)
    segs_header(P("dec03_fw.h"),
                ['DEC "mpeg" code (type 0x03), loaded into DEC before the DEC system code by',
                 'MDrv_MAD_Init (0x8001375c -> 0x800136e4(3)). Lengths in BYTES. Generated, see docs/audio.md.'],
                "DEC03", "dec03", dec03, ("cm", "pm", "x1", "x2"))
    se31 = pau(app, 0x804F725C + 1 * 0x34)
    segs_header(P("se31_fw.h"),
                ['SE "mpeg_ad" code (type 0x31), loaded into SE before the SE system code by',
                 'MDrv_SOUND_Init (0x8001580c). Lengths in BYTES. Generated, see docs/audio.md.'],
                "SE31", "se31", se31, ("cm", "pm", "x1", "x2"))
    xp = pau(app, 0x804F7500 + 0x0b * 0x34)
    segs_header(P("xpcm_fw.h"),
                ['xPCM DEC decoder (type 0x0b) from the stock firmware, pau_info "xpcm":',
                 'CM 0x%x bytes at DSP 0x%x (src 0x%x), PM 0x%x bytes at 0x%x (src 0x%x).'
                 % (xp["cm"][1], xp["cm"][0], xp["cm"][3], xp["pm"][1], xp["pm"][0], xp["pm"][3]),
                 'Lengths in BYTES (3 per 24-bit word). Generated, see docs/audio.md.'],
                "XPCM", "xpcm", xp, ("cm", "pm"))
    print("written to %s: audio_init_table.h dsp_fw.h dec03_fw.h se31_fw.h xpcm_fw.h" % out)


if __name__ == "__main__":
    main()
