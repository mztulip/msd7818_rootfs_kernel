"""Instruction decoder for the MStar Kronus audio DSP (24-bit words).

The core is an Analog Devices ADSP-21xx variant (README.md, "Rdzeń ADSP-21xx"):
register numbering, ALU/MAC function codes and most types as in the ADSP-218x
(datasheets/ADSP-218x DSP_instruction_set_reference.pdf, appendix A), some
encodings as in the ADSP-219x (datasheets/ADSP_219x_ISR__web_.pdf, chapter 8):
16-bit direct DM addresses (type 3), PC-relative JUMP/CALL, register-file
ALU/MAC (type 9a), shifts with a 4-bit XREG (types 15, 16).

Shared by dis.py (text) and emu.py (execution). decode(word, pc) returns an
Insn; Insn.op is None for words that are not decoded yet. Insn.verified says
whether the encoding is confirmed by board results (dsptest, dspfuzz,
condtest); the emulator runs unverified ones only with all_hypo.
"""

DREG = ('AX0', 'AX1', 'MX0', 'MX1', 'AY0', 'AY1', 'MY0', 'MY1',
        'SI', 'SE', 'AR', 'MR0', 'MR1', 'MR2', 'SR0', 'SR1')
AX0, AX1, MX0, MX1, AY0, AY1, MY0, MY1, SI, SE, AR, MR0, MR1, MR2, SR0, SR1 = range(16)

# register groups (218x table A-51; REG3 partly)
REG1 = ('I0', 'I1', 'I2', 'I3', 'M0', 'M1', 'M2', 'M3', 'L0', 'L1', 'L2', 'L3', None, None, 'PMOVLAY', 'DMOVLAY')
REG2 = ('I4', 'I5', 'I6', 'I7', 'M4', 'M5', 'M6', 'M7', 'L4', 'L5', 'L6', 'L7', None, None, None, None)
REG3 = ('ASTAT', 'MSTAT', 'SSTAT', 'IMASK', 'ICNTL', 'CNTR', 'SB', 'PX',
        'RX0', 'TX0', 'RX1', 'TX1', 'IFC', 'OWRCNTR', None, None)
GROUPS = (DREG, REG1, REG2, REG3)

COND = ('EQ', 'NE', 'GT', 'LE', 'LT', 'GE', 'AV', 'NOT AV', 'AC', 'NOT AC',
        'NEG', 'POS', 'MV', 'NOT MV', 'NOT CE', '')

# operand selectors: ('d', dreg) register, ('af',) / ('mf',) feedback, ('k', value) constant
ALU_X = (AX0, AX1, AR, MR0, MR1, MR2, SR0, SR1)
MAC_X = (MX0, MX1, AR, MR0, MR1, MR2, SR0, SR1)
ALU_Y = (('d', AY0), ('d', AY1), ('af',), ('k', 0))
MAC_Y = (('d', MY0), ('d', MY1), ('mf',), ('k', 0))

AMF_TEXT = {
    0x01: '{x} * {y} (RND)', 0x02: '{mr} + {x} * {y} (RND)', 0x03: '{mr} - {x} * {y} (RND)',
    0x04: '{x} * {y} (SS)', 0x05: '{x} * {y} (SU)', 0x06: '{x} * {y} (US)', 0x07: '{x} * {y} (UU)',
    0x08: '{mr} + {x} * {y} (SS)', 0x09: '{mr} + {x} * {y} (SU)', 0x0A: '{mr} + {x} * {y} (US)',
    0x0B: '{mr} + {x} * {y} (UU)', 0x0C: '{mr} - {x} * {y} (SS)', 0x0D: '{mr} - {x} * {y} (SU)',
    0x0E: '{mr} - {x} * {y} (US)', 0x0F: '{mr} - {x} * {y} (UU)',
    0x10: 'PASS {y}', 0x11: '{y} + 1', 0x12: '{x} + {y} + C', 0x13: '{x} + {y}', 0x14: 'NOT {y}',
    0x15: '-{y}', 0x16: '{x} - {y} + C - 1', 0x17: '{x} - {y}', 0x18: '{y} - 1', 0x19: '{y} - {x}',
    0x1A: '{y} - {x} + C - 1', 0x1B: 'NOT {x}', 0x1C: '{x} AND {y}', 0x1D: '{x} OR {y}',
    0x1E: '{x} XOR {y}', 0x1F: 'ABS {x}',
}
Y_ONLY = (0x10, 0x11, 0x14, 0x15, 0x18)	# ALU functions that use only y


