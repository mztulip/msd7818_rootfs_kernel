/*
 * own_audio.h - PCM playback on HDMI on the MStar Kronus (MSD7818) from Linux with our own
 * SE DSP program (tools/dsp/pcm.s, own_pcm_prog.h), no MStar DSP code and no MStar
 * firmware tables. Same API as mstar_audio.h, header-only, needs root (/dev/mem).
 *
 *   if (maudio_open(48000, 2)) ...          init audio, load and start our SE program
 *   maudio_write(samples, frames);          16-bit LE (interleaved if stereo), blocks/paces
 *   maudio_close();                          play out, stop the SE
 *
 * How it works (docs: own_dsp_audio.md, Polish wlasny_dsp_dzwiek.md): the SE raises its slot 0x08
 * interrupt once per HDMI output frame (48 kHz); our program acknowledges it (DM(0x817F)) and
 * writes one L/R pair to DM(0x8060)/DM(0x8061) as 24-bit signed words (pcm << 8), taken from a
 * ring in SE DM (0x1000..0x1FFF, 2048 frames). It counts frames in mailbox 0x2DE0; we write the
 * ring through IDMA and stay OWN_LEAD frames ahead of it.
 * Only 48 kHz is supported (the output rate); mono is duplicated to both channels.
 * Verified on HDMI; GPIO44 (analog mute) is released, but the analog SCART output is not
 * fed by this program yet.
 */
#ifndef OWN_AUDIO_H
#define OWN_AUDIO_H
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "own_pcm_prog.h"

#ifndef OWN_LEAD
#define OWN_LEAD	1536		/* frames written ahead of the DSP (32 ms) */
#endif
#define OWN_RING	0x1000		/* SE DM word address of the ring */
#define OWN_FRAMES	2048		/* ring size in frames (0x1000 words) */

#define RIU_PHYS	0x1f000000
#define RIU_SIZE	0x400000

static volatile uint8_t *riu;

static inline volatile uint8_t *rb(uint32_t reg) { return riu + reg * 2 - (reg & 1); }
static void w8(uint32_t reg, uint8_t v) { *rb(reg) = v; }
static void wm8(uint32_t reg, uint8_t m, uint8_t v) { *rb(reg) = (*rb(reg) & ~m) | (v & m); }
static uint8_t r8(uint32_t reg) { return *rb(reg); }
static inline volatile uint16_t *rw(uint32_t reg) { return (volatile uint16_t *)(riu + reg * 2); }
static void w16(uint32_t reg, uint16_t v) { *rw(reg) = v; }
static uint16_t r16(uint32_t reg) { return *rw(reg); }
#define A(x)	(0x110000 + (x))		/* audio-bank register */

static void msleep(int ms) { usleep(ms * 1000); }

/*
 * Minimal audio block set-up found by bisecting the stock init on a freshly booted board
 * (ddmin over the 192 entries of the stock init table, these 4 are enough): {RIU byte register,
 * mask, value}; reg 0 = delay of val ms. Plus the block reset and the power-on writes below.
 */
static const struct { uint32_t reg; uint8_t mask, val; } own_init[] = {
	{ 0x110c80, 0xff, 0x0e },	/* bank 0x110C (clocks?) */
	{ 0x112ca4, 0xff, 0xff },
	{ 0x112c48, 0x80, 0x80 },
	{ 0x112cb1, 0x04, 0x04 },
};

static void own_audio_init(void)
{
	w8(A(0x2c00), 3); msleep(2); w8(A(0x2c00), 0); msleep(1);	/* audio block reset */
	for (unsigned i = 0; i < sizeof(own_init) / sizeof(own_init[0]); i++) {
		if (!own_init[i].reg)
			msleep(own_init[i].val);
		else
			wm8(own_init[i].reg, own_init[i].mask, own_init[i].val);
	}
	wm8(A(0x2d31), 2, 0);		/* power on */
	w8(A(0x2b42), 0);
}

/* HDMI audio path (banks 0x1131/0x1133), as in mstar_audio.h */
static void own_hdmi_on(void)
{
	volatile uint16_t *b31 = (volatile uint16_t *)(riu + 0x113100 * 2);
	volatile uint16_t *b33 = (volatile uint16_t *)(riu + 0x113300 * 2);
#define H31(n) b31[(n) * 2]
#define H33(n) b33[(n) * 2]
	H33(5) = (H33(5) & ~0x70) | 0x60;
	H33(0) = 0; H33(1) = 0; H33(2) = 2 << 4; H33(3) = 2; H33(4) = 0;
	H31(6) &= ~0x0f00; H31(7) = 6144;
	H33(5) = (H33(5) & ~0x1087) | 0x1086;
	H31(8) = (H31(8) & ~0xff) | 5;
	H33(5) = (H33(5) & ~0x81) | 0x80;
	H31(0x11) = 1; H31(0x12) &= ~0xff00; H31(0x14) = (0x70 << 8) | 5;
	H33(6) = 0x2100;
#undef H31
#undef H33
}

