#!/usr/bin/env python3
"""At the MBoot prompt: load the stock firmware, apply a probe's mw.l commands
(tools/baremetal/stock*/mw_cmds*.txt) and start the stock firmware.

Usage: run_stockprobe.py MW_CMDS_FILE
"""
import sys
import time

import serial

PROMPT = b"<< MStar >>#"


def cmd(s, c, timeout=30):
    s.write(c.encode() + b"\r")
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        buf += s.read(4096)
        if PROMPT in buf[len(c):]:
            return buf
    sys.exit("timeout on: " + c)


with serial.Serial("/dev/ttyUSB0", 115200, timeout=0.05) as s:
    cmd(s, "")
    cmd(s, "spi_rdc 0x80B00000 0x8001C 0x289977")
    cmd(s, "LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000", 60)
    lines = [l.strip() for l in open(sys.argv[1]) if l.strip()]
    for l in lines:
        cmd(s, l, 5)
    print("%d mw.l written, starting stock" % len(lines))
    s.write(b"go 0x80000224\r")
