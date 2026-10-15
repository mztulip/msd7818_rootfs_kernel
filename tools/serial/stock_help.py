#!/usr/bin/env python3
"""Run a loaded probe in the stock firmware: type "Help" at the ">>" console
and save everything it prints until the END marker (docs/sondy_stock.md).

Usage: stock_help.py OUTPUT.txt [END_MARKER] [TIMEOUT_S]
       e.g. stock_help.py binaries/ghidra_stock/my_probe.txt "SZABLON END" 120
"""
import sys
import time

import serial

out = sys.argv[1]
marker = (sys.argv[2] if len(sys.argv) > 2 else "END").encode()
timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 180

with serial.Serial("/dev/ttyUSB0", 115200, timeout=0.3) as s:
    s.write(b"\rHelp\r")
    buf, end = b"", time.time() + timeout
    while time.time() < end and marker not in buf:
        buf += s.read(65536)
    time.sleep(0.5)
    buf += s.read(65536)
text = buf.decode(errors="replace").replace("\r", "")
open(out, "w").write(text)
print("%s: %d bytes, marker %s" % (out, len(text), "found" if marker in buf else "NOT found"))
