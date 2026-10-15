/*
 * phaseflip DELAY_MS [SECONDS] [copy] - flip between two identical buffers
 * exactly DELAY_MS after a vsync (mstar-gopfb). With "copy" the same
 * picture is copied into the hidden buffer before every flip instead of
 * waiting (like fbDOOM does), to add DRAM load. Both buffers hold the same picture
 * (centred checkerboard with black borders, like fbDOOM), so a correct flip
 * is invisible and any glitch (shifted picture) means the flip went wrong.
 * Used to check whether the moment of the trigger within the frame matters.
 * Build on the board: gcc -O2 -o phaseflip phaseflip.c
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

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

static double now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

int main(int argc, char **argv)
{
	double delay = argc > 1 ? atof(argv[1]) : 2;
	int secs = argc > 2 ? atoi(argv[2]) : 8;
	int copy = argc > 3 && !strcmp(argv[3], "copy");
	int fd = open("/dev/fb0", O_RDWR);
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	uint32_t zero = 0;

	ioctl(fd, FBIOGET_VSCREENINFO, &var);
	ioctl(fd, FBIOGET_FSCREENINFO, &fix);
	uint16_t *fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	int W = var.xres, H = var.yres;

	for (int b = 0; b < 2; b++)
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				int in = x >= 160 && x < 1760 && y >= 40 && y < 1040;
				fb[(b * H + y) * W + x] = !in ? 0 :
					((x / 80 + y / 80) & 1) ? 0xffe0 : 0x07ff;
			}

	size_t frame = (size_t)W * H * 2;
	uint8_t *pic = malloc(frame);

	memcpy(pic, fb, frame);
	double end = now_ms() + secs * 1000.0;
	int back = 1, n = 0;

	ioctl(fd, FBIO_WAITFORVSYNC, &zero);
	while (now_ms() < end) {
		ioctl(fd, FBIO_WAITFORVSYNC, &zero);	/* edge (and the previous flip) */
		double t = now_ms() + delay;
		if (copy)
			memcpy((uint8_t *)fb + back * frame, pic, frame);	/* same picture */
		while (now_ms() < t)
			;
		var.yoffset = back * H;
		ioctl(fd, FBIOPAN_DISPLAY, &var);
		back ^= 1;
		n++;
	}
	var.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &var);
	printf("delay %.1f ms%s: %d flips\n", delay, copy ? " + copy" : "", n);
	return 0;
}
