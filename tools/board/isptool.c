/*
 * isptool - send raw SPI flash commands through the Kronus ISP controller
 * (0x1f001000) from userspace, for debugging the spi-mstar-isp driver.
 * Same byte sequence as the driver: unlock, bytes, CS up, lock.
 * Build on the board: gcc -O2 -o isptool isptool.c
 * Usage: isptool HEXBYTE... [rN]     e.g. "isptool 9f r3" (RDID),
 *        "isptool 05 r1" (RDSR), "isptool 06" (WREN)
 * Register 0x1c8 bit 2 can be forced with ISP_WRCTRL=0|1 in the environment.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static volatile uint8_t *isp;

static uint16_t rd(unsigned off) { return *(volatile uint16_t *)(isp + off); }
static void wr(unsigned off, uint16_t v) { *(volatile uint16_t *)(isp + off) = v; }

static int wait(unsigned off)
{
	for (int i = 0; i < 1000000; i++)
		if (rd(off) & 1)
			return 0;
	fprintf(stderr, "timeout 0x%x\n", off);
	return -1;
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	uint8_t *m = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x1f001000);
	char *w = getenv("ISP_WRCTRL");
	uint16_t wrctrl, rst;

	if (m == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	isp = m;
	wrctrl = rd(0x1c8);
	printf("regs: pw %04x trig %04x clkdiv %04x rst %04x 1c8 %04x\n",
	       rd(0x00), rd(0xa8), rd(0x18), rd(0xfc), wrctrl);
	wr(0x00, 0xaaaa);
	wr(0xa8, 0x3333);
	if (w)
		wr(0x1c8, atoi(w) ? (wrctrl | 4) : (wrctrl & ~4));
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == 'r') {
			int n = atoi(argv[i] + 1);
			printf("rx:");
			while (n--) {
				wr(0x30, 1);
				wait(0x54);
				printf(" %02x", rd(0x14) & 0xff);
			}
			printf("\n");
		} else {
			wr(0x10, strtoul(argv[i], NULL, 16));
			wait(0x58);
		}
	}
	wr(0x20, 1);
	wr(0x1c8, wrctrl);
	wr(0xa8, 0x2222);
	wr(0x00, 0x5555);
	rst = rd(0xfc);
	wr(0xfc, rst & ~4);
	wr(0xfc, rst | 4);
	return 0;
}
