/*
 * specan - live FFT spectrum analyzer on the WIWA HD95 (MSD7818 + MxL5007T).
 *
 * Sweeps the tuner, samples the raw ADC I word (demod reg 0x2098 = RIU 0x112098)
 * from MIPS at ~9.09 MS/s as dmdts.c does, and draws a trace plus a waterfall to
 * /dev/fb0. No FPU on this CPU, so the signal path is integer only.
 *
 * The sample rate folds the IF band (0.57-8.57 MHz) around fs/2 = 4.54 MHz, and
 * IF 4.57 MHz is the tuning point, so a bin shows RF F-x and F+x at once. Every RF
 * point is therefore measured from several tunings and a low reading wins: the mirror
 * image of a signal lands elsewhere for each tuning, while a real signal stays put.
 * The second smallest is used from 4 readings up, so that one bad tuning (219 MHz has
 * its LO 28 kHz off) does not erase a signal. The tuner's LO leak at a fixed IF goes
 * away the same way. The IF filter shape is measured at startup from the median over
 * many tunings and divided out. Spurs: docs/falszywe_sygnaly.md.
 *
 * Needs MBoot `tuner_init` before Linux starts (ADC + demod 8051 running) and the
 * i2c-mstar-miic driver. See docs/analizator_widma.md.
 *
 *   specan [-n FFT] [-a AVG] [-t STEP_MHZ] [-s SETTLE_MS] [-v] [START_MHZ STOP_MHZ]
 *   specan -S [-a AVG] START_MHZ STOP_MHZ   list every peak on stdout, no screen
 *   specan -S -D -M ...   also dump the whole trace (D WINDOW v v v ... in dB*16), -M mean per column
 *   specan -R [-a AVG] START_MHZ STOP_MHZ   raw peaks of each single tuning, 1 MHz steps
 *   -C turns the tuner clock output off (for tracing spurs)
 *   -W REG=VAL[,REG=VAL...] tuner register writes after its init, hex (for experiments)
 *   defaults: -n 4096 -a 2 -s 20, 88 108
 *
 * FM radio on HDMI, live: click a station with a USB mouse, or move the marker with the
 * left/right arrows and press Enter. VPE1 samples and mixes to 120 kS/s I/Q, VPE0 demodulates
 * and feeds our own SE DSP program (own_audio.h). The sweep stops while listening.
 *
 * Mouse: left click tunes, wheel moves the marker, right click stops the radio.
 * Keys (serial console, USB keyboard): left/right marker -/+100 kHz, Enter radio on/off,
 *   r new sweep, a/d move the view, up/down or w/s zoom, 1-4 presets (FM, VHF, UHF, all),
 *   h max hold, m max/mean per column, +/- reference level (volume while listening),
 *   , . tuner gain, [ ] ADC gain (neither improves the signal to noise ratio over the default;
 *   they can only avoid clipping),
 *   b tuner filter while listening (8, 7, 6 MHz), p pause, q quit.
 *   Front panel: CH+/CH- marker, POWER radio on/off.
 *
 *   -F RF_KHZ starts the radio at once, -B MHZ tuner filter while listening,
 *   -O MHZ how far above the station to tune (default 2)
 *
 * Build (own_audio.h, own_pcm_prog.h next to it):
 *   mipsel-buildroot-linux-musl-gcc -O2 -static -pthread -o specan specan.c -lm
 * PC test with a simulated tuner and ADC (writes a PPM):
 *   gcc -DHOST -O2 -o specan_host specan.c -lm && ./specan_host -x 3 88 108
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef HOST
#define OWN_LEAD	1800		/* 37 ms of the DSP ring: screen drawing must not click */
#include "own_audio.h"
#endif

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define DMD_PHYS	0x1F200000UL
#define DMD_SIZE	0x40000UL
#define DMD_OWNER	0x03C71		/* RIU 0x101E39 */
#define SIG_2098	0x24130		/* RIU 0x112098 */
#define TUNER_ADDR	0x60
#define COUNT_HZ	276000000	/* CP0 Count = 552 MHz / 2 */

#define FIF_HZ		4595300		/* nominal 4.57; set so the tuner-locked 6 MHz comb reads exact */
#define DELTA_MIN	120000		/* RF point to tuning: skip the fold and 4.495 MHz spur */
#define DELTA_MAX	3200000		/* inside the 8 MHz IF filter */
#define RF_MIN_MHZ	44
#define RF_MAX_MHZ	885

#define COLS		1800		/* plot width in pixels */
#define PLOT_X		70
#define PLOT_Y		44
#define PLOT_H		620
#define WF_Y		700
#define WF_H		340
#define DB_UNIT		16		/* internal dB values are dB * 16 */
#define DB_SPAN		60		/* plot height in dB */
#define WF_REP		4		/* waterfall lines per sweep */
#define NO_DATA		INT16_MAX

struct rec {
	uint32_t cnt;
	uint16_t w;
	uint16_t pad;
};

static int N = 4096, AVG = 2, settle_ms = 20, verbose, no_clkout;
static uint32_t fs_hz;
static uint64_t bin_k;			/* Hz -> FFT bin, Q32 */
static int gap_ticks;
static int32_t *z;
static int16_t *tw;
static uint16_t *brev, *win;
static uint64_t *pwr;
static int16_t *db, *ifgain;
static struct rec *cap;
static int cap_n;
static int db_full;			/* db16() of a full-scale sine summed over the blocks */
static int16_t db_tab[256];

static int start_hz, stop_hz, step_hz, cur_step, first_tune, last_tune, tune_hz;
static int col_hz[COLS + 1];
static int16_t acc[COLS], acc2[COLS], trace[COLS], hold[COLS];
static uint8_t nobs[COLS];
static int top_db = 0, mode_mean, hold_on, paused;
static uint16_t *wf;
static int wf_row, wf_dirty, sweep_n;
static double sweep_s, last_sweep_s;
static volatile sig_atomic_t quit;

#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- hardware, or a simulated tuner + ADC for testing on the PC ---- */

#ifndef HOST

static volatile uint8_t *dmd;
static volatile uint16_t *sig;
static int i2c;

static inline uint32_t cc(void)
{
	uint32_t v;

	__asm__ volatile(".set push\n.set mips32r2\nrdhwr %0, $2\n.set pop" : "=r"(v));
	return v;
}

static void ui_wait(double until);
static const char *tuner_writes;
static int bw_reg = 0x3f;		/* tuner filter: 0x15, 0x2a, 0x3f = 6, 7, 8 MHz */

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

/* DVB-T, IF 4.57 MHz, 24 MHz xtal, clock out on (as rfscan.c, docs/mxl5007t.md) */
static void mxl_init(void)
{
	static const uint8_t tab[][2] = {
		{ 0x02, 0x03 }, { 0x03, 0x48 }, { 0x05, 0x04 }, { 0x06, 0x11 },
		{ 0x2e, 0x15 }, { 0x30, 0x10 }, { 0x45, 0x58 }, { 0x48, 0x19 },
		{ 0x52, 0x03 }, { 0x53, 0x44 }, { 0x6a, 0x4b }, { 0x76, 0x00 },
		{ 0x78, 0x18 }, { 0x7a, 0x17 }, { 0x85, 0x06 },
	};

	/* soft reset, then the table; experiments go in before the chip is enabled */
	if (tuner_writes) {
		uint8_t rst = 0xff;
		struct i2c_msg msg = { .addr = TUNER_ADDR, .flags = 0, .len = 1, .buf = &rst };
		struct i2c_rdwr_ioctl_data d = { .msgs = &msg, .nmsgs = 1 };

		if (ioctl(i2c, I2C_RDWR, &d) < 0)
			perror("i2c reset");
		usleep(10000);
	}
	for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		mxl_w(tab[i][0], tab[i][1]);
	if (no_clkout)
		mxl_w(0x03, 0x40);
	for (const char *p = tuner_writes; p && *p;) {
		unsigned r, v;
		int used;

		if (sscanf(p, "%x=%x%n", &r, &v, &used) != 2)
			break;
		mxl_w(r, v);
		printf("tuner reg %02x: wrote %02x, reads %02x\n", r, v, mxl_r(r));
		p += used + (p[used] == ',');
	}
	mxl_w(0x01, 0x01);	/* TOP_MASTER_ENABLE */
}

/* All tuner registers, to look for what changes with the signal */
static void mxl_dump(void)
{
	for (int r = 0; r < 256; r++)
		printf("%s%02x%s", r % 16 ? " " : "MXL ", mxl_r(r), r % 16 == 15 ? "\n" : "");
}

static void tune(int hz)
{
	int n = hz / 15625;	/* 1/64 MHz; callers tune on whole MHz, so this is exact */
	struct timespec ms = { 0, 1000000L };

	mxl_w(0x0f, 0x00);
	mxl_w(0x0c, bw_reg);
	mxl_w(0x0d, n & 0xff);
	mxl_w(0x0e, n >> 8);
	mxl_w(0x1f, 0x87);
	mxl_w(0x20, 0x1f);
	mxl_w(0x21, 0x87);
	mxl_w(0x22, 0x1f);
	mxl_w(0x80, hz >= 333000000 ? 0x41 : 0x01);
	mxl_w(0x0f, 0x01);
	for (int t = 0; t < 100 && mxl_r(0xd8) != 0x0f; t++)
		nanosleep(&ms, NULL);	/* RF + ref PLL lock */
	ui_wait(now() + settle_ms * 1e-3);
}

static void capture(void)
{
	uint8_t owner = dmd[DMD_OWNER];

	dmd[DMD_OWNER] = 0;
	for (int i = 0; i < cap_n; i++) {
		cap[i].cnt = cc();
		cap[i].w = *sig;
	}
	dmd[DMD_OWNER] = owner;
}

static void hw_init(void)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	if (fd < 0) {
		perror("/dev/mem");
		exit(1);
	}
	dmd = mmap(NULL, DMD_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DMD_PHYS);
	if (dmd == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	sig = (volatile uint16_t *)(dmd + SIG_2098);
	i2c = open("/dev/i2c-0", O_RDWR);
	if (i2c < 0) {
		perror("/dev/i2c-0");
		exit(1);
	}
	if (mxl_r(0xd9) != 0x14)
		fprintf(stderr, "warning: tuner id 0x%02x, expected 0x14\n", mxl_r(0xd9));
	mxl_init();
}

