#ifndef MAC_SANE_ARITH_H
#define MAC_SANE_ARITH_H
#include <stdint.h>
/* SANE 80-bit extended: sign and 15-bit biased exponent in one word, an explicit 64-bit significand. */
typedef struct { uint16_t sign_exp; uint64_t significand; } Extended;
/* dst op src for FADDX (op 0), FSUBX (2), FMULX (4), FDIVX (6), in integer arithmetic only.
 * rounding: 0 to nearest even, 1 toward +infinity, 2 toward -infinity, 3 toward zero (the environment's rounding bits).
 * Returns 0 and sets *out and *inexact, or -1 with *why naming what is unsupported: infinities, NaNs, denormals, division by zero,
 * and results that overflow or underflow the normal range. */
int mac_sane_arith(unsigned op, Extended dst, Extended src, unsigned rounding, Extended *out, int *inexact, const char **why);
/* Extended to single (mant_bits 23, exp_bits 8) or double (52, 11), written to *bits. Zero and infinity convert; NaNs, results that are
 * denormal or overflow the target, and so on, are refused by name. */
int mac_sane_to_float(Extended x, unsigned mant_bits, unsigned exp_bits, unsigned rounding, uint64_t *bits, int *inexact, const char **why);
/* Double to extended (exact). NaNs are refused; denormal doubles are normalized. */
int mac_sane_from_double(uint64_t bits, Extended *out, const char **why);
#endif
