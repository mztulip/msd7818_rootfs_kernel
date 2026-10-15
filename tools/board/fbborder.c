/*
 * fbborder SECONDS LEFT TOP - watch the black borders of both fb buffers.
 *
 * While a program (fbDOOM) draws a centred picture, the area left of column
 * LEFT and above row TOP must stay black in both buffers. Any non-zero pixel
 * there means the memory content got corrupted (not a scanout problem).
 * Build on the board: gcc -O2 -o fbborder fbborder.c
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 20;
	int left = argc > 2 ? atoi(argv[2]) : 480;
	int top = argc > 3 ? atoi(argv[3]) : 240;
	int fd = open("/dev/fb0", O_RDONLY);
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	time_t end = time(NULL) + secs;
	long scans = 0, bad = 0;

	ioctl(fd, FBIOGET_VSCREENINFO, &var);
	ioctl(fd, FBIOGET_FSCREENINFO, &fix);
	uint16_t *fb = mmap(0, fix.smem_len, PROT_READ, MAP_SHARED, fd, 0);
	int W = var.xres, H = var.yres, nb = var.yres_virtual / H;

	while (time(NULL) < end) {
		for (int b = 0; b < nb; b++)
			for (int y = 0; y < H; y++) {
				uint16_t *row = fb + (b * H + y) * W;
				int n = y < top ? W : left;

				for (int x = 0; x < n; x++)
					if (row[x]) {
						if (bad < 10)
							printf("buffer %d x %d y %d = 0x%04x\n", b, x, y, row[x]);
						bad++;
						break;
					}
			}
		scans++;
		usleep(20000);
	}
	printf("%ld scans, %ld bad rows\n", scans, bad);
	return 0;
}