#else /* HOST */

static const struct { int hz, amp; } sim_tones[] = {
	{ 96030000, 40 }, { 99100000, 12 }, { 101500000, 25 }, { 106800000, 6 }, { 474000000, 0 },
};
static int sim_tune;

static double sim_filter(double delta)
{
	double d = fabs(delta) / 4e6;

	return 1 / sqrt(1 + pow(d, 12));	/* 8 MHz wide IF filter, Butterworth-ish */
}

static void tune(int hz)
{
	sim_tune = hz;
}

static double gauss(void)
{
	double a = (rand() + 1.0) / (RAND_MAX + 2.0), b = rand() / (RAND_MAX + 1.0);

	return sqrt(-2 * log(a)) * cos(2 * M_PI * b);
}

static void capture(void)
{
	static uint32_t c;
	static double *x;
	int ntones = sizeof(sim_tones) / sizeof(sim_tones[0]);
	double step = 30.375 / 276000000;	/* matches the ticks below */

	if (!x)
		x = malloc(cap_n * sizeof(*x));
	for (int i = 0; i < cap_n; i++)
		x[i] = gauss() * 2.5;
	for (int t = 0; t < ntones; t++) {
		double delta = sim_tones[t].hz - sim_tune;
		double ifr = delta + FIF_HZ, ph = rand() * 1e-3;
		double a = sim_tones[t].amp * sim_filter(delta);

		if (a < 0.01)
			continue;
		for (int i = 0; i < cap_n; i++)
			x[i] += a * sin(2 * M_PI * ifr * i * step + ph);
	}
	/* DVB-T mux at 474 MHz: flat 7.6 MHz wide */
	if (abs(474000000 - sim_tune) < 8000000) {
		for (int m = 0; m < 200; m++) {
			double f = 474000000 + (m - 100) * 38000.0;
			double delta = f - sim_tune, ifr = delta + FIF_HZ, ph = rand() * 1e-3;
			double a = 1.0 * sim_filter(delta);

			for (int i = 0; i < cap_n; i += 1)
				x[i] += a * sin(2 * M_PI * ifr * i * step + ph);
		}
	}
	/* ADC clips and rounds; a timer interrupt leaves one gap per capture */
	int gap_at = rand() % cap_n;

	for (int i = 0; i < cap_n; i++) {
		int v = (int)lrint(x[i]) + 128;

		if (i == gap_at)
			c += 400;
		cap[i].cnt = c;
		cap[i].w = v < 0 ? 0 : v > 255 ? 255 : v;
		c += 30 + (i % 8 < 3);
	}
}

static void hw_init(void)
{
}

#endif

/* ---- integer log and FFT ---- */

static inline int db16(uint64_t p)
{
	int l;

	if (!p)
		return 0;
	l = 63 - __builtin_clzll(p);
	return ((l * 12330) >> 8) + db_tab[((p << (63 - l)) >> 55) & 0xff];
}

static void dsp_init(void)
{
	int b = __builtin_ctz(N);

	z = malloc(N * 2 * sizeof(*z));
	tw = malloc(N * sizeof(*tw));
	brev = malloc(N * sizeof(*brev));
	win = malloc(N * sizeof(*win));
	pwr = malloc((N / 2 + 1) * sizeof(*pwr));
	db = malloc((N / 2 + 1) * sizeof(*db));
	ifgain = calloc(N / 2 + 1, sizeof(*ifgain));
	for (int i = 0; i < N / 2; i++) {
		tw[2 * i] = (int16_t)lrint(16384 * cos(2 * M_PI * i / N));
		tw[2 * i + 1] = (int16_t)lrint(-16384 * sin(2 * M_PI * i / N));
	}
	for (int i = 0; i < N; i++) {
		unsigned r = 0;

		for (int j = 0; j < b; j++)
			if (i & (1 << j))
				r |= 1u << (b - 1 - j);
		brev[i] = r;
		win[i] = (uint16_t)lrint(32767 * (0.5 - 0.5 * cos(2 * M_PI * i / N)));
	}
	for (int i = 0; i < 256; i++)
		db_tab[i] = (int16_t)lrint(DB_UNIT * 10 * log10(1 + i / 256.0));

	/* full-scale sine (peak 2^15 after windowing): X = 2^15 * (N/2) / 2 in every one
	 * of the 2*AVG real blocks, see fft_block() for the sum */
	double x = 32768.0 * N / 4;

	db_full = (int)lrint(DB_UNIT * 10 * log10(x * x * 2 * 2 * AVG));
}

static void fft(void)
{
	int32_t *zz = z;

	for (int i = 0; i < N; i++) {
		int j = brev[i];

		if (j > i) {
			int32_t a = zz[2 * i], b = zz[2 * i + 1];

			zz[2 * i] = zz[2 * j];
			zz[2 * i + 1] = zz[2 * j + 1];
			zz[2 * j] = a;
			zz[2 * j + 1] = b;
		}
	}
	for (int half = 1, st = N / 2; half < N; half *= 2, st /= 2) {
		for (int i = 0; i < N; i += 2 * half) {
			for (int j = 0, t = 0; j < half; j++, t += st) {
				int32_t *a = zz + 2 * (i + j), *b = a + 2 * half;
				int32_t wr = tw[2 * t], wi = tw[2 * t + 1];
				int32_t tr = (int32_t)(((int64_t)wr * b[0] - (int64_t)wi * b[1]) >> 14);
				int32_t ti = (int32_t)(((int64_t)wr * b[1] + (int64_t)wi * b[0]) >> 14);

				b[0] = a[0] - tr;
				b[1] = a[1] - ti;
				a[0] += tr;
				a[1] += ti;
			}
		}
	}
}

/*
 * Two real blocks go through one complex FFT as x1 + j*x2. For the separated
 * spectra |X1(k)|^2 + |X2(k)|^2 = (|Z(k)|^2 + |Z(N-k)|^2) / 2, so the power of both
 * comes straight from Z without unpacking them. db_full above includes the 2.
 */
static void fft_block(const struct rec *a, const struct rec *b)
{
	for (int i = 0; i < N; i++) {
		z[2 * i] = (((a[i].w & 0xff) - 128) * (int32_t)win[i]) >> 7;
		z[2 * i + 1] = (((b[i].w & 0xff) - 128) * (int32_t)win[i]) >> 7;
	}
	fft();
	for (int k = 1; k < N / 2; k++) {
		int32_t r0 = z[2 * k] >> 4, i0 = z[2 * k + 1] >> 4;
		int32_t r1 = z[2 * (N - k)] >> 4, i1 = z[2 * (N - k) + 1] >> 4;

		pwr[k] += (uint64_t)((int64_t)r0 * r0 + (int64_t)i0 * i0 +
				     (int64_t)r1 * r1 + (int64_t)i1 * i1) << 8;
	}
}

/*
 * CP0 Count runs at exactly CPU/2 and Linux time comes from it, so timing it against the
 * clock would only add noise. The mean sample period differs by 0.1 % between runs and
 * tunings (9.087 to 9.093 MS/s), which is 3 kHz at 3 MHz IF: it is set for every measurement.
 */
static void set_fs(uint64_t samples, uint64_t ticks)
{
	fs_hz = (uint32_t)((double)COUNT_HZ * samples / ticks + 0.5);
	bin_k = (uint64_t)((double)N * 4294967296.0 / fs_hz + 0.5);
}

/* Average AVG FFTs over gap-free stretches of the capture, result in db[] (dB * 16, dBFS) */
static void measure(void)
{
	const struct rec *pending = NULL;
	int ffts = 0;
	uint64_t ticks = 0, samples = 0;

	memset(pwr, 0, (N / 2 + 1) * sizeof(*pwr));
	for (int tries = 0; ffts < AVG && tries < 16; tries++) {
		int i = 0;

		capture();
		pending = NULL;
		while (ffts < AVG && i + N <= cap_n) {
			int bad = -1;

			for (int j = i + 1; j < i + N; j++)
				if (cap[j].cnt - cap[j - 1].cnt > (uint32_t)gap_ticks)
					bad = j;
			if (bad >= 0) {
				i = bad;
				continue;
			}
			ticks += cap[i + N - 1].cnt - cap[i].cnt;
			samples += N - 1;
			if (pending) {
				fft_block(pending, cap + i);
				ffts++;
				pending = NULL;
			} else {
				pending = cap + i;
			}
			i += N;
		}
	}
	if (ffts < AVG)
		fprintf(stderr, "specan: only %d FFTs: too many gaps\n", ffts);
	if (ticks)
		set_fs(samples, ticks);
	for (int k = 1; k < N / 2; k++)
		db[k] = (int16_t)(db16(pwr[k]) - db_full);
	db[0] = db[1];
	db[N / 2] = db[N / 2 - 1];
}

static int cmp16(const void *a, const void *b)
{
	return *(const int16_t *)a - *(const int16_t *)b;
}

/* Sample rate from the Count timestamps; the gap limit is 1.5 nominal periods */
static void rate_init(void)
{
	uint64_t sum = 0;
	int n = 0, med;
	int16_t *d = malloc(4000 * sizeof(*d));

	capture();
	for (int i = 1; i <= 4000; i++)
		d[i - 1] = (int16_t)(cap[i].cnt - cap[i - 1].cnt);
	qsort(d, 4000, sizeof(*d), cmp16);
	med = d[2000];
	free(d);
	gap_ticks = med + med / 2;
	for (int i = 1; i < cap_n; i++) {
		uint32_t dt = cap[i].cnt - cap[i - 1].cnt;

		if (dt <= (uint32_t)gap_ticks) {
			sum += dt;
			n++;
		}
	}
	set_fs(n, sum);
	printf("%d ticks per sample, fs %.3f MS/s, bin %.2f kHz\n", med, fs_hz / 1e6, fs_hz / 1e3 / N);

	/* a stopped ADC or an unowned bus reads constant: no tuner_init was run */
	int lo = 255, hi = 0;

	for (int i = 0; i < cap_n; i++) {
		int v = cap[i].w & 0xff;

		lo = v < lo ? v : lo;
		hi = v > hi ? v : hi;
	}
	printf("ADC range %d..%d\n", lo, hi);
	if (hi - lo < 2) {
		fprintf(stderr, "ADC is not running: start MBoot `tuner_init` before Linux\n");
		exit(1);
	}
}