def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def opname(o):
    if o[0] == 'd':
        return DREG[o[1]]
    if o[0] == 'k':
        return '0x%04x' % o[1] if o[1] else '0'
    return o[0].upper()


def regname(g, c):
    n = GROUPS[g][c]
    return n if n else 'REG%d[%x]' % (g, c)


class Insn:
    # plain class, not a dataclass: dataclasses imports the stdlib "dis",
    # which our dis.py shadows when run from this directory
    def __init__(self, word):
        self.word = word
        self.op = None		# emulator operation, None = unknown
        self.args = {}
        self.verified = False
        self.text = ''


def _compute(i, w, z, amf, x, y, cond=0xF):
    """ALU (AMF >= 0x10) or MAC operation; fills i.args/i.text."""
    alu = amf >= 0x10
    dst = ('AF' if z else 'AR') if alu else ('MF' if z else 'MR')
    if not alu and amf == 0:
        body = 'NOP'
    else:
        body = AMF_TEXT[amf].format(x=opname(x), y=opname(y), mr='MR')
    i.op = 'alu' if alu else 'mac'
    i.args = {'z': z, 'amf': amf, 'x': x, 'y': y, 'cond': cond}
    pre = 'IF %s ' % COND[cond] if cond != 0xF else ''
    i.text = '%s%s = %s' % (pre, dst, body)


def _type9(i, w):
    z, amf = (w >> 18) & 1, (w >> 13) & 0x1F
    yy, xx, low, cond = (w >> 11) & 3, (w >> 8) & 7, (w >> 4) & 0xF, w & 0xF
    alu = amf >= 0x10
    X = ('d', (ALU_X if alu else MAC_X)[xx])
    if low == 0:			# xop, yop
        _compute(i, w, z, amf, X, (ALU_Y if alu else MAC_Y)[yy], cond)
    elif low == 1 and not alu and yy == 0:	# MAC squaring xop * xop
        _compute(i, w, z, amf, X, X, cond)
    elif low & 3 in (1, 3):		# constant: YY, CC = bit number, BO = sign (219x table 8-4)
        n = yy << 2 | (low >> 2)
        k = 1 << n if low & 3 == 1 else ~(1 << n) & 0xFFFF
        _compute(i, w, z, amf, X, ('k', k), cond)
    elif low & 3 == 2:		# BO = 10: constant 0 if YY < 2, else 0xFFFF (board, all 16 bit numbers)
        _compute(i, w, z, amf, X, ('k', 0xFFFF if yy & 2 else 0), cond)
    elif (low >> 1) & 3 == 2:	# 9a: register file, XREG bits 11:8, YREG bits 3:0 (219x)
        xr = (w >> 8) & 0xF
        y = ('k', 0) if (w >> 12) & 1 else ('d', w & 0xF)
        if alu and amf in Y_ONLY:	# "Dreg + 1" etc. take the operand from XREG (dspfuzz 222xcf)
            y = ('d', xr)
        _compute(i, w, z, amf, ('d', xr), y)
        i.args['form'] = '9a'
        i.verified = True
        return True
    else:
        return False
    i.verified = alu or amf == 0 or xx < 2	# MAC xop AR/MR*/SR*: MR = 0 on the board, meaning unknown
    return True


