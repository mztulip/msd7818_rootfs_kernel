/*
 * dmdmbx - access the Kronus DVB-T demodulator 8051 XDATA space from Linux
 * through the MIPS<->8051 mailbox (see docs/demodulator_if.md).
 *
 * Requires that MBoot ran `tuner_init` (8051 firmware loaded and running).
 *
 *   dmdmbx r ADDR [N]          read N bytes of XDATA (cmd 1)
 *   dmdmbx w ADDR VAL          write one byte of XDATA (cmd 2)
 *   dmdmbx dr ADDR [N]         read "DSP registers" = XDATA 0x8300+ADDR (cmd 3)
 *   dmdmbx changes ADDR N [K]  read the range K times (default 8), print bytes that change
 *   dmdmbx sample ADDR N K     print the range K times, one line per pass
 *   dmdmbx pwr ADDR [K]        mean/variance of K reads (default 1000) of an
 *                              offset-binary byte, e.g. 0x2098 (signal level)
 *
 * Build on the board: gcc -O2 -o dmdmbx dmdmbx.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define RIU_PHYS	0x1F200000UL	/* non-PM RIU, CPU 0xBF200000 */
#define RIU_SIZE	0x40000UL

#define MBX_CMD		0x24C00		/* RIU 0x112600 low  */
#define MBX_ADDR_L	0x24C01		/* RIU 0x112600 high */
#define MBX_ADDR_H	0x24C04		/* RIU 0x112602 low  */
#define MBX_DATA	0x24C05		/* RIU 0x112602 high */
#define MCU_INT		0x06905		/* RIU 0x103483, bit 1 = INT0 to the 8051 */

static volatile uint8_t *riu;

static int wait_cmd(uint8_t want)
{
	struct timespec t0, t;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (riu[MBX_CMD] != want) {
		clock_gettime(CLOCK_MONOTONIC, &t);
		if ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 > 200)
			return -1;
	}
	return 0;
}

static int mbx(uint8_t cmd, uint16_t addr, uint8_t *data)
{
	if (wait_cmd(0)) {
		fprintf(stderr, "mailbox busy (cmd=0x%02x)\n", riu[MBX_CMD]);
		return -1;
	}
	riu[MBX_ADDR_H] = addr >> 8;
	riu[MBX_ADDR_L] = addr & 0xff;
	riu[MBX_DATA] = *data;
	riu[MBX_CMD] = cmd;
	riu[MCU_INT] = 2;
	riu[MCU_INT] = 0;
	if (wait_cmd(0xff)) {
		fprintf(stderr, "no handshake from 8051 (cmd=0x%02x)\n", riu[MBX_CMD]);
		return -1;
	}
	*data = riu[MBX_DATA];
	riu[MBX_CMD] = 0;
	return 0;
}

static int rd(uint16_t a, uint8_t *v) { *v = 0; return mbx(1, a, v); }

int main(int argc, char **argv)
{
	unsigned long addr, n = 1, k = 8;
	uint8_t v;
	int fd;

	if (argc < 3) {
		fprintf(stderr, "usage: %s r|w|dr|changes|sample ADDR [...]\n", argv[0]);
		return 1;
	}
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	riu = mmap(NULL, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	addr = strtoul(argv[2], NULL, 0);
	if (argc > 3)
		n = strtoul(argv[3], NULL, 0);
	if (argc > 4)
		k = strtoul(argv[4], NULL, 0);

	if (!strcmp(argv[1], "w")) {
		v = n;
		return mbx(2, addr, &v) ? 1 : 0;
	}
	if (!strcmp(argv[1], "r") || !strcmp(argv[1], "dr")) {
		uint8_t cmd = argv[1][0] == 'd' ? 3 : 1;

		for (unsigned long i = 0; i < n; i++) {
			if (i % 16 == 0)
				printf("%s%04lx:", i ? "\n" : "", addr + i);
			v = 0;
			if (mbx(cmd, addr + i, &v))
				return 1;
			printf(" %02x", v);
		}
		printf("\n");
		return 0;
	}
	if (!strcmp(argv[1], "pwr")) {
		/* pwr ADDR K: mean and variance of K reads of an offset-binary byte */
		double s = 0, q = 0;

		k = n > 1 ? n : 1000;
		for (unsigned long i = 0; i < k; i++) {
			int x;

			if (rd(addr, &v))
				return 1;
			x = (int)v - 128;
			s += x;
			q += (double)x * x;
		}
		printf("mean %.2f var %.1f\n", s / k, q / k - (s / k) * (s / k));
		return 0;
	}
	if (!strcmp(argv[1], "changes") || !strcmp(argv[1], "sample")) {
		uint8_t *first = calloc(n, 1), *cur = calloc(n, 1);
		unsigned long *cnt = calloc(n, sizeof(*cnt));
		int sample = argv[1][0] == 's';

		for (unsigned long p = 0; p < k; p++) {
			for (unsigned long i = 0; i < n; i++) {
				if (rd(addr + i, &cur[i]))
					return 1;
				if (p == 0)
					first[i] = cur[i];
				else if (cur[i] != first[i])
					cnt[i]++;
			}
			if (sample) {
				for (unsigned long i = 0; i < n; i++)
					printf("%02x ", cur[i]);
				printf("\n");
			}
		}
		if (!sample)
			for (unsigned long i = 0; i < n; i++)
				if (cnt[i])
					printf("%04lx: first %02x last %02x changed %lu/%lu\n",
					       addr + i, first[i], cur[i], cnt[i], k - 1);
		return 0;
	}
	fprintf(stderr, "unknown command\n");
	return 1;
}
