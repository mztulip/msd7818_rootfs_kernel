/*
 * szablon - template probe for the stock firmware (docs/sondy_stock.md).
 * Copy it as NAME.c, rename the function to NAME, then: ./build_probe.sh NAME
 * Runs inside the stock firmware when "Help" is typed at the ">>" console.
 * Rules: no libc; use the stock printf (at most 3 arguments per call);
 * read registers through KSEG1 (0xBF000000 + reg*2); read big memory areas
 * through KSEG0 (0x8xxxxxxx, cached) so the running player keeps its bandwidth.
 */
#include <stdint.h>

typedef int (*printf_t)(const char *, ...);
#define PRINTF ((printf_t)0x80244f74)		/* stock firmware printf */

/* 16-bit RIU register, address as in MStar sources (e.g. 0x112D6A) */
static uint16_t reg(uint32_t r) { return *(volatile uint16_t *)(0xbf000000 + r * 2); }

static uint32_t count(void)			/* CP0 Count: 276 ticks per us (552 MHz / 2) */
{
	uint32_t c;

	__asm__ volatile("mfc0 %0, $9" : "=r"(c));
	return c;
}

void szablon(void)
{
	PRINTF("\nSZABLON BEGIN");
	for (uint32_t r = 0x112d60; r < 0x112d70; r += 2)
		PRINTF("\n%06x = %04x", r, reg(r));
	/* wait 0.5 s without the OS (busy loop on the CPU counter) */
	uint32_t t0 = count();
	while (count() - t0 < 276u * 500000u)
		;
	PRINTF("\n0x2d6a after 0.5 s: %04x", reg(0x112d6a));
	PRINTF("\nSZABLON END\n");
}
