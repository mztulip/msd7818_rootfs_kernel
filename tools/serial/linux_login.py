#!/usr/bin/env python3
"""Optionally send a command (default none), wait for the Linux login and log in as root
(or accept the shell of the auto-logged-in root of the flash mini system).

Usage: linux_login.py ["go 0x805e86a4"]
"""
import sys
import time

import serial

with serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2) as s:
    # with no command, a bare CR makes an already printed login prompt reappear
    s.write((sys.argv[1].encode() if len(sys.argv) > 1 else b"") + b"\r")
    passwords = [b"1234"]
    buf, state, end = b"", "boot", time.time() + 120
    while time.time() < end:
        buf += s.read(4096)
        tail = buf[-300:]
        if state == "boot" and b"root login on" in buf and tail.rstrip().endswith(b"#"):
            state = "done"; break		# autologin of the flash mini system (since 2026-10-20)
        if state in ("boot", "pass") and b"login:" in tail:
            if not passwords:
                break
            s.write(b"root\r"); state, buf = "user", b""
        elif state == "user" and b"assword" in tail:
            s.write(passwords.pop(0) + b"\r"); state, buf = "pass", b""
        elif state == "pass" and tail.rstrip().endswith(b"#"):
            state = "done"; break
    print("STATE:", state)
    sys.exit(0 if state == "done" else 1)
