/*
 * logofix - bare-metal helper run from MBoot (`go`) before booting Linux.
 *
 * MBoot shows the boot logo on GOP3 from the same buffer Linux later uses as
 * its framebuffer (phys 0x2100000, 1920x1080), but in GOP format 9 = YUV 4:2:2
 * with byte order Y0 V Y1 U. `osd_create 1 1` only switches the window to
 * format 2, which is ARGB4444 (not RGB565, see EN_GOP_COLOR_TYPE in apiGOP.h),
 * without converting the pixels, which garbles the logo. Linux writes RGB565
 * (simple-framebuffer r5g6b5), so this program sets format 1 = RGB565.
 *
 * This program converts the logo to RGB565 and switches GOP3 without the
 * picture ever showing wrong colours:
 *   1. convert YVYU (displayed buffer) -> RGB565 into a scratch buffer
 *   2. at vsync: point GOP3 at the scratch buffer, format RGB565
 *   3. copy the scratch buffer back to 0x2100000
 *   4. at vsync: point GOP3 back at 0x2100000
 * It replaces `osd_create 1 1` in bootlinux. Returns to MBoot (rc 0 = done,
 * rc 1 = GOP3 was not in format 9, nothing changed).
 * Formats seen on the board: 1 RGB565, 2 ARGB4444, 9 YUV422.
 *
 * Registers (see docs/grafika_gop.md): RIU non-PM base 0xBF200000, GOP bank
 * select 0xBF203FFC, GOP3 control bank 9, GOP3 GWIN bank A.
 */
#include <stdint.h>

#define FB		((volatile uint16_t *)0xA2100000)	/* KSEG1, uncached */
#define TMP		((volatile uint16_t *)0xA2800000)	/* scratch, before kernel load */
#define FB_PHYS		0x2100000u
#define TMP_PHYS	0x2800000u
#define W		1920
#define H		1080

#define GOP_REG(off)	(*(volatile uint16_t *)(0xBF203E00u + (off)))	/* reg 0x1Fxx -> (xx*2) */
#define GOP_BANKSEL	GOP_REG(0x1FC)		/* 0x1FFE */
#define GWIN_CTRL	GOP_REG(0x000)		/* bank A 0x1F00, [7:4] format */
#define FMT_RGB565	1
#define FMT_YUV422	9
#define GWIN_ADDR_L	GOP_REG(0x004)		/* bank A 0x1F02, address / 8 */
#define GWIN_ADDR_H	GOP_REG(0x008)		/* bank A 0x1F04 */
#define GOP3_1F12	GOP_REG(0x024)		/* bank 9 0x1F12: 6 with YUV logo, 4 after osd_create */

#define BANK_GOP3_CTRL	0x9
#define BANK_GOP3_GWIN	0xA
#define BANK_KEEP	0x7000		/* upper bits as found at rest */
#define TRIGGER		0x0400		/* latch shadow registers at next vsync */
#define ACK_GOP3	0x8000		/* 1 = waiting for vsync */

static inline uint8_t clip(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* 4x4 ordered dither: without it the smooth grey gradients of the logo show
 * as rings in RGB565, tinted green/pink where G (6 bit) and R/B (5 bit)
 * round differently */
static const uint8_t bayer[4][4] = {
	{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static inline int q(int v, int d, int shift, int max)
{
	v = (clip(v) + (d >> (4 - shift))) >> shift;	/* d: 0..15 */
	return v > max ? max : v;
}

static inline uint16_t rgb565(int y, int cb, int cr, int d)
{
	/* JPEG full-range YCbCr -> RGB, 8.8 fixed point */
	int r = y + ((359 * cr) >> 8);
	int g = y - ((88 * cb + 183 * cr) >> 8);
	int b = y + ((454 * cb) >> 8);

	return q(r, d, 3, 31) << 11 | q(g, d, 2, 63) << 5 | q(b, d, 3, 31);
}

static void gop3_switch(uint32_t phys, int rgb)
{
	uint32_t a = phys >> 3;
	int t;

	GOP_BANKSEL = BANK_KEEP | BANK_GOP3_GWIN;
	GWIN_ADDR_L = a & 0xffff;
	GWIN_ADDR_H = a >> 16;
	if (rgb)
		GWIN_CTRL = (GWIN_CTRL & ~0x00f0) | FMT_RGB565 << 4;
	GOP_BANKSEL = BANK_KEEP | BANK_GOP3_CTRL;
	if (rgb)
		GOP3_1F12 = 0x0004;
	/* latch at vsync, like GOP_GWIN_TriggerRegWriteIn */
	GOP_BANKSEL = BANK_KEEP | TRIGGER | BANK_GOP3_CTRL;
	GOP_BANKSEL = BANK_KEEP | BANK_GOP3_CTRL;
	for (t = 0; t < 10000000 && (GOP_BANKSEL & ACK_GOP3); t++)
		;
	GOP_BANKSEL = BANK_KEEP;
}

__attribute__((section(".text.logofix"))) int logofix(void)
{
	int fmt, i, x, y;

	GOP_BANKSEL = BANK_KEEP | BANK_GOP3_GWIN;
	fmt = (GWIN_CTRL >> 4) & 0xf;
	GOP_BANKSEL = BANK_KEEP;
	if (fmt != FMT_YUV422)
		return 1;

	/* 1. YVYU -> RGB565 into the scratch buffer (logo stays on screen) */
	for (y = 0, i = 0; y < H; y++) {
		const uint8_t *d = bayer[y & 3];

		for (x = 0; x < W; x += 2, i += 2) {
			uint16_t w0 = FB[i], w1 = FB[i + 1];	/* bytes: Y0 V | Y1 U */
			int y0 = w0 & 0xff, cr = (w0 >> 8) - 128;
			int y1 = w1 & 0xff, cb = (w1 >> 8) - 128;

			TMP[i] = rgb565(y0, cb, cr, d[x & 3]);
			TMP[i + 1] = rgb565(y1, cb, cr, d[(x + 1) & 3]);
		}
	}

	/* 2. show the converted copy */
	gop3_switch(TMP_PHYS, 1);

	/* 3. put it where Linux expects its framebuffer */
	for (i = 0; i < W * H; i++)
		FB[i] = TMP[i];

	/* 4. back to 0x2100000, now RGB565 */
	gop3_switch(FB_PHYS, 0);
	return 0;
}
