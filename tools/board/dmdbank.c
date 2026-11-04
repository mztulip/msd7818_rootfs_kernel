/*
 * dmdbank - dump demodulator-related RIU banks as 16-bit words with the demod
 * register bus switched to MIPS (RIU 0x101E39 = 0, restored afterwards).
 *
 *   dmdbank [-k K] BANK...     e.g. dmdbank -k 8 0x1120 0x1127 0x111f
 *
 * Each bank is 0x80 words (RIU 0xBBBB00..0xBBBBFF). The bank is read K times
 * (default 4); words that changed between passes are marked with '*'.
 * Output: "BBBBoo: wwww wwww ..." with 8 words per line, oo = byte offset.
 * Build on the board: gcc -O2 -o dmdbank dmdbank.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define RIU_PHYS	0x1F200000UL
#define RIU_SIZE	0x40000UL
#define DMD_OWNER	0x03C71		/* RIU 0x101E39 */

int main(int argc, char **argv)
{
	volatile uint8_t *riu;
	int k = 4, i = 1, fd;
	uint8_t owner;

	if (argc > 2 && !strcmp(argv[1], "-k")) {
		k = atoi(argv[2]);
		i = 3;
	}
	if (i >= argc) {
		fprintf(stderr, "usage: %s [-k K] BANK...\n", argv[0]);
		return 1;
	}
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	riu = mmap(NULL, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	owner = riu[DMD_OWNER];
	riu[DMD_OWNER] = 0;
	for (; i < argc; i++) {
		unsigned long bank = strtoul(argv[i], NULL, 0);
		volatile uint16_t *w = (volatile uint16_t *)(riu + (bank - 0x1000) * 0x200);
		uint16_t first[0x80];
		uint8_t chg[0x80] = { 0 };

		for (int p = 0; p < k; p++)
			for (int r = 0; r < 0x80; r++) {
				uint16_t v = w[r * 2];

				if (!p)
					first[r] = v;
				else if (v != first[r])
					chg[r] = 1;
			}
		for (int r = 0; r < 0x80; r += 8) {
			printf("%04lx%02x:", bank, r * 2);
			for (int j = r; j < r + 8; j++)
				printf(" %04x%c", first[j], chg[j] ? '*' : ' ');
			printf("\n");
		}
	}
	riu[DMD_OWNER] = owner;
	return 0;
}
