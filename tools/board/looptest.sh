#!/bin/sh
# looptest.sh WORD... : counts loop iterations for each candidate jump word
# (program in dsp_re/README.md, "Skoki i pętle"). Prints: word, 0x2DF0 at
# 5 ms and 25 ms after the start, 0x2DF2 (0x00BB = fell through).
for x in "$@"; do
	/root/dsptest 400010 40000a 40000b 222acf 9b078a $x 40000b 40000b 40000b 400bbc 9b079c 0a001f 0a001f >/dev/null
	a=$(devmem 0x1F225BE0 16); usleep 20000; b=$(devmem 0x1F225BE0 16); c=$(devmem 0x1F225BE4 16)
	echo "$x $a $b $c"
	devmem 0x1F225BB8 16 0 >/dev/null
done
