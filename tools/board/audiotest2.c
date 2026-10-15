/*
 * audiotest2 - audio on the MStar Kronus (MSD7818) from Linux, second step:
 * start the SE and DEC DSPs with the stock firmware's "K1 system" code, like
 * MApi_AUDIO_Initialize of the stock firmware, then optionally play a sine
 * through the DMA Reader. See docs/audio.md for the sources of every step.
 *
 *   audiotest2            init + start both DSPs, report their "alive" bytes
 *   audiotest2 play [S] [HZ]   ... then play a sine for S seconds
 *
 * DSP memory in DRAM (stock: SE 0x3A00000, DEC 0x3B00000) is moved into the
 * region Linux already reserves ("tsukasa", 0x3C00000-0x4000000):
 * SE 0x3C00000, DEC 0x3D00000; the DMA Reader ring is DEC base + 0x130000.
 * Build on the board: gcc -O2 -o audiotest2 audiotest2.c -lm
 */
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "audio_init_table.h"
#include "dsp_fw.h"

#define RIU_PHYS	0x1f000000
#define RIU_SIZE	0x400000
#define DSPMEM_PHYS	0x3c00000
#define DSPMEM_SIZE	0x400000
#define SE_BASE		0x3c00000
#define DEC_BASE	0x3d00000
#define RING_OFF	0x130000		/* from DEC base, HAL_AUDIO_DmaReader_Init */
#define RING_SIZE	0x20000
#define RATE		48000

static volatile uint8_t *riu, *dspmem;

/* RIU byte register (0x11xxxx etc.) and 16-bit register helpers */
static inline volatile uint8_t *rb(uint32_t reg) { return riu + reg * 2 - (reg & 1); }
static void w8(uint32_t reg, uint8_t v) { *rb(reg) = v; }
static void wm8(uint32_t reg, uint8_t m, uint8_t v) { *rb(reg) = (*rb(reg) & ~m) | (v & m); }
static uint8_t r8(uint32_t reg) { return *rb(reg); }
static inline volatile uint16_t *rw(uint32_t reg) { return (volatile uint16_t *)(riu + reg * 2); }
static void w16(uint32_t reg, uint16_t v) { *rw(reg) = v; }
static void wm16(uint32_t reg, uint16_t m, uint16_t v) { *rw(reg) = (*rw(reg) & ~m) | (v & m); }
static uint16_t r16(uint32_t reg) { return *rw(reg); }
#define A(x)	(0x110000 + (x))		/* audio-bank register as used by the stock HAL */

static void msleep(int ms) { usleep(ms * 1000); }
static volatile uint8_t *dram(uint32_t phys) { return dspmem + (phys - DSPMEM_PHYS); }

