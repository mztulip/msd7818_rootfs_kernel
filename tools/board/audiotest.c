/*
 * audiotest - first attempt at sound on the MStar Kronus (MSD7818) from Linux:
 * audio block init + DMA Reader PCM playback + HDMI TX audio, no DSP firmware.
 * Everything replicates the stock eCos firmware / MStar K1 HDMITX library,
 * see docs/audio.md.
 *
 *   audiotest [SECONDS] [FREQ_HZ] [skipinit]
 *
 * Plays a sine (default 5 s, 1000 Hz) at 48 kHz stereo. The PCM ring buffer
 * is at phys 0x3C30000 (128 KB, inside the reserved "tsukasa" region).
 * Build on the board: gcc -O2 -o audiotest audiotest.c -lm
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

#define RIU_PHYS	0x1f000000
#define RIU_SIZE	0x400000
#define RING_PHYS	0x3c30000
#define RING_SIZE	0x20000		/* 128 KB, as in the stock firmware */
#define RATE		48000

static volatile uint8_t *riu;
static volatile uint8_t *ring;

/* byte register "reg" in RIU space (0x11xxxx etc.): CPU addr = reg*2 - (reg&1) */
static inline volatile uint8_t *rb(uint32_t reg) { return riu + reg * 2 - (reg & 1); }
static void wmask8(uint32_t reg, uint8_t mask, uint8_t val) { *rb(reg) = (*rb(reg) & ~mask) | (val & mask); }
static uint8_t r8(uint32_t reg) { return *rb(reg); }
/* 16-bit register at even byte address */
static inline volatile uint16_t *rw(uint32_t reg) { return (volatile uint16_t *)(riu + reg * 2); }
static void wmask16(uint32_t reg, uint16_t mask, uint16_t val) { *rw(reg) = (*rw(reg) & ~mask) | (val & mask); }
static uint16_t r16(uint32_t reg) { return *rw(reg); }

/* HDMI TX helpers: bank 0x113100 / 0x113300, 16-bit reg n at bank*2 + n*4 */
static volatile uint16_t *hreg(uint32_t bank, int n) { return (volatile uint16_t *)(riu + bank * 2 + n * 4); }
static void hw(uint32_t bank, int n, uint16_t v) { *hreg(bank, n) = v; }
static void hmask(uint32_t bank, int n, uint16_t m, uint16_t v) { *hreg(bank, n) = (*hreg(bank, n) & ~m) | (v & m); }

static void msleep(int ms) { usleep(ms * 1000); }

static void audio_init(void)
{
	/* MDrv_AUDIO_Init */
	wmask8(0x112c00, 0xff, 3);
	msleep(2);
	wmask8(0x112c00, 0xff, 0);
	msleep(1);
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
		wmask8(reg, m, v);
	}
	/* MDrv_AUDIO_SetPowerOn(1) */
	wmask8(0x112d31, 2, 0);
	wmask8(0x112b42, 0xff, 0);
	printf("audio block initialised (%zu table entries)\n",
	       sizeof(audio_init_table) / sizeof(audio_init_table[0]));
}

static void dmareader_init(void)
{
	/* HAL_AUDIO_DmaReader_Init, PCM-to-output variant, 48 kHz */
	wmask8(0x112cb0, 3, 3);
	wmask8(0x112c63, 0xff, 0xc6);
	wmask8(0x112c6b, 0xff, 0x83);
	wmask16(0x112b82, 0xffff, 0);
	wmask16(0x112b84, 0xffff, (RING_PHYS >> 11) & 0xffff);
	wmask16(0x112b86, 0xffff, 0x4000);
	wmask16(0x112b88, 0xffff, 0xfff4);
	wmask16(0x112b8a, 0xffff, 0x12);
	wmask8(0x112bcf, 8, 8);
	wmask16(0x112bb4, 0xffff, 216000000 / RATE);	/* 0x1194 */
	wmask8(0x112bce, 8, 8);
	msleep(1);
	wmask8(0x112bce, 8, 0);
	wmask16(0x112b80, 0xffff, 0x6400);
	msleep(1);
	wmask16(0x112b80, 0xffff, 0x2400);
	memset((void *)ring, 0, RING_SIZE);
	printf("DMA reader: ring at 0x%x, reg 0x2b84 = 0x%04x, rate reg 0x%04x\n",
	       RING_PHYS, r16(0x112b84), r16(0x112bb4));
}

