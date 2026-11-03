#!/bin/sh
# Host test of the fbDOOM music (branch music): renders a song lump through
# i_oplmusic.c + opl_fmopl.c to raw 48 kHz stereo s16.
#   ./h WAD LUMP SECONDS OUT.raw [-fmopl]   opl2.c (default) or fmopl
#   ./hn WAD LUMP SECONDS OUT.raw -fmopl    Nuked OPL3 (reference) instead of fmopl
set -e
cd "$(dirname "$0")"
F=../../fbDOOM/fbdoom
N=../../buildroot/output/build/chocolate-doom-3.0.1/opl
COMMON="$F/i_oplmusic.c $F/midifile.c $F/mus2mid.c $F/memio.c $F/m_misc.c $F/m_argv.c $F/opl_fmopl.c $F/opl2.c"
gcc -O2 -std=gnu99 -w -I$F -o h harness.c stubs.c $COMMON $F/fmopl.c -lm -lpthread
gcc -O2 -std=gnu99 -w -I$F -I$N -o hn harness.c stubs.c nukedwrap.c $N/opl3.c $COMMON -lm -lpthread