static void audio_basic_init(void)
{
	/* MDrv_AUDIO_Init */
	w8(A(0x2c00), 3); msleep(2); w8(A(0x2c00), 0); msleep(1);
	/* MDrv_AUDIO_WriteInitTable */
	for (unsigned i = 0; i < sizeof(audio_init_table) / sizeof(audio_init_table[0]); i++) {
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
	/* MDrv_AUDIO_SetPowerOn(1) */
	wm8(A(0x2d31), 2, 0);
	w8(A(0x2b42), 0);
}

/* HAL_MAD_SetMemInfo (DEC) */
static void mad_setmeminfo_dec(uint32_t base)
{
	uint16_t b = (base >> 11) & 0xffff;

	wm16(A(0x2a20), 0xff, 0); wm16(A(0x2a20), 1, 0);
	wm8(A(0x2a22), 0x80, ((base >> 27) & 1) << 7);
	wm8(A(0x2a40), 1, base >> 27);
	w16(A(0x2a10), b); w16(A(0x2a24), b);
	wm16(A(0x2a2a), 0xff, 2); w16(A(0x2a26), 0);     wm16(A(0x2a28), 0xff00, 0x1f00);
	wm16(A(0x2a2a), 0xff, 3); w16(A(0x2a26), 0x20);  wm16(A(0x2a28), 0xff00, 0x0f00);
	wm16(A(0x2a2a), 0xff, 4); w16(A(0x2a26), 0x130); wm16(A(0x2a28), 0xff00, 0xd700);
	wm16(A(0x2a2a), 0xff, 5); w16(A(0x2a26), 0x354); wm16(A(0x2a28), 0xff00, 0x0f00);
	wm16(A(0x2a20), 0x40, 0x40); wm16(A(0x2a20), 0x80, 0x80);
	wm16(A(0x2a20), 0x80, 0);    wm16(A(0x2a20), 0x40, 0);
}

/* HAL_MAD2_SetMemInfo (SE) */
static void mad2_setmeminfo_se(uint32_t base)
{
	uint16_t b = (base >> 11) & 0xffff;

	wm16(A(0x2aa0), 0xff, 0);
	wm8(A(0x2aa2), 0x80, ((base >> 27) & 1) << 7);
	wm8(A(0x2ac0), 1, base >> 27);
	w16(A(0x2a90), b);
	wm16(A(0x2aa0), 0xffff, 0);
	w16(A(0x2aa4), b);
	wm16(A(0x2aaa), 0xff, 2); w16(A(0x2aa6), 0x150); wm16(A(0x2aa8), 0xff00, 0x0f00);
	wm16(A(0x2aaa), 0xff, 4); w16(A(0x2aa6), 0);     wm16(A(0x2aa8), 0xff00, 0x2f00);
	wm16(A(0x2aa0), 0x40, 0x40); wm16(A(0x2aa0), 0x80, 0x80);
	msleep(1);
	wm16(A(0x2aa0), 0x80, 0);    wm16(A(0x2aa0), 0x40, 0);
}

/* IDMA write of len bytes (3 per 24-bit word) to DSP address addr (0x800bde84) */
static int idma_write(int se, uint16_t addr, const uint8_t *buf, unsigned len)
{
	uint32_t ctl = se ? A(0x2ac0) : A(0x2a40), adr = se ? A(0x2a84) : A(0x2a04);
	uint32_t d0 = se ? A(0x2a82) : A(0x2a02), d1 = se ? A(0x2a83) : A(0x2a03);
	uint32_t busy = se ? A(0x2a80) : A(0x2a00);

	wm8(A(0x2a7e), 1, se);
	w16(ctl, 0);
	if (!len)
		return 1;
	w16(adr, addr);
	msleep(10);
	for (unsigned i = 0; i < len; i += 3) {
		int t;

		w8(d0, buf[i + 1]);
		w8(d1, buf[i + 2]);
		w8(d0, buf[i]);
		w8(d1, 0);
		for (t = 0; t < 200 && (r8(busy) & 0x10); t++)
			if (se)
				usleep(1000);
		if (t == 200) {
			printf("IDMA timeout (%s) at byte %u\n", se ? "SE" : "DEC", i);
			return 0;
		}
	}
	return 1;
}

/* HAL_AUDSP_DspLoadCode for the "K1 system" codes (0x50 DEC / 0x60 SE) */
static int load_system(int se)
{
	const uint8_t *cm = se ? se_cm : dec_cm, *pm = se ? se_pm : dec_pm, *x1 = se ? se_x1 : dec_x1;
	unsigned cm_len = se ? SE_CM_LEN : DEC_CM_LEN, pm_len = se ? SE_PM_LEN : DEC_PM_LEN;
	unsigned x1_len = se ? SE_X1_LEN : DEC_X1_LEN;
	uint32_t base = se ? SE_BASE : DEC_BASE;

	wm8(A(0x2a7e), 1, se);
	if (!idma_write(se, se ? SE_PM_ADDR : DEC_PM_ADDR, pm, pm_len))
		return 0;
	/* code from DSP word 8 on; words 0..7 (the boot vector) go last */
	if (!idma_write(se, 8, cm + 0x18, cm_len - 0x18))
		return 0;
#if SE_X2_LEN
	if (se)
		memcpy((void *)dram(base + SE_X2_ADDR * 3), se_x2, SE_X2_LEN);
#endif
	memcpy((void *)dram(base + (se ? SE_X1_ADDR : DEC_X1_ADDR) * 3), x1, x1_len);
	__sync_synchronize();
	if (!idma_write(se, 1, cm + 3, 0x15))
		return 0;
	return idma_write(se, 0, cm, 3);
}

static int wait_alive(uint32_t reg, const char *name)
{
	for (int i = 0; i < 100; i++) {
		if (r8(reg) == 0xe3) {
			printf("%s DSP alive after %d ms (0x%06x = 0xE3)\n", name, i * 2, reg);
			return 1;
		}
		msleep(2);
	}
	printf("%s DSP did not answer (0x%06x = 0x%02x)\n", name, reg, r8(reg));
	return 0;
}

static void start_dsps(void)
{
	mad_setmeminfo_dec(DEC_BASE);
	mad2_setmeminfo_se(SE_BASE);
	/* MDrv_SOUND_Init: SE reset sequence, then SE system code */
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
	w8(A(0x2a80), 3);
	if (load_system(1)) {
		wait_alive(A(0x2dfc), "SE");
		wm16(A(0x2ddc), 0xff00, 0xf3 << 8);
	}
	/* MDrv_MAD_Init */
	wm16(A(0x2a00), 0xff, 2); msleep(2); wm16(A(0x2a00), 0xff, 3);
	/* MDrv_AUDIO_DecoderLoadCode: DEC system code */
	wm16(A(0x2a00), 3, 3);
	if (load_system(0)) {
		wait_alive(A(0x2dbd), "DEC");
		wm16(A(0x2d9c), 0xff00, 0xf3 << 8);
	}
}

static void hdmi_audio_on(void)
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
	H33(6) = 0x2100;			/* value seen while the stock player runs */
}

