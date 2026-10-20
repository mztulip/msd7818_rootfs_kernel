/*
 * dsptest - run our own words on the SE audio DSP and read its mailboxes.
 * Hardware oracle for reverse engineering the DSP instruction set
 * (dsp_re/README.md). Uses the init and IDMA helpers of mstar_audio.h.
 *
 * Build on the board: gcc -O2 -o dsptest dsptest.c
 * Usage: dsptest WORD...      (24-bit hex words, loaded at SE code address 0)
 * Prints SE mailboxes 0x112DF0..0x112DFE before the reset and 1/10/100 ms
 * after the start.
 */
#include <stdio.h>
#include <stdlib.h>
#include "mstar_audio.h"

static void dump(const char *tag)
{
	printf("%-8s", tag);
	for (unsigned r = 0x2df0; r <= 0x2dfe; r += 2)
		printf(" %04x=%04x", r, r16(A(r)));
	printf("\n");
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	uint8_t buf[3 * 64];
	int n = argc - 1;

	if (n < 1 || n > 64) {
		fprintf(stderr, "usage: %s WORD... (max 64)\n", argv[0]);
		return 1;
	}
	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (int i = 0; i < n; i++) {
		uint32_t w = strtoul(argv[i + 1], NULL, 16);

		buf[3 * i] = w;
		buf[3 * i + 1] = w >> 8;
		buf[3 * i + 2] = w >> 16;
	}

	audio_basic_init();
	mad2_setmeminfo_se(SE_BASE);
	dump("before");
	/* SE reset as in start_dsps(), then our code, word 0 last */
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	if (n > 1 && !idma_write(1, 1, buf + 3, (n - 1) * 3))
		return 1;
	if (!idma_write(1, 0, buf, 3))
		return 1;
	msleep(1);
	dump("1 ms");
	msleep(9);
	dump("10 ms");
	msleep(90);
	dump("100 ms");
	return 0;
}
