/*
 * dspfuzz - try candidate instruction words on the SE audio DSP, one by one
 * (dsp_re/README.md). For each candidate X it runs:
 *
 *   r0=0x0001 r1=0x0002 ... r8=0x0100 ra=0x0200 rb=0x0400 rc=0x0800
 *   re=0x1000 rf=0x2000 (r9, rd are special)
 *   X
 *   mailboxes 0x2DF0..0x2DF8 <- five registers, 0x2DFE <- run number
 *
 * three times with different dumped registers, and prints the registers that
 * differ from their start values. A wrong
 * run number means the program did not reach the end (hang or jump away).
 *
 * Build on the board: gcc -O2 -o dspfuzz dspfuzz.c
 * Usage: dspfuzz WORD...   or   dspfuzz - < list   (hex words)
 */
#include <stdio.h>
#include <stdlib.h>
#include "mstar_audio.h"

/* every usable register gets its own bit, so sums show their sources (r9, rd are special) */
static const uint8_t all[14] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xa, 0xb, 0xc, 0xe, 0xf };
static const uint16_t initv[14] = { 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
				    0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000 };
static const uint8_t sets[3][5] = { { 9, 10, 11, 0, 1 },	/* ra rb rc r0 r1 */
				    { 2, 3, 4, 5, 6 },		/* r2..r6 */
				    { 7, 8, 12, 13, 13 } };	/* r7 r8 re rf */
static int set;

static void put(uint8_t *b, int i, uint32_t w)
{
	b[3 * i] = w;
	b[3 * i + 1] = w >> 8;
	b[3 * i + 2] = w >> 16;
}

static void run(uint32_t x, unsigned n)
{
	uint8_t b[3 * 40];
	int k = 0;

	for (int i = 0; i < 14; i++)
		put(b, k++, 0x400000 | initv[i] << 4 | all[i]);	/* ld rN, #bit */
	put(b, k++, x);
	for (int i = 0; i < 5; i++)
		put(b, k++, 0x9b0000 | (0x078 + i) << 4 | all[sets[set][i]]);	/* st rN, io[0x78+i] */
	put(b, k++, 0x400000 | (n & 0xffff) << 4 | 0xf);	/* ld rf, #n (after the dump) */
	put(b, k++, 0x9b07ff);					/* st rf, io[0x7f] */
	put(b, k++, 0x0a001f);					/* idle */
	put(b, k++, 0x0a001f);

	wm8(A(0x2b40), 2, 2); usleep(200); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); usleep(500); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	idma_write(1, 1, b + 3, (k - 1) * 3);
	idma_write(1, 0, b, 3);
	usleep(2000);

	printf("%06x %d:", x, set);
	if (r16(A(0x2dfe)) != (n & 0xffff)) {
		printf(" did not finish (0x2dfe=%04x)\n", r16(A(0x2dfe)));
		return;
	}
	int same = 1;
	for (int i = 0; i < 5; i++) {
		int r = sets[set][i];
		uint16_t v = r16(A(0x2df0 + 2 * i));

		if (v != initv[r]) {
			printf(" r%x %04x->%04x", all[r], initv[r], v);
			same = 0;
		}
	}
	printf(same ? " no change\n" : "\n");
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	char line[64];
	unsigned n = 0x100;

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	audio_basic_init();
	mad2_setmeminfo_se(SE_BASE);
	if (argc == 2 && argv[1][0] == '-') {
		while (fgets(line, sizeof(line), stdin))
			for (set = 0; set < 3; set++)
				run(strtoul(line, NULL, 16), n++);
	} else {
		for (int i = 1; i < argc; i++)
			for (set = 0; set < 3; set++)
				run(strtoul(argv[i], NULL, 16), n++);
	}
	return 0;
}
