/*
 * dsppcm - play 16-bit stereo PCM on HDMI through our own SE DSP program (tools/dsp/pcm.s),
 * without any MStar DSP code. The program reads one L/R pair per HDMI output interrupt (48 kHz)
 * from a ring in SE DM (0x1000..0x1FFF, 2048 frames, words = 24-bit signed = pcm << 8) and
 * counts frames in mailbox 0x2DE0. We write the ring through IDMA and stay TARGET frames ahead.
 *
 * Build on the board: gcc -O2 -o dsppcm dsppcm.c -lm
 * Research tool: uses mstar_audio.h, so it needs the headers generated from your own flash
 * dump (extract_audio_fw.py). own_audio.h / owntest.c do not.
 * Usage: dsppcm PROGRAM.hex [FILE|-] [-t SECONDS] [-v SHIFT] [-n] [-i FROM-TO[,..]] [-m] [-H]
 *   FILE  raw s16le stereo 48 kHz ("-" = stdin); without FILE a test signal is played
 *         (1 kHz sine 2 s, then 440/660 Hz alternating every 0.5 s)
 *   -t    stop after SECONDS (default: end of the input, test signal 6 s)
 *   -v    attenuate by SHIFT bits (default 0)
 *   -n    skip audio_basic_init (the MStar register table), -i FROM-TO: only table entries
 *         FROM..TO (0-based, inclusive; several ranges with commas) and the power-on writes; -m skip mad2_setmeminfo_se,
 *         -H skip hdmi_audio_on/analog_unmute (finding the minimal init, docs: own_dsp_audio.md, Polish wlasny_dsp_dzwiek.md)
 * Prints underruns (DSP caught up with the writer) and the measured frame rate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include "mstar_audio.h"

#define MAXW	4096
#define RING	0x1000		/* DM word address of the ring */
#define FRAMES	2048		/* ring size in stereo frames (0x1000 words) */
#define TARGET	1536		/* frames to keep ahead of the DSP */

static double t_now(void) { struct timeval v; gettimeofday(&v, 0); return v.tv_sec + v.tv_usec / 1e6; }

/* IDMA burst into SE DM at word address addr (auto-increment), words already 24-bit */
static int dm_burst(uint16_t addr, const uint32_t *w, unsigned n)
{
	wm8(A(0x2a7e), 1, 1);
	w16(A(0x2ac0), 0);
	w16(A(0x2a84), addr | 0x8000);
	for (unsigned i = 0; i < n; i++) {
		int k;

		w8(A(0x2a82), w[i] >> 8);
		w8(A(0x2a83), w[i] >> 16);
		w8(A(0x2a82), w[i]);
		w8(A(0x2a83), 0);
		for (k = 0; k < 10000 && (r8(A(0x2a80)) & 0x10); k++)
			;
		if (k == 10000)
			return 0;
	}
	return 1;
}

/* audio_basic_init with only the table entries from..to (bisecting the minimal init) */
static unsigned char use_entry[256];

static void init_part(void)
{
	w8(A(0x2c00), 3); msleep(2); w8(A(0x2c00), 0); msleep(1);
	for (int i = 0; i < (int)(sizeof(audio_init_table) / sizeof(audio_init_table[0])); i++) {
		if (!use_entry[i])
			continue;
		uint32_t reg = audio_init_table[i].reg;
		uint8_t m = audio_init_table[i].mask, v = audio_init_table[i].val;

		if (reg == 0xffff) {
			if (m == 1 && v)
				msleep(v);
			continue;
		}
		if ((reg & 0xf0000) == 0xa0000)
			reg &= 0xffff;
		else if ((reg & 0xf0000) == 0xb0000)
			reg = (reg & 0xffff) + 0x100000;
		else {
			if (reg == 0x2d2a && m == 8)
				continue;
			reg += 0x110000;
		}
		wm8(reg, m, v);
	}
	wm8(A(0x2d31), 2, 0);
	w8(A(0x2b42), 0);
}

static uint16_t dsp_frames(void) { return r16(A(0x2de0)); }

/* test signal: 1 kHz for 2 s, then 440 / 660 Hz alternating every 0.5 s, -6 dBFS */
static void gen(uint64_t pos, int16_t *lr)
{
	double t = pos / 48000.0, f = t < 2 ? 1000 : ((int)(t * 2) & 1) ? 660 : 440;
	static double ph;

	ph += 2 * M_PI * f / 48000;
	if (ph > 2 * M_PI)
		ph -= 2 * M_PI;
	lr[0] = lr[1] = (int16_t)(16384 * sin(ph));
}

