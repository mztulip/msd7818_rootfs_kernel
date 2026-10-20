/*
 * memdump - write a physical address range to stdout through mmap of
 * /dev/mem (read() of /dev/mem stops at the end of RAM, mmap does not).
 * Used to read the memory mapped SPI flash at 0x1FC00000.
 * Build on the board: gcc -O2 -o memdump memdump.c
 * Usage: memdump PHYS_ADDR LENGTH > out.bin   (hex or decimal, page aligned)
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s PHYS_ADDR LENGTH\n", argv[0]);
		return 1;
	}
	unsigned long addr = strtoul(argv[1], NULL, 0), len = strtoul(argv[2], NULL, 0);
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	void *p = fd < 0 ? MAP_FAILED : mmap(0, len, PROT_READ, MAP_SHARED, fd, addr);

	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	return fwrite(p, 1, len, stdout) == len ? 0 : 1;
}
