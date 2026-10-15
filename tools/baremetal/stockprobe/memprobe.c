/*
 * memprobe - look at the DEC DSP memory while the stock player plays
 * test1k.wav (loaded like stockprobe: MBoot puts it at 0xA3800000, the
 * "Help" handler jumps here; printf takes at most 3 arguments).
 *  - prints the read-pointer register 0x112D6A (and 0x2D66/68/6C/8C)
 *  - hex dump of 64 B at DEC base + ptr*24, + ptr*8 and at DEC base + 0
 *  - for each 4 KB of the first 1 MB of DEC memory: hits of the WAV sample
 *    1566 (0x061E) as 16-bit LE ("w16") and as 24-bit word "00 1e 06" ("w24")
 */
#include <stdint.h>

typedef int (*printf_t)(const char *, ...);
#define PRINTF ((printf_t)0x80244f74)
#define DEC_BASE 0x83b00000u		/* KSEG0 alias of phys 0x3B00000 */

static uint16_t reg(uint32_t r) { return *(volatile uint16_t *)(0xbf000000 + r * 2); }

static void hexdump(uint32_t a, unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		if (!(i & 15))
			PRINTF("\n%08x:", a + i);
		PRINTF(" %02x", *(volatile uint8_t *)(a + i));
	}
}

void memprobe(void)
{
	uint16_t p = reg(0x112d6a);

	PRINTF("\nMEM BEGIN ptr2d6a=%04x", p);
	PRINTF(" 2d66=%04x 2d68=%04x", reg(0x112d66), reg(0x112d68));
	PRINTF(" 2d6c=%04x 2d8c=%04x", reg(0x112d6c), reg(0x112d8c));
	PRINTF("\n-- base+ptr*24");
	hexdump(DEC_BASE + p * 24u, 64);
	PRINTF("\n-- base+ptr*8");
	hexdump(DEC_BASE + p * 8u, 64);
	PRINTF("\n-- base+0");
	hexdump(DEC_BASE, 64);
	PRINTF("\n-- scan 4K blocks");
	for (uint32_t blk = DEC_BASE; blk < DEC_BASE + 0x100000; blk += 0x1000) {
		unsigned w16 = 0, w24 = 0;

		for (uint32_t a = blk; a < blk + 0x1000; a += 2)
			if (*(volatile uint16_t *)a == 0x061e)
				w16++;
		for (uint32_t a = blk; a < blk + 0x1000 - 2; a++) {
			volatile uint8_t *b = (volatile uint8_t *)a;

			if (b[0] == 0x00 && b[1] == 0x1e && b[2] == 0x06)
				w24++;
		}
		if (w16 || w24)
			PRINTF("\nblk %08x w16 %u", blk, w16), PRINTF(" w24 %u", w24);
	}
	PRINTF("\nMEM END\n");
}
