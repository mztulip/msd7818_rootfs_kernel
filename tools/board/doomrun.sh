#!/bin/sh
# BIN=fbdoom_own doomrun.sh SECONDS [FBDOOM_ARGS...] - run /home/fbdoom_own on tty2 for SECONDS (the attract
# demo plays with sound effects), then stop it and give the console back (fbDOOM puts the
# serial terminal into raw mode). Log: /tmp/doom.log
t=$1; shift
BIN=${BIN:-fbdoom_own}
openvt -c 2 -s -- sh -c "cd /home; ./$BIN -iwad doom1.wad $* > /tmp/doom.log 2>&1"
sleep "$t"
killall $BIN 2> /dev/null
sleep 1
chvt 1
stty sane
grep -a -i 'sound\|fail' /tmp/doom.log
