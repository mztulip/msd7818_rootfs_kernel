/*
 * bwtest [SECONDS] - memory bandwidth vs display test for mstar-gopfb.
 *
 * Shows a static checkerboard in buffer 0 and keeps copying 1600x1000x2 B
 * frames into the hidden buffer 1 as fast as possible. If the GOP could not
 * keep up with scanout while the CPU hammers DRAM, the checkerboard would
 * flicker. Result on the WIWA HD95 (2026-10-14): steady picture.
 * Build on the board: gcc -O2 -o bwtest bwtest.c
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
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int c,char**v){
	int secs=c>1?atoi(v[1]):15;
	int fd=open("/dev/fb0",O_RDWR); struct fb_var_screeninfo var; struct fb_fix_screeninfo fix;
	ioctl(fd,FBIOGET_VSCREENINFO,&var); ioctl(fd,FBIOGET_FSCREENINFO,&fix);
	uint16_t *fb=mmap(0,fix.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	int W=var.xres,H=var.yres;
	for(int y=0;y<H;y++)for(int x=0;x<W;x++) fb[y*W+x]=((x/120+y/120)&1)?0xffff:0x001f;   /* checkerboard */
	var.yoffset=0; ioctl(fd,FBIOPAN_DISPLAY,&var);
	size_t n=1600*1000*2; uint8_t *src=malloc(n); memset(src,0x5a,n);
	double t0=now(); double bytes=0;
	while(now()-t0<secs){ memcpy((uint8_t*)fb+fix.line_length*H,src,n); bytes+=n; }
	printf("copied %.0f MB/s into the hidden buffer for %d s\n",bytes/(now()-t0)/1e6,secs);
	return 0;}
