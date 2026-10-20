/*
 * dspload - load a program file into the SE audio DSP and watch its mailboxes.
 * Like dsptest, but the words come from a file (as written by
 * dsp_re/asm.py -o), so programs can be longer than 64 words.
 *
 * Build on the board: gcc -O2 -o dspload dspload.c
 * Usage: dspload [-h] [-s] [-k] [-r] FILE [SECONDS]
 *   FILE     24-bit hex words separated by spaces/newlines, loaded at SE address 0
 *   -h       set up the HDMI audio path and unmute SCART first (as audiotest3 does)
 *   -s       also write the stock_out register values (0x112D00.., HDMI 0x1131xx/0x1133xx)
 *            that audiotest3 writes before the DSPs start (dsp_re/res_dump_own.txt diff)
 *   -r       no SE reset: load over a running stock SE that is parked in a stub at 0x0710
 *            (dsppoke se 710 40000a 0d0c3a 18000f; dsppoke se 30 186e0f = IMASK 0, loop),
 *            then turn the stub loop into JUMP 0x0030 (0x1B91EF at 0x0712), so the program
 *            starts with all SE state the stock code set up
 *   -k       keep the program running at the end (default: stop SE with an RTI)
 *   SECONDS  how long to print the mailboxes, once per second (default 3)
 * Prints SE mailboxes 0x112DF0..0x112DFE before the start, after 1/10/100 ms,
 * then every second.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mstar_audio.h"

#define MAXW 4096

static void dump(const char *tag)
{
	printf("%-8s", tag);
	for (unsigned r = 0x2df0; r <= 0x2dfe; r += 2)
		printf(" %04x=%04x", r, r16(A(r)));
	printf("\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	static uint8_t buf[3 * MAXW];
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	int hdmi = 0, sout = 0, nores = 0, keep = 0, n = 0, a = 1;
	unsigned w;
	FILE *f;

	for (; a < argc && argv[a][0] == '-'; a++) {
		if (!strcmp(argv[a], "-h"))
			hdmi = 1;
		else if (!strcmp(argv[a], "-r"))
			nores = 1;
		else if (!strcmp(argv[a], "-s"))
			sout = 1;
		else if (!strcmp(argv[a], "-k"))
			keep = 1;
	}
	if (a >= argc) {
		fprintf(stderr, "usage: %s [-h] [-s] [-k] [-r] FILE [SECONDS]\n", argv[0]);
		return 1;
	}
	f = fopen(argv[a], "r");
	if (!f) {
		perror(argv[a]);
		return 1;
	}
	while (n < MAXW && fscanf(f, "%x", &w) == 1) {
		buf[3 * n] = w;
		buf[3 * n + 1] = w >> 8;
		buf[3 * n + 2] = w >> 16;
		n++;
	}
	fclose(f);
	if (n < 1) {
		fprintf(stderr, "no words in %s\n", argv[a]);
		return 1;
	}
	int secs = a + 1 < argc ? atoi(argv[a + 1]) : 3;

	riu = mmap(0, RIU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, RIU_PHYS);
	dspmem = mmap(0, DSPMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DSPMEM_PHYS);
	if (riu == MAP_FAILED || dspmem == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	printf("%d words from %s\n", n, argv[a]);
	if (nores) {
		static const uint8_t jmp30[3] = { 0xef, 0x91, 0x1b };	/* at 0x0712: JUMP 0x0030 */

		dump("before");
		if (!idma_write(1, 0, buf, n * 3) || !idma_write(1, 0x712, jmp30, 3))
			return 1;
		goto watch;
	}
	audio_basic_init();
	mad2_setmeminfo_se(SE_BASE);
	if (sout)
		for (unsigned i = 0; i < sizeof(stock_out) / sizeof(stock_out[0]); i++)
			w16(stock_out[i].reg, stock_out[i].val);
	if (hdmi) {
		hdmi_audio_on();
		analog_unmute();
	}
	dump("before");
	/* SE reset as in start_dsps(), then our code, word 0 last */
	wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
	w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
	wm8(A(0x2a7e), 1, 1);
	if (n > 1 && !idma_write(1, 1, buf + 3, (n - 1) * 3))
		return 1;
	if (!idma_write(1, 0, buf, 3))
		return 1;
watch:
	msleep(1);
	dump("1 ms");
	msleep(9);
	dump("10 ms");
	msleep(90);
	dump("100 ms");
	for (int s = 1; s <= secs; s++) {
		msleep(s == 1 ? 900 : 1000);
		char tag[16];
		snprintf(tag, sizeof tag, "%d s", s);
		dump(tag);
	}
	if (!keep) {			/* stop: SE reset, then a lone RTI at 0 */
		static const uint8_t rti[3] = { 0x1f, 0x00, 0x0a };

		wm8(A(0x2b40), 2, 2); msleep(1); wm8(A(0x2b40), 2, 0);
		w8(A(0x2a80), 2); msleep(10); w8(A(0x2a80), 3);
		wm8(A(0x2a7e), 1, 1);
		idma_write(1, 0, rti, 3);
		printf("SE stopped\n");
	}
	return 0;
}
