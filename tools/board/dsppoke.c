/*
 * dsppoke - write words into the program memory of a running audio DSP
 * through IDMA, without resetting it (dsp_re/README.md). Used to patch
 * single instructions of the running SE code while sound plays.
 *
 * Build on the board: gcc -O2 -o dsppoke dsppoke.c
 * Usage: dsppoke se|dec ADDR WORD...     (hex; ADDR in DSP words)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mstar_audio.h"

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	uint8_t b[3 * 64];
	int se, n = argc - 3;

	if (argc < 4 || n > 64) {
		fprintf(stderr, "usage: %s se|dec ADDR WORD...\n", argv[0]);
		return 1;
	}
	se = !strcmp(argv[1], "se");
	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (int i = 0; i < n; i++) {
		uint32_t w = strtoul(argv[i + 3], NULL, 16);

		b[3 * i] = w;
		b[3 * i + 1] = w >> 8;
		b[3 * i + 2] = w >> 16;
	}
	wm8(A(0x2a7e), 1, se);
	if (!idma_write(se, strtoul(argv[2], NULL, 16), b, n * 3)) {
		fprintf(stderr, "idma write failed\n");
		return 1;
	}
	return 0;
}
