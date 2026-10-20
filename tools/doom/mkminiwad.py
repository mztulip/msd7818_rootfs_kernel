#!/usr/bin/env python3
"""
mkminiwad - cut Freedoom Phase 1 down to a few E1 maps, small enough for the
2 MB squashfs rootfs in the SPI flash (docs/plan_mini_system.md).

Input: Freedoom: Phase 1 (https://freedoom.github.io/), tested with v0.13.0.
Copyright (c) 2001-2024 Contributors to the Freedoom project, BSD-3-Clause.
The output is a modified Freedoom: when it is distributed, ship the Freedoom
COPYING (here FREEDOOM_COPYING.txt) with it; credits are in
FREEDOOM_CREDITS*.txt. See README.md. This script itself is our own code.

The result is an IWAD with only E1 maps, which
Chocolate Doom / fbDOOM identifies as "shareware" (no E3M1/E4M1), so the
menu offers only episode 1 and the shareware switch list applies.

Kept:
  - the chosen maps E1M1..E1Mn and their music;
  - only the wall textures (TEXTURE1/2 entries and patches) those maps use,
    plus the episode-1 switch textures (P_InitSwitchList needs them) and
    complete animation ranges (p_spec.c animdefs) that touch a used texture;
  - only the used flats, plus F_SKY1, the E1 finale flat and whole animated
    flat ranges;
  - all sprites except the monsters that do not appear on the chosen maps
    (and their projectiles), and except sprites the engine never uses;
  - all sound effects, resampled to 11025 Hz (the original Doom rate; many
    Freedoom sounds are 22/44 kHz). Sounds of the removed monsters become
    short silence (a missing sound lump is fatal when it is played);
  - menus, status bar, fonts, the E1 intermission screen;
  - DEMO1..3 replaced by empty demos for E1M1 (the originals play other maps).

Usage: mkminiwad.py freedoom1.wad out.wad [NMAPS]
"""
import lzma
import struct
import sys

SWITCHES_E1 = ["BRCOM", "BRN1", "BRN2", "BRNGN", "BROWN", "COMM", "COMP", "DIRT", "EXIT",
               "GRAY", "GRAY1", "METAL", "PIPE", "SLAD", "STARG", "STON1", "STON2", "STONE",
               "STRTN"]
FLAT_ANIMS = [("NUKAGE1", "NUKAGE3"), ("FWATER1", "FWATER4"), ("SWATER1", "SWATER4"),
              ("LAVA1", "LAVA4"), ("BLOOD1", "BLOOD3"), ("RROCK05", "RROCK08"),
              ("SLIME01", "SLIME04"), ("SLIME05", "SLIME08"), ("SLIME09", "SLIME12")]
TEX_ANIMS = [("BLODGR1", "BLODGR4"), ("SLADRIP1", "SLADRIP3"), ("BLODRIP1", "BLODRIP4"),
             ("FIREWALA", "FIREWALL"), ("GSTFONT1", "GSTFONT3"), ("FIRELAV3", "FIRELAVA"),
             ("FIREMAG1", "FIREMAG3"), ("FIREBLU1", "FIREBLU2"), ("ROCKRED1", "ROCKRED3"),
             ("BFALL1", "BFALL4"), ("SFALL1", "SFALL4"), ("WFALL1", "WFALL4"),
             ("DBRAIN1", "DBRAIN4")]
# monster doomednum -> sprites only it uses, sounds only it uses
MONSTERS = {
    3006: (["SKUL"], ["DSSKLATK", "DSSKLDTH"]),
    3005: (["HEAD", "BAL2"], ["DSCACSIT", "DSCACDTH"]),
    3003: (["BOSS", "BAL7"], ["DSBRSSIT", "DSBRSDTH"]),
    16: (["CYBR"], ["DSCYBSIT", "DSCYBDTH", "DSHOOF"]),
    7: (["SPID"], ["DSSPISIT", "DSSPIDTH"]),
}
UNUSED_SPRITES = ["PLYC", "MANF", "BOSF"]	# not in sprnames / Doom II only
SFX_RATE = 11025
MAPLUMPS = ["THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES",
            "SECTORS", "REJECT", "BLOCKMAP"]