static void hdmi_audio_on(void)
{
	const uint32_t B31 = 0x113100, B33 = 0x113300;

	/* MHal_HDMITx_SetAudioFrequency(48K = 3, 2 ch, PCM) */
	hmask(B33, 5, 0x70, 0x60);			/* 2 channels */
	hw(B33, 0, 0);					/* PCM */
	hw(B33, 1, 0);
	hw(B33, 2, 2 << 4);				/* channel count */
	hw(B33, 3, 2);					/* IEC fs code 48 kHz */
	hw(B33, 4, 0);
	hmask(B31, 6, 0x0f00, 0);			/* N[19:16] */
	hmask(B31, 7, 0xffff, 6144);			/* N = 6144 for 48 kHz */
	/* MHal_HDMITx_SetAudioOnOff(1): HDMITxAudioOnTbl */
	hmask(B33, 5, 0x1087, 0x1086);
	/* SendPacket: ACR cyclic, audio samples on, audio infoframe */
	hmask(B31, 8, 0xff, 5);
	hmask(B33, 5, 0x81, 0x80);
	hw(B31, 0x11, 1);				/* CC: 2 channels */
	hmask(B31, 0x12, 0xff00, 0);
	hw(B31, 0x14, (0x70 << 8) | 5);			/* checksum 0x70, send every frame */
	printf("HDMI audio on: 0x1133 reg5 = 0x%04x, ACR 0x%04x, AIF 0x%04x\n",
	       *hreg(B33, 5), *hreg(B31, 8), *hreg(B31, 0x14));
}

static unsigned level(void)
{
	unsigned l;

	wmask16(0x112b80, 0x10, 0x10);
	l = r16(0x112b8c);
	wmask16(0x112b80, 0x10, 0);
	return l;		/* in 8-byte units */
}

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 5;
	double freq = argc > 2 ? atof(argv[2]) : 1000;
	int skipinit = argc > 3 && !strcmp(argv[3], "skipinit");
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	ring = mmap(0, RING_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RING_PHYS);
	if (riu == MAP_FAILED || ring == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	if (!skipinit)
		audio_init();
	dmareader_init();
	hdmi_audio_on();

	/* fill the ring: [0, lo, hi, 0] per 16-bit sample, L R interleaved */
	uint32_t wp = 0;
	double ph = 0, dph = 2 * M_PI * freq / RATE;
	long total = (long)(secs * RATE), done = 0;
	int started = 0;
	unsigned maxl = 0, minl = 0xffff;

	while (done < total) {
		unsigned l = level();

		if (started) {
			if (l > maxl) maxl = l;
			if (l < minl) minl = l;
		}
		/* same condition as WritePCM for a 4096-byte chunk */
		if (l < (0x20000 - 2 * 4096) / 8) {
			for (int i = 0; i < 1024; i++) {	/* 1024 frames = 4096 B of 16-bit stereo */
				int16_t s = (int16_t)(8000 * sin(ph));

				ph += dph;
				for (int ch = 0; ch < 2; ch++) {
					ring[wp + 0] = 0;
					ring[wp + 1] = s & 0xff;
					ring[wp + 2] = (s >> 8) & 0xff;
					ring[wp + 3] = 0;
					wp = (wp + 4) % RING_SIZE;
				}
			}
			done += 1024;
			wmask16(0x112b82, 0xffff, (4096 >> 2) & 0xffff);
			if (!started) {
				wmask16(0x112b80, 1, 1);
				msleep(1);
				wmask16(0x112b80, 0x100, 0x100);
				started = 1;
				printf("started, 0x2b80 = 0x%04x\n", r16(0x112b80));
			}
		} else {
			msleep(2);
		}
	}
	printf("done: %ld frames, buffer level min %u max %u (x8 B), 0x2b80 = 0x%04x\n",
	       done, minl, maxl, r16(0x112b80));
	return 0;
}
