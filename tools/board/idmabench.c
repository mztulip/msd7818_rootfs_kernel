/*
 * idmabench - how many 24-bit words per second the MIPS can write into SE DM through IDMA
 * (sample feeding for an own SE program, docs: own_dsp_audio.md, Polish wlasny_dsp_dzwiek.md). Writes N words to DM 0x2B40..
 * (repeating the same 64-word block) and prints the rate; with and without the busy poll.
 *
 * Build on the board: gcc -O2 -o idmabench idmabench.c
 * Research tool: uses mstar_audio.h, so it needs the headers generated from your own flash
 * dump (extract_audio_fw.py). own_audio.h / owntest.c do not.
 * Usage: idmabench [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "mstar_audio.h"

static double t(void) { struct timeval v; gettimeofday(&v, 0); return v.tv_sec + v.tv_usec / 1e6; }

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	unsigned n = argc > 1 ? atoi(argv[1]) : 96000;

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (int poll = 1; poll >= 0; poll--) {
		wm8(A(0x2a7e), 1, 1);
		double t0 = t();
		unsigned timeouts = 0;
		for (unsigned i = 0; i < n; i++) {
			if ((i & 63) == 0)
				w16(A(0x2a84), 0x2b40 | 0x8000);
			uint32_t v = (i & 63) << 12;
			w8(A(0x2a82), v >> 8);
			w8(A(0x2a83), v >> 16);
			w8(A(0x2a82), v);
			w8(A(0x2a83), 0);
			if (poll) {
				int k;
				for (k = 0; k < 1000 && (r8(A(0x2a80)) & 0x10); k++)
					;
				if (k == 1000)
					timeouts++;
			}
		}
		double dt = t() - t0;
		printf("poll %d: %u words in %.3f s = %.0f words/s, timeouts %u\n", poll, n, dt, n / dt, timeouts);
	}
	return 0;
}
