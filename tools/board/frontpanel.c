/*
 * frontpanel - read the WIWA HD95 front-panel buttons (POWER, CH+, CH-).
 *
 * The buttons sit on an FD650/TM1650-style 2-wire controller that the stock
 * firmware bit-bangs on SAR pins used as GPIO (see docs/przyciski_sar.md):
 *   GPIO 18 = DIO = SAR pin 1, GPIO 19 = CLK = SAR pin 2
 *   OEN 0x1F002845, OUT 0x1F002848, IN 0x1F002849 (bit 1 = DIO, bit 2 = CLK)
 * Key read: START, 0x4F, read 8 bits, STOP. Stock key codes 0x47/0x4F/0x57.
 *
 *   frontpanel keys [SECONDS]       poll every 100 ms, print code changes
 *   frontpanel cmd BYTE1 BYTE2      send a 2-byte command (e.g. 0x48 0x11)
 *   frontpanel disp TEXT [BRIGHT]   show up to 4 chars (0-9 A-Z subset, '-', ' ';
 *                                   '.' after a char lights its dot), BRIGHT 1..7
 *   frontpanel raw S0 S1 S2 S3      write raw segment bytes to the 4 digits
 *
 * Build on the board: gcc -O2 -o frontpanel frontpanel.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define PM_PAGE		0x1F002000UL
#define SAR_ANA		0x844	/* 0x1422: bits 0-3 = SAR pin analog mode */
#define GPIO_OEN	0x845	/* 0x1423: 1 = input */
#define GPIO_OUT	0x848	/* 0x1424 */
#define GPIO_IN		0x849	/* 0x1425 */
#define DIO		0x02
#define CLK		0x04

static volatile uint8_t *pm;

/* 7-segment font of this board, copied from the stock firmware (0x8038283C) */
static const struct { char c; uint8_t seg; } font[] = {
	{ '0', 0xd7 }, { '1', 0x14 }, { '2', 0xcd }, { '3', 0x5d }, { '4', 0x1e },
	{ '5', 0x5b }, { '6', 0xdb }, { '7', 0x15 }, { '8', 0xdf }, { '9', 0x5f },
	{ 'A', 0x9f }, { 'B', 0xda }, { 'b', 0xda }, { 'C', 0xc3 }, { 'c', 0xc8 },
	{ 'D', 0xdc }, { 'd', 0xdc }, { 'E', 0xcb }, { 'F', 0x8b }, { 'H', 0x9e },
	{ 'h', 0x9a }, { 'I', 0x82 }, { 'i', 0x80 }, { 'L', 0xc2 }, { 'n', 0x98 },
	{ 'N', 0x97 }, { 'O', 0xd8 }, { 'o', 0xd8 }, { 'P', 0x8f }, { 'R', 0xc8 },
	{ 'r', 0xc8 }, { 'S', 0x5b }, { 'T', 0xca }, { 't', 0xca }, { 'U', 0xd6 },
	{ 'V', 0xd0 }, { '-', 0x08 }, { ' ', 0x00 },
};
#define SEG_DOT		0x20	/* the only bit missing from '8' */
/* digit registers, leftmost first (stock firmware order) */
static const uint8_t digit_reg[4] = { 0x6e, 0x6c, 0x6a, 0x68 };

static void dly(void)
{
	struct timespec t = { 0, 10000 };	/* ~10 us */

	nanosleep(&t, NULL);
}

static void pin(uint8_t m, int high)
{
	if (high)
		pm[GPIO_OUT] |= m;
	else
		pm[GPIO_OUT] &= ~m;
	pm[GPIO_OEN] &= ~m;	/* drive */
}

static int dio_in(void)
{
	pm[GPIO_OEN] |= DIO;	/* release DIO */
	return !!(pm[GPIO_IN] & DIO);
}

static void start(void)
{
	pin(DIO, 1); dly(); pin(CLK, 1); dly();
	pin(DIO, 0); dly(); pin(CLK, 0); dly();
}

static void stop(void)
{
	pin(DIO, 0); dly(); pin(CLK, 1); dly();
	pin(DIO, 1); dly();
}

static int send(uint8_t b)
{
	int ack;

	for (int i = 0; i < 8; i++, b <<= 1) {
		pin(DIO, b & 0x80); dly();
		pin(CLK, 1); dly();
		pin(CLK, 0); dly();
	}
	ack = !dio_in(); dly();		/* 9th clock: ACK */
	pin(CLK, 1); dly();
	pin(CLK, 0); dly();
	return ack;
}

static uint8_t recv(void)
{
	uint8_t v = 0;

	dio_in();
	for (int i = 0; i < 8; i++) {
		dly();
		pin(CLK, 1); dly();
		v = (v << 1) | dio_in();
		pin(CLK, 0);
	}
	dly();
	pin(DIO, 0); dly();		/* ACK from master */
	pin(CLK, 1); dly();
	pin(CLK, 0); dly();
	return v;
}

static void cmd2(uint8_t a, uint8_t b)
{
	start();
	send(a);
	send(b);
	stop();
}

static uint8_t glyph(char c)
{
	for (unsigned i = 0; i < sizeof(font) / sizeof(font[0]); i++)
		if (font[i].c == c)
			return font[i].seg;
	return 0x08;	/* unknown: '-' */
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	if (fd < 0 || argc < 2) {
		fprintf(stderr, "usage: %s keys [SECONDS] | cmd B1 B2\n", argv[0]);
		return 1;
	}
	pm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PM_PAGE);
	if (pm == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	pm[SAR_ANA] &= ~(DIO | CLK);	/* SAR pins 1,2 as digital GPIO */
	pin(DIO, 1);
	pin(CLK, 1);

	if (!strcmp(argv[1], "cmd") && argc >= 4) {
		start();
		int a1 = send(strtoul(argv[2], NULL, 0));
		int a2 = send(strtoul(argv[3], NULL, 0));
		stop();
		printf("ack %d %d\n", a1, a2);
		return 0;
	}

	if (!strcmp(argv[1], "disp") && argc >= 3) {
		int bright = argc > 3 ? atoi(argv[3]) : 1;
		uint8_t seg[4] = { 0 };
		int d = 0;

		for (const char *p = argv[2]; *p && d < 4; p++) {
			if (*p == '.' && d > 0)
				seg[d - 1] |= SEG_DOT;
			else
				seg[d++] = glyph(*p);
		}
		cmd2(0x48, ((bright & 7) << 4) | 0x01);	/* display on, brightness */
		for (d = 0; d < 4; d++)
			cmd2(digit_reg[d], seg[d]);
		return 0;
	}
	if (!strcmp(argv[1], "raw") && argc >= 6) {
		cmd2(0x48, 0x11);
		for (int d = 0; d < 4; d++)
			cmd2(digit_reg[d], strtoul(argv[2 + d], NULL, 0));
		return 0;
	}

	double secs = argc > 2 ? atof(argv[2]) : 30;
	struct timespec t0, t, p = { 0, 100000000L };
	int last = -1;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	do {
		start();
		int ack = send(0x4f);
		uint8_t k = recv();
		stop();

		if (k != last) {
			clock_gettime(CLOCK_MONOTONIC, &t);
			printf("t=%6.2f  ack=%d  key=0x%02x%s\n",
			       (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9, ack, k,
			       k == 0x47 ? "  (stock key A)" : k == 0x4f ? "  (stock key B)" :
			       k == 0x57 ? "  (stock key C)" : "");
			fflush(stdout);
			last = k;
		}
		nanosleep(&p, NULL);
		clock_gettime(CLOCK_MONOTONIC, &t);
	} while ((t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9 < secs);
	return 0;
}
