/*
 * rfscan - swept RF spectrum on the WIWA HD95 (MSD7818 + MxL5007T).
 *
 * Tunes the MxL5007T over a frequency range (I2C, /dev/i2c-0), measures the
 * signal power at each step as the variance of demodulator XDATA byte 0x2098
 * read through the 8051 mailbox, and draws a bar graph (dB) straight into the
 * framebuffer at phys 0x2100000 (1920x1080 RGB565). Also prints a table.
 *
 * Needs: MBoot `tuner_init` (demod 8051 running), the i2c-mstar-miic driver,
 * and the framebuffer console unbound
 * (echo 0 > /sys/class/vtconsole/vtcon1/bind), otherwise fbcon overwrites it.
 *
 *   rfscan [START_MHZ] [STOP_MHZ] [STEP_MHZ] [READS] [LOOPS] [SETTLE_MS]
 *   defaults: 44 880 4 1500 1 50     (LOOPS 0 = forever)
 *
 * Build on the board: gcc -O2 -o rfscan rfscan.c -lm
 */
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define RIU_PHYS	0x1F200000UL
#define RIU_SIZE	0x40000UL
#define MBX_CMD		0x24C00
#define MBX_ADDR_L	0x24C01
#define MBX_ADDR_H	0x24C04
#define MBX_DATA	0x24C05
#define MCU_INT		0x06905

#define FB_PHYS		0x2100000UL
#define FB_W		1920
#define FB_H		1080

#define SIG_REG		0x2098	/* demod XDATA byte that follows the IF signal */
#define TUNER_ADDR	0x60

static volatile uint8_t *riu;
static volatile uint16_t *fb;
static int i2c;

/* ---- demod 8051 mailbox ---- */

static int wait_cmd(uint8_t want)
{
	for (int i = 0; i < 2000000; i++)
		if (riu[MBX_CMD] == want)
			return 0;
	return -1;
}

static int xdata_read(uint16_t addr, uint8_t *v)
{
	if (wait_cmd(0))
		return -1;
	riu[MBX_ADDR_H] = addr >> 8;
	riu[MBX_ADDR_L] = addr & 0xff;
	riu[MBX_DATA] = 0;
	riu[MBX_CMD] = 1;
	riu[MCU_INT] = 2;
	riu[MCU_INT] = 0;
	if (wait_cmd(0xff))
		return -1;
	*v = riu[MBX_DATA];
	riu[MBX_CMD] = 0;
	return 0;
}

static double measure_var(int reads)
{
	double s = 0, q = 0;
	uint8_t v;

	for (int i = 0; i < reads; i++) {
		if (xdata_read(SIG_REG, &v)) {
			fprintf(stderr, "mailbox timeout\n");
			exit(1);
		}
		int x = (int)v - 128;
		s += x;
		q += (double)x * x;
	}
	s /= reads;
	return q / reads - s * s;
}

/* ---- MxL5007T over /dev/i2c-0 ---- */

static void mxl_w(uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = TUNER_ADDR, .flags = 0, .len = 2, .buf = buf };
	struct i2c_rdwr_ioctl_data d = { .msgs = &msg, .nmsgs = 1 };

	if (ioctl(i2c, I2C_RDWR, &d) < 0) {
		perror("i2c write");
		exit(1);
	}
}

static uint8_t mxl_r(uint8_t reg)
{
	uint8_t w[2] = { 0xfb, reg }, r = 0;
	struct i2c_msg msg[2] = {
		{ .addr = TUNER_ADDR, .flags = 0, .len = 2, .buf = w },
		{ .addr = TUNER_ADDR, .flags = I2C_M_RD, .len = 1, .buf = &r },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = msg, .nmsgs = 2 };

	if (ioctl(i2c, I2C_RDWR, &d) < 0) {
		perror("i2c read");
		exit(1);
	}
	return r;
}

/* Init for DVB-T, IF 4.57 MHz, 24 MHz xtal, clock out on (see docs/mxl5007t.md) */
static void mxl_init(void)
{
	static const uint8_t tab[][2] = {
		{ 0x02, 0x03 }, { 0x03, 0x48 }, { 0x05, 0x04 }, { 0x06, 0x11 },
		{ 0x2e, 0x15 }, { 0x30, 0x10 }, { 0x45, 0x58 }, { 0x48, 0x19 },
		{ 0x52, 0x03 }, { 0x53, 0x44 }, { 0x6a, 0x4b }, { 0x76, 0x00 },
		{ 0x78, 0x18 }, { 0x7a, 0x17 }, { 0x85, 0x06 }, { 0x01, 0x01 },
	};

	for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		mxl_w(tab[i][0], tab[i][1]);
}

static void mxl_tune_khz(unsigned khz)
{
	unsigned n = khz * 64 / 1000;

	if (khz * 64 % 1000 > 500)
		n++;
	mxl_w(0x0f, 0x00);
	mxl_w(0x0c, 0x3f);	/* 8 MHz */
	mxl_w(0x0d, n & 0xff);
	mxl_w(0x0e, n >> 8);
	mxl_w(0x1f, 0x87);
	mxl_w(0x20, 0x1f);
	mxl_w(0x21, 0x87);
	mxl_w(0x22, 0x1f);
	mxl_w(0x80, khz >= 333000 ? 0x41 : 0x01);
	mxl_w(0x0f, 0x01);
}

