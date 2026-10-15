#!/usr/bin/env python3
"""Find functions from MStar K1 BSP .o files inside a raw RAM dump.

Relocated instruction fields are masked so the match is address-independent.
Usage: match_libs.py <dump> <base_hex> <obj.o>...
"""
import re
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

MIN_LEN = 16  # skip tiny functions, too ambiguous


def masks_for_section(elf, sec_idx):
    """Return {offset: mask_of_bits_to_wildcard} for relocations in a section."""
    masks = {}
    for rs in elf.iter_sections():
        if not isinstance(rs, RelocationSection) or rs['sh_info'] != sec_idx:
            continue
        for r in rs.iter_relocations():
            t = r['r_info_type']
            off = r['r_offset']
            if t == 4:        # R_MIPS_26 (jal/j)
                m = 0x03FFFFFF
            elif t in (5, 6):  # HI16 / LO16
                m = 0x0000FFFF
            else:             # R_MIPS_32 etc: whole word
                m = 0xFFFFFFFF
            masks[off] = masks.get(off, 0) | m
    return masks


def pattern(code, masks):
    out = b''
    for i in range(0, len(code), 4):
        word = code[i:i + 4]
        m = masks.get(i, 0)
        for b in range(4):
            if (m >> (8 * b)) & 0xFF:
                out += b'.'
            else:
                out += re.escape(word[b:b + 1])
    return re.compile(out, re.DOTALL)


def main():
    dump = open(sys.argv[1], 'rb').read()
    base = int(sys.argv[2], 16)
    for path in sys.argv[3:]:
        elf = ELFFile(open(path, 'rb'))
        symtab = elf.get_section_by_name('.symtab')
        for sym in symtab.iter_symbols():
            if sym['st_info']['type'] != 'STT_FUNC' or sym['st_size'] < MIN_LEN:
                continue
            idx = sym['st_shndx']
            sec = elf.get_section(idx)
            data = sec.data()
            masks_all = masks_for_section(elf, idx)
            start, size = sym['st_value'], sym['st_size']
            code = data[start:start + size]
            masks = {o - start: m for o, m in masks_all.items() if start <= o < start + size}
            hits = [m.start() for m in pattern(code, masks).finditer(dump)]
            hits = [h for h in hits if h % 4 == 0]
            if len(hits) == 1:
                status = '0x%08X' % (base + hits[0])
            elif hits:
                status = 'AMBIG(%d): ' % len(hits) + ' '.join('0x%08X' % (base + h) for h in hits[:4])
            else:
                status = '-'
            print('%-14s %-40s %5d  %s' % (path.split('/')[-1], sym.name, size, status))


if __name__ == '__main__':
    main()
