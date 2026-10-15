/*
 * wavplay - play a 16-bit PCM WAV file (mono/stereo, 8-48 kHz) through the
 * DEC DSP (mstar_audio.h). Build on the board: gcc -O2 -o wavplay wavplay.c
 */
#include "mstar_audio.h"

int main(int argc, char **argv)
{
	FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
	uint8_t h[12], c[8], fmt[16] = { 0 };
	uint32_t len;
	static int16_t buf[8192];

	if (!f) {
		fprintf(stderr, "usage: wavplay FILE.wav\n");
		return 1;
	}
	if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
		fprintf(stderr, "not a WAV file\n");
		return 1;
	}
	while (fread(c, 1, 8, f) == 8) {	/* walk the chunks up to "data" */
		len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
		if (!memcmp(c, "fmt ", 4)) {
			if (fread(fmt, 1, len < 16 ? len : 16, f) < 16)
				return 1;
			if (len > 16)
				fseek(f, len - 16, SEEK_CUR);
		} else if (!memcmp(c, "data", 4)) {
			break;
		} else {
			fseek(f, (len + 1) & ~1u, SEEK_CUR);
		}
	}
	unsigned format = fmt[0] | fmt[1] << 8, ch = fmt[2] | fmt[3] << 8;
	unsigned rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16, bits = fmt[14];

	printf("%s: format %u, %u ch, %u Hz, %u bit, %u bytes\n", argv[1], format, ch, rate, bits, len);
	if (format != 1 || bits != 16 || ch < 1 || ch > 2 || rate < 8000 || rate > 48000) {
		fprintf(stderr, "only 16-bit PCM, 1-2 channels, 8-48 kHz\n");
		return 1;
	}
	if (!maudio_open(rate, ch)) {
		fprintf(stderr, "audio init failed\n");
		return 1;
	}
	size_t n;
	unsigned frame = 2 * ch;

	while (len && (n = fread(buf, frame, sizeof(buf) / frame < len / frame ? sizeof(buf) / frame : len / frame, f)) > 0) {
		maudio_write(buf, n);
		len -= n * frame;
	}
	maudio_close();
	return 0;
}
