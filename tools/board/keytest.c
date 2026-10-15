/*
 * keytest COLOR SECONDS - is a colour transparent on the GOP layer?
 *
 * Fills the shown buffer 0 with COLOR (hex RGB565) and keeps writing a
 * moving pattern into the hidden buffer 1. If anything of the pattern shows
 * up on screen, COLOR is see-through and another layer reads that memory.
 * Build on the board: gcc -O2 -o keytest keytest.c
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>

int main(int argc, char **argv)
{
	uint16_t col = argc > 1 ? strtoul(argv[1], 0, 16) : 0;
	int secs = argc > 2 ? atoi(argv[2]) : 8;
	int fd = open("/dev/fb0", O_RDWR);
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	ioctl(fd, FBIOGET_VSCREENINFO, &var);
	ioctl(fd, FBIOGET_FSCREENINFO, &fix);
	uint16_t *fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	int W = var.xres, H = var.yres;

	var.yoffset = 0;
	ioctl(fd, FBIOPAN_DISPLAY, &var);
	for (int i = 0; i < W * H; i++)
		fb[i] = col;
	uint16_t *b1 = fb + W * H;
	time_t end = time(NULL) + secs;
	for (int f = 0; time(NULL) < end; f++)
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x += 2)
				b1[y * W + x] = ((x + y + f * 16) & 64) ? 0xffff : 0xf800;
	return 0;
}
