/*
 * Small LZMA ("LZMA alone") decoder for bootsel, written from the LZMA
 * format specification (range coder, literal coder with match byte, length
 * and distance coders, four repeat distances, 12-state machine). The output
 * buffer doubles as the dictionary.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lzma.h"

#define TOP		(1u << 24)
#define BITS		11
#define BIT_ONE		(1u << BITS)
#define MOVE		5
#define STATES		12
#define POS_MAX		4		/* pb up to 4 */
#define FULL_DIST	128
#define END_POS_MODEL	14
#define ALIGN_BITS	4

/* offsets into the probability array */
#define IS_MATCH	0					/* STATES << POS_MAX */
#define IS_REP		(IS_MATCH + (STATES << POS_MAX))
#define IS_REP_G0	(IS_REP + STATES)
#define IS_REP_G1	(IS_REP_G0 + STATES)
#define IS_REP_G2	(IS_REP_G1 + STATES)
#define IS_REP0_LONG	(IS_REP_G2 + STATES)			/* STATES << POS_MAX */
#define POS_SLOT	(IS_REP0_LONG + (STATES << POS_MAX))	/* 4 << 6 */
#define POS_DEC		(POS_SLOT + (4 << 6))			/* 1 + FULL_DIST - END_POS_MODEL */
#define ALIGN		(POS_DEC + 1 + FULL_DIST - END_POS_MODEL)
#define LEN_DEC		(ALIGN + (1 << ALIGN_BITS))
#define LEN_SIZE	(2 + (16 << 3) + (16 << 3) + 256)	/* choice, choice2, low, mid, high */
#define REP_LEN_DEC	(LEN_DEC + LEN_SIZE)
#define LITERAL		(REP_LEN_DEC + LEN_SIZE)

_Static_assert(LITERAL <= 1856, "probability layout too big");

struct rc {
	const uint8_t *in, *end;
	uint32_t range, code;
	int err;
};

static inline uint8_t rc_byte(struct rc *rc)
{
	if (rc->in == rc->end) {
		rc->err = 1;
		return 0;
	}
	return *rc->in++;
}

static inline void rc_norm(struct rc *rc)
{
	if (rc->range < TOP) {
		rc->range <<= 8;
		rc->code = (rc->code << 8) | rc_byte(rc);
	}
}

static inline unsigned rc_bit(struct rc *rc, uint16_t *p)
{
	uint32_t bound = (rc->range >> BITS) * *p;
	unsigned bit;

	if (rc->code < bound) {
		rc->range = bound;
		*p += (BIT_ONE - *p) >> MOVE;
		bit = 0;
	} else {
		rc->range -= bound;
		rc->code -= bound;
		*p -= *p >> MOVE;
		bit = 1;
	}
	rc_norm(rc);
	return bit;
}

static uint32_t rc_direct(struct rc *rc, unsigned n)
{
	uint32_t res = 0;

	while (n--) {
		rc->range >>= 1;
		rc->code -= rc->range;
		uint32_t t = 0 - (rc->code >> 31);	/* all ones if the subtraction wrapped */
		rc->code += rc->range & t;
		res = (res << 1) + (t + 1);
		rc_norm(rc);
	}
	return res;
}

static unsigned tree(struct rc *rc, uint16_t *p, unsigned bits)
{
	unsigned m = 1;

	for (unsigned i = 0; i < bits; i++)
		m = (m << 1) + rc_bit(rc, p + m);
	return m - (1u << bits);
}

static unsigned tree_rev(struct rc *rc, uint16_t *p, unsigned bits)
{
	unsigned m = 1, sym = 0;

	for (unsigned i = 0; i < bits; i++) {
		unsigned bit = rc_bit(rc, p + m);

		m = (m << 1) + bit;
		sym |= bit << i;
	}
	return sym;
}

static unsigned len_decode(struct rc *rc, uint16_t *l, unsigned pos_state)
{
	if (!rc_bit(rc, l + 0))
		return tree(rc, l + 2 + (pos_state << 3), 3);
	if (!rc_bit(rc, l + 1))
		return 8 + tree(rc, l + 2 + (16 << 3) + (pos_state << 3), 3);
	return 16 + tree(rc, l + 2 + (16 << 3) + (16 << 3), 8);
}

static uint32_t dist_decode(struct rc *rc, uint16_t *p, unsigned len)
{
	unsigned len_state = len < 3 ? len : 3;
	unsigned slot = tree(rc, p + POS_SLOT + (len_state << 6), 6);

	if (slot < 4)
		return slot;
	unsigned direct = (slot >> 1) - 1;
	uint32_t dist = (2 | (slot & 1)) << direct;

	if (slot < END_POS_MODEL)
		return dist + tree_rev(rc, p + POS_DEC + dist - slot, direct);
	dist += rc_direct(rc, direct - ALIGN_BITS) << ALIGN_BITS;
	return dist + tree_rev(rc, p + ALIGN, ALIGN_BITS);
}

