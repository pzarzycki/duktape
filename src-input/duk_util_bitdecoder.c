/*
 *  Bitstream decoder.
 */

#include "duk_internal.h"

/* Decode 'bits' bits from the input stream (bits must be 1...24).
 * When reading past bitstream end, zeroes are shifted in.  The result
 * is signed to match duk_bd_decode_flagged.
 */
DUK_INTERNAL duk_int32_t duk_bd_decode(duk_bitdecoder_ctx *ctx, duk_small_int_t bits) {
	duk_small_int_t shift;
	duk_uint32_t mask;
	duk_uint32_t tmp;

	/* Note: cannot read more than 24 bits without possibly shifting top bits out.
	 * Fixable, but adds complexity.
	 */
	DUK_ASSERT(bits >= 1 && bits <= 24);

	while (ctx->currbits < bits) {
#if 0
		DUK_DDD(DUK_DDDPRINT("decode_bits: shift more data (bits=%ld, currbits=%ld)",
		                     (long) bits, (long) ctx->currbits));
#endif
		ctx->currval <<= 8;
		if (ctx->offset < ctx->length) {
			/* If ctx->offset >= ctx->length, we "shift zeroes in"
			 * instead of croaking.
			 */
			ctx->currval |= ctx->data[ctx->offset++];
		}
		ctx->currbits += 8;
	}
#if 0
	DUK_DDD(DUK_DDDPRINT("decode_bits: bits=%ld, currbits=%ld, currval=0x%08lx",
	                     (long) bits, (long) ctx->currbits, (unsigned long) ctx->currval));
#endif

	/* Extract 'top' bits of currval; note that the extracted bits do not need
	 * to be cleared, we just ignore them on next round.
	 */
	shift = ctx->currbits - bits;
	mask = (1 << bits) - 1;
	tmp = (ctx->currval >> shift) & mask;
	ctx->currbits = shift;  /* remaining */

#if 0
	DUK_DDD(DUK_DDDPRINT("decode_bits: %ld bits -> 0x%08lx (%ld), currbits=%ld, currval=0x%08lx",
	                     (long) bits, (unsigned long) tmp, (long) tmp, (long) ctx->currbits, (unsigned long) ctx->currval));
#endif

	return tmp;
}

DUK_INTERNAL duk_small_int_t duk_bd_decode_flag(duk_bitdecoder_ctx *ctx) {
	return (duk_small_int_t) duk_bd_decode(ctx, 1);
}

/* Decode a one-bit flag, and if set, decode a value of 'bits', otherwise return
 * default value.  Return value is signed so that negative marker value can be
 * used by caller as a "not present" value.
 */
DUK_INTERNAL duk_int32_t duk_bd_decode_flagged(duk_bitdecoder_ctx *ctx, duk_small_int_t bits, duk_int32_t def_value) {
	if (duk_bd_decode_flag(ctx)) {
		return (duk_int32_t) duk_bd_decode(ctx, bits);
	} else {
		return def_value;
	}
}

/* Decode a bit packed string from a custom format used by genbuiltins.py.
 * This function is here because it's used for both heap and thread inits.
 * Caller must supply the output buffer whose size is NOT checked!
 */

#define DUK__BITPACK_LETTER_LIMIT  26
#define DUK__BITPACK_LOOKUP1       26
#define DUK__BITPACK_LOOKUP2       27
#define DUK__BITPACK_SWITCH1       28
#define DUK__BITPACK_SWITCH        29
#define DUK__BITPACK_UNUSED1       30
#define DUK__BITPACK_EIGHTBIT      31

DUK_LOCAL const duk_uint8_t duk__bitpacked_lookup[16] = {
	DUK_ASC_0, DUK_ASC_1, DUK_ASC_2, DUK_ASC_3,
	DUK_ASC_4, DUK_ASC_5, DUK_ASC_6, DUK_ASC_7,
	DUK_ASC_8, DUK_ASC_9, DUK_ASC_UNDERSCORE, DUK_ASC_SPACE,
	0xff, 0x80, DUK_ASC_DOUBLEQUOTE, DUK_ASC_LCURLY
};

DUK_INTERNAL duk_small_uint_t duk_bd_decode_bitpacked_string(duk_bitdecoder_ctx *bd, duk_uint8_t *out) {
	duk_small_uint_t len;
	duk_small_uint_t mode;
	duk_small_uint_t t;
	duk_small_uint_t i;

	len = duk_bd_decode(bd, 5);
	if (len == 31) {
		len = duk_bd_decode(bd, 8);  /* Support up to 256 bytes; rare. */
	}

	mode = 32;  /* 0 = uppercase, 32 = lowercase (= 'a' - 'A') */
	for (i = 0; i < len; i++) {
		t = duk_bd_decode(bd, 5);
		if (t < DUK__BITPACK_LETTER_LIMIT) {
			t = t + DUK_ASC_UC_A + mode;
		} else if (t == DUK__BITPACK_LOOKUP1) {
			t = duk__bitpacked_lookup[duk_bd_decode(bd, 3)];
		} else if (t == DUK__BITPACK_LOOKUP2) {
			t = duk__bitpacked_lookup[8 + duk_bd_decode(bd, 3)];
		} else if (t == DUK__BITPACK_SWITCH1) {
			t = duk_bd_decode(bd, 5);
			DUK_ASSERT_DISABLE(t >= 0);  /* unsigned */
			DUK_ASSERT(t <= 25);
			t = t + DUK_ASC_UC_A + (mode ^ 32);
		} else if (t == DUK__BITPACK_SWITCH) {
			mode = mode ^ 32;
			t = duk_bd_decode(bd, 5);
			DUK_ASSERT_DISABLE(t >= 0);
			DUK_ASSERT(t <= 25);
			t = t + DUK_ASC_UC_A + mode;
		} else if (t == DUK__BITPACK_EIGHTBIT) {
			t = duk_bd_decode(bd, 8);
		}
		out[i] = (duk_uint8_t) t;
	}

	return len;
}
