/*
 * gpiodump_linux - dump the GPIO / pad RIU banks in the same
 * format as tools/baremetal/stockdump (patched "Help" of the stock firmware),
 * so the two can be compared with diff.
 * Lines: "bfXXXXXX: vvvv ..." with 8 16-bit registers (CPU stride 4).
 * Build on the board: gcc -O2 -o gpiodump_linux gpiodump_linux.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

static const uint32_t ranges[][2] = {
	{ 0xbf001c00, 0xbf001e00 },	/* PM 0x0Exx */
	{ 0xbf002800, 0xbf002a00 },	/* SAR 0x14xx */
	{ 0xbf203c00, 0xbf203e00 },	/* chiptop 0x101Exx */
	{ 0xbf204a00, 0xbf204c00 },	/* adc_atop */
	{ 0xbf204c00, 0xbf204e00 },	/* adc_dtop */
};

int main(void)
{
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	volatile uint8_t *riu = mmap(0, 0x400000, PROT_READ, MAP_SHARED, fd, 0x1f000000);

	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (unsigned r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++)
		for (uint32_t a = ranges[r][0]; a < ranges[r][1]; a += 4) {
			if (!(a & 0x1f))
				printf("%s%08x:", a == ranges[0][0] ? "" : "\n", a);
			printf(" %04x", *(volatile uint16_t *)(riu + (a - 0xbf000000)));
		}
	printf("\n");
	return 0;
}
