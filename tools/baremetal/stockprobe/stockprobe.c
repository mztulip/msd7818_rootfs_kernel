/*
 * stockprobe - bigger probe for the running stock eCos firmware. The stock
 * "Help" handler (0x80142468) is patched with a jump to this code, which
 * MBoot loads at 0xA3800000 (MVD video decoder memory, unused while only
 * audio plays). Output goes through the firmware's printf (0x80244f74).
 *
 *  1. dumps the audio and HDMI TX registers (same format as stockdump)
 *  2. searches DRAM for the samples of test1k.wav (1 kHz sine, 48 kHz,
 *     16-bit stereo, amplitude 12000) in several layouts and prints, per
 *     64 KB block, how many hits there are and the first hit address.
 *     Scans read through the cache (KSEG0): much faster, and uncached
 *     reads starved the audio player (choppy sound).
 */
#include <stdint.h>

typedef int (*printf_t)(const char *, ...);
#define PRINTF ((printf_t)0x80244f74)

static const uint32_t ranges[][2] = {
	{ 0xbf225400, 0xbf226000 },	/* audio */
	{ 0xbf226200, 0xbf226400 },	/* HDMI TX 0x1131 */
	{ 0xbf226600, 0xbf226800 },	/* HDMI TX 0x1133 */
};

/* sample 1 of the sine: int(12000 * sin(2*pi*1000/48000)) = 1566 (0x061e) */
#define S1 0x061e

static void dump_regs(void)
{
	for (unsigned r = 0; r < 3; r++)
		for (uint32_t a = ranges[r][0]; a < ranges[r][1]; a += 4) {
			if (!(a & 0x1f))
				PRINTF("\n%08x:", a);
			PRINTF(" %04x", *(volatile uint16_t *)a);
		}
	PRINTF("\n");
}

/* layout 0: L,R int16 (WAV as is)            -> 0x061e061e at 4-byte step
 * layout 1: [0,lo,hi,0] per sample (DMA Reader style) -> 0x00061e00
 * layout 2: 24-bit words, sample in top 16 bits: bytes 00 1e 06 */
static void scan(uint32_t start, uint32_t end)
{
	for (uint32_t blk = start; blk < end; blk += 0x10000) {
		unsigned n0 = 0, n1 = 0, n2 = 0;
		uint32_t f0 = 0, f1 = 0, f2 = 0;

		for (uint32_t a = blk; a < blk + 0x10000; a += 4) {
			uint32_t w = *(volatile uint32_t *)a;

			if (w == ((S1 << 16) | S1)) { if (!n0++) f0 = a; }
			if (w == (S1 << 8)) { if (!n1++) f1 = a; }
		}
		for (uint32_t a = blk; a < blk + 0x10000 - 3; a += 1) {
			volatile uint8_t *p = (volatile uint8_t *)a;

			if (p[0] == 0x00 && p[1] == 0x1e && p[2] == 0x06 && p[3] == 0x00 && p[4] == 0x1e && p[5] == 0x06) {
				if (!n2++) f2 = a;
			}
		}
		if (n0 || n1 || n2)
			PRINTF("\nblk %08x: wav %u@%08x  dmar %u@%08x  w24 %u@%08x", blk, n0, f0, n1, f1, n2, f2);
	}
}

void probe(void)
{
	PRINTF("\nPROBE BEGIN");
	dump_regs();
	PRINTF("\nscan MAD 0x3A00000-0x3D00000");
	scan(0x83a00000, 0x83d00000);
	PRINTF("\nscan pool/GOP 0x0C4DED0.. 0x1B60000");
	scan(0x80c40000, 0x81b60000);
	PRINTF("\nscan MVD 0x1D00000-0x3800000");
	scan(0x81d00000, 0x83800000);
	PRINTF("\nPROBE END\n");
}
