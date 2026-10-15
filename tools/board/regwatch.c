/*
 * regwatch - report RIU registers that change, e.g. while pressing buttons.
 *
 * Watches 16-bit registers in a few RIU windows (physical addresses, 4-byte
 * stride). First LEARN_S seconds learn which registers change by themselves
 * (timers, counters) and ignore them; then every change is printed with a
 * timestamp until RUN_S seconds have passed.
 *
 *   regwatch [RUN_S] [LEARN_S]          defaults 40 3
 *
 * Build on the board: gcc -O2 -o regwatch regwatch.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct win {
	unsigned long phys;
	unsigned long size;
	const char *name;
};

/*
 * Banks known to be safe to read. Reading the whole RIU hangs the bus
 * (tested 2026-10-13: board froze, needed a power cycle).
 */
static const struct win wins[] = {
	{ 0x1F001C00, 0x0800, "PM 0x0E00-0x11FF (PM ctrl/GPIO)" },
	{ 0x1F002800, 0x0200, "PM SAR" },
	{ 0x1F203C00, 0x0400, "chiptop" },
	{ 0x1F204A00, 0x0400, "adc_atop/dtop" },
};
#define NW (sizeof(wins) / sizeof(wins[0]))

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	double run = argc > 1 ? atof(argv[1]) : 40, learn = argc > 2 ? atof(argv[2]) : 3;
	volatile uint16_t *map[NW];
	uint16_t *base[NW], *last[NW];
	uint8_t *noisy[NW];
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	for (unsigned w = 0; w < NW; w++) {
		unsigned n = wins[w].size / 4;

		unsigned long pg = wins[w].phys & ~0xfffUL, off = wins[w].phys - pg;
		volatile uint8_t *m = mmap(NULL, wins[w].size + off, PROT_READ, MAP_SHARED, fd, pg);

		if (m == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		map[w] = (volatile uint16_t *)(m + off);
		base[w] = calloc(n, 2);
		last[w] = calloc(n, 2);
		noisy[w] = calloc(n, 1);
		for (unsigned i = 0; i < n; i++)
			base[w][i] = last[w][i] = map[w][i * 2];
	}

	double t0 = now();
	int learning = 1;

	fprintf(stderr, "learning noisy registers for %.0f s - do not touch anything\n", learn);
	while (now() - t0 < run + learn) {
		if (learning && now() - t0 >= learn) {
			learning = 0;
			unsigned cnt = 0;

			for (unsigned w = 0; w < NW; w++)
				for (unsigned i = 0; i < wins[w].size / 4; i++)
					if (noisy[w][i]) {
						cnt++;
						printf("noisy: %08lx\n", wins[w].phys + i * 4);
					}
			printf("%u noisy registers (reported only on changes > 8); PRESS BUTTONS NOW\n", cnt);
			fflush(stdout);
		}
		for (unsigned w = 0; w < NW; w++) {
			for (unsigned i = 0; i < wins[w].size / 4; i++) {
				uint16_t v = map[w][i * 2];

				if (v == last[w][i])
					continue;
				int big = abs((v & 0xff) - (last[w][i] & 0xff)) > 8 ||
					  abs((v >> 8) - (last[w][i] >> 8)) > 8;

				if (learning) {
					noisy[w][i] = 1;
				} else if (!noisy[w][i] || big) {
					printf("t=%5.2f  %08lx  %04x -> %04x   (%s, RIU 0x%06lx)\n",
					       now() - t0 - learn, wins[w].phys + i * 4,
					       last[w][i], v, wins[w].name, ((wins[w].phys + i * 4) & 0x3fffff) / 2);
					fflush(stdout);
				}
				last[w][i] = v;
			}
		}
		usleep(2000);
	}
	printf("done\n");
	return 0;
}
