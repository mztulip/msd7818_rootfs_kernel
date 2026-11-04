#!/bin/sh
# After a USB device is unplugged, every later one times out (error -145): reload the EHCI driver.
D=/sys/bus/platform/drivers/fotg210-ehci
echo 1f220600.uhc > $D/unbind
echo 1f220600.uhc > $D/bind
