/*
 * fbflip - double buffering through the standard fbdev API (mstar-gopfb).
 *
 * Draws a moving white bar into the hidden half of /dev/fb0
 * (yres_virtual = 2 * yres), shows it with FBIOPAN_DISPLAY and waits for
 * the flip with FBIO_WAITFORVSYNC.
 *
 *   fbflip [frames]          double buffered animation
 *   fbflip single [frames]   draw into the visible buffer, no sync (tears)
 *   fbflip vsync [n]         measure FBIO_WAITFORVSYNC period
 *
 * Unbind the console first: echo 0 > /sys/class/vtconsole/vtcon1/bind
 * Build on the board: gcc -O2 -o fbflip fbflip.c
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define BAR 40

static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static int W, H;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline uint16_t grad(int x, int y)
{
	return ((x * 31 / W) << 11) | ((y * 63 / H) << 5) | 8;
}

static void background(uint16_t *fb)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			fb[y * W + x] = grad(x, y);
}

static void bar(uint16_t *fb, int oldx, int x)
{
	for (int y = 0; y < H; y++) {
		uint16_t *row = fb + y * W;

		if (oldx >= 0)
			for (int i = oldx; i < oldx + BAR; i++)
				row[i] = grad(i, y);
		for (int i = x; i < x + BAR; i++)
			row[i] = 0xffff;
	}
}

int main(int argc, char **argv)
{
	int single = argc > 1 && !strcmp(argv[1], "single");
	int vs = argc > 1 && !strcmp(argv[1], "vsync");
	int frames = argc > 1 && (single || vs) ? (argc > 2 ? atoi(argv[2]) : 500)
					       : (argc > 1 ? atoi(argv[1]) : 500);
	int fd = open("/dev/fb0", O_RDWR);
	uint32_t zero = 0;

	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
		perror("/dev/fb0");
		return 1;
	}
	W = var.xres;
	H = var.yres;
	printf("%s: %dx%d virtual %dx%d, %d bpp, line %d, smem %u B\n", fix.id, W, H,
	       var.xres_virtual, var.yres_virtual, var.bits_per_pixel, fix.line_length, fix.smem_len);

	if (vs) {
		double t0, t;

		ioctl(fd, FBIO_WAITFORVSYNC, &zero);
		t0 = now();
		for (int i = 0; i < frames; i++)
			if (ioctl(fd, FBIO_WAITFORVSYNC, &zero)) {
				perror("FBIO_WAITFORVSYNC");
				return 1;
			}
		t = now() - t0;
		printf("%d vsyncs: period %.3f ms, %.2f Hz\n", frames, t * 1000 / frames, frames / t);
		return 0;
	}

	int nbuf = var.yres_virtual / H;
	uint16_t *mem = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (mem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	if (!single && nbuf < 2) {
		fprintf(stderr, "only %d buffer(s)\n", nbuf);
		return 1;
	}

	uint16_t *buf[2] = { mem, mem + W * H };
	int x[2] = { -1, -1 }, back = 1, pos = 0, step = 24;
	double t0, t;

	var.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &var);
	background(buf[0]);
	if (!single)
		background(buf[1]);

	t0 = now();
	for (int f = 0; f < frames; f++) {
		pos += step;
		if (pos < 0 || pos > W - BAR) {
			step = -step;
			pos += 2 * step;
		}
		if (single) {
			bar(buf[0], x[0], pos);
			x[0] = pos;
			continue;
		}
		bar(buf[back], x[back], pos);
		x[back] = pos;
		var.yoffset = back * H;
		if (ioctl(fd, FBIOPAN_DISPLAY, &var)) {
			perror("FBIOPAN_DISPLAY");
			return 1;
		}
		ioctl(fd, FBIO_WAITFORVSYNC, &zero);	/* flip done, old front is free */
		back ^= 1;
	}
	t = now() - t0;
	printf("%s: %d frames, %.1f fps\n", single ? "single" : "double", frames, frames / t);

	var.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &var);
	return 0;
}
