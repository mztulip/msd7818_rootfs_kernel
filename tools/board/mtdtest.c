/*
 * mtdtest - erase / write / verify test of one MTD partition (whole size).
 * Used to bring up the Kronus ISP flash driver (spi-mstar-isp) on a spare
 * partition. Destroys the partition contents!
 * Build on the board: gcc -O2 -o mtdtest mtdtest.c
 * Usage: mtdtest /dev/mtdN [seed]     (pattern = simple LCG from seed)
 */
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	struct mtd_info_user mi;
	struct erase_info_user ei;
	unsigned seed = argc > 2 ? strtoul(argv[2], NULL, 0) : 1, bad = 0;
	unsigned char *buf, *rd;
	double t;
	int fd;

	if (argc < 2 || (fd = open(argv[1], O_RDWR)) < 0 || ioctl(fd, MEMGETINFO, &mi)) {
		perror(argc < 2 ? "usage: mtdtest /dev/mtdN [seed]" : argv[1]);
		return 1;
	}
	printf("%s: size 0x%x, erasesize 0x%x, writesize %u\n", argv[1], mi.size, mi.erasesize, mi.writesize);
	buf = malloc(mi.size);
	rd = malloc(mi.size);

	ei.start = 0;
	ei.length = mi.size;
	t = now();
	if (ioctl(fd, MEMERASE, &ei)) {
		perror("MEMERASE");
		return 1;
	}
	printf("erase: %.2f s\n", now() - t);
	pread(fd, rd, mi.size, 0);
	for (unsigned i = 0; i < mi.size; i++)
		bad += rd[i] != 0xff;
	printf("after erase: %u bytes not 0xff\n", bad);

	for (unsigned i = 0; i < mi.size; i++) {
		seed = seed * 1103515245 + 12345;
		buf[i] = seed >> 16;
	}
	t = now();
	if (pwrite(fd, buf, mi.size, 0) != (ssize_t)mi.size) {
		perror("write");
		return 1;
	}
	printf("write: %.2f s\n", now() - t);
	pread(fd, rd, mi.size, 0);
	bad = 0;
	for (unsigned i = 0; i < mi.size; i++)
		bad += rd[i] != buf[i];
	printf("verify: %u bytes differ -> %s\n", bad, bad ? "FAIL" : "OK");
	return bad != 0;
}