/* IDMA write of 24-bit words to SE memory: dm = 1 for data memory (address | 0x8000) */
static int own_idma(int dm, uint16_t addr, const uint32_t *w, unsigned n)
{
	wm8(A(0x2a7e), 1, 1);			/* IDMA port to the SE */
	w16(A(0x2ac0), 0);
	w16(A(0x2a84), addr | (dm ? 0x8000 : 0));
	for (unsigned i = 0; i < n; i++) {
		int k;

		w8(A(0x2a82), w[i] >> 8);
		w8(A(0x2a83), w[i] >> 16);
		w8(A(0x2a82), w[i]);
		w8(A(0x2a83), 0);
		for (k = 0; k < 100000 && (r8(A(0x2a80)) & 0x10); k++)
			;
		if (k == 100000)
			return 0;
	}
	return 1;
}

static void own_se_reset(void)
{
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
}

static struct {
	unsigned channels;
	uint64_t wpos, rpos;		/* frames written / played (unwrapped) */
	uint16_t last;
} oa;

static double own_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void own_track(void)
{
	uint16_t c = r16(A(0x2de0));

	oa.rpos += (uint16_t)(c - oa.last);
	oa.last = c;
	if (oa.wpos < oa.rpos)		/* underrun: restart just ahead of the DSP */
		oa.wpos = oa.rpos;
}

static int maudio_open(unsigned rate, unsigned channels)
{
	static uint32_t zero[OWN_FRAMES * 2];
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	unsigned n = sizeof(own_pcm_prog) / sizeof(own_pcm_prog[0]);

	if (fd < 0 || rate != 48000 || channels < 1 || channels > 2)
		return 0;
	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	if (riu == MAP_FAILED)
		return 0;
	own_audio_init();
	own_hdmi_on();
	*rw(0x10253c) = (*rw(0x10253c) & ~0x30);	/* GPIO44 low: analog (SCART) unmuted */
	own_se_reset();
	/* program words 1.., the zeroed ring, then word 0 (the SE starts on it) */
	if (!own_idma(0, 1, own_pcm_prog + 1, n - 1) || !own_idma(1, OWN_RING, zero, OWN_FRAMES * 2) ||
	    !own_idma(0, 0, own_pcm_prog, 1))
		return 0;
	oa.channels = channels;
	oa.wpos = oa.rpos = 0;
	msleep(5);
	oa.last = r16(A(0x2de0));
	double t0 = own_now();
	while (r16(A(0x2de0)) == oa.last)	/* the DSP counts frames: it runs */
		if (own_now() - t0 > 0.5)
			return 0;
	return 1;
}

static void maudio_write(const void *data, unsigned frames)
{
	const int16_t *p = data;
	static uint32_t w[OWN_FRAMES * 2];

	while (frames) {
		own_track();
		unsigned fill = oa.wpos - oa.rpos;

		if (fill >= OWN_LEAD) {
			usleep(2000);
			continue;
		}
		unsigned n = OWN_LEAD - fill;

		if (n > frames)
			n = frames;
		for (unsigned i = 0; i < n; i++) {
			int16_t l = p[i * oa.channels], r = p[i * oa.channels + oa.channels - 1];

			w[2 * i] = ((uint32_t)(int32_t)l << 8) & 0xffffff;
			w[2 * i + 1] = ((uint32_t)(int32_t)r << 8) & 0xffffff;
		}
		unsigned at = oa.wpos % OWN_FRAMES, first = n < OWN_FRAMES - at ? n : OWN_FRAMES - at;

		own_idma(1, OWN_RING + 2 * at, w, 2 * first);
		if (n > first)
			own_idma(1, OWN_RING, w + 2 * first, 2 * (n - first));
		oa.wpos += n;
		p += n * oa.channels;
		frames -= n;
	}
}

static void maudio_close(void)
{
	static const int16_t silence[2 * 256];
	static const uint32_t rti = 0x0a001f;

	for (int i = 0; i < OWN_FRAMES / 256 + 1; i++)		/* let the ring play out */
		maudio_write(silence, 512 / oa.channels);
	own_se_reset();
	own_idma(0, 0, &rti, 1);		/* stop: a lone RTI at 0 */
}
#endif
