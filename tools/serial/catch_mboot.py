#!/usr/bin/env python3
"""Restart the board and stop at the MBoot prompt by spamming CR.

Usage: catch_mboot.py [reboot|reset]   (reboot from Linux, reset from MBoot;
without an argument just wait, e.g. while you power-cycle the board)
"""
import sys
import time

import serial

with serial.Serial("/dev/ttyUSB0", 115200, timeout=0.05) as s:
    if len(sys.argv) > 1:
        s.write(b"\r" + sys.argv[1].encode() + b"\r")
        time.sleep(0.5)
    buf = b""
    end = time.time() + float(__import__("os").environ.get("WAIT", 60))
    while time.time() < end:
        s.write(b"\r")
        buf += s.read(4096)
        if b"<< MStar >>#" in buf[-200:]:
            break
    ok = b"<< MStar >>#" in buf
    print("PROMPT CAUGHT" if ok else "NO PROMPT")
    sys.exit(0 if ok else 1)