static void play(double secs, double freq)
{
	uint32_t ring = DEC_BASE + RING_OFF;
	volatile uint8_t *r = dram(ring);

	/* HAL_AUDIO_DmaReader_Init, PCM-to-output variant, 48 kHz */
	wm8(A(0x2cb0), 3, 3); w8(A(0x2c63), 0xc6); w8(A(0x2c6b), 0x83);
	w16(A(0x2b82), 0); w16(A(0x2b84), (ring >> 11) & 0xffff);
	w16(A(0x2b86), 0x4000); w16(A(0x2b88), 0xfff4); w16(A(0x2b8a), 0x12);
	wm8(A(0x2bcf), 8, 8); w16(A(0x2bb4), 216000000 / RATE);
	wm8(A(0x2bce), 8, 8); msleep(1); wm8(A(0x2bce), 8, 0);
	w16(A(0x2b80), 0x6400); msleep(1); w16(A(0x2b80), 0x2400);
	memset((void *)r, 0, RING_SIZE);
	hdmi_audio_on();

	uint32_t wp = 0;
	double ph = 0, dph = 2 * M_PI * freq / RATE;
	long total = (long)(secs * RATE), done = 0;
	int started = 0;

	while (done < total) {
		wm16(A(0x2b80), 0x10, 0x10);
		unsigned l = r16(A(0x2b8c));
		wm16(A(0x2b80), 0x10, 0);
		if (l < (0x20000 - 2 * 4096) / 8) {
			for (int i = 0; i < 1024; i++) {
				int16_t s = (int16_t)(8000 * sin(ph));

				ph += dph;
				for (int ch = 0; ch < 2; ch++) {
					r[wp] = 0; r[wp + 1] = s & 0xff; r[wp + 2] = (s >> 8) & 0xff; r[wp + 3] = 0;
					wp = (wp + 4) % RING_SIZE;
				}
			}
			done += 1024;
			w16(A(0x2b82), 4096 >> 2);
			if (!started) {
				wm16(A(0x2b80), 1, 1); msleep(1); wm16(A(0x2b80), 0x100, 0x100);
				started = 1;
			}
		} else {
			msleep(2);
		}
	}
	printf("played %.1f s at %.0f Hz\n", secs, freq);
}

int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	audio_basic_init();
	start_dsps();
	printf("DEC 0x2dbd=0x%02x SE 0x2dfc=0x%02x, 0x2a00=0x%04x 0x2a80=0x%04x\n",
	       r8(A(0x2dbd)), r8(A(0x2dfc)), r16(A(0x2a00)), r16(A(0x2a80)));
	if (argc > 1 && !strcmp(argv[1], "play"))
		play(argc > 2 ? atof(argv[2]) : 5, argc > 3 ? atof(argv[3]) : 1000);
	return 0;
}
