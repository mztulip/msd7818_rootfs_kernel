/*
 * bootsel - small boot menu started by MBoot (`go`), docs/plan_bootloader.md.
 *
 * MBoot does what it already can (watchdog off, USB, boot logo, logofix) and
 * optionally loads a kernel from the USB stick to USB_ADDR. bootsel then
 *   - shows a menu on UART0, on the screen (over the boot logo) and on the
 *     front-panel LED display: 1 = system from flash, 2 = kernel from the
 *     stick (only if one was loaded), default 1 after TIMEOUT_S seconds.
 *     UART: 1, 2, Enter. Panel: CH+/CH- move, POWER boots. Any key stops
 *     the countdown;
 *   - flash: decodes the LZMA kernel straight from the memory mapped flash,
 *     checks the CRC32 in its last 4 bytes (same format as for MBoot LzmaDec),
 *     copies it to 0x80000000 and jumps there;
 *   - stick: copies the kernel from USB_ADDR to 0x80000000 and jumps there.
 * Our kernels start with "j kernel_entry" (CONFIG_BOOT_RAW), which is how a
 * loaded kernel is recognised. On an error bootsel returns to MBoot (rc != 0),
 * so `bootcmd` can fall back to `run flashboot`.
 *
 * MBoot usage (test from RAM, see build.sh):
 *   mw.l 0x81000000 0 1; fatload usb 0 0x81000000 vmlinux.bin
 *   run flashlogo; fatload usb 0 0x80F00400 bootsel.bin; go 0x80F00400
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include "lzma.h"
#include "font.h"

/* UART0, 16550 with registers 8 bytes apart (DTS reg-shift = 3) */
#define UART		0xBF201300u
#define UART_DATA	(*(volatile uint32_t *)(UART + 0x00))
#define UART_LSR	(*(volatile uint32_t *)(UART + 0x28))
#define LSR_DR		0x01
#define LSR_THRE	0x20

#define KERNEL_DST	0x80000000u
#define USB_ADDR	0x81000000u	/* kernel loaded by MBoot fatload */
#define USB_COPY	0x00E00000u	/* bytes copied from USB_ADDR (< 16 MB, no overlap issue) */
#define FLASH_KERNEL	0x9FC80000u	/* flash 0x80000 through the cached window */
#define FLASH_KMAX	0x00150000u	/* kernel partition size */
#define TMP_ADDR	0x82A00000u	/* LZMA output, after the 2 gopfb buffers 0x2100000..0x2A00000 */
#define TMP_MAX		0x01000000u
#define TICKS_PER_S	276000000u	/* CP0 Count: 552 MHz / 2 */
#define TIMEOUT_S	3
#define KERNEL_MAX	0x00F00000u	/* the copy to 0x80000000 must stay below bootsel itself */

/* boot logo framebuffer (GOP3, RGB565 after logofix), uncached like logofix */
#define FB		((volatile uint16_t *)0xA2100000u)
#define FB_W		1920
#define FB_H		1080
#define COLS		40		/* text columns in the menu box */
#define ROWS		8
#define PAD		24
#define BOX_W		(COLS * FONT_W + 2 * PAD)
#define BOX_H		(ROWS * FONT_H + 2 * PAD)
#define BOX_X		((FB_W - BOX_W) / 2)
#define BOX_Y		((FB_H - BOX_H) / 2)
#define C_BG		0x10A4u		/* RGB565 */
#define C_BORDER	0x9CF3u
#define C_TEXT		0xFFFFu
#define C_DIM		0x8410u
#define C_SEL		0x2B9Bu
#define C_ERR		0xF9A6u

/* front panel FD650 bit-banged on SAR pins (docs/przyciski_sar.md, tools/board/frontpanel.c) */
#define PM		((volatile uint8_t *)0xBF002000u)
#define SAR_ANA		0x844
#define GPIO_OEN	0x845		/* 1 = input */
#define GPIO_OUT	0x848
#define GPIO_IN		0x849
#define DIO		0x02
#define CLK		0x04
#define KEY_DOWN	0x40		/* bit 6 = pressed */
#define KEY_POWER	0x17
#define KEY_CHUP	0x0F
#define KEY_CHDN	0x07
#define PANEL_POLL	(TICKS_PER_S / 20)

static struct lzma_probs probs;
static uint32_t crc_table[256];

