/*
 * dmdraw - read demodulator register XDATA 0x2096..0x2099 (RIU bank 0x1120)
 * directly from MIPS, without the 8051 mailbox.
 *
 * RIU 0x101E39 selects who owns the demodulator register bus (1 = 8051,
 * as left by MBoot tuner_init; 0 = MIPS, see HAL_DMD_ADC_IQ_Switch).
 * We switch it to MIPS for the capture only and restore it afterwards.
 *
 *   dmdraw N [out.bin]   N 32-bit reads of RIU 0x112096/0x112098
 *                        (bytes 0x2096,0x2097,0x2098,0x2099 per sample)
 *
 * Prints the read rate, mean/variance of 0x2098 and the lag-1..4
 * autocorrelation. Build on the board: gcc -O2 -o dmdraw dmdraw.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define RIU_PHYS	0x1F200000UL
#define RIU_SIZE	0x40000UL
#define DMD_OWNER	0x03C71		/* RIU 0x101E39 */
#define SIG_2096	0x2412C		/* RIU 0x112096 (lo) / 0x112097 (hi) */
#define SIG_2098	0x24130		/* RIU 0x112098 (lo) / 0x112099 (hi) */

int main(int argc, char **argv)
{
	unsigned long n = argc > 1 ? strtoul(argv[1], NULL, 0) : 10000;
	volatile uint8_t *riu;
	struct timespec t0, t1;
	uint8_t *s, owner;
	double m = 0, v = 0, dt;
	int fd;

	s = malloc(n * 4);
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0 || !s) {
		perror("open");
		return 1;
	}
	riu = mmap(NULL, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	owner = riu[DMD_OWNER];
	riu[DMD_OWNER] = 0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (unsigned long i = 0; i < n; i++) {
		uint16_t a = *(volatile uint16_t *)(riu + SIG_2096);
		uint16_t b = *(volatile uint16_t *)(riu + SIG_2098);
		s[i * 4] = a;
		s[i * 4 + 1] = a >> 8;
		s[i * 4 + 2] = b;
		s[i * 4 + 3] = b >> 8;
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	riu[DMD_OWNER] = owner;

	dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	for (unsigned long i = 0; i < n; i++)
		m += s[i * 4 + 2] - 128.0;
	m /= n;
	for (unsigned long i = 0; i < n; i++) {
		double x = s[i * 4 + 2] - 128.0 - m;
		v += x * x;
	}
	v /= n;
	printf("owner was 0x%02x, %lu samples in %.3f ms = %.1f ns/sample\n",
	       owner, n, dt * 1e3, dt * 1e9 / n);
	printf("0x2098: mean %.2f var %.1f\n", m, v);
	for (int lag = 1; lag <= 4; lag++) {
		double c = 0;
		for (unsigned long i = lag; i < n; i++)
			c += (s[i * 4 + 2] - 128.0 - m) * (s[(i - lag) * 4 + 2] - 128.0 - m);
		printf("  autocorr lag %d: %.3f\n", lag, c / (n - lag) / v);
	}
	if (argc > 2) {
		FILE *f = fopen(argv[2], "wb");
		fwrite(s, 4, n, f);
		fclose(f);
	}
	return 0;
}
