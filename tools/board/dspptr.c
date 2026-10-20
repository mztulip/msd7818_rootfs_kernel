/*
 * dspptr - find pointer (register indirect) memory accesses of the SE audio
 * DSP (dsp_re/README.md). For each candidate word X:
 *
 *   internal memory [0x120 + i] = 0x0700 + i     (i = 0..15)
 *   r0..r8 = 0x120..0x128, rb rc re rf = 0x12b 0x12c 0x12e 0x12f,
 *   ra = 0x0A0A (value)
 *   X, two fillers
 *   pass 0: mailboxes 0x2DE0..0x2DFE <- [0x120..0x12f]   (stores show here)
 *   pass 1: mailboxes <- ra r0..r8 rb rc re rf            (loads show here)
 *
 * Build on the board: gcc -O2 -o dspptr dspptr.c
 * Usage: dspptr WORD...
 */
#include <stdio.h>
#include <stdlib.h>
#include "mstar_audio.h"

static const uint8_t reg[14] = { 0xa, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xb, 0xc, 0xe, 0xf };
static const uint16_t val[14] = { 0x0a0a, 0x120, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x127, 0x128,
				  0x12b, 0x12c, 0x12e, 0x12f };

static void put(uint8_t *b, int i, uint32_t w)
{
	b[3 * i] = w;
	b[3 * i + 1] = w >> 8;
	b[3 * i + 2] = w >> 16;
}

static int run(uint32_t x, int pass, uint16_t *out)
{
	uint8_t b[3 * 96];
	int k = 0;

	for (int i = 0; i < 16; i++) {
		put(b, k++, 0x400000 | (0x700 + i) << 4 | 0xa);	/* ld ra, #0x700+i */
		put(b, k++, 0x900000 | (0x120 + i) << 4 | 0xa);	/* st ra, [0x120+i] */
	}
	for (int i = 0; i < 14; i++)
		put(b, k++, 0x400000 | val[i] << 4 | reg[i]);
	put(b, k++, x);
	put(b, k++, 0x40000d);					/* fillers: ld rd, #0 */
	put(b, k++, 0x40000d);
	if (pass == 0) {
		for (int i = 0; i < 16; i++) {
			put(b, k++, 0x800000 | (0x120 + i) << 4 | 0xa);	/* ld ra, [0x120+i] */
			put(b, k++, 0x9b0000 | (0x070 + i) << 4 | 0xa);	/* st ra, io[0x70+i] */
		}
	} else {
		for (int i = 0; i < 14; i++)
			put(b, k++, 0x9b0000 | (0x070 + i) << 4 | reg[i]);
	}
	put(b, k++, 0x0a001f);
	put(b, k++, 0x0a001f);

	wm8(A(0x2b40), 2, 2); usleep(200); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); usleep(500); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	idma_write(1, 1, b + 3, (k - 1) * 3);
	idma_write(1, 0, b, 3);
	usleep(2000);
	for (int i = 0; i < 16; i++)
		out[i] = r16(A(0x2de0 + 2 * i));
	return 0;
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	uint16_t o[16];

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	audio_basic_init();
	mad2_setmeminfo_se(SE_BASE);
	for (int a = 1; a < argc; a++) {
		uint32_t x = strtoul(argv[a], NULL, 16);

		printf("%06x:", x);
		run(x, 0, o);
		for (int i = 0; i < 16; i++)
			if (o[i] != 0x700 + i)
				printf(" [%03x]=%04x", 0x120 + i, o[i]);
		run(x, 1, o);
		for (int i = 0; i < 14; i++)
			if (o[i] != val[i])
				printf(" r%x=%04x", reg[i], o[i]);
		printf("\n");
	}
	return 0;
}
