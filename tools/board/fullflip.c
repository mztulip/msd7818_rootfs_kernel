/*
 * fullflip [FRAMES] [DELAY_US] - fbDOOM-like load: every frame the whole
 * 1600x1000 picture (moving diagonal stripes, centred, black borders) is
 * copied line by line into the hidden buffer, then FBIOPAN_DISPLAY and
 * FBIO_WAITFORVSYNC. DELAY_US: sleep after the vsync before copying.
 * Build on the board: gcc -O2 -o fullflip fullflip.c
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif
#define PW 1600
#define PH 1000
#define NF 4

int main(int argc, char **argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 500;
	int delay = argc > 2 ? atoi(argv[2]) : 0;
	int fd = open("/dev/fb0", O_RDWR);
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	uint32_t zero = 0;

	ioctl(fd, FBIOGET_VSCREENINFO, &var);
	ioctl(fd, FBIOGET_FSCREENINFO, &fix);
	uint8_t *fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	int W = var.xres, H = var.yres, left = (W - PW) / 2, top = (H - PH) / 2;
	uint16_t *pics = malloc((size_t)NF * PW * PH * 2);

	for (int f = 0; f < NF; f++)
		for (int y = 0; y < PH; y++)
			for (int x = 0; x < PW; x++)
				pics[((size_t)f * PH + y) * PW + x] =
					((x + y + f * 32) & 64) ? 0xffff : ((x / 200) << 13 | 0x001f);
	memset(fb, 0, fix.line_length * H * 2);
	var.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &var);

	int back = 1;
	for (int i = 0; i < frames; i++) {
		uint8_t *dst = fb + (size_t)back * H * fix.line_length + top * fix.line_length + left * 2;
		uint16_t *src = pics + (size_t)(i % NF) * PW * PH;

		for (int y = 0; y < PH; y++)
			memcpy(dst + (size_t)y * fix.line_length, src + (size_t)y * PW, PW * 2);
		var.yoffset = back * H;
		ioctl(fd, FBIOPAN_DISPLAY, &var);
		ioctl(fd, FBIO_WAITFORVSYNC, &zero);
		if (delay)
			usleep(delay);
		back ^= 1;
	}
	var.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &var);
	printf("fullflip: %d frames\n", frames);
	return 0;
}
