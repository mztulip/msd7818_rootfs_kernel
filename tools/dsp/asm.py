#!/usr/bin/env python3
"""Small assembler for the SE DSP (syntax as printed by isa.py / dis.py).

Supported (one instruction per line, ';' or '#' starts a comment):
  label:                         .org ADDR      .word 0x123456
  NOP   RTS   RTI   IF cond RTS/RTI
  [IF cond] JUMP label|addr      [IF cond] CALL label|addr   (14-bit PC-relative)
  DREG = 0x1234                  REG1/REG2 = 0x1234 (I0..I7, M0..M7, L0..L7)
  DREG = DM(0xB210)              DM(0xB210) = DREG
  DREG = DM(In, Mm)              DM(In, Mm) = DREG          (type 4, no compute)
  AR|AF = X op Y                 op: + - AND OR XOR; X/Y any DREG (type 9a) or
                                 xop/yop with a power-of-two constant (type 9)
  AR|AF = X + 1 / X - 1 / PASS X / NOT X / ABS X
  NONE = X - Y                   (compare, type 8)
  REG = REG                      (type 17, any group)
  SR = [SR OR] LSHIFT X BY n (HI|LO)   (type 15)
  ENA|DIS mode[, mode]           IO(0x007) = DREG, DREG = IO(0x008)
Every encoding is checked by decoding it again (isa.decode) before it is emitted.
Usage: asm.py FILE.s [-o FILE.hex]   (prints address, word, decoded text)
"""
import re
import sys

from isa import decode, DREG, GROUPS, COND, ALU_X, ALU_Y, AMF_TEXT

ALU_OPS = {'+': 0x13, '-': 0x17, 'AND': 0x1C, 'OR': 0x1D, 'XOR': 0x1E}
UNARY = {'+ 1': 0x11, '- 1': 0x18}
MODES = ('GM', 'SEC_REG', 'BIT_REV', 'AV_LATCH', 'AR_SAT', 'M_MODE', 'TIMER')


class AsmError(Exception):
    pass


def num(s):
    return int(s, 0)


def dreg(s):
    s = s.strip().upper()
    if s not in DREG:
        raise AsmError('not a data register: %s' % s)
    return DREG.index(s)


def anyreg(s):
    s = s.strip().upper()
    for g, names in enumerate(GROUPS):
        if s in names:
            return g, names.index(s)
    raise AsmError('unknown register: %s' % s)


def const_code(k):
    """YY, CC, BO for a type 9 constant (219x table 8-4), or None."""
    for n in range(16):
        if k == 1 << n:
            return n >> 2, n & 3, 1
        if k == ~(1 << n) & 0xFFFF:
            return n >> 2, n & 3, 3
    return None


def cond_prefix(line):
    m = re.match(r'IF\s+(NOT\s+\w+|\w+)\s+(.*)', line, re.I)
    if not m:
        return 0xF, line
    c = m.group(1).upper()
    if c not in COND:
        raise AsmError('unknown condition %s' % c)
    return COND.index(c), m.group(2)


def encode(line, pc, labels):
    u = line.upper().strip()
    c, body = cond_prefix(u)
    if body == 'NOP':
        return 0x000000
    if body in ('RTS', 'RTI'):
        return 0x0A0000 | (body == 'RTI') << 4 | c
    m = re.match(r'(JUMP|CALL)\s+(\S+)$', body)
    if m:
        t = labels[m.group(2).lower()] if m.group(2).lower() in labels else num(m.group(2))
        off = t - pc
        if not -0x2000 <= off < 0x2000 or (t ^ pc) & ~0x3FFF:
            raise AsmError('target 0x%x out of range from 0x%x' % (t, pc))
        return 0x180000 | (m.group(1) == 'CALL') << 18 | (off & 0x3FFF) << 4 | c
    if c != 0xF:
        raise AsmError('condition only for JUMP/CALL/RTS/RTI here')
    m = re.match(r'SR\s*=\s*(SR OR\s+)?LSHIFT\s+(\w+)\s+BY\s+(-?\w+)\s*\((HI|LO)\)$', body)
    if m:				# type 15 (board: SF bit 0 OR, bit 1 LO, always logical)
        sf = (1 if m.group(1) else 0) | (2 if m.group(4) == 'LO' else 0)
        return 0x0F0000 | sf << 12 | dreg(m.group(2)) << 8 | (num(m.group(3)) & 0xFF)
    m = re.match(r'(ENA|DIS)\s+(.*)', body)
    if m:
        w = 0x0C0000
        for name in m.group(2).split(','):
            w |= (3 if m.group(1) == 'ENA' else 2) << (2 + 2 * MODES.index(name.strip()))
        return w
    m = re.match(r'IO\((\w+)\)\s*=\s*(\w+)$', body)
    if m:
        return 0x018000 | (num(m.group(1)) & 0x7FF) << 4 | dreg(m.group(2))
    m = re.match(r'(\w+)\s*=\s*IO\((\w+)\)$', body)
    if m:
        return 0x010000 | (num(m.group(2)) & 0x7FF) << 4 | dreg(m.group(1))
    m = re.match(r'DM\((0X[0-9A-F]+|\d+)\)\s*=\s*(\w+)$', body)
    if m:
        return 0x900000 | num(m.group(1)) << 4 | dreg(m.group(2))
    m = re.match(r'(\w+)\s*=\s*DM\((0X[0-9A-F]+|\d+)\)$', body)
    if m:
        return 0x800000 | num(m.group(2)) << 4 | dreg(m.group(1))
    m = re.match(r'DM\(I(\d),\s*M(\d)\)\s*=\s*(\w+)$', body)
    if m:
        i, mm = int(m.group(1)), int(m.group(2))
        if i >> 2 != mm >> 2:
            raise AsmError('I and M from different DAGs')
        return 0x680000 | (i >> 2) << 20 | dreg(m.group(3)) << 4 | (i & 3) << 2 | (mm & 3)
    m = re.match(r'(\w+)\s*=\s*DM\(I(\d),\s*M(\d)\)$', body)
    if m:
        i, mm = int(m.group(2)), int(m.group(3))
        if i >> 2 != mm >> 2:
            raise AsmError('I and M from different DAGs')
        return 0x600000 | (i >> 2) << 20 | dreg(m.group(1)) << 4 | (i & 3) << 2 | (mm & 3)
    m = re.match(r'(AR|AF|NONE)\s*=\s*(.+)$', body)
    if m and not re.match(r'(0X[0-9A-F]+|\d+|\w+)$', m.group(2).strip()):
        return alu(m.group(1), m.group(2).strip())
    m = re.match(r'(\w+)\s*=\s*(0X[0-9A-F]+|\d+)$', body)
    if m:
        v = num(m.group(2)) & 0xFFFF
        g, r = anyreg(m.group(1))
        if g == 0:
            return 0x400000 | v << 4 | r
        if g in (1, 2) and r < 12:
            return (0xA00000 if g == 1 else 0xB00000) | v << 4 | r
        raise AsmError('no immediate load for %s' % m.group(1))
    m = re.match(r'(\w+)\s*=\s*(\w+)$', body)
    if m:
        dg, dr = anyreg(m.group(1))
        sg, sr = anyreg(m.group(2))
        return 0x0D0000 | dg << 10 | sg << 8 | dr << 4 | sr
    raise AsmError('cannot assemble: %s' % line)