int main(int argc, char **argv)
{
	static uint8_t prog[3 * MAXW];
	static uint32_t ring0[FRAMES * 2];
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	const char *pfile = 0, *in = 0;
	double secs = -1;
	int shift = 0, n = 0, noinit = 0, nomem = 0, nohdmi = 0, from = -1, to = -1;
	unsigned w;
	FILE *f, *src = 0;

	for (int a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-t") && a + 1 < argc)
			secs = atof(argv[++a]);
		else if (!strcmp(argv[a], "-v") && a + 1 < argc)
			shift = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-n"))
			noinit = 1;
		else if (!strcmp(argv[a], "-m"))
			nomem = 1;
		else if (!strcmp(argv[a], "-H"))
			nohdmi = 1;
		else if (!strcmp(argv[a], "-i") && a + 1 < argc) {
			char *p = argv[++a];		/* FROM-TO[,FROM-TO...] */

			while (*p) {
				int x, y, k;

				if (sscanf(p, "%d-%d%n", &x, &y, &k) < 2)
					break;
				for (int i = x; i <= y && i < 256; i++)
					use_entry[i] = 1;
				from = 0;
				p += k;
				if (*p == ',')
					p++;
			}
		}
		else if (!pfile)
			pfile = argv[a];
		else
			in = argv[a];
	}
	if (!pfile) {
		fprintf(stderr, "usage: %s PROGRAM.hex [FILE|-] [-t SECONDS] [-v SHIFT]\n", argv[0]);
		return 1;
	}
	f = fopen(pfile, "r");
	if (!f) {
		perror(pfile);
		return 1;
	}
	while (n < MAXW && fscanf(f, "%x", &w) == 1) {
		prog[3 * n] = w;
		prog[3 * n + 1] = w >> 8;
		prog[3 * n + 2] = w >> 16;
		n++;
	}
	fclose(f);
	if (in) {
		src = strcmp(in, "-") ? fopen(in, "rb") : stdin;
		if (!src) {
			perror(in);
			return 1;
		}
	} else if (secs < 0)
		secs = 6;

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	if (from >= 0)
		init_part();
	else if (!noinit)
		audio_basic_init();
	if (!nomem)
		mad2_setmeminfo_se(SE_BASE);
	if (!nohdmi) {
		hdmi_audio_on();
		analog_unmute();
	}
	/* SE reset, program (word 0 last), the ring zeroed before the start */
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	if (n > 1 && !idma_write(1, 1, prog + 3, (n - 1) * 3))
		return 1;
	if (!dm_burst(RING, ring0, FRAMES * 2))
		return 1;
	if (!idma_write(1, 0, prog, 3))
		return 1;

	uint64_t wpos = 0, rpos = 0;		/* frames written / played (unwrapped) */
	uint16_t last = dsp_frames();
	unsigned under = 0, maxfill = 0, minfill = FRAMES;
	int eof = 0;
	double t0 = t_now();
	uint64_t r0 = 0;
	int started = 0;

	msleep(20);
	double tprog = t_now();
	for (;;) {
		uint16_t c = dsp_frames();

		if (c != last)
			tprog = t_now();
		else if (t_now() - tprog > 1) {
			printf("DSP not running (frame counter stuck at %u)\n", c);
			break;
		}
		rpos += (uint16_t)(c - last);
		last = c;
		if (!started)
			started = 1, r0 = rpos, t0 = t_now();
		if (wpos < rpos) {			/* underrun: the DSP read old data */
			under++;
			wpos = rpos;
		}
		unsigned fill = wpos - rpos;
		if (fill > maxfill)
			maxfill = fill;
		if (fill < minfill && wpos > TARGET)
			minfill = fill;
		if (secs >= 0 && rpos >= secs * 48000)
			break;
		if (eof && rpos >= wpos + FRAMES)
			break;
		if (fill >= TARGET) {
			usleep(2000);
			continue;
		}
		unsigned todo = TARGET - fill;
		static uint32_t words[FRAMES * 2];
		static int16_t pcm[FRAMES * 2];
		unsigned got;

		if (eof)
			memset(pcm, 0, todo * 4), got = todo;
		else if (src) {
			got = fread(pcm, 4, todo, src);
			if (got < todo) {
				memset(pcm + 2 * got, 0, (todo - got) * 4);
				eof = 1;
				got = todo;
			}
		} else {
			for (unsigned i = 0; i < todo; i++)
				gen(wpos + i, pcm + 2 * i);
			got = todo;
		}
		for (unsigned i = 0; i < 2 * got; i++)
			words[i] = ((uint32_t)(int32_t)(pcm[i] >> shift) << 8) & 0xffffff;
		/* write in at most two pieces (ring wrap) */
		unsigned at = wpos % FRAMES, first = got < FRAMES - at ? got : FRAMES - at;
		if (!dm_burst(RING + 2 * at, words, 2 * first) ||
		    (got > first && !dm_burst(RING, words + 2 * first, 2 * (got - first)))) {
			fprintf(stderr, "IDMA timeout\n");
			break;
		}
		wpos += got;
	}
	double dt = t_now() - t0;
	printf("played %llu frames in %.2f s (%.0f frames/s), underruns %u, fill %u..%u\n",
	       (unsigned long long)(rpos - r0), dt, (rpos - r0) / dt, under, minfill, maxfill);
	/* stop: SE reset, then a lone RTI at 0 */
	static const uint8_t rti[3] = { 0x1f, 0x00, 0x0a };
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	idma_write(1, 0, rti, 3);
	return 0;
}
