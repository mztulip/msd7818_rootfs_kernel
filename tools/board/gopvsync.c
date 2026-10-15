/*
 * gopvsync - check the GOP3 vsync interrupt status bit by polling.
 *
 * GOP3 control bank 9, reg 0x1F10: bits [1:0] interrupt masks (MStar GFLIP
 * writes 2 to enable the vsync interrupt, 3 to disable it), bit 8 = vsync
 * interrupt status. Toggling the mask (3 then 2) clears the status, like
 * _MDrv_GFLIP_IntHandler does. Counts vsyncs for 1 s and restores the reg.
 * Build on the board: gcc -O2 -o gopvsync gopvsync.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>

#define BANKSEL	0x1fc
#define INT	0x020	/* reg 0x1F10 */

static volatile uint8_t *gop;
static inline void wr(unsigned o, uint16_t v) { *(volatile uint16_t *)(gop + 0xe00 + o) = v; }
static inline uint16_t rd(unsigned o) { return *(volatile uint16_t *)(gop + 0xe00 + o); }

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	uint16_t orig, v;
	int n = 0, polls = 0;
	double t0, first = 0, last = 0;

	gop = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x1f203000);
	wr(BANKSEL, 0x7009);
	orig = rd(INT);
	printf("bank 9 reg 0x1F10 = 0x%04x\n", orig);
	wr(INT, (orig & ~3) | 2);			/* unmask vsync */
	t0 = now();
	while (now() - t0 < 1.0) {
		polls++;
		v = rd(INT);
		if (v & 0x100) {
			double t = now();

			if (!n)
				first = t;
			last = t;
			n++;
			wr(INT, (orig & ~3) | 3);	/* clear: mask ... */
			wr(INT, (orig & ~3) | 2);	/* ... and unmask */
		}
	}
	wr(INT, orig);
	wr(BANKSEL, 0x7000);
	printf("%d vsync flags in 1 s (%d polls)", n, polls);
	if (n > 1)
		printf(", period %.3f ms", (last - first) * 1000 / (n - 1));
	printf("\n");
	return 0;
}
