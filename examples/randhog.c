/*
 * randhog [SECONDS] [MB] [mode] - random-access DRAM load (like a game
 * renderer, unlike memcpy): every access hits a different cache line and
 * usually a different DRAM row.
 *   mode w  random 32-bit writes (default)
 *   mode r  random reads
 *   mode c  column writes: stride 320 B, like DOOM drawing wall columns
 * Used to reproduce display glitches caused by memory load.
 * Build on the board: gcc -O2 -o randhog randhog.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 20;
	size_t n = (size_t)(argc > 2 ? atoi(argv[2]) : 8) << 20;
	char mode = argc > 3 ? argv[3][0] : 'w';
	uint32_t *m = malloc(n), x = 2463534242u, sum = 0;
	size_t words = n / 4;
	time_t end = time(NULL) + secs;
	unsigned long ops = 0;

	memset(m, 0, n);
	while (time(NULL) < end) {
		for (int i = 0; i < 100000; i++) {
			x ^= x << 13; x ^= x >> 17; x ^= x << 5;
			size_t k = x % words;

			if (mode == 'r') {
				sum += m[k];
			} else if (mode == 'c') {
				uint32_t *col = m + (k & ~(size_t)0xffff) % (words - 16000);
				for (int y = 0; y < 200; y++)
					col[y * 80 + (x & 63)] = x;
				i += 199;
			} else {
				m[k] = x;
			}
		}
		ops += 100000;
	}
	printf("randhog %c: %lu M ops in %d s (%u)\n", mode, ops / 1000000, secs, sum);
	return 0;
}
