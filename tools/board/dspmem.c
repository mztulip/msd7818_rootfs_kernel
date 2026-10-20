/*
 * dspmem - find the data memory instructions of the SE audio DSP
 * (dsp_re/README.md). The SE data memory is in DRAM at 0x3C00000 + 3*addr
 * (one 24-bit word per 3 bytes). For each candidate word X:
 *
 *   fill the SE area with "word at address A = A & 0xFFFFFF"
 *   run:  ra=0x5A5A, r0=0x8000 r1=0x8100 ... (pointers into X1); X;
 *         dump ra r0 r1 r2 r3 to mailboxes
 *   print the changed memory words and changed registers
 *
 * A store shows up as changed memory, a load as a register that now holds
 * an address.
 *
 * Build on the board: gcc -O2 -o dspmem dspmem.c
 * Usage: dspmem WORD...
 */
#include <stdio.h>
#include <stdlib.h>
#include "mstar_audio.h"

#define AREA_WORDS	(0x100000 / 3)		/* SE area: 1 MB */

static volatile uint8_t *se;
/* ra = value to store, the other registers point into X1 (0x8000..) */
static const uint8_t r[14] = { 0xa, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xb, 0xc, 0xe, 0xf };
static const uint16_t v[14] = { 0x5a5a, 0x8000, 0x8100, 0x8200, 0x8300, 0x8400, 0x8500, 0x8600,
				0x8700, 0x8800, 0x8b00, 0x8c00, 0x8e00, 0x8f00 };

static uint32_t rd24(unsigned a) { return se[3 * a] | se[3 * a + 1] << 8 | se[3 * a + 2] << 16; }

static void put(uint8_t *b, int i, uint32_t w)
{
	b[3 * i] = w;
	b[3 * i + 1] = w >> 8;
	b[3 * i + 2] = w >> 16;
}

static void run(uint32_t x)
{
	uint8_t b[3 * 32];
	int k = 0, changed = 0;

	for (unsigned a = 0; a < AREA_WORDS; a++) {
		se[3 * a] = a;
		se[3 * a + 1] = a >> 8;
		se[3 * a + 2] = a >> 16;
	}
	__sync_synchronize();
	for (int i = 0; i < 14; i++)
		put(b, k++, 0x400000 | v[i] << 4 | r[i]);
	put(b, k++, x);
	put(b, k++, 0x40000f);		/* filler (slot) */
	put(b, k++, 0x40000f);
	for (int i = 0; i < 5; i++)
		put(b, k++, 0x9b0000 | (0x078 + i) << 4 | r[i]);
	put(b, k++, 0x0a001f);
	put(b, k++, 0x0a001f);

	wm8(A(0x2b40), 2, 2); usleep(200); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); usleep(500); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	idma_write(1, 1, b + 3, (k - 1) * 3);
	idma_write(1, 0, b, 3);
	usleep(3000);

	printf("%06x:", x);
	for (int i = 0; i < 5; i++) {
		uint16_t now = r16(A(0x2df0 + 2 * i));

		if (now != v[i])
			printf(" r%x=%04x", r[i], now);
	}
	for (unsigned a = 0; a < AREA_WORDS; a++) {
		uint32_t w = rd24(a);

		if (w != (a & 0xffffff)) {
			if (changed++ < 8)
				printf(" [%05x]=%06x", a, w);
		}
	}
	printf(changed > 8 ? " (+%d more)\n" : "\n", changed - 8);
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	se = dram(SE_BASE);
	audio_basic_init();
	mad2_setmeminfo_se(SE_BASE);
	for (int i = 1; i < argc; i++)
		run(strtoul(argv[i], NULL, 16));
	return 0;
}
