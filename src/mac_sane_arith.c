#include "mac_sane_arith.h"

/* Extended-precision add, subtract, multiply and divide with the four IEEE rounding directions, using only 128-bit integers.
 * Operands must be zero or normalized finite numbers; anything else is refused by name (see the header), never guessed. A magnitude is
 * kept as `mag * 2^scale` with a sticky bit folded into its lowest bit; rounding to the 64-bit significand then looks at the discarded
 * bits. The inexact flag is whether any discarded bit was set. */
typedef unsigned __int128 u128;
static int top_bit(u128 v) { int n = 0; while (v >>= 1) n++; return n; }

static int pack(int negative, u128 mag, int scale, unsigned rounding, Extended *out, int *inexact, const char **why) {
    if (!mag) { out->sign_exp = (uint16_t)(rounding == 2 ? 0x8000 : 0); out->significand = 0; return 0; }   /* exact cancellation: +0, or -0 when rounding down */
    int top = top_bit(mag), shift = top - 63;
    u128 sig, rem = 0, half = 0;
    if (shift > 0) { sig = mag >> shift; rem = mag & (((u128)1 << shift) - 1); half = (u128)1 << (shift - 1); }
    else sig = mag << -shift;
    if (rem) {
        int up = rounding == 0 ? (rem > half || (rem == half && (sig & 1))) : rounding == 1 ? !negative : rounding == 2 ? negative : 0;
        if (up) { sig++; if (sig >> 64) { sig >>= 1; shift++; } }
        *inexact = 1;
    }
    int exponent = scale + shift + 63 + 16383;
    if (exponent < 1 || exponent > 0x7ffe) { *why = exponent < 1 ? "SANE extended underflow is unsupported." : "SANE extended overflow is unsupported."; return -1; }
    out->sign_exp = (uint16_t)((negative ? 0x8000 : 0) | exponent);
    out->significand = (uint64_t)sig;
    return 0;
}

static int normal_or_zero(Extended x, const char **why) {
    unsigned e = x.sign_exp & 0x7fff;
    if (e == 0x7fff) { *why = "SANE arithmetic on infinities and NaNs is unsupported."; return -1; }
    if (e == 0 && x.significand) { *why = "SANE arithmetic on denormals is unsupported."; return -1; }
    if (e && !(x.significand >> 63)) { *why = "SANE arithmetic on unnormalized operands is unsupported."; return -1; }
    return 0;
}

int mac_sane_arith(unsigned op, Extended a, Extended b, unsigned rounding, Extended *out, int *inexact, const char **why) {
    if (normal_or_zero(a, why) || normal_or_zero(b, why)) return -1;
    int sa = (a.sign_exp & 0x8000) != 0, sb = (b.sign_exp & 0x8000) != 0;
    if (op == 2) sb = !sb;                                           /* a - b = a + (-b) */
    int za = !a.significand, zb = !b.significand;
    int ea = (int)(a.sign_exp & 0x7fff) - 16383, eb = (int)(b.sign_exp & 0x7fff) - 16383;
    *inexact = 0;
    if (op == 0 || op == 2) {
        if (za && zb) { out->sign_exp = (uint16_t)((sa && sb) || (rounding == 2 && sa != sb) ? 0x8000 : 0); out->significand = 0; return 0; }
        if (zb) { *out = a; return 0; }
        if (za) { *out = b; if (sb != ((b.sign_exp & 0x8000) != 0)) out->sign_exp ^= 0x8000; return 0; }
        Extended hi = a, lo = b; int shi = sa, slo = sb, ehi = ea, elo = eb;
        if (eb > ea || (eb == ea && b.significand > a.significand)) { hi = b; lo = a; shi = sb; slo = sa; ehi = eb; elo = ea; }
        u128 x = (u128)hi.significand << 63, y = (u128)lo.significand << 63;
        int d = ehi - elo;
        if (d >= 127) y = y ? 1 : 0;
        else if (d > 0) { u128 lost = y & (((u128)1 << d) - 1); y = (y >> d) | (lost ? 1 : 0); }
        u128 mag; int neg;
        if (shi == slo) { mag = x + y; neg = shi; } else { mag = x - y; neg = shi; }
        return pack(neg, mag, ehi - 63 - 63, rounding, out, inexact, why);
    }
    int neg = sa ^ ((b.sign_exp & 0x8000) != 0);
    if (op == 4) {
        if (za || zb) { out->sign_exp = (uint16_t)(neg ? 0x8000 : 0); out->significand = 0; return 0; }
        return pack(neg, (u128)a.significand * b.significand, ea - 63 + eb - 63, rounding, out, inexact, why);
    }
    if (op == 6) {
        if (zb) { *why = "SANE division by zero is unsupported."; return -1; }
        if (za) { out->sign_exp = (uint16_t)(neg ? 0x8000 : 0); out->significand = 0; return 0; }
        u128 num = (u128)a.significand << 63, den = b.significand;
        u128 q1 = num / den, r = num % den;
        u128 num2 = r << 64, q2 = num2 / den, r2 = num2 % den;
        u128 mag = (q1 << 64) | q2;
        if (r2) mag |= 1;
        return pack(neg, mag, (ea - eb) - 127, rounding, out, inexact, why);
    }
    *why = "SANE arithmetic selector is unsupported."; return -1;
}