static void putc_(char c)
{
	while (!(UART_LSR & LSR_THRE))
		;
	UART_DATA = c;
}

static void puts_(const char *s)
{
	while (*s) {
		if (*s == '\n')
			putc_('\r');
		putc_(*s++);
	}
}

static void puthex(uint32_t v)
{
	puts_("0x");
	for (int i = 28; i >= 0; i -= 4)
		putc_("0123456789abcdef"[(v >> i) & 0xf]);
}

static int getc_nowait(void)
{
	return UART_LSR & LSR_DR ? (int)(UART_DATA & 0xff) : -1;
}

static uint32_t count(void)
{
	uint32_t c;

	__asm__ volatile("mfc0 %0, $9" : "=r"(c));
	return c;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
	uint32_t c = 0xFFFFFFFF;

	for (uint32_t i = 0; i < 256; i++) {
		uint32_t t = i;

		for (int k = 0; k < 8; k++)
			t = t & 1 ? 0xEDB88320 ^ (t >> 1) : t >> 1;
		crc_table[i] = t;
	}
	while (n--)
		c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return ~c;
}

/* ---- screen ---- */

static void fill(int x, int y, int w, int h, uint16_t c)
{
	for (int j = 0; j < h; j++) {
		volatile uint16_t *p = FB + (y + j) * FB_W + x;

		for (int i = 0; i < w; i++)
			p[i] = c;
	}
}

/* text at text cell (col, row) of the menu box, padded with bg up to the end of the line */
static void text(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
	int x = BOX_X + PAD + col * FONT_W, y = BOX_Y + PAD + row * FONT_H;

	for (; col < COLS; col++, x += FONT_W) {
		char c = *s ? *s++ : ' ';
		const uint16_t *g = font[(c < 32 || c > 126 ? '?' : c) - 32];

		for (int j = 0; j < FONT_H; j++) {
			volatile uint16_t *p = FB + (y + j) * FB_W + x;

			for (int i = 0; i < FONT_W; i++)
				p[i] = g[j] & (0x8000 >> i) ? fg : bg;
		}
	}
}

static void screen_box(void)
{
	fill(BOX_X - 3, BOX_Y - 3, BOX_W + 6, BOX_H + 6, C_BORDER);
	fill(BOX_X, BOX_Y, BOX_W, BOX_H, C_BG);
}

static void screen_status(const char *s, uint16_t fg)
{
	text(0, ROWS - 1, s, fg, C_BG);
}

/* ---- front panel ---- */

static void dly(void)
{
	uint32_t t0 = count();

	while (count() - t0 < TICKS_PER_S / 100000)	/* ~10 us */
		;
}

static void pin(uint8_t m, int high)
{
	if (high)
		PM[GPIO_OUT] |= m;
	else
		PM[GPIO_OUT] &= ~m;
	PM[GPIO_OEN] &= ~m;
}

static int dio_in(void)
{
	PM[GPIO_OEN] |= DIO;
	return !!(PM[GPIO_IN] & DIO);
}

static void fd_start(void)
{
	pin(DIO, 1); dly(); pin(CLK, 1); dly();
	pin(DIO, 0); dly(); pin(CLK, 0); dly();
}

static void fd_stop(void)
{
	pin(DIO, 0); dly(); pin(CLK, 1); dly();
	pin(DIO, 1); dly();
}

static void fd_send(uint8_t b)
{
	for (int i = 0; i < 8; i++, b <<= 1) {
		pin(DIO, b & 0x80); dly();
		pin(CLK, 1); dly();
		pin(CLK, 0); dly();
	}
	dio_in(); dly();			/* 9th clock: ACK from the chip */
	pin(CLK, 1); dly();
	pin(CLK, 0); dly();
}

static uint8_t fd_recv(void)
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
	pin(DIO, 0); dly();			/* ACK from the master */
	pin(CLK, 1); dly();
	pin(CLK, 0); dly();
	return v;
}

static void fd_cmd(uint8_t a, uint8_t b)
{
	fd_start();
	fd_send(a);
	fd_send(b);
	fd_stop();
}

static uint8_t fd_key(void)
{
	uint8_t k;

	fd_start();
	fd_send(0x4F);
	k = fd_recv();
	fd_stop();
	return k;
}