def decode(w, pc=0):
    i = Insn(w)
    t4, t8 = w >> 20, w >> 16
    if w == 0:
        i.op, i.verified, i.text = 'nop', True, 'NOP'
    elif t4 == 0x4:			# type 6: dreg = data16
        r = w & 0xF
        i.op, i.args, i.verified = 'ldi', {'r': r, 'imm': (w >> 4) & 0xFFFF}, r not in (SE, MR2)
        i.text = '%s = 0x%04x' % (DREG[r], (w >> 4) & 0xFFFF)
    elif t4 in (0xA, 0xB):		# REG1/REG2 = data16 (MStar; init writes M=0,1,-1,-2, L=0)
        g, r = t4 - 0x9, w & 0xF
        i.op, i.args, i.verified = 'ldr', {'g': g, 'r': r, 'imm': (w >> 4) & 0xFFFF}, r < 12	# board: M1 = 0x1234
        i.text = '%s = 0x%04x' % (regname(g, r), (w >> 4) & 0xFFFF)
    elif t4 == 0x3:			# Ireg/Mreg = DM(addr16) (MStar; register code as 219x table 8-9) ?
        r, a = w & 0xF, (w >> 4) & 0xFFFF	# the addresses hold code pointers set by init, e.g. DM(0x2A8E)
        g, c = (1 if r & 4 == 0 else 2), (r & 3) | (4 if r & 8 else 0)	# = 0x0056 (NOP; RTS stub)
        i.op, i.args, i.verified = 'ldr_dm', {'g': g, 'r': c, 'a': a}, True	# board: I5 = DM(0x0123)
        i.text = '%s = DM(0x%04x)' % (regname(g, c), a)
    elif t4 in (0x8, 0x9):		# type 3 (219x): DM(addr16) <-> dreg
        d, a, r = (w >> 20) & 1, (w >> 4) & 0xFFFF, w & 0xF
        i.op, i.args, i.verified = ('st' if d else 'ld'), {'r': r, 'a': a}, True
        i.text = 'DM(0x%04x) = %s' % (a, DREG[r]) if d else '%s = DM(0x%04x)' % (DREG[r], a)
    elif t8 >> 3 == 0x4:		# type 9 / 9a: conditional ALU/MAC
        if not _type9(i, w):
            i.text = 'type9 form %x?' % ((w >> 4) & 0xF)
    elif t8 >> 3 == 0x5:		# type 8: ALU/MAC with dreg move, or NONE = ALU
        z, amf = (w >> 18) & 1, (w >> 13) & 0x1F
        yy, xx = (w >> 11) & 3, (w >> 8) & 7
        alu = amf >= 0x10
        _compute(i, w, z, amf, ('d', (ALU_X if alu else MAC_X)[xx]), (ALU_Y if alu else MAC_Y)[yy])
        if w & 0xFF == 0xAA and alu:
            i.args['none'] = True
            i.text = 'NONE = ' + i.text.split(' = ', 1)[1]
        else:
            i.args['move'] = ((w >> 4) & 0xF, w & 0xF)
            i.text += ', %s = %s' % (DREG[(w >> 4) & 0xF], DREG[w & 0xF])
        i.verified = alu or amf == 0 or xx < 2
    elif w >> 19 == 0x3:		# type 10: JUMP/CALL, 14-bit PC-relative within a 16K page (MStar), COND
        s, f14, c = (w >> 18) & 1, (w >> 4) & 0x3FFF, w & 0xF
        tgt = (pc & ~0x3FFF) | ((pc + sx(f14, 14)) & 0x3FFF)
        i.op, i.args = 'jump', {'call': s, 'target': tgt, 'field': f14, 'cond': c}
        i.verified = True			# JUMP and CALL both checked on the board
        i.text = '%s%s 0x%04x' % ('IF %s ' % COND[c] if c != 0xF else '', 'CALL' if s else 'JUMP', tgt)
    elif t8 >> 2 == 0x5:		# type 11: DO end UNTIL term (218x layout, end PC-relative like JUMP) ?
        f14, t = (w >> 4) & 0x3FFF, w & 0xF
        end = (pc & ~0x3FFF) | ((pc + sx(f14, 14)) & 0x3FFF)
        i.op, i.args, i.verified = 'do', {'end': end, 'term': t}, t == 0xE	# board: 3 runs with CNTR = 3
        i.text = 'DO 0x%04x UNTIL %s  ?' % (end, ('NE', 'EQ', 'LE', 'GT', 'GE', 'LT', 'NOT AV', 'AV', 'NOT AC',
                                                   'AC', 'POS', 'NEG', 'NOT MV', 'MV', 'CE', 'FOREVER')[t])
    elif t8 == 0x01:			# type 29 (218x): IO(addr11) <-> dreg ?
        d, a, r = (w >> 15) & 1, (w >> 4) & 0x7FF, w & 0xF
        i.op, i.args = ('io_w' if d else 'io_r'), {'a': a, 'r': r}
        i.text = ('IO(0x%03x) = %s' % (a, DREG[r]) if d else '%s = IO(0x%03x)' % (DREG[r], a)) + '  ?'
    elif t8 == 0x0A and w & 0xFFE0 == 0:	# type 20: RTS / RTI
        t, c = (w >> 4) & 1, w & 0xF
        i.op, i.args = 'ret', {'rti': t, 'cond': c}
        i.verified = c == 0xF			# RTI stops our programs; RTS after CALL checked on the board
        i.text = '%s%s' % ('IF %s ' % COND[c] if c != 0xF else '', 'RTI' if t else 'RTS')
    elif t8 == 0x0D and w & 0xF000 == 0:	# type 17: reg = reg
        dg, sg, dr, sr = (w >> 10) & 3, (w >> 8) & 3, (w >> 4) & 0xF, w & 0xF
        i.op, i.args = 'move', {'dg': dg, 'dr': dr, 'sg': sg, 'sr': sr}
        i.verified = dg < 3 and sg < 3 or (dg, dr) == (3, 5)	# board: M5, I5, CNTR
        i.text = '%s = %s' % (regname(dg, dr), regname(sg, sr))
    elif t8 == 0x0C and w & 3 == 0:	# type 18: ENA/DIS modes
        names = ('GM', 'SEC_REG', 'BIT_REV', 'AV_LATCH', 'AR_SAT', 'M_MODE', 'TIMER')
        ena, dis = [], []
        for k, n in enumerate(names):
            f = (w >> (2 + 2 * k)) & 3
            (ena if f == 3 else dis if f == 2 else []).append(n)
        i.op, i.args = 'mode', {'ena': ena, 'dis': dis}
        i.text = ' '.join(x for x in ('ENA ' + ', '.join(ena) if ena else '', 'DIS ' + ', '.join(dis) if dis else '') if x)
    elif t8 == 0x02 and w & 0xFFF0 == 0x8000:	# type 31: IDLE
        i.op, i.text = 'idle', 'IDLE'
    elif t8 == 0x0B and w & 0xFF20 == 0:	# type 19: JUMP/CALL (I4..I7) (218x)
        r, s, c = (w >> 6) & 3, (w >> 4) & 1, w & 0xF
        i.op, i.args, i.verified = 'jumpi', {'i': 4 + r, 'call': s, 'cond': c}, True	# board: CALL (I4)
        i.text = '%s%s (I%d)' % ('IF %s ' % COND[c] if c != 0xF else '', 'CALL' if s else 'JUMP', 4 + r)
    elif t8 == 0x0F:			# type 15 (219x layout): SR = shift XREG BY imm8
        sf, xr, e = (w >> 12) & 0xF, (w >> 8) & 0xF, sx(w & 0xFF, 8)
        if sf < 0xC:			# board (res_board_day3.log): SF 0-B all shift logically;
            i.op, i.args = 'shift', {'sf': sf & 3, 'x': xr, 'e': e, 'cond': 0xF}	# bit 0 OR, bit 1 LO
            i.verified = True
            i.text = 'SR = %sLSHIFT %s BY %d (%s)%s' % ('SR OR ' if sf & 1 else '', DREG[xr], e,
                                                     'LO' if sf & 2 else 'HI', '  [SF=%x]' % sf if sf & 0xC else '')
        else:
            i.text = 'SE = EXP %s?  [SF=%x]' % (DREG[xr], sf)
    elif t8 == 0x0E and w & 0xF0 == 0:	# type 16 (219x layout): IF cond SR = shift XREG (by SE)
        sf, xr, c = (w >> 12) & 0xF, (w >> 8) & 0xF, w & 0xF
        if sf < 8:
            i.op, i.args = 'shift', {'sf': sf, 'x': xr, 'e': None, 'cond': c}
            i.text = '%sSR = %s%s %s (%s)' % ('IF %s ' % COND[c] if c != 0xF else '', 'SR OR ' if sf & 1 else '',
                                              'ASHIFT' if sf & 4 else 'LSHIFT', DREG[xr], 'LO' if sf & 2 else 'HI')
    elif t8 >> 5 == 0x3:		# type 4: compute, dreg <-> DM(I, M)
        g, d, z, amf = (w >> 20) & 1, (w >> 19) & 1, (w >> 18) & 1, (w >> 13) & 0x1F
        yy, xx, r, ir, mr = (w >> 11) & 3, (w >> 8) & 7, (w >> 4) & 0xF, (w >> 2) & 3, w & 3
        ix, mx = 4 * g + ir, 4 * g + mr
        alu = amf >= 0x10
        if amf:
            _compute(i, w, z, amf, ('d', (ALU_X if alu else MAC_X)[xx]), (ALU_Y if alu else MAC_Y)[yy])
            comp = i.text + ', '
        else:
            comp = ''
        mem = 'DM(I%d, M%d)' % (ix, mx)
        i.op = 'dag'
        i.args.update({'d': d, 'r': r, 'i': ix, 'm': mx, 'amf': amf})
        i.text = comp + ('%s = %s' % (mem, DREG[r]) if d else '%s = %s' % (DREG[r], mem))
    elif w >> 8 == 0x0780:		# page prefix: next JUMP/CALL goes to (NN << 14) | field (absolute);
        i.op, i.args, i.verified = 'page', {'nn': w & 0xFF}, True	# board: 078000 + CALL -> absolute 5
        i.text = 'PAGE 0x%02x' % (w & 0xFF)
    elif t8 == 0x07:
        i.text = 'type 0x07? %04x' % (w & 0xFFFF)
    return i
