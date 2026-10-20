#!/bin/sh
# condtest.sh: for each condition code c (0..e) after "cmp ra, r4" (2ae2aa),
# prints which path ran: 0111 = not taken, 0222 = taken. Two cases: ra == r4, ra != r4.
for v in 0f00 0f01 0eff; do
	line="r4=$v:"
	for c in 0 1 2 3 4 5 6 7 8 9 a b c d e; do
		devmem 0x1F225BE4 16 0 >/dev/null
		/root/dsptest 40f00a 4${v}4 2ae2aa 18004$c 40111b 9b079b 0a001f 40222b 9b079b 0a001f > /tmp/ct.txt
		r=$(grep "100 ms" /tmp/ct.txt | sed 's/.*2df2=\(....\).*/\1/')
		line="$line $c:$r"
	done
	echo "$line"
done