/* 7-segment glyphs of this board (stock firmware table 0x8038283C), only what we show */
static uint8_t seg(char c)
{
	static const char ch[] = "0123456789FLUSbotEr ";
	static const uint8_t sg[] = { 0xd7, 0x14, 0xcd, 0x5d, 0x1e, 0x5b, 0xdb, 0x15, 0xdf, 0x5f,
				      0x8b, 0xc2, 0xd6, 0x5b, 0xda, 0xd8, 0xca, 0xcb, 0xc8, 0x00 };

	for (int i = 0; ch[i]; i++)
		if (ch[i] == c)
			return sg[i];
	return 0x08;				/* '-' */
}

static void panel_show(const char *s)
{
	static const uint8_t reg[4] = { 0x6E, 0x6C, 0x6A, 0x68 };	/* leftmost first */

	for (int i = 0; i < 4; i++)
		fd_cmd(reg[i], seg(*s ? *s++ : ' '));
}

static void panel_init(void)
{
	PM[SAR_ANA] &= ~(DIO | CLK);		/* SAR pins 1, 2 as digital GPIO */
	pin(DIO, 1);
	pin(CLK, 1);
	fd_cmd(0x48, 0x11);			/* display on: also starts the key scan */
}

static int is_kernel(uint32_t addr)
{
	uint32_t w = *(volatile uint32_t *)addr;

	/* "j target" with a target inside the first 16 MB of KSEG0 */
	return (w >> 26) == 2 && ((w & 0x03FFFFFF) << 2) < 0x01000000;
}

/* write back and invalidate L1 D, invalidate L1 I (index ops over the whole caches) */
static void flush_caches(void)
{
	uint32_t c1, line, size;

	__asm__ volatile("mfc0 %0, $16, 1" : "=r"(c1));
	line = ((c1 >> 10) & 7) ? 2u << ((c1 >> 10) & 7) : 0;
	size = (64u << ((c1 >> 13) & 7)) * line * (((c1 >> 7) & 7) + 1);
	for (uint32_t a = 0x80000000u; line && a < 0x80000000u + size; a += line)
		__asm__ volatile("cache 0x01, 0(%0)" : : "r"(a));	/* Index_Writeback_Inv_D */
	__asm__ volatile("sync");
	line = ((c1 >> 19) & 7) ? 2u << ((c1 >> 19) & 7) : 0;
	size = (64u << ((c1 >> 22) & 7)) * line * (((c1 >> 16) & 7) + 1);
	for (uint32_t a = 0x80000000u; line && a < 0x80000000u + size; a += line)
		__asm__ volatile("cache 0x00, 0(%0)" : : "r"(a));	/* Index_Invalidate_I */
	__asm__ volatile("sync; ehb");
}

static void __attribute__((noreturn)) boot(uint32_t src, uint32_t len)
{
	volatile uint32_t *d = (volatile uint32_t *)KERNEL_DST;
	const volatile uint32_t *s = (const volatile uint32_t *)src;

	puts_("bootsel: starting the kernel\n\n");
	screen_status("Starting the kernel...", C_TEXT);
	panel_show("boot");
	while (!(UART_LSR & 0x40))		/* TEMT: let the last character go out */
		;
	__asm__ volatile("di; ehb");		/* MBoot's vectors at 0x80000000 get overwritten */
	flush_caches();
	for (uint32_t i = 0; i < (len + 3) / 4; i++)
		d[i] = s[i];
	flush_caches();
	/* a0..a3 = 0: the kernel must not take MBoot's argc/argv for firmware arguments */
	((void (*)(long, long, long, long))KERNEL_DST)(0, 0, 0, 0);
	for (;;)
		;
}

static int fail(int rc, const char *msg)
{
	puts_(msg);
	puts_("\n");
	screen_status(msg, C_ERR);
	panel_show("Err");
	return rc;
}

