/*
 * gopdump - dump all GOP register banks (0x0-0xD), regs 0x1F00-0x1F7E.
 * Each bank is read twice and marked '!' if the two reads differ (the
 * mstar-gopfb vsync interrupt also switches the bank select).
 * Build on the board: gcc -O2 -o gopdump gopdump.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main(void)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	volatile uint16_t *r = (volatile uint16_t *)((uint8_t *)mmap(0, 0x1000, PROT_READ | PROT_WRITE,
					MAP_SHARED, fd, 0x1f203000) + 0xe00);
	uint16_t a[64], b[64];

	for (int bank = 0; bank <= 0xd; bank++) {
		for (int pass = 0; pass < 2; pass++) {
			uint16_t *d = pass ? b : a;

			r[0x1fc / 2] = 0x7000 | bank;
			for (int i = 0; i < 64; i++)
				d[i] = r[i * 2];
			r[0x1fc / 2] = 0x7000;
		}
		printf("bank %X%s\n", bank, memcmp(a, b, sizeof(a)) ? " !" : "");
		for (int i = 0; i < 64; i++)
			printf("%s%04x%s", i % 16 ? " " : "  ", a[i], i % 16 == 15 ? "\n" : "");
	}
	return 0;
}
