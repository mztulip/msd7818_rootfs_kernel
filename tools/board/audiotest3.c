/*
 * audiotest3 - PCM through the DEC DSP, the way the stock media player plays
 * a WAV file (docs/audio.md, "Odtwarzanie WAV w stocku"):
 *   1. audio block init + SE/DEC "K1 system" code (as audiotest2),
 *   2. reload the DEC decoder with xPCM (type 0x0b): cmd 0xF0 -> 0x33,
 *      IDMA PM + CM, cmd 0xF1 -> 0x77 (HAL_AUDIO_AlgReloadCode),
 *   3. xPCM params in DSP DM 0x37A0.. (format 1, channels-1, rate, bits),
 *   4. decoder command PLAYFILE (0x2A2C = 4),
 *   5. on every DSP request (counter 0x2DAC changes) copy 0x2D6C*8 bytes of
 *      16-bit LE stereo PCM to DEC base + 0x2D6A*8 and report "data ready":
 *      0x2D8C = n++, 0x2D8B = 0xE0, toggle 0x2A00 bit 5.
 * The output path / SE / HDMI registers are set to the values the stock
 * firmware has while it plays.
 *
 *   audiotest3 [SECONDS] [FREQ_HZ] [RATE] [LEAD]  default 5 s, 1000 Hz, 48000, 32768 B
 * The DSP asks for data whenever there is room, faster than it plays, so the
 * feeding is paced to real time + LEAD bytes.
 * Build on the board: gcc -O2 -o audiotest3 audiotest3.c -lm
 */
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "audio_init_table.h"
#include "dsp_fw.h"
#include "xpcm_fw.h"
#include "se31_fw.h"
#include "dec03_fw.h"

#define RIU_PHYS	0x1f000000
#define RIU_SIZE	0x400000
#define DSPMEM_PHYS	0x3c00000
#define DSPMEM_SIZE	0x400000
#define SE_BASE		0x3c00000
#define DEC_BASE	0x3d00000
#define ES_SIZE		0x10000		/* ES ring = DEC base + 0..64 KB */

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

/* MDrv_SOUND_Init loads the SE decoder 0x31 before the SE system code */
static int load_se31(void)
{
	wm8(A(0x2a7e), 1, 1);
	if (!idma_write(1, SE31_PM_ADDR, se31_pm, SE31_PM_LEN) ||
	    !idma_write(1, SE31_CM_ADDR, se31_cm, SE31_CM_LEN))
		return 0;
	memcpy((void *)dram(SE_BASE + SE31_X1_ADDR * 3), se31_x1, SE31_X1_LEN);
	memcpy((void *)dram(SE_BASE + SE31_X2_ADDR * 3), se31_x2, SE31_X2_LEN);
	__sync_synchronize();
	printf("SE code 0x31 loaded\n");
	return 1;
}

/* MDrv_MAD_Init loads the DEC decoder 0x03 before the DEC system code; without
 * it a cold-booted DEC runs into empty memory (a warm reset keeps the stock's) */
static int load_dec03(void)
{
	wm8(A(0x2a7e), 1, 0);
	if (!idma_write(0, DEC03_PM_ADDR, dec03_pm, DEC03_PM_LEN) ||
	    !idma_write(0, DEC03_CM_ADDR, dec03_cm, DEC03_CM_LEN))
		return 0;
	memcpy((void *)dram(DEC_BASE + DEC03_X1_ADDR * 3), dec03_x1, DEC03_X1_LEN);
	memcpy((void *)dram(DEC_BASE + DEC03_X2_ADDR * 3), dec03_x2, DEC03_X2_LEN);
	__sync_synchronize();
	printf("DEC code 0x03 loaded\n");
	return 1;
}

