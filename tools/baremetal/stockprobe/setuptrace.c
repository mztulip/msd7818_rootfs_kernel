/*
 * setuptrace - record the audio register writes while the stock player
 * STARTS a file (loaded like stockprobe at 0xA3800000, "Help" jumps here).
 * After "Help" it waits 2 s, prints "GO", then for 25 s samples the audio
 * registers below and stores every change (time, reg, value) in MVD memory
 * (0xA3900000), at most PER_REG changes per register (noisy counters are
 * left out). Afterwards prints "<ms>.<us> <reg> <value>" (printf: <= 3 args).
 * Start playing test1k.wav when "GO" appears.
 */
#include <stdint.h>

typedef int (*printf_t)(const char *, ...);
#define PRINTF ((printf_t)0x80244f74)

#define MAXEV	20000
#define PER_REG	300

struct ev { uint32_t t; uint32_t reg; uint32_t val; };
#define EVS ((volatile struct ev *)0xa3900000)

static inline uint32_t count(void)
{
	uint32_t c;

	__asm__ volatile("mfc0 %0, $9" : "=r"(c));
	return c;
}

/* RIU 16-bit registers to watch */
static uint32_t regs[200];
static uint16_t last[200], nchg[200];

static int skip(uint32_t r)
{
	return r == 0x112d60 || r == 0x112d62 || r == 0x112d68 || r == 0x112d6e || r == 0x112a16;
}

void setuptrace(void)
{
	int nr = 0;
	uint32_t n = 0, t0;

	for (uint32_t r = 0x112a00; r < 0x112a30; r += 2) regs[nr++] = r;	/* DEC IDMA, mem */
	for (uint32_t r = 0x112a80; r < 0x112ab0; r += 2) regs[nr++] = r;	/* SE IDMA, mem */
	for (uint32_t r = 0x112d00; r < 0x112e00; r += 2) regs[nr++] = r;	/* mailboxes */
	for (uint32_t r = 0x112c60; r < 0x112c90; r += 2) regs[nr++] = r;	/* paths */
	for (int i = 0; i < nr; i++) {
		last[i] = *(volatile uint16_t *)(0xbf000000 + regs[i] * 2);
		nchg[i] = 0;
	}
	t0 = count();
	while (count() - t0 < 276u * 2000000u)
		;
	PRINTF("\nGO\n");
	t0 = count();
	while (count() - t0 < 276u * 25000000u && n < MAXEV) {
		for (int i = 0; i < nr; i++) {
			uint16_t v = *(volatile uint16_t *)(0xbf000000 + regs[i] * 2);

			if (v != last[i]) {
				last[i] = v;
				if (skip(regs[i]) || nchg[i] >= PER_REG)
					continue;
				nchg[i]++;
				EVS[n].t = (count() - t0) / 276;
				EVS[n].reg = regs[i];
				EVS[n].val = v;
				n++;
			}
		}
	}
	PRINTF("\nSETUP BEGIN %u", n);
	for (uint32_t i = 0; i < n; i++) {
		PRINTF("\n%u.%03u", EVS[i].t / 1000, EVS[i].t % 1000);
		PRINTF(" %06x %04x", EVS[i].reg, EVS[i].val);
	}
	PRINTF("\nSETUP END\n");
}
