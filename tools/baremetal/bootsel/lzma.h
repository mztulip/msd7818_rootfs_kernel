/*
 * Small LZMA ("LZMA alone", .lzma) decoder for bootsel, written from the
 * LZMA format specification. Decodes a whole stream into one output buffer
 * (the buffer is the dictionary), so it needs no extra memory besides the
 * probability tables (struct lzma_probs, about 30 KB).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LZMA_H
#define LZMA_H

#include <stdint.h>

#define LZMA_HEADER	13		/* props, dict size (4), uncompressed size (8) */
#define LZMA_MAX_LCLP	4		/* lc + lp supported (our images: lc 3, lp 0) */

struct lzma_probs {
	uint16_t p[1856 + (0x300 << LZMA_MAX_LCLP)];
};

enum {
	LZMA_OK = 0,
	LZMA_ERR_HEADER = -1,		/* bad props or lc + lp too big */
	LZMA_ERR_DATA = -2,		/* corrupt stream (distance before start, bad first byte) */
	LZMA_ERR_INPUT = -3,		/* ran out of input */
	LZMA_ERR_OUTPUT = -4,		/* output larger than the buffer */
};

/* Uncompressed size from the header, or -1 if it is "unknown" (0xFF..FF). */
int64_t lzma_size(const uint8_t *in);

/*
 * Decode in[0..in_len) (header included) into out[0..out_max).
 * *out_len gets the number of bytes written. Returns LZMA_OK or an error.
 */
int lzma_decode(const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t out_max,
		uint32_t *out_len, struct lzma_probs *probs);

#endif