/*
 * IF filter shape: median of each bin over tunings spread across the band, so signals
 * drop out, then smoothed so narrow spurs stay for the min across tunings to remove.
 * Saved as dB * 16 relative to the passband top, and subtracted from every reading.
 */
static void ifgain_init(void)
{
	enum { TUNES = 24 };
	int nb = N / 2 + 1, sm = N / 64;
	int16_t *all = malloc(nb * TUNES * sizeof(*all));
	int16_t *med = malloc(nb * sizeof(*med)), tmp[TUNES];
	int best;

	for (int t = 0; t < TUNES; t++) {
		tune((50 + t * (860 - 50) / (TUNES - 1)) * 1000000);
		measure();
		memcpy(all + t * nb, db, nb * sizeof(*db));
	}
	for (int k = 0; k < nb; k++) {
		for (int t = 0; t < TUNES; t++)
			tmp[t] = all[t * nb + k];
		qsort(tmp, TUNES, sizeof(tmp[0]), cmp16);
		med[k] = tmp[TUNES / 2];
	}
	for (int k = 0; k < nb; k++) {
		int a = k - sm < 0 ? 0 : k - sm, b = k + sm >= nb ? nb - 1 : k + sm, s = 0;

		for (int j = a; j <= b; j++)
			s += med[j];
		ifgain[k] = (int16_t)(s / (b - a + 1));
	}
	/* spurs at a fixed IF: in the median over all tunings, well above its smoothed curve */
	if (verbose)
		for (int k = 2; k < nb - 2; k++)
			if (med[k] > ifgain[k] + 6 * DB_UNIT && med[k] >= med[k - 1] && med[k] >= med[k + 1])
				printf("IFSPUR %.3f MHz +%.0f dB\n", (double)k * fs_hz / N / 1e6,
				       (med[k] - ifgain[k]) / (double)DB_UNIT);
	/* 0 dB = median of 1.5-4 MHz; the max would follow what is left of a spur */
	{
		int ka = (int)((1500000ULL * bin_k) >> 32), kb = (int)((4000000ULL * bin_k) >> 32);

		memcpy(med, ifgain + ka, (kb - ka) * sizeof(*med));
		qsort(med, kb - ka, sizeof(*med), cmp16);
		best = med[(kb - ka) / 2];
	}
	for (int k = 0; k < nb; k++)
		ifgain[k] -= best;
	printf("IF gain dB (MHz:dB):");
	for (int mhz10 = 2; mhz10 <= 45; mhz10 += 4) {
		int k = (int)((mhz10 * 100000ULL * bin_k) >> 32);

		printf(" %.1f:%.1f", mhz10 / 10.0, ifgain[k] / (double)DB_UNIT);
	}
	printf("\nnoise in the passband %.1f dBFS per bin\n", best / (double)DB_UNIT);
	free(all);
	free(med);
}

/* ---- sweep ---- */

static void set_range(int mhz_a, int mhz_b)
{
	int span;

	if (mhz_a < RF_MIN_MHZ)
		mhz_a = RF_MIN_MHZ;
	if (mhz_b > RF_MAX_MHZ)
		mhz_b = RF_MAX_MHZ;
	if (mhz_b < mhz_a + 2)
		mhz_b = mhz_a + 2;
	start_hz = mhz_a * 1000000;
	stop_hz = mhz_b * 1000000;
	span = mhz_b - mhz_a;
	cur_step = step_hz ? step_hz : (span <= 30 ? 1000000 : span <= 150 ? 2000000 : 3000000);
	int st = cur_step;

	first_tune = ((start_hz - DELTA_MAX) / st) * st;
	last_tune = ((stop_hz + DELTA_MAX + st - 1) / st) * st;
	if (first_tune < RF_MIN_MHZ * 1000000)
		first_tune = RF_MIN_MHZ * 1000000;
	if (last_tune > RF_MAX_MHZ * 1000000)
		last_tune = RF_MAX_MHZ * 1000000;
	tune_hz = first_tune;
	for (int c = 0; c <= COLS; c++)
		col_hz[c] = start_hz + (int)((int64_t)(stop_hz - start_hz) * c / COLS);
	for (int c = 0; c < COLS; c++) {
		acc[c] = acc2[c] = NO_DATA;
		nobs[c] = 0;
		trace[c] = -120 * DB_UNIT;
		hold[c] = -120 * DB_UNIT;
	}
	if (wf)
		memset(wf, 0, COLS * WF_H * sizeof(*wf));
	wf_dirty = 2;
	sweep_s = now();
}

static inline int if_bin(int delta)
{
	int fi = delta + FIF_HZ;

	if (fi > (int)fs_hz / 2)
		fi = (int)fs_hz - fi;
	if (fi < 0)
		fi = 0;
	return (int)(((uint64_t)fi * bin_k) >> 32);
}

/* The current tuning reads each column it can reach; keep the two smallest readings */
static void combine(int f)
{
	for (int c = 0; c < COLS; c++) {
		int mid = (col_hz[c] + col_hz[c + 1]) / 2, delta = mid - f;
		int ad = delta < 0 ? -delta : delta;

		if (ad < DELTA_MIN || ad > DELTA_MAX)
			continue;
		int a = if_bin(col_hz[c] - f), b = if_bin(col_hz[c + 1] - f), v;

		if (a > b) {
			int t = a;

			a = b;
			b = t;
		}
		if (b > N / 2)
			b = N / 2;
		if (mode_mean) {
			int s = 0;

			for (int k = a; k <= b; k++)
				s += db[k] - ifgain[k];
			v = s / (b - a + 1);
		} else {
			/* +-1 bin: a small fs error moves the image-side reading of a narrow
			 * carrier, and the min across tunings would then erase it */
			a = a > 0 ? a - 1 : 0;
			b = b < N / 2 ? b + 1 : b;
			v = INT_MIN;
			for (int k = a; k <= b; k++)
				if (db[k] - ifgain[k] > v)
					v = db[k] - ifgain[k];
		}
		if (v < acc[c]) {
			acc2[c] = acc[c];
			acc[c] = (int16_t)v;
		} else if (v < acc2[c]) {
			acc2[c] = (int16_t)v;
		}
		nobs[c]++;
	}
}

/* Columns no later tuning can reach are final */
static void commit_upto(int hz)
{
	for (int c = 0; c < COLS && col_hz[c + 1] < hz; c++) {
		if (acc[c] == NO_DATA)
			continue;
		trace[c] = nobs[c] >= 4 ? acc2[c] : acc[c];
		acc[c] = acc2[c] = NO_DATA;
		nobs[c] = 0;
		if (trace[c] > hold[c])
			hold[c] = trace[c];
	}
}

/* ---- ADC gain ---- */

#define ADC_GAIN	0x2502C		/* RIU 0x112816: 4 bits per channel, smaller = more gain */
static int adc_gain = -1;

static void adc_gain_set(int g)
{
#ifndef HOST
	static int orig = -1;
	volatile uint16_t *r = (volatile uint16_t *)(dmd + ADC_GAIN);

	if (orig < 0)
		orig = *r;
	if (g < 0) {		/* put the original value back */
		*r = (uint16_t)orig;
		return;
	}
	*r = (uint16_t)(g | g << 4 << 4);
#endif
	adc_gain = g;
}

/* Tuner register 0x78 (undocumented, found by scanning): about 18 dB, default 0x18 */
static const uint8_t tuner_gains[] = { 0x00, 0x08, 0x10, 0x14, 0x18, 0x1a };
static int tuner_gain = 4;

static void tuner_gain_step(int d)
{
	int g = tuner_gain + d;

	if (g < 0 || g >= (int)sizeof(tuner_gains))
		return;
	tuner_gain = g;
#ifndef HOST
	mxl_w(0x78, tuner_gains[g]);
#endif
	if (verbose)
		printf("tuner gain reg 0x78 = 0x%02x\n", tuner_gains[g]);
}

/* ---- radio ---- */

#define IQ_BIN		2300		/* Count ticks per I/Q pair: 120 kS/s */
#define IQ_RATE		120000
#define IQ_RING		(1u << 18)	/* I/Q pairs, 2.2 s */
#define IQ_EMPTY	INT16_MIN	/* marks a bin inside an interrupt gap */
#define AUDIO_BLOCK	256		/* frames */

static int marker_hz = 101000000, marker_set, radio_want, radio_on, rescan, volume = 4;
static int radio_bw = 8, radio_off_mhz = 2;
static int radio_pending;
static double radio_change_t, pilot_db, rx_db;

#ifndef HOST
static int16_t *iq_ring;
static volatile uint32_t iq_w, iq_r;
static volatile int smp_run, smp_idle = 1, smp_quit, audio_on, dem_reset, dem_quit;
static volatile uint32_t nco_inc, st_samples, st_empty, st_run, st_maxrun;
static pthread_t smp_thread, dem_thread;

static void pin_rt(int cpu, int prio)
{
	cpu_set_t set;
	struct sched_param sp = { .sched_priority = prio };

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);
	sched_setscheduler(0, SCHED_FIFO, &sp);
}

/*
 * Mixes every sample with an NCO stepped by the sample's own timestamp, so gaps keep the
 * phase, and averages over fixed time bins (fmrx.c). Runs on VPE1 and does nothing else.
 */
