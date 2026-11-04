#!/usr/bin/env python3
"""uart_get.py REMOTE LOCAL : fetch a file from the board shell (gzip+base64+md5)."""
import base64, gzip, hashlib, re, sys, time, serial
rem, loc = sys.argv[1], sys.argv[2]
with serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2) as s:
    s.reset_input_buffer()
    s.write(f"stty -echo; md5sum {rem}; echo ==B==; gzip -c {rem} | base64; echo ==E==; stty echo\r".encode())
    buf = b""; end = time.time() + 600
    while time.time() < end:
        c = s.read(65536)
        if c: buf += c; end = max(end, time.time() + 5)
        if b"==E==\r\n" in buf or b"==E==\n" in buf: break
t = buf.decode(errors="replace")
md5 = re.search(r"([0-9a-f]{32})\s+" + re.escape(rem), t).group(1)
b64 = re.split(r"==B==\r?\n", t, 1)[1].split("==E==")[0]
data = gzip.decompress(base64.b64decode("".join(b64.split())))
open(loc, "wb").write(data)
ok = hashlib.md5(data).hexdigest() == md5
print(len(data), "bytes", "MD5 OK" if ok else "MD5 MISMATCH")
sys.exit(0 if ok else 1)
