/*
 * stocktrace - time trace of the DEC DSP mailbox while the stock player
 * plays (loaded like stockprobe: MBoot puts it at 0xA3800000, the "Help"
 * handler jumps here). For 2 s it samples RIU 0x112D60-0x112D9E and
 * 0x112A00-0x112A2E as fast as it can, records every change in RAM
 * (0xA3900000, MVD memory) and prints them afterwards as
 *   "<us> <riu reg> <new value>"
 * The firmware's printf (0x80244f74) only handles 3 arguments reliably.
 * Time from the CP0 Count register (552 MHz / 2 = 276 counts per us).
 */
#include <stdint.h>

typedef int (*printf_t)(const char *, ...);
#define PRINTF ((printf_t)0x80244f74)

#define NREG	(32 + 24)
#define MAXEV	6000

struct ev { uint32_t t; uint32_t reg; uint32_t val; };
#define EVS ((volatile struct ev *)0xa3900000)

static inline uint32_t count(void)
{
	uint32_t c;

	__asm__ volatile("mfc0 %0, $9" : "=r"(c));
	return c;
}

static uint32_t reg_of(int i)
{
	return i < 32 ? 0x112d60 + 2 * i : 0x112a00 + 2 * (i - 32);
}

void trace(void)
{
	uint16_t last[NREG];
	uint32_t t0 = count(), n = 0;

	for (int i = 0; i < NREG; i++) {
		last[i] = *(volatile uint16_t *)(0xbf000000 + reg_of(i) * 2);
		PRINTF("\ninit %06x %04x", reg_of(i), last[i]);
	}
	t0 = count();
	while ((count() - t0) < 276u * 2000000u && n < MAXEV) {
		for (int i = 0; i < NREG; i++) {
			uint16_t v = *(volatile uint16_t *)(0xbf000000 + reg_of(i) * 2);

			if (v != last[i]) {
				EVS[n].t = (count() - t0) / 276;
				EVS[n].reg = reg_of(i);
				EVS[n].val = v;
				n++;
				last[i] = v;
			}
		}
	}
	PRINTF("\nTRACE BEGIN %u", n);
	for (uint32_t i = 0; i < n; i++)
		PRINTF("\n%u %06x %04x", EVS[i].t, EVS[i].reg, EVS[i].val);
	PRINTF("\nTRACE END\n");
}