int mac_sane_to_float(Extended x, unsigned mant_bits, unsigned exp_bits, unsigned rounding, uint64_t *bits, int *inexact, const char **why) {
    unsigned e = x.sign_exp & 0x7fff, total = 1 + exp_bits + mant_bits;
    uint64_t sign = (uint64_t)((x.sign_exp >> 15) & 1) << (total - 1), allones = ((uint64_t)1 << exp_bits) - 1;
    int negative = (x.sign_exp & 0x8000) != 0;
    *inexact = 0;
    if (e == 0x7fff) {
        if (x.significand != ((uint64_t)1 << 63)) { *why = "SANE NaN conversion is unsupported."; return -1; }
        *bits = sign | (allones << mant_bits); return 0;
    }
    if (e == 0) {
        if (x.significand) { *why = "SANE conversion of denormals is unsupported."; return -1; }
        *bits = sign; return 0;
    }
    if (!(x.significand >> 63)) { *why = "SANE conversion of unnormalized operands is unsupported."; return -1; }
    unsigned keep = mant_bits + 1, drop = 64 - keep;
    uint64_t sig = x.significand >> drop, rem = x.significand & (((uint64_t)1 << drop) - 1), half = (uint64_t)1 << (drop - 1);
    int exponent = (int)e - 16383;
    if (rem) {
        int up = rounding == 0 ? (rem > half || (rem == half && (sig & 1))) : rounding == 1 ? !negative : rounding == 2 ? negative : 0;
        if (up) { sig++; if (sig >> keep) { sig >>= 1; exponent++; } }
        *inexact = 1;
    }
    int bias = (int)(allones >> 1), biased = exponent + bias;
    if (biased < 1 || biased >= (int)allones) { *why = biased < 1 ? "SANE conversion underflow is unsupported." : "SANE conversion overflow is unsupported."; return -1; }
    *bits = sign | ((uint64_t)biased << mant_bits) | (sig & (((uint64_t)1 << mant_bits) - 1));
    return 0;
}

int mac_sane_from_double(uint64_t bits, Extended *out, const char **why) {
    uint64_t fraction = bits & (((uint64_t)1 << 52) - 1);
    unsigned exponent = (unsigned)((bits >> 52) & 0x7ff), sign = (unsigned)(bits >> 63) << 15;
    out->significand = 0; out->sign_exp = (uint16_t)sign;
    if (exponent == 0x7ff) {
        if (fraction) { *why = "SANE double NaN conversion is unsupported."; return -1; }
        out->sign_exp |= 0x7fff; out->significand = (uint64_t)1 << 63; return 0;
    }
    if (exponent) { out->sign_exp |= (uint16_t)(exponent + 16383 - 1023); out->significand = (((uint64_t)1 << 52) | fraction) << 11; return 0; }
    if (fraction) {
        unsigned highest = 0; for (uint64_t v = fraction; v >>= 1;) highest++;
        out->sign_exp |= (uint16_t)(16383 - 1074 + (int)highest);
        out->significand = fraction << (63 - highest);
    }
    return 0;
}