/* ---- drawing (RGB565) ---- */

#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static void rect(int x, int y, int w, int h, uint16_t c)
{
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			if (i >= 0 && i < FB_W && j >= 0 && j < FB_H)
				fb[j * FB_W + i] = c;
}

int main(int argc, char **argv)
{
	unsigned start = argc > 1 ? atoi(argv[1]) : 44;
	unsigned stop = argc > 2 ? atoi(argv[2]) : 880;
	unsigned step = argc > 3 ? atoi(argv[3]) : 4;
	int reads = argc > 4 ? atoi(argv[4]) : 1500;
	int loops = argc > 5 ? atoi(argv[5]) : 1;
	int settle_ms = argc > 6 ? atoi(argv[6]) : 50;
	int npts = (stop - start) / step + 1;
	double *db = calloc(npts, sizeof(*db));
	const int X0 = 60, Y0 = 60, GW = FB_W - 120, GH = FB_H - 160;
	const double DB_MIN = 0, DB_MAX = 40;	/* 10*log10(var), var 1..10000 */
	int fd;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	riu = mmap(NULL, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	fb = mmap(NULL, FB_W * FB_H * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FB_PHYS);
	if (riu == MAP_FAILED || fb == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	i2c = open("/dev/i2c-0", O_RDWR);
	if (i2c < 0) {
		perror("/dev/i2c-0");
		return 1;
	}
	if (mxl_r(0xd9) != 0x14)
		fprintf(stderr, "warning: tuner id 0x%02x, expected 0x14\n", mxl_r(0xd9));

	mxl_init();

	/* background, frame and 100 MHz grid */
	rect(0, 0, FB_W, FB_H, RGB(0, 0, 0));
	rect(X0 - 2, Y0 - 2, GW + 4, 2, RGB(90, 90, 90));
	rect(X0 - 2, Y0 + GH, GW + 4, 2, RGB(90, 90, 90));
	for (unsigned f = (start + 99) / 100 * 100; f <= stop; f += 100) {
		int x = X0 + (int)((double)(f - start) / (stop - start) * GW);

		rect(x, Y0, 1, GH, RGB(40, 40, 70));
		rect(x - 1, Y0 + GH + 4, 3, 16, RGB(200, 200, 200));
	}
	for (int d = 10; d < DB_MAX; d += 10)
		rect(X0, Y0 + GH - (int)(d / DB_MAX * GH), GW, 1, RGB(40, 70, 40));

	for (int loop = 0; loops == 0 || loop < loops; loop++) {
		for (int i = 0; i < npts; i++) {
			unsigned f = start + i * step;
			struct timespec ts = { settle_ms / 1000, (settle_ms % 1000) * 1000000L };
			struct timespec ms = { 0, 1000000L };
			double var;

			mxl_tune_khz(f * 1000);
			for (int t = 0; t < 100 && mxl_r(0xd8) != 0x0f; t++)
				nanosleep(&ms, NULL);	/* wait for RF + ref PLL lock */
			nanosleep(&ts, NULL);
			var = measure_var(reads);
			db[i] = 10 * log10(var > 1 ? var : 1);
			if (loop == 0 || loops == 1)
				printf("%4u MHz  var %8.1f  %5.1f dB  lock 0x%02x\n",
				       f, var, db[i], mxl_r(0xd8));

			int x = X0 + i * GW / npts;
			int w = GW / npts > 1 ? GW / npts - 1 : 1;
			int h = (int)((db[i] - DB_MIN) / (DB_MAX - DB_MIN) * GH);

			if (h < 0)
				h = 0;
			if (h > GH)
				h = GH;
			/* replace this column only: clear, dB grid, bar, sweep cursor */
			rect(x, Y0, w + 1, GH, RGB(0, 0, 0));
			for (int d = 10; d < DB_MAX; d += 10)
				rect(x, Y0 + GH - (int)(d / DB_MAX * GH), w + 1, 1, RGB(40, 70, 40));
			rect(x, Y0 + GH - h, w, h, RGB(40, 200, 255));
			rect(x, Y0 + GH - h, w, 3, RGB(255, 255, 80));
			for (unsigned g = (start + 99) / 100 * 100; g <= stop; g += 100) {
				int gx = X0 + (int)((double)(g - start) / (stop - start) * GW);

				if (gx >= x && gx <= x + w)
					rect(gx, Y0, 1, GH - h, RGB(40, 40, 70));
			}
			rect(X0, Y0 + GH + 24, GW, 6, RGB(0, 0, 0));
			rect(x, Y0 + GH + 24, w > 4 ? w : 4, 6, RGB(255, 60, 60));
		}
		fflush(stdout);
	}
	return 0;
}
