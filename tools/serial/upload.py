#!/usr/bin/env python3
"""Upload files to the board over the serial console (base64 in 200-char lines + md5 check).

Usage: upload.py LOCAL REMOTE [LOCAL REMOTE ...]   (the board must be at a shell prompt)
"""
import base64
import hashlib
import re
import sys
import time

import serial


def cmd(ser, c, t=3):
    ser.reset_input_buffer()
    ser.write(c.encode() + b'\r')
    buf, end = b'', time.time() + t
    while time.time() < end:
        ch = ser.read(4096)
        if ch:
            buf += ch
            end = max(end, time.time() + 0.3)
            if re.search(rb'# ?$', buf):
                break
    return buf.decode(errors='replace')


def main(argv):
    if len(argv) < 2 or len(argv) % 2:
        print(__doc__)
        return 1
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.1)
    ok = True
    for src, dst in zip(argv[0::2], argv[1::2]):
        data = open(src, 'rb').read()
        b = base64.b64encode(data).decode()
        cmd(ser, 'rm -f /tmp/u.b64')
        for i in range(0, len(b), 200):
            cmd(ser, "echo '%s' >> /tmp/u.b64" % b[i:i + 200], 2)
        out = cmd(ser, 'base64 -d /tmp/u.b64 > %s && md5sum %s' % (dst, dst), 5)
        good = hashlib.md5(data).hexdigest() in out
        ok &= good
        print(dst, 'OK' if good else 'MD5 MISMATCH: ' + out)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