def name8(b):
    return b.split(b"\0")[0].decode("latin1").upper()


def read_wad(fn):
    d = open(fn, "rb").read()
    _, n, off = struct.unpack("<4sii", d[:12])
    out = []
    for i in range(n):
        p, s, nm = struct.unpack("<ii8s", d[off + 16 * i:off + 16 * i + 16])
        out.append((name8(nm), d[p:p + s]))
    return out


def write_wad(fn, lumps):
    data = b""
    dirs = b""
    pos = 12
    for nm, b in lumps:
        dirs += struct.pack("<ii8s", pos, len(b), nm.encode("latin1"))
        data += b
        pos += len(b)
    open(fn, "wb").write(struct.pack("<4sii", b"IWAD", len(lumps), pos) + data + dirs)


def resample(b, rate):
    """DMX sound lump (u16 3, u16 rate, u32 count, 8-bit unsigned samples) to rate."""
    fmt, r, cnt = struct.unpack_from("<HHI", b)
    if fmt != 3 or r <= rate:
        return b
    smp = b[8:8 + cnt]
    n = cnt * rate // r
    out = bytearray(n)
    for i in range(n):
        x = i * (cnt - 1) / max(n - 1, 1)
        k = int(x)
        f = x - k
        nxt = smp[k + 1] if k + 1 < cnt else smp[k]
        out[i] = int(round(smp[k] * (1 - f) + nxt * f))
    return struct.pack("<HHI", 3, rate, n) + bytes(out)


SILENCE = struct.pack("<HHI", 3, SFX_RATE, 64) + b"\x80" * 64	# > 48 samples (fbDOOM check)


def parse_textures(b):
    n = struct.unpack_from("<i", b)[0]
    offs = struct.unpack_from("<%di" % n, b, 4)
    tex = []
    for o in offs:
        npatch = struct.unpack_from("<h", b, o + 20)[0]
        raw = b[o:o + 22 + 10 * npatch]
        patches = [struct.unpack_from("<hhhhh", raw, 22 + 10 * k)[2] for k in range(npatch)]
        tex.append((name8(raw[:8]), raw, patches))
    return tex


def build_textures(tex):
    hdr = struct.pack("<i", len(tex))
    pos = 4 + 4 * len(tex)
    offs, body = b"", b""
    for _, raw, _ in tex:
        offs += struct.pack("<i", pos + len(body))
        body += raw
    return hdr + offs + body


