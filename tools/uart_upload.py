#!/usr/bin/env python3
"""Upload a file to the board's Linux shell over the serial console.

The file is sent as base64 text into `base64 -d > REMOTE` running on the board,
then verified with md5sum. No network, no extra tools on the board (BusyBox
base64 + md5sum are enough). See docs/przesylanie_uart.md.

Usage:
  uart_upload.py LOCAL REMOTE [-p /dev/ttyUSB0] [-b 115200]

Tip: gzip big files first (gzip -9 -k file) and gunzip them on the board.
"""
import argparse
import base64
import hashlib
import re
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("local")
    ap.add_argument("remote")
    ap.add_argument("-p", "--port", default="/dev/ttyUSB0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    a = ap.parse_args()

    data = open(a.local, "rb").read()
    lines = base64.encodebytes(data).splitlines(keepends=True)  # 76-char lines
    local_md5 = hashlib.md5(data).hexdigest()

    with serial.Serial(a.port, a.baud, timeout=0.1) as s:
        # disable echo so the board does not send every character back,
        # then let base64 -d read the rest of the console input
        s.write(b"\rstty -echo; base64 -d > %s\r" % a.remote.encode())
        time.sleep(1)
        s.reset_input_buffer()
        t0 = time.time()
        for i, ln in enumerate(lines):
            s.write(ln)
            if i % 2000 == 0:
                s.flush()
                s.read(65536)  # drain anything the board printed
                pct = 100 * i / len(lines)
                print(f"\r{pct:5.1f}%  {i}/{len(lines)} lines  {time.time() - t0:.0f}s",
                      end="", flush=True)
        s.flush()
        time.sleep(1)
        s.write(b"\x04")  # Ctrl-D = end of input for base64 -d
        time.sleep(2)
        s.write(b"\rstty echo; md5sum %s\r" % a.remote.encode())
        time.sleep(3)
        out = s.read(65536).decode(errors="replace")

    m = re.search(r"\b([0-9a-f]{32})\b", out)
    remote_md5 = m.group(1) if m else "?"
    print(f"\nlocal  md5 {local_md5}\nremote md5 {remote_md5}\n"
          f"{len(data)} bytes in {time.time() - t0:.0f}s")
    if remote_md5 != local_md5:
        print("MD5 MISMATCH", file=sys.stderr)
        sys.exit(1)
    print("OK")


if __name__ == "__main__":
    main()