static void *sampler(void *arg)
{
	static int16_t c8[256], s8[256];

	(void)arg;
	pin_rt(1, 3);
	for (int i = 0; i < 256; i++) {
		c8[i] = (int16_t)lrint(4095 * cos(2 * M_PI * i / 256));
		s8[i] = (int16_t)lrint(-4095 * sin(2 * M_PI * i / 256));
	}
	while (!smp_quit) {
		uint8_t owner;
		uint32_t ph = 0, tlast, tbin;
		int sleep_bins = 0;

		if (!smp_run) {
			smp_idle = 1;
			usleep(1000);
			continue;
		}
		smp_idle = 0;
		owner = dmd[DMD_OWNER];
		dmd[DMD_OWNER] = 0;
		tlast = tbin = cc();
		while (smp_run) {
			int32_t si = 0, sq = 0;
			int n = 0;
			uint32_t inc = nco_inc, w;

			for (;;) {
				uint32_t t = cc();
				int v;

				if (t - tbin >= IQ_BIN)
					break;
				v = (*sig & 0xff) - 128;
				ph += (t - tlast) * inc;
				tlast = t;
				si += v * c8[ph >> 24];
				sq += v * s8[ph >> 24];
				n++;
			}
			tbin += IQ_BIN;
			/* the real-time limit is off (radio_init): let VPE1's kernel threads run sometimes */
			if (++sleep_bins == IQ_RATE / 4) {
				/* no sleep: with a 250 Hz tick the shortest one costs 4 ms */
				struct sched_param none = { 0 }, rt = { .sched_priority = 3 };

				sleep_bins = 0;
				sched_setscheduler(0, SCHED_OTHER, &none);
				sched_yield();
				sched_setscheduler(0, SCHED_FIFO, &rt);
			}
			w = iq_w;
			st_samples += n;
			if (n) {
				st_run = 0;
			} else {
				st_empty++;
				if (++st_run > st_maxrun)
					st_maxrun = st_run;
			}
			if (n) {
				iq_ring[2 * (w & (IQ_RING - 1))] = (int16_t)((si / n) >> 4);
				iq_ring[2 * (w & (IQ_RING - 1)) + 1] = (int16_t)((sq / n) >> 4);
			} else {
				iq_ring[2 * (w & (IQ_RING - 1))] = IQ_EMPTY;
			}
			iq_w = w + 1;
		}
		dmd[DMD_OWNER] = owner;
	}
	smp_idle = 1;
	return NULL;
}

/* ---- demodulator, integer only ---- */

#define FIR_N 21
#define GAP_MASK (FIR_N + 3)		/* bins to hold the audio after a gap */

static int32_t fir_c[FIR_N];
static int16_t atan_tab[258];
struct biquad { int32_t b0, b1, b2, a1, a2, x1, x2, y1, y2; };
static struct biquad lp[2];
static int32_t de_a, hp_a;

static void lp_design(struct biquad *q, double fc, double Q)
{
	double w = 2 * M_PI * fc / IQ_RATE, al = sin(w) / (2 * Q), a0 = 1 + al;

	q->b0 = q->b2 = (int32_t)lrint((1 - cos(w)) / 2 / a0 * 16384);
	q->b1 = (int32_t)lrint((1 - cos(w)) / a0 * 16384);
	q->a1 = (int32_t)lrint(-2 * cos(w) / a0 * 16384);
	q->a2 = (int32_t)lrint((1 - al) / a0 * 16384);
}

static void dem_init(void)
{
	double sum = 0, h[FIR_N];

	for (int i = 0; i < FIR_N; i++) {	/* 45 kHz low-pass: a weak station is about +-40 kHz */
		double t = i - (FIR_N - 1) / 2.0, fc = 45000.0 / IQ_RATE;

		h[i] = (t == 0 ? 2 * fc : sin(2 * M_PI * fc * t) / (M_PI * t)) *
		       (0.54 + 0.46 * cos(2 * M_PI * t / FIR_N));
		sum += h[i];
	}
	for (int i = 0; i < FIR_N; i++)
		fir_c[i] = (int32_t)lrint(h[i] / sum * 16384);
	for (int i = 0; i <= 257; i++)
		atan_tab[i] = (int16_t)lrint(atan(i / 256.0) * 65536 / (2 * M_PI));
	lp_design(&lp[0], 7000, 0.541);
	lp_design(&lp[1], 7000, 1.307);
	de_a = (int32_t)lrint((1 - exp(-1 / (IQ_RATE * 50e-6))) * 32768);	/* 50 us */
	hp_a = (int32_t)lrint((1 - exp(-2 * M_PI * 80.0 / IQ_RATE)) * 32768);
}

/* 65536 = full circle */
static inline int fast_atan2(int y, int x)
{
	int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a, m = ax > ay ? ax : ay;

	if (!m)
		return 0;
	while (m >= (1 << 18)) {
		ax >>= 4;
		ay >>= 4;
		m >>= 4;
	}
	if (!ax && !ay)
		return 0;
	if (ay <= ax) {
		int r = (ay << 12) / ax;

		a = atan_tab[r >> 4] + (((atan_tab[(r >> 4) + 1] - atan_tab[r >> 4]) * (r & 15)) >> 4);
	} else {
		int r = (ax << 12) / ay;

		a = 16384 - atan_tab[r >> 4] - (((atan_tab[(r >> 4) + 1] - atan_tab[r >> 4]) * (r & 15)) >> 4);
	}
	if (x < 0)
		a = 32768 - a;
	return y < 0 ? -a : a;
}

static inline int biquad(struct biquad *q, int x)
{
	int y = (int)(((int64_t)q->b0 * x + (int64_t)q->b1 * q->x1 + (int64_t)q->b2 * q->x2 -
		       (int64_t)q->a1 * q->y1 - (int64_t)q->a2 * q->y2) >> 14);

	q->x2 = q->x1;
	q->x1 = x;
	q->y2 = q->y1;
	q->y1 = y;
	return y;
}

static void *demod(void *arg)
{
	static int16_t out[AUDIO_BLOCK * 2], zero[AUDIO_BLOCK * 2];
	static int16_t bi[FIR_N], bq[FIR_N], c120[120];
	int fpos = 0, blank = 0, nout = 0, phase5 = 0, pi = 0, pq = 0, lasti = 0, lastq = 0;
	int prev_y = 0;
	int32_t de = 0, hp = 0;
	int p19 = 0, p17 = 0;
	int64_t ar19 = 0, ai19 = 0, ar17 = 0, ai17 = 0, lev = 0;
	int stat_n = 0, silent = 0, prebuf = 1;

	(void)arg;
	pin_rt(0, 2);
	for (int i = 0; i < 120; i++)
		c120[i] = (int16_t)lrint(1023 * cos(2 * M_PI * i / 120));
	while (!dem_quit) {
		uint32_t w, r;

		if (!audio_on) {
			/* the DSP loops over its ring: zero it once, then keep off the bus for the sweep */
			if (!silent) {
				for (int i = 0; i < OWN_FRAMES / AUDIO_BLOCK + 1; i++)
					maudio_write(zero, AUDIO_BLOCK);
				silent = 1;
			}
			usleep(20000);
			continue;
		}
		silent = 0;
		if (dem_reset) {
			dem_reset = 0;
			prebuf = 1;
			memset(bi, 0, sizeof(bi));
			memset(bq, 0, sizeof(bq));
			pi = pq = de = hp = nout = phase5 = blank = stat_n = 0;
			ar19 = ai19 = ar17 = ai17 = lev = 0;
			for (int i = 0; i < 2; i++)
				lp[i].x1 = lp[i].x2 = lp[i].y1 = lp[i].y2 = 0;
		}
		w = iq_w;
		r = iq_r;
		if (w - r > IQ_RING - 4096)
			r = w - IQ_RING / 2;	/* fell far behind: drop */
		/* start with some margin: writing exactly in step with the DSP would underrun */
		if (w == r || (prebuf && w - r < IQ_RATE / 16)) {
			usleep(1000);
			continue;
		}
		prebuf = 0;
		while (r != w && !dem_reset && audio_on) {
			int i = iq_ring[2 * (r & (IQ_RING - 1))], q = iq_ring[2 * (r & (IQ_RING - 1)) + 1];

			r++;
			if (i == IQ_EMPTY) {
				i = lasti;
				q = lastq;
				blank = GAP_MASK;
			} else {
				lasti = i;
				lastq = q;
			}
			bi[fpos] = i;
			bq[fpos] = q;
			if (++fpos == FIR_N)
				fpos = 0;

			int32_t fi = 0, fq = 0;

			for (int k = 0, j = fpos; k < FIR_N; k++) {
				fi += fir_c[k] * bi[j];
				fq += fir_c[k] * bq[j];
				if (++j == FIR_N)
					j = 0;
			}
			fi >>= 16;	/* FIR gain Q14, 2 more bits: products stay in 32 bits */
			fq >>= 16;
			lev += fi * fi + fq * fq;

			int d = fast_atan2(fq * pi - fi * pq, fi * pi + fq * pq);

			pi = fi;
			pq = fq;
			int held = blank > 0;

			if (blank)
				blank--;	/* the phase across a gap is not usable */
			if (held)
				d = 0;
			/* pilot test: the same power measure at 19 kHz and at an empty 17 kHz */
			ar19 += d * c120[p19];
			ai19 += d * c120[(p19 + 30) % 120];
			ar17 += d * c120[p17];
			ai17 += d * c120[(p17 + 30) % 120];
			p19 = (p19 + 19) % 120;
			p17 = (p17 + 17) % 120;
			if (++stat_n == IQ_RATE / 2) {
				double a = (double)ar19 * ar19 + (double)ai19 * ai19;
				double b = (double)ar17 * ar17 + (double)ai17 * ai17;

				pilot_db = 10 * log10((a + 1) / (b + 1));
				rx_db = 10 * log10((double)lev / stat_n + 1);
				if (verbose) {
					printf("radio %.2f MS/s, empty bins %.1f %%, longest gap %.0f us, backlog %u, pilot %+.0f dB, rx %.0f dB\n",
					       st_samples * 2.0 / 1e6, st_empty * 100.0 / (IQ_RATE / 2),
					       st_maxrun * 1e6 / IQ_RATE, iq_w - r, pilot_db, rx_db);
					fflush(stdout);
				}
				st_samples = st_empty = st_maxrun = 0;
				ar19 = ai19 = ar17 = ai17 = lev = 0;
				stat_n = 0;
			}
			/* 80 Hz high-pass, 50 us de-emphasis, 7 kHz low-pass, then 120 -> 48 kHz */
			int x = d * 16;

			if (!held) {	/* in a gap the filters coast on their own smoothed value */
				hp += (int32_t)(((int64_t)(x - hp) * hp_a) >> 15);
				x -= hp;
				de += (int32_t)(((int64_t)(x - de) * de_a) >> 15);
			}
			x = biquad(&lp[1], biquad(&lp[0], de));

			int o = 0, emit = 0;

			if (phase5 == 0) {
				o = x;
				emit = 1;
			} else if (phase5 == 3) {	/* halfway between two inputs */
				o = (x + prev_y) >> 1;
				emit = 1;
			}
			prev_y = x;
			if (++phase5 == 5)
				phase5 = 0;
			if (emit) {
				int v = (int)(((int64_t)o * volume) >> 6);

				v = v > 32000 ? 32000 : v < -32000 ? -32000 : v;
				out[2 * nout] = out[2 * nout + 1] = (int16_t)v;
				if (++nout == AUDIO_BLOCK) {
					iq_r = r;
					maudio_write(out, AUDIO_BLOCK);
					nout = 0;
				}
			}
		}
		iq_r = r;
	}
	return NULL;
}

