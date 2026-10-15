/*
 * riudump - dump the audio / HDMI TX related RIU register ranges in the same
 * format as tools/baremetal/stockdump (patched "Help" of the stock firmware),
 * so the two can be compared with diff.
 * Lines: "bfXXXXXX: vvvv ..." with 8 16-bit registers (CPU stride 4).
 * Build on the board: gcc -O2 -o riudump riudump.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

static const uint32_t ranges[][2] = {	/* CPU addresses as in the stock dump */
	{ 0xbf225400, 0xbf226000 },	/* audio 0x112A00-0x112FFF */
	{ 0xbf226200, 0xbf226400 },	/* HDMI TX 0x1131xx */
	{ 0xbf226600, 0xbf226800 },	/* HDMI TX 0x1133xx */
	{ 0xbf221800, 0xbf221a00 },	/* 0x110Cxx */
	{ 0xbf201600, 0xbf201800 },	/* clkgen 0x100Bxx */
	{ 0xbf206600, 0xbf206800 },	/* 0x1033xx */
	{ 0xbf006800, 0xbf006a00 },	/* PM 0x34xx */
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