def alu(dst, expr):
    z = 1 if dst == 'AF' else 0
    for tail, amf in UNARY.items():
        if expr.endswith(tail):
            x = dreg(expr[:-len(tail)])
            return 0x200000 | z << 18 | amf << 13 | x << 8 | 0xCF	# 9a, operand in XREG
    m = re.match(r'(PASS|NOT|ABS)\s+(\w+)$', expr)
    if m:
        x = dreg(m.group(2))
        amf = {'PASS': 0x10, 'NOT': 0x1B, 'ABS': 0x1F}[m.group(1)]
        return 0x200000 | z << 18 | amf << 13 | x << 8 | 0xCF
    m = re.match(r'(\w+)\s*(\+|-|AND|OR|XOR)\s*(\w+)$', expr)
    if not m:
        raise AsmError('cannot assemble ALU expression %s' % expr)
    amf = ALU_OPS[m.group(2)]
    xs, ys = m.group(1), m.group(3)
    if dst == 'NONE':			# type 8 status only: xop / yop restricted
        x, y = ALU_X.index(dreg(xs)), [o[1] if o[0] == 'd' else None for o in ALU_Y].index(dreg(ys))
        return 0x280000 | amf << 13 | y << 11 | x << 8 | 0xAA
    if re.match(r'(0X[0-9A-F]+|\d+)$', ys):	# constant
        k = const_code(num(ys) & 0xFFFF)
        if k is None or dreg(xs) not in ALU_X:
            raise AsmError('constant %s not encodable (powers of two only)' % ys)
        yy, cc, bo = k
        return 0x200000 | z << 18 | amf << 13 | yy << 11 | ALU_X.index(dreg(xs)) << 8 | cc << 6 | bo << 4 | 0xF
    x, y = dreg(xs), dreg(ys)
    if x in ALU_X and y in (4, 5):	# type 9 xop/yop form (AY0/AY1), as the stock code uses
        return 0x200000 | z << 18 | amf << 13 | (y - 4) << 11 | ALU_X.index(x) << 8 | 0x0F
    return 0x200000 | z << 18 | amf << 13 | x << 8 | 0x40 | y	# 9a: two DREGs


def assemble(text):
    lines, labels, pc = [], {}, 0
    for raw in text.splitlines():
        line = re.split(r'[;#]', raw, maxsplit=1)[0].strip()
        if not line:
            continue
        m = re.match(r'(\w+):\s*(.*)', line)
        if m and m.group(1).upper() not in ('IF',):
            labels[m.group(1).lower()] = pc
            line = m.group(2).strip()
            if not line:
                continue
        if line.lower().startswith('.org'):
            pc = num(line.split()[1])
            continue
        lines.append((pc, line))
        pc += 1
    words = {}
    for pc, line in lines:
        if line.lower().startswith('.word'):
            words[pc] = num(line.split()[1])
            continue
        try:
            w = encode(line, pc, labels)
        except (AsmError, KeyError, ValueError) as e:
            raise AsmError('0x%04x "%s": %s' % (pc, line, e))
        if decode(w, pc).op is None:
            raise AsmError('0x%04x "%s" assembled to %06x, which does not decode' % (pc, line, w))
        words[pc] = w
    return words, labels


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    words, labels = assemble(open(argv[0]).read())
    out = argv[argv.index('-o') + 1] if '-o' in argv else None
    for a in sorted(words):
        print('%04x: %06x  %s' % (a, words[a], decode(words[a], a).text))
    if out:
        top = max(words) + 1
        open(out, 'w').write(' '.join('%06x' % words.get(a, 0) for a in range(top)) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