static void radio_stop(void)
{
	audio_on = 0;
	smp_run = 0;
	while (!smp_idle)
		usleep(500);
	radio_on = 0;
	bw_reg = 0x3f;		/* the sweep calculates with the 8 MHz filter */
}

static void radio_start(void)
{
	/* nearest whole MHz about 2 MHz above the station: IF 2.1-3.1 MHz, clear of the edge of
	 * the 6 MHz filter (10 dB lost at IF 1.6 MHz; the 8 MHz filter does not care) */
	int mhz = (marker_hz + radio_off_mhz * 1000000 + 500000) / 1000000;
	double ifhz = (double)marker_hz - mhz * 1e6 + FIF_HZ;

	if (mhz > RF_MAX_MHZ)
		mhz = RF_MAX_MHZ;
	bw_reg = radio_bw == 6 ? 0x15 : radio_bw == 7 ? 0x2a : 0x3f;
	tune(mhz * 1000000);
	nco_inc = (uint32_t)(ifhz / COUNT_HZ * 4294967296.0);
	iq_r = iq_w;
	dem_reset = 1;
	pilot_db = rx_db = 0;
	smp_run = 1;
	audio_on = 1;
	radio_on = 1;
	if (verbose)
		printf("radio %.3f MHz, tune %d MHz, IF %.3f MHz, filter %d MHz\n", marker_hz / 1e6, mhz, ifhz / 1e6, radio_bw);
}

/*
 * With the default sched_rt_runtime_us the kernel stops a 100 % real-time thread for
 * 50 ms every second, which is a click in the audio. Returns the old value.
 */
static int rt_limit(int val)
{
	char b[32];
	int old = 950000, fd = open("/proc/sys/kernel/sched_rt_runtime_us", O_RDWR), n;

	if (fd < 0)
		return old;
	n = read(fd, b, sizeof(b) - 1);
	if (n > 0) {
		b[n] = 0;
		old = atoi(b);
	}
	n = snprintf(b, sizeof(b), "%d\n", val);
	lseek(fd, 0, SEEK_SET);
	if (write(fd, b, n) != n)
		perror("sched_rt_runtime_us");
	close(fd);
	return old;
}

static int rt_old;

static void radio_init(void)
{
	cpu_set_t set;

	rt_old = rt_limit(-1);

	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(0, sizeof(set), &set);	/* VPE1 is the sampler's alone */
	iq_ring = malloc(IQ_RING * 4);
	if (!iq_ring || !maudio_open(48000, 2)) {
		fprintf(stderr, "radio: no memory or no audio\n");
		exit(1);
	}
	memset(iq_ring, 0, IQ_RING * 4);
	dem_init();
	pthread_create(&smp_thread, NULL, sampler, NULL);
	pthread_create(&dem_thread, NULL, demod, NULL);
}

static void radio_exit(void)
{
	radio_stop();
	smp_quit = 1;
	pthread_join(smp_thread, NULL);
	dem_quit = 1;
	pthread_join(dem_thread, NULL);
	maudio_close();
	adc_gain_set(-1);
	rt_limit(rt_old);
}
#else
static void radio_stop(void) { radio_on = 0; }
static void radio_start(void) { radio_on = 1; }
static void radio_init(void) { }
static void radio_exit(void) { }
#endif

/* ---- screen ---- */

static uint16_t *fbmem, *back;
static int W = 1920, H = 1080, shown;
#ifndef HOST
static int fbfd = -1;
static struct fb_var_screeninfo var;
#endif
static uint16_t wf_lut[256];

static const uint8_t font[64][5] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x5F, 0x00, 0x00 }, { 0x00, 0x07, 0x00, 0x07, 0x00 },
	{ 0x14, 0x7F, 0x14, 0x7F, 0x14 }, { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, { 0x23, 0x13, 0x08, 0x64, 0x62 },
	{ 0x36, 0x49, 0x55, 0x22, 0x50 }, { 0x00, 0x05, 0x03, 0x00, 0x00 }, { 0x00, 0x1C, 0x22, 0x41, 0x00 },
	{ 0x00, 0x41, 0x22, 0x1C, 0x00 }, { 0x14, 0x08, 0x3E, 0x08, 0x14 }, { 0x08, 0x08, 0x3E, 0x08, 0x08 },
	{ 0x00, 0x50, 0x30, 0x00, 0x00 }, { 0x08, 0x08, 0x08, 0x08, 0x08 }, { 0x00, 0x60, 0x60, 0x00, 0x00 },
	{ 0x20, 0x10, 0x08, 0x04, 0x02 }, { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 },
	{ 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 }, { 0x18, 0x14, 0x12, 0x7F, 0x10 },
	{ 0x27, 0x45, 0x45, 0x45, 0x39 }, { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
	{ 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E }, { 0x00, 0x36, 0x36, 0x00, 0x00 },
	{ 0x00, 0x56, 0x36, 0x00, 0x00 }, { 0x08, 0x14, 0x22, 0x41, 0x00 }, { 0x14, 0x14, 0x14, 0x14, 0x14 },
	{ 0x00, 0x41, 0x22, 0x14, 0x08 }, { 0x02, 0x01, 0x51, 0x09, 0x06 }, { 0x32, 0x49, 0x79, 0x41, 0x3E },
	{ 0x7E, 0x11, 0x11, 0x11, 0x7E }, { 0x7F, 0x49, 0x49, 0x49, 0x36 }, { 0x3E, 0x41, 0x41, 0x41, 0x22 },
	{ 0x7F, 0x41, 0x41, 0x22, 0x1C }, { 0x7F, 0x49, 0x49, 0x49, 0x41 }, { 0x7F, 0x09, 0x09, 0x09, 0x01 },
	{ 0x3E, 0x41, 0x49, 0x49, 0x7A }, { 0x7F, 0x08, 0x08, 0x08, 0x7F }, { 0x00, 0x41, 0x7F, 0x41, 0x00 },
	{ 0x20, 0x40, 0x41, 0x3F, 0x01 }, { 0x7F, 0x08, 0x14, 0x22, 0x41 }, { 0x7F, 0x40, 0x40, 0x40, 0x40 },
	{ 0x7F, 0x02, 0x0C, 0x02, 0x7F }, { 0x7F, 0x04, 0x08, 0x10, 0x7F }, { 0x3E, 0x41, 0x41, 0x41, 0x3E },
	{ 0x7F, 0x09, 0x09, 0x09, 0x06 }, { 0x3E, 0x41, 0x51, 0x21, 0x5E }, { 0x7F, 0x09, 0x19, 0x29, 0x46 },
	{ 0x46, 0x49, 0x49, 0x49, 0x31 }, { 0x01, 0x01, 0x7F, 0x01, 0x01 }, { 0x3F, 0x40, 0x40, 0x40, 0x3F },
	{ 0x1F, 0x20, 0x40, 0x20, 0x1F }, { 0x3F, 0x40, 0x38, 0x40, 0x3F }, { 0x63, 0x14, 0x08, 0x14, 0x63 },
	{ 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 }, { 0x00, 0x7F, 0x41, 0x41, 0x00 },
	{ 0x02, 0x04, 0x08, 0x10, 0x20 }, { 0x00, 0x41, 0x41, 0x7F, 0x00 }, { 0x04, 0x02, 0x01, 0x02, 0x04 },
	{ 0x40, 0x40, 0x40, 0x40, 0x40 },
};

static void rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
	for (int j = y; j < y + h; j++)
		if (j >= 0 && j < H)
			for (int i = x; i < x + w; i++)
				if (i >= 0 && i < W)
					fb[j * W + i] = c;
}