static int boot_flash(void)
{
	const uint8_t *in = (const uint8_t *)FLASH_KERNEL;
	uint8_t *out = (uint8_t *)TMP_ADDR;
	int64_t size = lzma_size(in);
	uint32_t len, crc, want;
	int r;

	puts_("bootsel: flash kernel, ");
	if (size < 8 || size > TMP_MAX)
		return fail(2, "Flash kernel: bad LZMA header");
	puthex((uint32_t)size);
	puts_(" bytes, decoding... ");
	screen_status("Decoding the flash kernel...", C_TEXT);
	r = lzma_decode(in, FLASH_KMAX, out, TMP_MAX, &len, &probs);
	if (r != LZMA_OK || len != size) {
		puthex((uint32_t)r);
		return fail(3, " Flash kernel: LZMA error");
	}
	want = out[len - 4] | out[len - 3] << 8 | out[len - 2] << 16 | (uint32_t)out[len - 1] << 24;
	crc = crc32(out, len - 4);
	if (crc != want) {
		puthex(crc);
		puts_(" != ");
		puthex(want);
		return fail(4, " Flash kernel: CRC error");
	}
	puts_("CRC ok\n");
	if (!is_kernel(TMP_ADDR) || len - 4 > KERNEL_MAX)
		return fail(5, "Flash kernel: not a kernel image");
	boot(TMP_ADDR, len - 4);
}

static void draw_menu(int usb, int sel)
{
	text(2, 2, "1  System from flash", C_TEXT, sel == 0 ? C_SEL : C_BG);
	if (usb)
		text(2, 3, "2  Kernel from USB stick", C_TEXT, sel == 1 ? C_SEL : C_BG);
	else
		text(2, 3, "   (no vmlinux.bin on USB)", C_DIM, C_BG);
}

static void show_countdown(int sel, int left)
{
	static char line[] = "Booting in 0 s";	/* static: no memcpy in freestanding code */
	static char led[] = "FL 0";

	if (left >= 0) {
		line[11] = '0' + left;
		screen_status(line, C_TEXT);
	} else {
		screen_status("", C_TEXT);
	}
	led[0] = sel ? 'U' : 'F';
	led[1] = sel ? 'S' : 'L';
	led[2] = sel ? 'b' : ' ';
	led[3] = left >= 0 ? '0' + left : ' ';
	panel_show(led);
}

int __attribute__((section(".text.bootsel"))) bootsel(void)
{
	int usb = is_kernel(USB_ADDR), n = usb ? 2 : 1, sel = 0, wait = 1, shown = -1;
	uint32_t t0, tp;
	uint8_t last;

	panel_init();
	screen_box();
	text(0, 0, "MSD7818 boot menu", C_TEXT, C_BG);
	text(0, 5, usb ? "CH+/CH- select, POWER boot" : "POWER boot", C_DIM, C_BG);
	draw_menu(usb, sel);

	puts_("\nbootsel: boot menu (docs/plan_bootloader.md)\n");
	puts_("  1) system from flash (default)\n");
	if (usb)
		puts_("  2) kernel from the USB stick (vmlinux.bin)\n");
	else
		puts_("     no kernel from the USB stick\n");
	puts_(usb ? "  press 1 or 2 (Enter = selected): " : "  press 1 or Enter: ");
	last = fd_key();
	t0 = tp = count();
	for (;;) {
		int s = (count() - t0) / TICKS_PER_S, c = getc_nowait(), key = -1;

		if (c == '1' || (usb && c == '2')) {
			sel = c - '1';
			break;
		}
		if (c == '\r' || c == '\n')
			break;
		if (count() - tp >= PANEL_POLL) {
			uint8_t k = fd_key();

			tp = count();
			if ((k & KEY_DOWN) && !(last & KEY_DOWN))
				key = k & 0x3F;
			last = k;
		}
		if (c >= 0 || key >= 0) {		/* any key stops the countdown */
			if (wait)
				puts_("\n  countdown stopped: ");
			wait = 0;
		}
		if (key == KEY_POWER)
			break;
		if (key == KEY_CHUP || key == KEY_CHDN) {
			sel = (sel + (key == KEY_CHUP ? n - 1 : 1)) % n;
			draw_menu(usb, sel);
			show_countdown(sel, -1);
		}
		if (wait && s >= TIMEOUT_S)
			break;
		if (wait && s != shown) {		/* countdown */
			shown = s;
			putc_('0' + TIMEOUT_S - s);
			putc_(' ');
			show_countdown(sel, TIMEOUT_S - s);
		} else if (!wait && shown >= 0) {
			shown = -1;
			show_countdown(sel, -1);
		}
	}
	putc_('1' + sel);
	puts_("\n");
	if (sel == 1)
		boot(USB_ADDR, USB_COPY);
	return boot_flash();
}
