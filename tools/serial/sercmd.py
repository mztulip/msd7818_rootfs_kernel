#!/usr/bin/env python3
"""Send commands over the serial console, print output until a prompt appears.

Usage: sercmd.py [-p PORT] [-b BAUD] [-t TIMEOUT] [--prompt REGEX] CMD [CMD...]
Default prompt matches the Linux shell (#, $) and MBoot (<< MStar >>#).
"""
import argparse
import re
import sys
import time

import serial


def run(ser, cmd, prompt, timeout):
    ser.reset_input_buffer()
    ser.write(cmd.encode() + b"\r")
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
            end = max(end, time.time() + 0.5)
            text = buf.decode(errors="replace")
            tail = text[len(cmd):] if text.startswith(cmd) else text
            lines = tail.splitlines()
            if lines and re.search(prompt, lines[-1]):
                break
    return buf.decode(errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="/dev/ttyUSB0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-t", "--timeout", type=float, default=5)
    ap.add_argument("--prompt", default=r"(# ?$|\$ ?$|<< MStar >>#)")
    ap.add_argument("cmds", nargs="+")
    a = ap.parse_args()
    with serial.Serial(a.port, a.baud, timeout=0.2) as ser:
        for c in a.cmds:
            sys.stdout.write(run(ser, c, a.prompt, a.timeout).replace("\r", ""))
            sys.stdout.flush()


if __name__ == "__main__":
    main()