/* Scale 2, 12 px per character, lower case shown as upper */
static void text(uint16_t *fb, int x, int y, uint16_t c, const char *fmt, ...)
{
	char s[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	for (const char *p = s; *p; p++, x += 12) {
		int ch = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;

		if (ch < 32 || ch > 95)
			continue;
		for (int cx = 0; cx < 5; cx++)
			for (int cy = 0; cy < 7; cy++)
				if (font[ch - 32][cx] & (1 << cy))
					rect(fb, x + cx * 2, y + cy * 2, 2, 2, c);
	}
}

static int db_y(int v)	/* dB * 16 to a plot row */
{
	int y = (top_db * DB_UNIT - v) * PLOT_H / (DB_SPAN * DB_UNIT);

	return y < 0 ? 0 : y >= PLOT_H ? PLOT_H - 1 : y;
}

/*
 * Plot background. Only two kinds of row exist (with or without a horizontal grid line),
 * each with and without the fill under the trace, so they fit the 16 KB data cache.
 */
static uint16_t grid_row[2][2][COLS];
static uint8_t grid_is_h[PLOT_H];

static void draw_grid(void)
{
	static const int steps[] = { 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000 };
	int span_khz = (stop_hz - start_hz) / 1000, st = 100000;

	memset(grid_is_h, 0, sizeof(grid_is_h));
	for (int y = 0; y <= DB_SPAN; y += 10) {
		int row = y * PLOT_H / DB_SPAN;

		if (row < PLOT_H)
			grid_is_h[row] = 1;
	}
	for (int x = 0; x < COLS; x++) {
		grid_row[0][0][x] = 0;
		grid_row[0][1][x] = RGB(0, 50, 90);
		grid_row[1][0][x] = grid_row[1][1][x] = RGB(40, 60, 40);
	}
	for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
		if (span_khz / steps[i] <= 10) {
			st = steps[i];
			break;
		}
	for (int khz = (start_hz / 1000 + st - 1) / st * st; khz <= stop_hz / 1000; khz += st) {
		int x = (int)((int64_t)(khz * 1000LL - start_hz) * COLS / (stop_hz - start_hz));

		if (x < COLS)
			for (int h = 0; h < 2; h++)
				for (int f = 0; f < 2; f++)
					grid_row[h][f][x] = RGB(40, 40, 70);
	}
}

static void draw_axes(uint16_t *fb)
{
	static const int steps[] = { 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000 };
	int span_khz = (stop_hz - start_hz) / 1000, st = 100000;

	rect(fb, 0, PLOT_Y, PLOT_X, PLOT_H, 0);
	rect(fb, 0, PLOT_Y + PLOT_H, W, WF_Y - PLOT_Y - PLOT_H, 0);
	for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
		if (span_khz / steps[i] <= 10) {
			st = steps[i];
			break;
		}
	for (int y = 0; y <= DB_SPAN; y += 10) {
		int row = y * PLOT_H / DB_SPAN;

		text(fb, 4, PLOT_Y + row - (y == DB_SPAN ? 14 : 0), RGB(150, 200, 150), "%4d", top_db - y);
	}
	for (int khz = (start_hz / 1000 + st - 1) / st * st; khz <= stop_hz / 1000; khz += st) {
		int x = (int)((int64_t)(khz * 1000LL - start_hz) * COLS / (stop_hz - start_hz));

		text(fb, PLOT_X + x - 30, PLOT_Y + PLOT_H + 8, RGB(200, 200, 200), "%.1f", khz / 1000.0);
	}
}

static void lut_init(void)
{
	static const uint8_t k[][3] = { { 0, 0, 0 }, { 0, 0, 140 }, { 0, 170, 200 },
					{ 240, 230, 0 }, { 240, 40, 0 }, { 255, 255, 255 } };

	for (int i = 0; i < 256; i++) {
		int s = i * 5 / 256, f = i * 5 % 256;
		int r = (k[s][0] * (256 - f) + k[s + 1][0] * f) >> 8;
		int g = (k[s][1] * (256 - f) + k[s + 1][1] * f) >> 8;
		int b = (k[s][2] * (256 - f) + k[s + 1][2] * f) >> 8;

		wf_lut[i] = RGB(r, g, b);
	}
}

static void fb_init(void)
{
	wf = calloc(COLS * WF_H, sizeof(*wf));
	lut_init();
#ifdef HOST
	fbmem = calloc(W * H * 2, 2);
#else
	struct fb_fix_screeninfo fix;
	int tty = open("/dev/tty0", O_RDWR);

	if (tty >= 0) {
		ioctl(tty, KDSETMODE, KD_GRAPHICS);	/* keep fbcon off the screen */
		close(tty);
	}
	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd < 0 || ioctl(fbfd, FBIOGET_VSCREENINFO, &var) || ioctl(fbfd, FBIOGET_FSCREENINFO, &fix)) {
		perror("/dev/fb0");
		exit(1);
	}
	W = var.xres;
	H = var.yres;
	if (W != 1920 || H != 1080 || var.yres_virtual < 2 * (unsigned)H) {
		fprintf(stderr, "need 1920x1080 with 2 buffers, got %dx%d/%d\n", W, H, var.yres_virtual);
		exit(1);
	}
	fbmem = mmap(NULL, W * H * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fbmem == MAP_FAILED) {
		perror("mmap fb");
		exit(1);
	}
	memset(fbmem, 0, W * H * 4);
#endif
	back = fbmem + W * H;
}

static void fb_flip(void)
{
#ifndef HOST
	uint32_t zero = 0;

	shown = !shown;
	var.xoffset = 0;
	var.yoffset = shown * H;
	ioctl(fbfd, FBIOPAN_DISPLAY, &var);
	ioctl(fbfd, FBIO_WAITFORVSYNC, &zero);
#else
	shown = !shown;
#endif
	back = fbmem + (shown ? 0 : W * H);
}

static void fb_exit(void)
{
#ifndef HOST
	int tty = open("/dev/tty0", O_RDWR);

	var.yoffset = 0;
	ioctl(fbfd, FBIOPAN_DISPLAY, &var);
	if (tty >= 0) {
		ioctl(tty, KDSETMODE, KD_TEXT);
		close(tty);
	}
#endif
}

struct peak { int col, v; };

/* Strongest local maxima at least peak_db over the floor, at least peak_gap columns apart */
static int peak_db = 10, peak_gap = 40;

static int find_peaks(struct peak *pk, int maxp, int *floor_out)
{
	int hist[256] = { 0 }, cum = 0, fl = -100, n = 0;
	static uint8_t used[COLS];

	for (int c = 0; c < COLS; c++) {
		int d = -trace[c] / DB_UNIT;

		hist[d < 0 ? 0 : d > 255 ? 255 : d]++;
	}
	for (int d = 255; d >= 0; d--) {
		cum += hist[d];
		if (cum >= COLS / 2) {
			fl = -d;
			break;
		}
	}
	*floor_out = fl;
	memset(used, 0, sizeof(used));
	while (n < maxp) {
		int best = -1, bv = (fl + peak_db) * DB_UNIT;

		for (int c = 1; c < COLS - 1; c++)
			if (!used[c] && trace[c] > bv && trace[c] >= trace[c - 1] && trace[c] >= trace[c + 1]) {
				best = c;
				bv = trace[c];
			}
		if (best < 0)
			break;
		pk[n].col = best;
		pk[n].v = bv;
		n++;
		for (int c = best - peak_gap; c <= best + peak_gap; c++)
			if (c >= 0 && c < COLS)
				used[c] = 1;
	}
	return n;
}

/* Mouse pointer in screen pixels */
static int ptr_x = 960, ptr_y = 300, ptr_seen, ptr_moved;
static int ptr_old_x[2] = { -1, -1 }, ptr_old_y[2];	/* where it is drawn in each buffer */
static double ptr_t;		/* when the pointer first moved since the last frame */

/* Plot column to a frequency on the 100 kHz station grid */
static int snap_hz(int col)
{
	return (col_hz[col] + 50000) / 100000 * 100000;
}

static void render(void)
{
	struct peak pk[8];
	double t0 = now(), t_trace, t_copy, t_flip;
	int fl, np = find_peaks(pk, 8, &fl);
	char line[160];
	int off = 0;
	uint16_t *row = back + PLOT_Y * W + PLOT_X;
	static int16_t top[COLS], bot[COLS], hold_y[COLS];
	int bufi = back != fbmem, wipe_a = 1, wipe_b = 0;

	/* only the plot and text are redrawn every frame: remove the pointer from this buffer */
	if (ptr_old_x[bufi] >= 0) {
		rect(back, ptr_old_x[bufi] - 10, ptr_old_y[bufi] - 10, 21, 21, 0);
		wipe_a = ptr_old_y[bufi] - 10 - WF_Y;
		wipe_b = ptr_old_y[bufi] + 10 - WF_Y;
	}
	static uint16_t mix[COLS];
	int py = -1, amin = PLOT_H, bmax = -1, tick_x = -1, mark_x = -1;
	const uint16_t line_c = RGB(80, 230, 255), hold_c = RGB(255, 200, 40);
	const uint16_t mark_c = radio_on ? RGB(255, 80, 80) : RGB(200, 200, 80);

	for (int c = 0; c < COLS; c++) {
		int y = db_y(trace[c]);

		/* the line joins this column to the previous one; below it is the fill */
		top[c] = py < 0 ? y : py < y ? py : y;
		bot[c] = py < 0 ? y - 1 : py < y ? y : py;
		py = y;
		amin = top[c] < amin ? top[c] : amin;
		bmax = bot[c] > bmax ? bot[c] : bmax;
		if (hold_on) {
			hold_y[c] = (int16_t)db_y(hold[c]);
			amin = hold_y[c] < amin ? hold_y[c] : amin;
			bmax = hold_y[c] > bmax ? hold_y[c] : bmax;
		}
	}
	if (!radio_on) {
		tick_x = (int)((int64_t)(tune_hz - start_hz) * COLS / (stop_hz - start_hz));
		tick_x = tick_x >= 0 && tick_x < COLS ? tick_x : -1;
	}
	if (marker_set && marker_hz >= start_hz && marker_hz < stop_hz)
		mark_x = (int)((int64_t)(marker_hz - start_hz) * COLS / (stop_hz - start_hz));
	t_trace = now();
	/* row by row: going through a frame-sized buffer by columns misses the cache on every pixel */
	for (int j = 0; j < PLOT_H; j++) {
		uint16_t *dst = row + j * W;
		const uint16_t *plain = grid_row[grid_is_h[j]][0], *fill = grid_row[grid_is_h[j]][1];

		if (j < amin) {
			memcpy(dst, plain, sizeof(mix));
		} else if (j > bmax) {
			memcpy(dst, fill, sizeof(mix));
		} else {
			memcpy(mix, plain, sizeof(mix));
			for (int c = 0; c < COLS; c++)
				if (j >= top[c])
					mix[c] = j <= bot[c] ? line_c : fill[c];
			if (hold_on)
				for (int c = 0; c < COLS; c++)
					if (hold_y[c] == j)
						mix[c] = hold_c;
			memcpy(dst, mix, sizeof(mix));
		}
		if (mark_x >= 0 && (radio_on || !(j & 1)))
			dst[mark_x] = mark_c;
		if (tick_x >= 0 && j < 8)
			dst[tick_x] = RGB(255, 60, 60);
	}
	t_copy = now();

	/* labels are drawn straight into the back buffer */
	rect(back, 0, 0, W, PLOT_Y, 0);
	off = snprintf(line, sizeof(line), "%.3f-%.3f MHZ  RBW %.1f KHZ  AVG %d  %s  SWEEP %.2f S",
		       start_hz / 1e6, stop_hz / 1e6, fs_hz * 1.5 / N / 1e3, AVG * 2,
		       mode_mean ? "MEAN" : "MAX", last_sweep_s);
	if (hold_on)
		off += snprintf(line + off, sizeof(line) - off, "  HOLD");
	if (paused)
		off += snprintf(line + off, sizeof(line) - off, "  PAUSED");
	text(back, 8, 4, RGB(255, 255, 255), "%s", line);
	text(back, 8, 24, RGB(170, 220, 170), "DBFS  FLOOR %d  ADC %X  TUNER %02X", fl, adc_gain & 15,
	     tuner_gains[tuner_gain]);
	if (radio_on)
		text(back, 400, 24, RGB(255, 120, 120), "RADIO %.1f MHZ  PILOT %+.0f DB  RX %.0f DB  VOL %d  FILTER %d",
		     marker_hz / 1e6, pilot_db, rx_db, volume, radio_bw);
	else if (marker_set)
		text(back, 400, 24, RGB(200, 200, 80), "MARKER %.1f MHZ  ENTER OR CLICK: LISTEN", marker_hz / 1e6);
	if (ptr_seen && ptr_x >= PLOT_X && ptr_x < PLOT_X + COLS)
		text(back, 1200, 24, RGB(255, 255, 255), "CURSOR %.1f MHZ", snap_hz(ptr_x - PLOT_X) / 1e6);

	for (int i = 0; i < np; i++) {
		int f = col_hz[pk[i].col] + (col_hz[pk[i].col + 1] - col_hz[pk[i].col]) / 2;
		int y = PLOT_Y + db_y(pk[i].v) - 18;

		text(back, PLOT_X + pk[i].col - 36, y < PLOT_Y ? PLOT_Y : y, RGB(255, 255, 120),
		     "%.2f", f / 1e6);
	}

	draw_axes(back);
	/* the whole waterfall when it has a new row, else only where the old pointer was */
	if (wf_dirty) {
		wipe_a = 0;
		wipe_b = WF_H - 1;
		wf_dirty--;
	}
	for (int j = wipe_a < 0 ? 0 : wipe_a; j <= wipe_b && j < WF_H; j++) {
		int r = (wf_row - 1 - j + WF_H) % WF_H;

		memcpy(back + (WF_Y + j) * W + PLOT_X, wf + r * COLS, COLS * sizeof(*wf));
	}
	rect(back, 0, H - 30, W, 30, 0);
	text(back, 8, H - 26, RGB(120, 120, 120),
	     "CLICK OR ARROWS+ENTER LISTEN  R SWEEP  A/D MOVE  W/S ZOOM  1 FM  2 VHF  3 UHF  4 ALL  H HOLD  M MAX/MEAN  +/- LEVEL  P PAUSE  Q QUIT");
	if (ptr_seen) {
		rect(back, ptr_x - 10, ptr_y, 21, 1, RGB(255, 255, 255));
		rect(back, ptr_x, ptr_y - 10, 1, 21, RGB(255, 255, 255));
		ptr_old_x[bufi] = ptr_x;
		ptr_old_y[bufi] = ptr_y;
	}
	t_flip = now();
	fb_flip();

	if (verbose) {
		printf("render ms: trace %.1f, plot copy %.1f, text %.1f, flip %.1f", (t_trace - t0) * 1e3,
		       (t_copy - t_trace) * 1e3, (t_flip - t_copy) * 1e3, (now() - t_flip) * 1e3);
		if (ptr_t > 0)
			printf(", pointer latency %.0f", (now() - ptr_t) * 1e3);
		printf("\n");
	}
	ptr_t = 0;
	if (verbose && np) {
		printf("sweep %d %.2fs floor %d peaks:", sweep_n, last_sweep_s, fl);
		for (int i = 0; i < np; i++) {
			int f = col_hz[pk[i].col] + (col_hz[pk[i].col + 1] - col_hz[pk[i].col]) / 2;

			printf(" %.3f/%.0f", f / 1e6, pk[i].v / (double)DB_UNIT);
		}
		printf("\n");
		fflush(stdout);
	}
}

