/*
 * owntest - test of own_audio.h (our own SE DSP program, no MStar code): plays a 1 kHz sine
 * for 2 s, then 440 / 660 Hz alternating every 0.5 s, through maudio_open/write/close.
 *
 * Build on the board: gcc -O2 -o owntest owntest.c -lm
 * Usage: owntest [SECONDS]   (default 6)
 */
#include <math.h>
#include "own_audio.h"

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 6, ph = 0;
	static int16_t buf[512 * 2];
	unsigned long pos = 0;

	if (!maudio_open(48000, 2)) {
		fprintf(stderr, "maudio_open failed (root? /dev/mem?)\n");
		return 1;
	}
	double t0 = own_now();
	while (pos < secs * 48000) {
		for (int i = 0; i < 512; i++, pos++) {
			double t = pos / 48000.0, f = t < 2 ? 1000 : ((int)(t * 2) & 1) ? 660 : 440;

			ph += 2 * M_PI * f / 48000;
			if (ph > 2 * M_PI)
				ph -= 2 * M_PI;
			buf[2 * i] = buf[2 * i + 1] = (int16_t)(16384 * sin(ph));
		}
		maudio_write(buf, 512);
	}
	double dt = own_now() - t0;
	maudio_close();
	printf("wrote %lu frames in %.2f s (%.0f/s)\n", pos, dt, pos / dt);
	return 0;
}
