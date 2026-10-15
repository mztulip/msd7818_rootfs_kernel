/* flipcheck: FBIOPAN_DISPLAY + FBIO_WAITFORVSYNC 1000 times with varying work, prints wait times and a histogram of intervals between flips (10 ms bins). On mstar-gopfb (50 Hz) all intervals are ~20 ms, waits 10-20 ms. */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec*1e-6;}
int main(int c,char**v){
	int fd=open("/dev/fb0",O_RDWR); struct fb_var_screeninfo var; uint32_t z=0; int n=1000,hist[8]={0},short_waits=0;
	ioctl(fd,FBIOGET_VSCREENINFO,&var);
	double prev=now(), wmin=1e9, wmax=0;
	for(int i=0;i<n;i++){
		/* some "rendering" time that varies, like a game */
		volatile int k; for(k=0;k<(i%7)*40000;k++);
		var.yoffset=(i&1)*var.yres; ioctl(fd,FBIOPAN_DISPLAY,&var);
		double t0=now(); ioctl(fd,FBIO_WAITFORVSYNC,&z); double t=now(), w=t-t0;
		if(w<wmin)wmin=w; if(w>wmax)wmax=w; if(w<0.5)short_waits++;
		double d=t-prev; prev=t; int b=(int)(d/10); if(b>7)b=7; hist[b]++;
	}
	var.yoffset=0; ioctl(fd,FBIOPAN_DISPLAY,&var);
	printf("wait min %.2f max %.2f ms, waits <0.5ms: %d\n",wmin,wmax,short_waits);
	printf("interval histogram (10 ms bins): "); for(int b=0;b<8;b++)printf("%d ",hist[b]); printf("\n");
	return 0;}
