/*
 * fmrx - record one narrow RF channel to complex baseband on the WIWA HD95.
 *
 * Mixes every raw ADC sample (demod reg 0x2098, see specan.c) with an NCO stepped by
 * the sample's own CP0 Count timestamp, so interrupt gaps do not break the phase, and
 * averages I/Q over fixed time bins. This runs inside the sampling loop, so a recording
 * can last many seconds; the RAM holds I/Q instead of 72 MB/s of raw samples.
 *
 *   fmrx RF_KHZ TUNE_MHZ SECONDS out.iq [BIN_TICKS]     default bin 1150 ticks = 240 kS/s
 *
 * out.iq: u32 bin ticks, u32 bins, u32 samples, then bins of s16 I, s16 Q (s8 if the file
 * name ends in .i8). Needs MBoot `tuner_init`. Demodulate on the PC: tools/fm_demod.py.
 * Build: mipsel-buildroot-linux-musl-gcc -O2 -static -o fmrx fmrx.c -lm
 */
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define RIU_PHYS	0x1F200000UL
#define RIU_SIZE	0x40000UL
#define DMD_OWNER	0x03C71		/* RIU 0x101E39 */
#define SIG_2098	0x24130		/* RIU 0x112098 */
#define TUNER_ADDR	0x60
#define FIF_HZ		4595300		/* see docs/falszywe_sygnaly.md */
#define COUNT_HZ	276000000

static int i2c;

static inline uint32_t cc(void)
{
	uint32_t v;

	__asm__ volatile(".set push\n.set mips32r2\nrdhwr %0, $2\n.set pop" : "=r"(v));
	return v;
}

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

/* as rfscan.c / specan.c */
static void mxl_tune(int mhz)
{
	static const uint8_t tab[][2] = {
		{ 0x02, 0x03 }, { 0x03, 0x48 }, { 0x05, 0x04 }, { 0x06, 0x11 },
		{ 0x2e, 0x15 }, { 0x30, 0x10 }, { 0x45, 0x58 }, { 0x48, 0x19 },
		{ 0x52, 0x03 }, { 0x53, 0x44 }, { 0x6a, 0x4b }, { 0x76, 0x00 },
		{ 0x78, 0x18 }, { 0x7a, 0x17 }, { 0x85, 0x06 }, { 0x01, 0x01 },
	};
	int n = mhz * 64;

	for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		mxl_w(tab[i][0], tab[i][1]);
	mxl_w(0x0f, 0x00);
	mxl_w(0x0c, 0x3f);
	mxl_w(0x0d, n & 0xff);
	mxl_w(0x0e, n >> 8);
	mxl_w(0x1f, 0x87);
	mxl_w(0x20, 0x1f);
	mxl_w(0x21, 0x87);
	mxl_w(0x22, 0x1f);
	mxl_w(0x80, mhz >= 333 ? 0x41 : 0x01);
	mxl_w(0x0f, 0x01);
	usleep(100000);
}

int main(int argc, char **argv)
{
	static int16_t c8[256], s8[256];
	volatile uint8_t *riu;
	volatile uint16_t *sig;
	int fd, bin = argc > 5 ? atoi(argv[5]) : 1150;
	uint32_t hdr[3], nb, ph = 0, inc, samples = 0, gaps = 0, tlast, tbin;
	int16_t *iq;
	int i8out;
	FILE *f;

	if (argc < 5) {
		fprintf(stderr, "usage: %s RF_KHZ TUNE_MHZ SECONDS out.iq [BIN_TICKS]\n", argv[0]);
		return 1;
	}
	int rf = atoi(argv[1]), mhz = atoi(argv[2]);
	double ifhz = rf * 1e3 - mhz * 1e6 + FIF_HZ;

	nb = (uint32_t)(atof(argv[3]) * COUNT_HZ / bin);
	inc = (uint32_t)(ifhz / COUNT_HZ * 4294967296.0);
	i8out = strlen(argv[4]) > 3 && !strcmp(argv[4] + strlen(argv[4]) - 3, ".i8");
	for (int i = 0; i < 256; i++) {
		c8[i] = (int16_t)lrint(4095 * cos(2 * M_PI * i / 256));
		s8[i] = (int16_t)lrint(-4095 * sin(2 * M_PI * i / 256));
	}
	iq = calloc(nb, 4);
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	i2c = open("/dev/i2c-0", O_RDWR);
	if (!iq || fd < 0 || i2c < 0) {
		perror("open");
		return 1;
	}
	riu = mmap(NULL, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	sig = (volatile uint16_t *)(riu + SIG_2098);
	memset(iq, 0, nb * 4);
	mlockall(MCL_CURRENT | MCL_FUTURE);
	{
		struct sched_param sp = { .sched_priority = 1 };

		sched_setscheduler(0, SCHED_FIFO, &sp);
	}
	mxl_tune(mhz);

	uint8_t owner = riu[DMD_OWNER];

	riu[DMD_OWNER] = 0;
	tlast = tbin = cc();
	for (uint32_t b = 0; b < nb; b++) {
		int32_t si = 0, sq = 0;
		int n = 0;

		for (;;) {
			uint32_t t = cc();
			int v;
			uint32_t dt;

			if (t - tbin >= (uint32_t)bin)
				break;
			v = (*sig & 0xff) - 128;
			dt = t - tlast;
			tlast = t;
			if (dt > 100)
				gaps++;
			ph += dt * inc;
			si += v * c8[ph >> 24];
			sq += v * s8[ph >> 24];
			n++;
		}
		tbin += bin;
		samples += n;
		/* a bin inside an interrupt gap stays 0 */
		if (n) {
			iq[2 * b] = (int16_t)((si / n) >> 2);
			iq[2 * b + 1] = (int16_t)((sq / n) >> 2);
		}
	}
	riu[DMD_OWNER] = owner;

	printf("IF %.3f MHz, %u bins, %.2f s, %.3f MS/s mean, %u gaps >0.36 us\n", ifhz / 1e6, nb,
	       (double)nb * bin / COUNT_HZ, samples / ((double)nb * bin / COUNT_HZ) / 1e6, gaps);
	hdr[0] = bin;
	hdr[1] = nb;
	hdr[2] = samples;
	f = fopen(argv[4], "wb");
	fwrite(hdr, 4, 3, f);
	if (i8out) {
		/* scale by the RMS so noise uses a good part of s8 */
		double p = 0;

		for (uint32_t i = 0; i < 2 * nb; i += 16)
			p += (double)iq[i] * iq[i];
		p = sqrt(p / (nb / 8.0 + 1));
		for (uint32_t i = 0; i < 2 * nb; i++) {
			int v = (int)(iq[i] * 40.0 / (p + 1));
			int8_t o = v > 127 ? 127 : v < -127 ? -127 : v;

			fwrite(&o, 1, 1, f);
		}
	} else {
		fwrite(iq, 4, nb, f);
	}
	fclose(f);
	return 0;
}
