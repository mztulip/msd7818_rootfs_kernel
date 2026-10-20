/*
 * dspdm - write words into the SE DSP data memory (DM) through IDMA while it
 * runs (address | 0x8000 selects DM, as HAL_MAD's DM write does for DEC).
 * Test tool for feeding samples from Linux (dsp_re/README.md).
 *
 * Build on the board: gcc -O2 -o dspdm dspdm.c
 * Usage: dspdm ADDR WORD...     (hex; WORD = 24-bit value as sent over IDMA)
 */
#include <stdio.h>
#include <stdlib.h>
#include "mstar_audio.h"

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	uint8_t buf[3 * 64];
	int n = argc - 2;

	if (n < 1 || n > 64) {
		fprintf(stderr, "usage: %s ADDR WORD... (max 64)\n", argv[0]);
		return 1;
	}
	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (int i = 0; i < n; i++) {
		uint32_t w = strtoul(argv[i + 2], NULL, 16);

		buf[3 * i] = w;
		buf[3 * i + 1] = w >> 8;
		buf[3 * i + 2] = w >> 16;
	}
	if (!idma_write(1, 0x8000 | strtoul(argv[1], NULL, 16), buf, n * 3)) {
		fprintf(stderr, "IDMA write failed\n");
		return 1;
	}
	return 0;
}
