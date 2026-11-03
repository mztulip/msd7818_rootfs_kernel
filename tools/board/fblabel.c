/*
 * fblabel N - big phase number for by-ear/by-eye tests: black screen with
 * the digit N (0-9) as a huge seven-segment figure, in buffer 0 of /dev/fb0.
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
static const uint8_t seg[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
static uint16_t *fb; static int W, H;
static void box(int x0, int y0, int w, int h)
{
	for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) fb[y * W + x] = 0xffe0;
}
int main(int c, char **v)
{
	int d = c > 1 ? atoi(v[1]) % 10 : 0;
	int fd = open("/dev/fb0", O_RDWR); struct fb_var_screeninfo var; struct fb_fix_screeninfo fix;
	ioctl(fd, FBIOGET_VSCREENINFO, &var); ioctl(fd, FBIOGET_FSCREENINFO, &fix);
	fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	W = fix.line_length / 2; H = var.yres;
	for (int i = 0; i < W * H; i++) fb[i] = 0;
	int t = H / 12, w = H / 2, h = H * 3 / 8, x = (var.xres - w) / 2, y = H / 8;
	if (seg[d] & 1) box(x, y, w, t);			/* a */
	if (seg[d] & 2) box(x + w - t, y, t, h);		/* b */
	if (seg[d] & 4) box(x + w - t, y + h, t, h);		/* c */
	if (seg[d] & 8) box(x, y + 2 * h - t, w, t);		/* d */
	if (seg[d] & 16) box(x, y + h, t, h);			/* e */
	if (seg[d] & 32) box(x, y, t, h);			/* f */
	if (seg[d] & 64) box(x, y + h - t / 2, w, t);		/* g */
	var.yoffset = 0; ioctl(fd, FBIOPAN_DISPLAY, &var);
	return 0;
}