def main():
    src, dst = sys.argv[1], sys.argv[2]
    nmaps = int(sys.argv[3]) if len(sys.argv) > 3 else 9
    lumps = read_wad(src)
    names = [n for n, _ in lumps]
    maps = ["E1M%d" % i for i in range(1, nmaps + 1)]

    # used wall textures and flats
    walls, flats = set(), set()
    for m in maps:
        i = names.index(m)
        mp = dict(lumps[i + 1:i + 11])
        sd = mp["SIDEDEFS"]
        for k in range(0, len(sd), 30):
            for t in (sd[k + 4:k + 12], sd[k + 12:k + 20], sd[k + 20:k + 28]):
                walls.add(name8(t))
        se = mp["SECTORS"]
        for k in range(0, len(se), 26):
            flats.add(name8(se[k + 4:k + 12]))
            flats.add(name8(se[k + 12:k + 20]))
    walls.discard("-")
    types = set()
    for m in maps:
        t = dict(lumps[names.index(m) + 1:names.index(m) + 11])["THINGS"]
        types |= {struct.unpack_from("<h", t, k + 6)[0] for k in range(0, len(t), 10)}
    drop_spr, drop_sfx = set(UNUSED_SPRITES), set()
    for num, (spr, sfx) in MONSTERS.items():
        if num not in types:
            drop_spr |= set(spr)
            drop_sfx |= set(sfx)
    walls |= {"SKY1"} | {"SW1" + s for s in SWITCHES_E1} | {"SW2" + s for s in SWITCHES_E1}
    flats |= {"F_SKY1", "FLOOR4_8"}

    # textures: keep the first (dummy) entry, used ones, whole animation ranges
    pnames = dict(lumps)["PNAMES"]
    npn = struct.unpack_from("<i", pnames)[0]
    pn = [name8(pnames[4 + 8 * k:12 + 8 * k]) for k in range(npn)]
    used_patches = set()
    newtex = {}
    alltex = []
    for tl in ("TEXTURE1", "TEXTURE2"):
        if tl in names:
            alltex += [(tl, t) for t in parse_textures(dict(lumps)[tl])]
    order = [t[1][0] for t in alltex]
    keep = set(walls)
    for a, b in TEX_ANIMS:
        if a in order and b in order:
            rng = order[order.index(a):order.index(b) + 1]
            if keep & set(rng):
                keep |= set(rng)
    for idx, (tl, t) in enumerate(alltex):
        if (idx == 0 and tl == "TEXTURE1") or t[0] in keep:
            newtex.setdefault(tl, []).append(t)
            used_patches |= {pn[p] for p in t[2]}
    missing = walls - set(order)
    if missing:
        print("warning: textures not found:", sorted(missing))

    # flats: whole animation ranges (by lump order between F_START/F_END)
    fs, fe = names.index("F_START"), names.index("F_END")
    flist = names[fs + 1:fe]
    for a, b in FLAT_ANIMS:
        if a in flist and b in flist:
            rng = flist[flist.index(a):flist.index(b) + 1]
            if flats & set(rng):
                flats |= set(rng)

    demo = bytes([109, 2, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0x80])  # v1.9, HMP, E1M1, ends at once
    out, section = [], None
    i = 0
    while i < len(lumps):
        nm, b = lumps[i]
        if len(nm) == 4 and nm[0] == "E" and nm[2] == "M" and nm[1].isdigit() and nm[3].isdigit():
            if nm in maps:
                out += lumps[i:i + 11]
            i += 11
            continue
        if nm.endswith("_START") or nm.endswith("_END"):
            section = nm[:-6] if nm.endswith("_START") else None
            out.append((nm, b))
            i += 1
            continue
        keepit = True
        if section in ("P", "P1", "P2", "P3"):
            keepit = nm in used_patches
        elif section in ("F", "F1", "F2", "F3"):
            keepit = nm in flats
        elif section in ("S", "SS"):
            keepit = nm[:4] not in drop_spr
        elif section is None:
            if nm.startswith("D_E"):
                keepit = nm[2:] in maps
            elif nm in ("TEXTURE1", "TEXTURE2"):
                keepit = nm in newtex
                b = build_textures(newtex[nm]) if keepit else b
            elif nm.startswith("DS"):
                b = SILENCE if nm in drop_sfx else resample(b, SFX_RATE)
            elif nm.startswith("DEMO"):
                b = demo
            elif nm in ("WIMAP1", "WIMAP2", "VICTORY2", "ENDPIC", "PFUB1", "PFUB2",
                      "BOSSBACK", "INTERPIC") \
                    or nm.startswith("WIA1") or nm.startswith("WIA2") or nm.startswith("END") and nm[3:].isdigit() \
                    or nm.startswith("DP"):
                keepit = False
        if keepit:
            out.append((nm, b))
        i += 1

    write_wad(dst, out)
    raw = sum(len(b) for _, b in out)
    xz = len(lzma.compress(open(dst, "rb").read(), preset=9 | lzma.PRESET_EXTREME))
    print("%s: %d maps, %d lumps, %.2f MB, xz %.2f MB (%d textures, %d flats, %d patches)"
          % (dst, nmaps, len(out), raw / 1e6, xz / 1e6, sum(len(v) for v in newtex.values()),
             len([f for f in flist if f in flats]), len(used_patches)))


if __name__ == "__main__":
    main()
