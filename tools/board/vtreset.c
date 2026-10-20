/*
 * vtreset - bring the framebuffer console back after a program (e.g. a killed
 * fbdoom) left tty1 in KD_GRAPHICS or the display panned to the second buffer.
 * Build on the board: gcc -O2 -o vtreset vtreset.c
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>

int main(void)
{
	struct fb_var_screeninfo v;
	int t = open("/dev/tty1", O_RDWR), f = open("/dev/fb0", O_RDWR);

	if (t >= 0 && ioctl(t, KDSETMODE, KD_TEXT))
		perror("KDSETMODE");
	if (f >= 0 && !ioctl(f, FBIOGET_VSCREENINFO, &v)) {
		v.yoffset = 0;
		if (ioctl(f, FBIOPAN_DISPLAY, &v))
			perror("PAN");
	}
	return 0;
}
