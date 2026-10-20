/*
 * gpiodump - dump the GPIO / pad register banks of the running stock firmware
 * in the stockdump format ("bfXXXXXX: vvvv x8"), to compare with
 * tools/board/gpiodump_linux.c. Used to find the external audio mute GPIO.
 */
#include <stdint.h>

typedef int (*printf_t)(const char *, ...);
#define PRINTF ((printf_t)0x80244f74)		/* stock firmware printf */

static const uint32_t ranges[][2] = {		/* CPU addresses */
	{ 0xbf001c00, 0xbf001e00 },	/* PM 0x0Exx (PM GPIO) */
	{ 0xbf002800, 0xbf002a00 },	/* PM 0x14xx (SAR) */
	{ 0xbf203c00, 0xbf203e00 },	/* chiptop 0x101Exx */
	{ 0xbf204a00, 0xbf204c00 },	/* adc_atop 0x1025xx */
	{ 0xbf204c00, 0xbf204e00 },	/* adc_dtop 0x1026xx */
};

void gpiodump(void)
{
	PRINTF("\nGPIODUMP BEGIN");
	for (unsigned r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++)
		for (uint32_t a = ranges[r][0]; a < ranges[r][1]; a += 0x20) {
			volatile uint32_t *p = (volatile uint32_t *)a;

			PRINTF("\n%08x:", a, 0);
			for (int i = 0; i < 8; i++)
				PRINTF(" %04x", p[i] & 0xffff, 0);
		}
	PRINTF("\nGPIODUMP END\n");
}
