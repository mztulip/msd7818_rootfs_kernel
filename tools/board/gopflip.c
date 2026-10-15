/*
 * gopflip - double buffering test on GOP3 (the Linux framebuffer layer).
 *
 * Buffer 0 is the Linux fb at phys 0x2100000, buffer 1 is the reserved
 * "tsukasa" region at phys 0x3c00000 (4 MB, one 1920x1080 RGB565 frame fits).
 * A white bar moves fast across a gradient so tearing is easy to see.
 *
 *   gopflip single [frames]   draw into the displayed buffer (tears)
 *   gopflip double [frames]   draw into the back buffer, flip at vsync
 *   gopflip vsync  [n]        only measure the vsync period
 *
 * Unbind the console first: echo 0 > /sys/class/vtconsole/vtcon1/bind
 * The display is always pointed back at 0x2100000 at the end.
 * Build on the board: gcc -O2 -o gopflip gopflip.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W		1920
#define H		1080
#define FB_SIZE		(W * H * 2)
static const uint32_t buf_phys[2] = { 0x2100000, 0x3c00000 };

#define GOP_PAGE	0x1f203000	/* GOP registers: 0x1f203e00..0x1f203ffc */
#define GOP_OFF		0xe00
#define BANKSEL		0x1fc		/* reg 0x1FFE */
#define GWIN_ADDR_L	0x004		/* bank A reg 0x1F02, address / 8 */
#define GWIN_ADDR_H	0x008		/* bank A reg 0x1F04 */
#define BANK_KEEP	0x7000
#define BANK_CTRL	0x9		/* GOP3 control bank, used for the trigger */
#define BANK_GWIN	0xa		/* GOP3 GWIN bank */
#define TRIGGER		0x0400
#define ACK_GOP3	0x8000

static volatile uint8_t *gop;
static uint16_t *buf[2];

static inline void wr(unsigned off, uint16_t v) { *(volatile uint16_t *)(gop + GOP_OFF + off) = v; }
static inline uint16_t rd(unsigned off) { return *(volatile uint16_t *)(gop + GOP_OFF + off); }

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Latch pending GOP3 registers at the next vsync and wait for it. */
static int vsync_latch(void)
{
	int n = 0;

	wr(BANKSEL, BANK_KEEP | TRIGGER | BANK_CTRL);
	wr(BANKSEL, BANK_KEEP | BANK_CTRL);
	while ((rd(BANKSEL) & ACK_GOP3) && n < 10000000)
		n++;
	wr(BANKSEL, BANK_KEEP);
	return n;
}

static void show(int b)
{
	uint32_t a = buf_phys[b] >> 3;

	wr(BANKSEL, BANK_KEEP | BANK_GWIN);
	wr(GWIN_ADDR_L, a & 0xffff);
	wr(GWIN_ADDR_H, a >> 16);
	vsync_latch();
}

static void background(uint16_t *fb)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			fb[y * W + x] = ((x * 31 / W) << 11) | ((y * 63 / H) << 5) | 8;
}

/* restore the gradient where the bar was, draw the bar at its new place */
static void bar(uint16_t *fb, int oldx, int x)
{
	for (int y = 0; y < H; y++) {
		uint16_t *row = fb + y * W;

		if (oldx >= 0)
			for (int i = oldx; i < oldx + 40; i++)
				row[i] = ((i * 31 / W) << 11) | ((y * 63 / H) << 5) | 8;
		for (int i = x; i < x + 40; i++)
			row[i] = 0xffff;
	}
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "double";
	int frames = argc > 2 ? atoi(argv[2]) : 600;
	int fd = open("/dev/mem", O_RDWR | O_SYNC);

	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	gop = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GOP_PAGE);
	for (int i = 0; i < 2; i++)
		buf[i] = mmap(0, FB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf_phys[i]);
	if (gop == MAP_FAILED || buf[0] == MAP_FAILED || buf[1] == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	if (!strcmp(mode, "vsync")) {
		double t0 = now(), t;

		vsync_latch();
		t0 = now();
		for (int i = 0; i < frames; i++)
			vsync_latch();
		t = now() - t0;
		printf("%d vsyncs in %.3f s: period %.3f ms, %.2f Hz\n",
		       frames, t, t * 1000 / frames, frames / t);
		return 0;
	}

	int dbl = !strcmp(mode, "double");
	int x[2] = { -1, -1 }, back = 1, pos = 0, step = 24;
	double t0;

	background(buf[0]);
	if (dbl)
		background(buf[1]);
	show(0);
	t0 = now();
	for (int f = 0; f < frames; f++) {
		pos += step;
		if (pos < 0 || pos > W - 40) {
			step = -step;
			pos += 2 * step;
		}
		if (dbl) {
			bar(buf[back], x[back], pos);
			x[back] = pos;
			show(back);
			back ^= 1;
		} else {
			bar(buf[0], x[0], pos);
			x[0] = pos;
		}
	}
	double t = now() - t0;

	printf("%s: %d frames in %.2f s, %.1f fps\n", mode, frames, t, frames / t);
	show(0);
	return 0;
}