static void start_dsps(void)
{
	mad_setmeminfo_dec(DEC_BASE);
	mad2_setmeminfo_se(SE_BASE);
	/* MDrv_SOUND_Init: SE reset sequence, then SE system code */
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
	w8(A(0x2a80), 3);
	if (load_se31() && load_system(1)) {
		wait_alive(A(0x2dfc), "SE");
		wm16(A(0x2ddc), 0xff00, 0xf3 << 8);
	}
	/* MDrv_MAD_Init */
	wm16(A(0x2a00), 0xff, 2); msleep(2); wm16(A(0x2a00), 0xff, 3);
	load_dec03();
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

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static int wait8(uint32_t reg, uint8_t val, int ms)
{
	for (int i = 0; i < ms; i++) {
		if (r8(reg) == val)
			return 1;
		msleep(1);
	}
	printf("timeout: 0x%06x = 0x%02x, expected 0x%02x\n", reg, r8(reg), val);
	return 0;
}

/* HAL_MAD write to DSP data memory (0x800100fc with "DM" = addr | 0x8000) */
static void dm_write(uint16_t addr, uint32_t val)
{
	wm8(A(0x2a7e), 1, 0);
	wm16(0x103c14, 1, 0);
	addr |= 0x8000;
	w8(A(0x2a04), addr & 0xff);
	w8(A(0x2a05), addr >> 8);
	w8(A(0x2a02), val >> 8);
	w8(A(0x2a03), val >> 16);
	w8(A(0x2a02), val & 0xff);
	w8(A(0x2a03), 0);
	for (int t = 0; t < 1000 && (r8(A(0x2a00)) & 0x10); t++)
		;
}

/* HAL_AUDIO_Alg2ReloadCode (0x8000f71c) for the SIF code the stock init loads
 * into SE (0xb1): its pau_info is empty here, so only the handshake runs:
 * cmd 0xF0 -> 0x33, cmd 0xF1 -> 0x77, cmd 0. Without it SE stays at 0xF3. */
static void se_cmd(uint8_t c)
{
	wm16(A(0x2ddc), 0xff00, c << 8);
	wm8(A(0x2a80), 0x20, 0x20);
	msleep(2);
	wm8(A(0x2a80), 0x20, 0);
}

static int se_reload(void)
{
	wm16(A(0x2aa0), 0x40, 0x40); msleep(1); wm16(A(0x2aa0), 0x40, 0);
	w8(A(0x2a80), 3);
	se_cmd(0xf0);
	if (!wait8(A(0x2dfe), 0x33, 2000))
		return 0;
	w8(A(0x2a80), 3);
	se_cmd(0xf1);
	if (!wait8(A(0x2dfe), 0x77, 3000))
		return 0;
	wm16(A(0x2ddc), 0xff00, 0);
	printf("SE reload handshake done (0x2dfe = 0x%02x, 0x2dfb = 0x%02x)\n", r8(A(0x2dfe)), r8(A(0x2dfb)));
	return 1;
}

/* MAD command to the DEC system code: 0x2D9C high byte + pulse 0x2A00 bit 5 */
static void dec_cmd(uint8_t c)
{
	wm16(A(0x2d9c), 0xff00, c << 8);
	wm16(A(0x2a00), 0x20, 0x20);
	msleep(2);
	wm16(A(0x2a00), 0x20, 0);
}

static void set_deccmd(uint8_t c)		/* 0x8001056c: 0 stop, 1 play, 4 playfile */
{
	wm8(A(0x2a2c), 0x3f, (r8(A(0x2a2c)) & 0x20) | c);
}

/* HAL_MAD_SetDecoderType(9) -> AlgReloadCode(0x0b) (0x8000f348/0x80013628/0x8000f5bc) */
static int load_xpcm(void)
{
	w8(A(0x2d2c), 0);
	set_deccmd(0);
	wm16(A(0x2a20), 0x40, 0x40); msleep(1); wm16(A(0x2a20), 0x40, 0);
	dec_cmd(0xf0);
	if (!wait8(A(0x2dbe), 0x33, 2000))
		return 0;
	wm16(A(0x2a00), 3, 3);
	wm8(A(0x2a7e), 1, 0);
	if (!idma_write(0, XPCM_PM_ADDR, xpcm_pm, XPCM_PM_LEN) ||
	    !idma_write(0, XPCM_CM_ADDR, xpcm_cm, XPCM_CM_LEN))
		return 0;
	dec_cmd(0xf1);
	if (!wait8(A(0x2dbe), 0x77, 3000))
		return 0;
	wm8(A(0x2b01), 0x40, 0);
	wm16(A(0x2d9c), 0xff00, 0);
	wm16(A(0x2a7e), 0x8000, 0);
	/* 0x800128d8, branch matching the stock register values */
	wm8(A(0x2d8f), 0x10, 0x10); wm8(A(0x2d8e), 0x20, 0x20);
	wm8(A(0x2d86), 2, 0); wm8(A(0x2dd9), 0x40, 0);
	printf("xPCM decoder loaded (0x2dbe = 0x%02x, 0x2dbb = 0x%02x)\n", r8(A(0x2dbe)), r8(A(0x2dbb)));
	return 1;
}

/* registers that differ while the stock player plays (stock_regs_playing_wav.txt) */
static const struct { uint32_t reg; uint16_t val; } stock_out[] = {
	{ A(0x2c62), 0x8780 }, { A(0x2c6a), 0x0700 }, { A(0x2c82), 0x0030 }, { A(0x2c86), 0x0040 },
	{ A(0x2cc8), 0x38e0 }, { A(0x2d00), 0x0d20 }, { A(0x2d0a), 0x0c00 }, { A(0x2d0e), 0x0900 },
	{ A(0x2d2a), 0x87ff }, { A(0x2d50), 0x6676 }, { A(0x2d52), 0x0005 }, { A(0x2d54), 0x4460 },
	{ A(0x2dc2), 0x0180 }, { A(0x2d8e), 0x10a0 }, { A(0x2dca), 0x0080 },
	{ 0x113102, 0x0001 }, { 0x113106, 0x0029 }, { 0x113108, 0x0004 }, { 0x113120, 0x6305 },
	{ 0x11330c, 0x2100 }, { 0x11330e, 0x3838 },
};

static void status(const char *tag)
{
	printf("%s: cmd 0x2a2c=%x dec 0x2dbb=%02x req 0x2dac=%04x addr 0x2d6a=%04x size 0x2d6c=%04x "
	       "0x2db2=%x t 0x2d66=%x 0x2d68=%x 0x2dba=%04x\n", tag,
	       r16(A(0x2a2c)), r8(A(0x2dbb)), r16(A(0x2dac)), r16(A(0x2d6a)), r16(A(0x2d6c)),
	       r16(A(0x2db2)), r16(A(0x2d66)), r16(A(0x2d68)), r16(A(0x2dba)));
}

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 5, freq = argc > 2 ? atof(argv[2]) : 1000;
	unsigned rate = argc > 3 ? atoi(argv[3]) : 48000;
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	audio_basic_init();
	/* the SE/output settings must be in place before the DSPs start: the
	 * first run after a cold boot was silent when they were written only
	 * after the SE handshake (the second run, finding them set, played) */
	for (unsigned i = 0; i < sizeof(stock_out) / sizeof(stock_out[0]); i++)
		w16(stock_out[i].reg, stock_out[i].val);
	start_dsps();
	if (r8(A(0x2dfc)) != 0xe3 || !se_reload())
		return 1;
	if (r8(A(0x2dbd)) != 0xe3 || !load_xpcm())
		return 1;

	/* HAL_MAD_XPCM_setParam: format 1 (PCM), channels - 1, rate, bits */
	dm_write(0x37a0, 1); dm_write(0x37a1, 1); dm_write(0x37a2, rate);
	dm_write(0x37a3, 16); dm_write(0x37a4, 0); dm_write(0x37a5, 0);
	w16(A(0x2d8a), 0);			/* mailbox 5: no short last chunk */
	for (unsigned i = 0; i < sizeof(stock_out) / sizeof(stock_out[0]); i++)
		w16(stock_out[i].reg, stock_out[i].val);
	hdmi_audio_on();
	memset((void *)dram(DEC_BASE), 0, ES_SIZE);
	status("before play");

	/* PLAYFILE */
	wm8(A(0x2c60), 7, 0);
	set_deccmd(4);

	volatile uint8_t *es = dram(DEC_BASE);
	uint16_t lastreq = r16(A(0x2dac));
	uint8_t n = 1;
	unsigned feeds = 0;
	double fed = 0, lead = argc > 4 ? atoi(argv[4]) : 32768;
	double ph = 0, dph = 2 * M_PI * freq / rate, t0 = now(), tlast = t0, tstat = t0;

	while (now() - t0 < secs) {
		uint16_t req = r16(A(0x2dac));

		/* pace: stay at most LEAD bytes ahead of real time, or the ring overruns */
		int room = fed < (now() - t0) * rate * 4 + lead;

		if (room && (req != lastreq || (feeds == 0 && now() - tlast > 0.3))) {
			uint32_t off = r16(A(0x2d6a)) * 8u, size = r16(A(0x2d6c)) * 8u;

			if (req == lastreq)
				printf("no request seen, feeding anyway\n");
			lastreq = req;
			if (!size || size > ES_SIZE)
				size = 4096;
			for (uint32_t i = 0; i < size; i += 4) {
				int16_t s = (int16_t)(8000 * sin(ph));

				ph += dph;
				for (int b = 0; b < 4; b++)	/* L, R */
					es[(off + i + b) % ES_SIZE] = (b & 1) ? s >> 8 : s & 0xff;
			}
			__sync_synchronize();
			w8(A(0x2d8c), n++);
			w8(A(0x2d8b), 0xe0);
			uint8_t v = r8(A(0x2a00));
			wm8(A(0x2a00), 0x20, v ^ 0x20);
			wm8(A(0x2a00), 0x20, v);
			feeds++;
			fed += size;
			tlast = now();
		} else {
			usleep(300);
		}
		if (now() - tstat > 1) {
			tstat = now();
			printf("[%u feeds] ", feeds);
			status("play");
		}
	}
	set_deccmd(0);
	status("stopped");
	printf("%u chunks fed in %.1f s\n", feeds, secs);
	return 0;
}