/* Colours run from 5 dB under the noise floor to 30 dB above it */
static void wf_add_row(void)
{
	struct peak pk[1];
	int fl;

	find_peaks(pk, 0, &fl);
	for (int rep = 0; rep < WF_REP; rep++) {
		uint16_t *r = wf + wf_row * COLS;

		for (int c = 0; c < COLS; c++) {
			int v = (trace[c] - (fl - 5) * DB_UNIT) * 255 / (35 * DB_UNIT);

			r[c] = wf_lut[v < 0 ? 0 : v > 255 ? 255 : v];
		}
		wf_row = (wf_row + 1) % WF_H;
	}
	wf_dirty = 2;
}

/* ---- keys ---- */

static int evfd[8];
static struct termios old_tio;
static int have_tio;

static void keys_init(void)
{
	struct termios t;

	if (isatty(0) && !tcgetattr(0, &old_tio)) {
		t = old_tio;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 0;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
		have_tio = 1;
	}
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	for (int i = 0; i < 8; i++)
		evfd[i] = -1;
}

/* Called once a second: a USB mouse or keyboard may be plugged in at any time */
static void keys_rescan(void)
{
	for (int i = 0; i < 8; i++) {
		char name[32];

		if (evfd[i] >= 0)
			continue;
		snprintf(name, sizeof(name), "/dev/input/event%d", i);
		evfd[i] = open(name, O_RDONLY | O_NONBLOCK);
	}
}

static void keys_exit(void)
{
	if (have_tio)
		tcsetattr(0, TCSANOW, &old_tio);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~O_NONBLOCK);
}

static void view(int mhz_a, int mhz_b)
{
	set_range(mhz_a, mhz_b);
	draw_grid();
	if (radio_want)
		rescan = 1;	/* a new view needs one sweep; the radio comes back after it */
}

static void marker_to(int hz, int listen)
{
	if (hz < RF_MIN_MHZ * 1000000)
		hz = RF_MIN_MHZ * 1000000;
	if (hz > RF_MAX_MHZ * 1000000)
		hz = RF_MAX_MHZ * 1000000;
	marker_hz = hz;
	marker_set = 1;
	if (listen)
		radio_want = 1;
	radio_pending = 1;
	radio_change_t = now();
}

static void apply_key(int k)
{
	static int preset;
	static const int pre[][2] = { { 88, 108 }, { 174, 230 }, { 470, 790 }, { 44, 880 } };
	int a = start_hz / 1000000, b = stop_hz / 1000000, c = (a + b) / 2, span = b - a;

	switch (k) {
	case 'a':
		view(a - (span / 4 ? span / 4 : 1), b - (span / 4 ? span / 4 : 1));
		break;
	case 'd':
		view(a + (span / 4 ? span / 4 : 1), b + (span / 4 ? span / 4 : 1));
		break;
	case 'w':
		view(c - span / 4, c + (span + 3) / 4);
		break;
	case 's':
		view(c - span, c + span);
		break;
	case '1': case '2': case '3': case '4':
		preset = k - '1';
		view(pre[preset][0], pre[preset][1]);
		break;
	case 'h':
		hold_on = !hold_on;
		for (int i = 0; i < COLS; i++)
			hold[i] = trace[i];
		break;
	case 'm':
		mode_mean = !mode_mean;
		break;
	case '+': case '=':
		if (radio_want)
			volume = volume < 32 ? volume * 2 : volume;
		else
			top_db += 5;
		break;
	case '-':
		if (radio_want)
			volume = volume > 1 ? volume / 2 : volume;
		else
			top_db -= 5;
		break;
	case 'L':
		marker_to(marker_hz - 100000, 0);
		break;
	case 'R':
		marker_to(marker_hz + 100000, 0);
		break;
	case '\r': case '\n':
		radio_want = !radio_want;
		marker_set = 1;
		radio_pending = 1;
		radio_change_t = 0;
		break;
	case 'r':
		rescan = 1;
		break;
	case ',':
		tuner_gain_step(-1);
		break;
	case '.':
		tuner_gain_step(1);
		break;
	case '[':
		if (adc_gain < 15)
			adc_gain_set(adc_gain + 1);
		break;
	case ']':
		if (adc_gain > 0)
			adc_gain_set(adc_gain - 1);
		break;
	case 'b':
		radio_bw = radio_bw == 8 ? 7 : radio_bw == 7 ? 6 : 8;
		radio_pending = 1;
		radio_change_t = 0;
		break;
	case 'o':
		radio_want = 0;
		radio_pending = 1;
		radio_change_t = 0;
		break;
	case 'p':
		paused = !paused;
		break;
	case 'q':
		quit = 1;
		break;
	}
}

/*
 * Input is read often (also while the tuner settles, see ui_wait) but acted on only between
 * tunings: a view or marker change in the middle of one would mix it with the new state.
 * The pointer itself moves at once. Queue values: a key for apply_key, or KQ_CLICK + column.
 */
#define KQ_CLICK 0x10000
static int kq[64];
static unsigned kq_w, kq_r;

static void kq_put(int k)
{
	if (k && kq_w - kq_r < 64)
		kq[kq_w++ % 64] = k;
}

static void ptr_add(int dx, int dy)
{
	ptr_x += dx;
	ptr_y += dy;
	ptr_x = ptr_x < 0 ? 0 : ptr_x >= W ? W - 1 : ptr_x;
	ptr_y = ptr_y < 0 ? 0 : ptr_y >= H ? H - 1 : ptr_y;
	if (!ptr_moved)
		ptr_t = now();
	ptr_seen = ptr_moved = 1;
}