int64_t lzma_size(const uint8_t *in)
{
	uint64_t s = 0;

	for (int i = 7; i >= 0; i--)
		s = (s << 8) | in[5 + i];
	return s == ~0ull ? -1 : (int64_t)s;
}

int lzma_decode(const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t out_max,
		uint32_t *out_len, struct lzma_probs *probs)
{
	uint16_t *p = probs->p;
	unsigned d = in[0], lc, lp, pb;
	uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0, pos = 0, size;
	unsigned state = 0;
	int64_t hsize;
	struct rc rc;

	*out_len = 0;
	if (in_len < LZMA_HEADER + 5 || d >= 9 * 5 * 5)
		return LZMA_ERR_HEADER;
	lc = d % 9;
	d /= 9;
	lp = d % 5;
	pb = d / 5;
	if (lc + lp > LZMA_MAX_LCLP || pb > POS_MAX)
		return LZMA_ERR_HEADER;
	hsize = lzma_size(in);
	size = hsize < 0 || hsize > out_max ? out_max : (uint32_t)hsize;
	if (hsize > (int64_t)out_max)
		return LZMA_ERR_OUTPUT;

	for (unsigned i = 0; i < LITERAL + (0x300u << (lc + lp)); i++)
		p[i] = BIT_ONE >> 1;

	rc.in = in + LZMA_HEADER;
	rc.end = in + in_len;
	rc.err = 0;
	if (rc_byte(&rc) != 0)
		return LZMA_ERR_DATA;
	rc.range = 0xFFFFFFFF;
	rc.code = 0;
	for (int i = 0; i < 4; i++)
		rc.code = (rc.code << 8) | rc_byte(&rc);

	while (pos < size) {
		unsigned pos_state = pos & ((1u << pb) - 1);
		unsigned len;

		if (rc.err)
			return LZMA_ERR_INPUT;
		if (!rc_bit(&rc, p + IS_MATCH + (state << POS_MAX) + pos_state)) {
			/* literal */
			unsigned prev = pos ? out[pos - 1] : 0;
			uint16_t *lit = p + LITERAL + 0x300 *
				(((pos & ((1u << lp) - 1)) << lc) + (prev >> (8 - lc)));
			unsigned sym = 1;

			if (state >= 7) {
				unsigned match = out[pos - rep0 - 1];

				do {
					unsigned mbit = (match >> 7) & 1, bit;

					match <<= 1;
					bit = rc_bit(&rc, lit + ((1 + mbit) << 8) + sym);
					sym = (sym << 1) | bit;
					if (mbit != bit)
						break;
				} while (sym < 0x100);
			}
			while (sym < 0x100)
				sym = (sym << 1) | rc_bit(&rc, lit + sym);
			out[pos++] = sym;
			state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
			continue;
		}

		if (rc_bit(&rc, p + IS_REP + state)) {
			if (pos == 0)
				return LZMA_ERR_DATA;
			if (!rc_bit(&rc, p + IS_REP_G0 + state)) {
				if (!rc_bit(&rc, p + IS_REP0_LONG + (state << POS_MAX) + pos_state)) {
					/* short rep: one byte from rep0 */
					state = state < 7 ? 9 : 11;
					out[pos] = out[pos - rep0 - 1];
					pos++;
					continue;
				}
			} else {
				uint32_t dist;

				if (!rc_bit(&rc, p + IS_REP_G1 + state)) {
					dist = rep1;
				} else {
					if (!rc_bit(&rc, p + IS_REP_G2 + state)) {
						dist = rep2;
					} else {
						dist = rep3;
						rep3 = rep2;
					}
					rep2 = rep1;
				}
				rep1 = rep0;
				rep0 = dist;
			}
			len = len_decode(&rc, p + REP_LEN_DEC, pos_state);
			state = state < 7 ? 8 : 11;
		} else {
			rep3 = rep2;
			rep2 = rep1;
			rep1 = rep0;
			len = len_decode(&rc, p + LEN_DEC, pos_state);
			state = state < 7 ? 7 : 10;
			rep0 = dist_decode(&rc, p, len);
			if (rep0 == 0xFFFFFFFF)
				break;			/* end marker */
		}
		len += 2;
		if (rep0 >= pos)
			return LZMA_ERR_DATA;
		while (len-- && pos < size) {
			out[pos] = out[pos - rep0 - 1];
			pos++;
		}
	}
	if (rc.err)
		return LZMA_ERR_INPUT;
	*out_len = pos;
	return LZMA_OK;
}
