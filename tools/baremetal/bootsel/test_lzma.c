/* Host test of lzma.c: test_lzma IN.lzma OUT  (writes the decoded data) */
#include <stdio.h>
#include <stdlib.h>
#include "lzma.h"

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	static uint8_t in[16 << 20], out[64 << 20];
	static struct lzma_probs probs;
	uint32_t n = fread(in, 1, sizeof(in), f), len;
	int r = lzma_decode(in, n, out, sizeof(out), &len, &probs);

	fclose(f);
	printf("%s: in %u, rc %d, out %u (header size %lld)\n", argv[1], n, r, len, (long long)lzma_size(in));
	f = fopen(argv[2], "wb");
	fwrite(out, 1, len, f);
	fclose(f);
	return r != 0;
}
