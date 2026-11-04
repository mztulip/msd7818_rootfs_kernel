/*
 * dmdts - capture raw ADC I samples (demod reg 0x2098 = RIU 0x112098) from
 * MIPS with a CP0 Count timestamp per sample, to measure how regular the
 * sampling is and to compute spectra on the PC (see docs/demodulator_if.md).
 *
 * RIU 0x101E39 is switched to 0 (demod register bus owned by MIPS) for the
 * capture and restored afterwards, as in dmdraw.c.
 *
 *   dmdts N out.bin
 *
 * out.bin: 8-byte header (u32 count rate in Hz, u32 N), then N records of
 * u32 Count + u16 RIU word 0x112098 (low byte = 0x2098) + u16 pad.
 * Count is read with rdhwr $2 (CC) and ticks every CCRes CPU cycles.
 * Build on the board: gcc -O2 -o dmdts dmdts.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define RIU_PHYS	0x1F200000UL
#define RIU_SIZE	0x40000UL
#define DMD_OWNER	0x03C71		/* RIU 0x101E39 */
#define SIG_2098	0x24130		/* RIU 0x112098 */

struct rec {
	uint32_t cnt;
	uint16_t w;
	uint16_t pad;
};

static inline uint32_t cc(void)
{
	uint32_t v;

	__asm__ volatile(".set push\n.set mips32r2\nrdhwr %0, $2\n.set pop" : "=r"(v));
	return v;
}

int main(int argc, char **argv)
{
	unsigned long n;
	volatile uint8_t *riu;
	volatile uint16_t *sig;
	struct timespec t0, t1;
	struct rec *r;
	uint32_t c0, c1, hdr[2];
	uint8_t owner;
	double dt;
	FILE *f;
	int fd;

	if (argc < 3) {
		fprintf(stderr, "usage: %s N out.bin\n", argv[0]);
		return 1;
	}
	n = strtoul(argv[1], NULL, 0);
	r = calloc(n, sizeof(*r));
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0 || !r) {
		perror("open");
		return 1;
	}
	riu = mmap(NULL, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	sig = (volatile uint16_t *)(riu + SIG_2098);
	/* fault in and lock the buffer: a first-touch page fault every 512 records
	 * (4 KB) otherwise leaves a ~20 us hole in the capture */
	memset(r, 0, n * sizeof(*r));
	mlockall(MCL_CURRENT | MCL_FUTURE);

	/* calibrate the Count rate against CLOCK_MONOTONIC */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	c0 = cc();
	usleep(200000);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	c1 = cc();
	dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	hdr[0] = (uint32_t)((c1 - c0) / dt + 0.5);
	hdr[1] = n;

	owner = riu[DMD_OWNER];
	riu[DMD_OWNER] = 0;
	for (unsigned long i = 0; i < n; i++) {
		r[i].cnt = cc();
		r[i].w = *sig;
	}
	riu[DMD_OWNER] = owner;

	printf("count rate %u Hz, %lu samples in %.3f ms\n", hdr[0], n,
	       (double)(r[n - 1].cnt - r[0].cnt) / hdr[0] * 1e3);
	f = fopen(argv[2], "wb");
	fwrite(hdr, 4, 2, f);
	fwrite(r, sizeof(*r), n, f);
	fclose(f);
	return 0;
}