static void poll_input(void)
{
	static int esc;
	unsigned char ch;

	while (read(0, &ch, 1) == 1) {
		if (esc == 1) {
			esc = ch == '[' ? 2 : 0;
		} else if (esc == 2) {
			esc = 0;
			kq_put(ch == 'A' ? 'w' : ch == 'B' ? 's' : ch == 'C' ? 'R' : ch == 'D' ? 'L' : 0);
		} else if (ch == 27) {
			esc = 1;
#ifdef PTR_TEST
		} else if (ch == 'x') {		/* pointer from the serial console, for testing */
			ptr_add(ptr_x > W - 40 ? -ptr_x : 37, ptr_y > H - 40 ? -ptr_y : 29);
#endif
		} else {
			kq_put(ch);
		}
	}
	for (int i = 0; i < 8; i++) {
		struct input_event e;
		int n;

		if (evfd[i] < 0)
			continue;
		while ((n = read(evfd[i], &e, sizeof(e))) == sizeof(e)) {
			static const struct { int code, key; } map[] = {
				{ KEY_LEFT, 'L' }, { KEY_RIGHT, 'R' }, { KEY_UP, 'w' }, { KEY_DOWN, 's' },
				{ KEY_A, 'a' }, { KEY_D, 'd' }, { KEY_W, 'w' }, { KEY_S, 's' },
				{ KEY_CHANNELUP, 'R' }, { KEY_CHANNELDOWN, 'L' }, { KEY_POWER, '\r' },
				{ KEY_ENTER, '\r' }, { KEY_KPENTER, '\r' }, { KEY_R, 'r' },
				{ KEY_1, '1' }, { KEY_2, '2' }, { KEY_3, '3' }, { KEY_4, '4' },
				{ KEY_H, 'h' }, { KEY_M, 'm' }, { KEY_EQUAL, '+' }, { KEY_MINUS, '-' },
				{ KEY_P, 'p' }, { KEY_Q, 'q' }, { KEY_B, 'b' },
				{ KEY_LEFTBRACE, '[' }, { KEY_RIGHTBRACE, ']' },
				{ KEY_COMMA, ',' }, { KEY_DOT, '.' },
			};

			if (e.type == EV_REL) {
				if (e.code == REL_X)
					ptr_add(e.value, 0);
				else if (e.code == REL_Y)
					ptr_add(0, e.value);
				else if (e.code == REL_WHEEL)
					for (int k = e.value; k; k += k < 0 ? 1 : -1)
						kq_put(k > 0 ? 'R' : 'L');
				continue;
			}
			if (e.type != EV_KEY || !e.value)
				continue;
			if (e.code == BTN_LEFT && e.value == 1) {
				if (ptr_x >= PLOT_X && ptr_x < PLOT_X + COLS && ptr_y < WF_Y + WF_H)
					kq_put(KQ_CLICK + ptr_x - PLOT_X);
				continue;
			}
			if (e.code == BTN_RIGHT && e.value == 1) {
				kq_put('o');
				continue;
			}
			/* auto-repeat only for the marker */
			if (e.value == 2 && e.code != KEY_LEFT && e.code != KEY_RIGHT)
				continue;
			for (unsigned j = 0; j < sizeof(map) / sizeof(map[0]); j++)
				if (map[j].code == e.code)
					kq_put(map[j].key);
		}
		if (n < 0 && errno == ENODEV) {		/* unplugged */
			close(evfd[i]);
			evfd[i] = -1;
		}
	}
}

static void apply_queue(void)
{
	while (kq_r != kq_w) {
		int k = kq[kq_r++ % 64];

		if (k >= KQ_CLICK)
			marker_to(snap_hz(k - KQ_CLICK), 1);
		else
			apply_key(k);
	}
}

/* Wait out the tuner settling; a moving pointer gets drawn in the meantime */
static int ui_ready;
static double last_render;

#ifndef HOST
static void ui_wait(double until)
{
	while (now() < until) {
		if (ui_ready) {
			poll_input();
			if (ptr_moved) {
				ptr_moved = 0;
				render();
				last_render = now();
			}
		}
		usleep(1000);
	}
}
#endif

static void on_signal(int s)
{
	(void)s;
	quit = 1;
}

/* ---- peak list ---- */

static int dump;

/* 10 MHz windows give 5.6 kHz per column */
static void scan_list(int a, int b)
{
	struct peak pk[64];

	peak_db = 8;
	peak_gap = 8;
	for (int w = a; w < b && !quit; w += 10) {
		int fl, np;

		set_range(w, w + 10);
		for (tune_hz = first_tune; tune_hz <= last_tune; tune_hz += cur_step) {
			tune(tune_hz);
			measure();
			combine(tune_hz);
		}
		commit_upto(INT_MAX);
		np = find_peaks(pk, 64, &fl);
		printf("W %d %d\n", w, fl);
		if (dump) {
			printf("D %d", w);
			for (int c = 0; c < COLS; c += 2)
				printf(" %d", trace[c]);
			printf("\n");
		}
		for (int i = 0; i < np; i++)
			printf("P %.4f %.1f\n", (col_hz[pk[i].col] + col_hz[pk[i].col + 1]) / 2 / 1e6,
			       pk[i].v / (double)DB_UNIT);
		fflush(stdout);
	}
}

/* Peaks of one tuning before any combining, with both RF readings of each bin */
static void raw_list(int a, int b)
{
	int16_t *srt = malloc((N / 2 + 1) * sizeof(*srt));

	for (int mhz = a; mhz <= b && !quit; mhz++) {
		int med;

		tune(mhz * 1000000);
		measure();
		memcpy(srt, db, (N / 2 + 1) * sizeof(*srt));
		qsort(srt, N / 2 + 1, sizeof(*srt), cmp16);
		med = srt[N / 4];
		printf("T %d med %.1f\n", mhz, med / (double)DB_UNIT);
#ifndef HOST
		if (verbose)
			mxl_dump();
#endif
		for (int k = 2; k < N / 2 - 1; k++)
			if (db[k] > med + 12 * DB_UNIT && db[k] >= db[k - 1] && db[k] > db[k + 1]) {
				/* parabola through the dB values: about 1/10 of a bin */
				int l = db[k - 1], c = db[k], r = db[k + 1], den = l - 2 * c + r;
				double frac = den ? 0.5 * (l - r) / den : 0;
				double fb = (k + frac) * fs_hz / N;

				printf("R %.4f %.1f rf %.4f / %.4f\n", fb / 1e6, db[k] / (double)DB_UNIT,
				       (mhz * 1e6 - FIF_HZ + fb) / 1e6, (mhz * 1e6 - FIF_HZ + fs_hz - fb) / 1e6);
			}
		fflush(stdout);
	}
	free(srt);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
	int opt, a = 88, b = 108, exit_after = 0, scan = 0, raw = 0;
	double last_rescan = 0;

	while ((opt = getopt(argc, argv, "n:a:t:s:vx:SRCDMF:B:O:W:")) != -1) {
		switch (opt) {
		case 'n': N = atoi(optarg); break;
		case 'a': AVG = atoi(optarg); break;
		case 't': step_hz = atoi(optarg) * 1000000; break;
		case 's': settle_ms = atoi(optarg); break;
		case 'v': verbose = 1; break;
		case 'S': scan = 1; break;
		case 'R': raw = 1; break;
		case 'C': no_clkout = 1; break;
		case 'D': dump = 1; break;
		case 'M': mode_mean = 1; break;
		case 'B': radio_bw = atoi(optarg); break;
#ifndef HOST
		case 'W': tuner_writes = optarg; break;
#endif
		case 'O': radio_off_mhz = atoi(optarg); break;
		case 'F': marker_hz = atoi(optarg) * 1000; marker_set = 1; radio_want = 1; radio_pending = 1; break;
		case 'x': exit_after = atoi(optarg); break;
		default:
			fprintf(stderr, "usage: %s [-n FFT] [-a AVG] [-t STEP_MHZ] [-s SETTLE_MS] [-v] [START_MHZ STOP_MHZ]\n", argv[0]);
			return 1;
		}
	}
	if (optind + 2 <= argc) {
		a = atoi(argv[optind]);
		b = atoi(argv[optind + 1]);
	}
	if (N < 256 || N > 16384 || (N & (N - 1))) {
		fprintf(stderr, "FFT size must be a power of 2, 256..16384\n");
		return 1;
	}
	cap_n = N * (2 * AVG) + N / 2;
	cap = calloc(cap_n, sizeof(*cap));
	if (!cap) {
		perror("calloc");
		return 1;
	}
	memset(cap, 0, cap_n * sizeof(*cap));
	mlockall(MCL_CURRENT | MCL_FUTURE);
	{
		struct sched_param sp = { .sched_priority = 1 };

		sched_setscheduler(0, SCHED_FIFO, &sp);	/* fewer gaps in the capture */
	}
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	hw_init();
	dsp_init();
	rate_init();
	tune(474000000);
	if (raw) {
		raw_list(a, b);
		return 0;
	}
	ifgain_init();
	if (scan) {
		scan_list(a, b);
		return 0;
	}
	fb_init();
	keys_init();
	radio_init();
	adc_gain = 5;
#ifndef HOST
	adc_gain = *(volatile uint16_t *)(dmd + ADC_GAIN) & 15;
	adc_gain_set(adc_gain);
#endif
	view(a, b);
	ui_ready = 1;
	fflush(stdout);

	while (!quit) {
		if (now() - last_rescan > 1) {
			keys_rescan();
			last_rescan = now();
		}
		poll_input();
		apply_queue();
		/* retune after the marker has rested, not on every arrow step */
		if (radio_pending && now() - radio_change_t > 0.15) {
			radio_pending = 0;
			if (radio_on)
				radio_stop();
			if (radio_want && !rescan)
				radio_start();
			tune_hz = first_tune;
			sweep_s = now();
		}
		if (radio_on && rescan) {
			radio_stop();
			tune_hz = first_tune;
			sweep_s = now();
		}
		if (radio_on) {
			if (ptr_moved || now() - last_render > 0.5) {
				ptr_moved = 0;
				render();
				last_render = now();
			}
			usleep(5000);
			continue;
		}
		if (paused) {
			render();
			usleep(100000);
			continue;
		}
		tune(tune_hz);
		measure();
		combine(tune_hz);
		commit_upto(tune_hz - DELTA_MAX);
		if (tune_hz >= last_tune) {
			commit_upto(INT_MAX);
			last_sweep_s = now() - sweep_s;
			sweep_s = now();
			sweep_n++;
			wf_add_row();
			tune_hz = first_tune;
			render();
			last_render = now();
			if (exit_after && sweep_n >= exit_after)
				break;
			if (rescan) {
				rescan = 0;
				if (radio_want)
					radio_start();
			}
			continue;
		}
		tune_hz += cur_step;
		if (ptr_moved || now() - last_render > 0.2) {
			ptr_moved = 0;
			render();
			last_render = now();
		}
	}
	radio_exit();
	fb_exit();
	keys_exit();
#ifdef HOST
	{
		/* show the most recent frame */
		uint16_t *f = fbmem + (shown ? W * H : 0);
		FILE *o = fopen("specan_host.ppm", "wb");

		fprintf(o, "P6\n%d %d\n255\n", W, H);
		for (int i = 0; i < W * H; i++) {
			uint8_t c[3] = { (f[i] >> 8) & 0xf8, (f[i] >> 3) & 0xfc, (f[i] << 3) & 0xf8 };

			fwrite(c, 1, 3, o);
		}
		fclose(o);
	}
#endif
	return 0;
}
