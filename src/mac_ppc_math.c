#include "mac_ppc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MathLib: the C math functions the host's libm computes on IEEE doubles. Arguments arrive in f1, f2 and results
 * return in f1 (integers in r3). Results follow the host libm, which may differ from the original library in the last
 * bit; of the decimal-record conversions only num2dec exists (dec2num and str2dec are not implemented). */
#define UNARY(fn) static int m_##fn(PpcCpu *c, void *u) { (void)u; ppc_fpr_set(c, 1, fn(ppc_fpr_get(c, 1))); return 0; }
#define BINARY(fn) static int m_##fn(PpcCpu *c, void *u) { (void)u; ppc_fpr_set(c, 1, fn(ppc_fpr_get(c, 1), ppc_fpr_get(c, 2))); return 0; }
UNARY(ceil) UNARY(floor) UNARY(fabs) UNARY(sin) UNARY(cos) UNARY(tan) UNARY(asin) UNARY(acos) UNARY(atan) UNARY(exp2)
UNARY(round) UNARY(exp) UNARY(log) UNARY(log10) UNARY(trunc) UNARY(rint)
BINARY(pow) BINARY(fmod) BINARY(atan2)

static int m_modf(PpcCpu *c, void *u) { /* the integral part is stored through the pointer in r4 */
    (void)u;
    double whole, part = modf(ppc_fpr_get(c, 1), &whole);
    uint64_t bits;
    memcpy(&bits, &whole, sizeof bits);
    if (ppc_mem_write64(c, c->gpr[4], bits)) ppc_fpr_set(c, 1, part);
    return 0;
}

static int m_roundtol(PpcCpu *c, void *u) { /* nearest, halfway away from zero, saturating */
    (void)u;
    double v = round(ppc_fpr_get(c, 1));
    c->gpr[3] = v != v ? UINT32_C(0x80000000) : v >= 2147483647.0 ? UINT32_C(0x7fffffff)
              : v <= -2147483648.0 ? UINT32_C(0x80000000) : (uint32_t)(int32_t)v;
    return 0;
}

static int m_signbit(PpcCpu *c, void *u) { (void)u; c->gpr[3] = (uint32_t)(c->fpr[1] >> 63); return 0; }

static int m_fpclassify(PpcCpu *c, void *u) { /* 0 signalling NaN, 1 quiet NaN, 2 infinite, 3 zero, 4 normal, 5 subnormal */
    (void)u;
    uint64_t bits = c->fpr[1], exponent = (bits >> 52) & 0x7ff, fraction = bits & UINT64_C(0xfffffffffffff);
    c->gpr[3] = exponent == 0x7ff ? (fraction ? ((fraction >> 51) ? 1u : 0u) : 2u) : exponent ? 4u : fraction ? 5u : 3u;
    return 0;
}

/* num2dec(const decform *f, double x, decimal *d). decform: style (0 FLOATDECIMAL, 1 FIXEDDECIMAL) and digits, two shorts. decimal: sgn (a byte at 0),
 * exp (short at 2) and sig (a Pascal string of at most 31 digits at 4); the value is sig * 10^exp. FLOATDECIMAL rounds to `digits` significant digits and
 * drops trailing zeros; FIXEDDECIMAL rounds to `digits` places after the point and keeps exp = -digits. Zero is "0", infinity "I", a NaN "N". The
 * layout and rounding are from memory of the public numerics interface and have not been compared with the original library. */
static int m_num2dec(PpcCpu *c, void *u) {
    (void)u;
    uint8_t *form = ppc_mem_span(c, c->gpr[3], 4), *out = ppc_mem_span(c, c->gpr[4], 36);
    if (!form || !out) { ppc_cpu_halt(c, "num2dec was given a pointer outside guest RAM."); return 0; }
    int style = (int16_t)(form[0] << 8 | form[1]), digits = (int16_t)(form[2] << 8 | form[3]);
    double x = ppc_fpr_get(c, 1);
    memset(out, 0, 36);
    out[0] = signbit(x) ? 1 : 0;
    char digs[400]; int n = 0, exp10 = 0;
    if (x != x) { digs[0] = 'N'; n = 1; }
    else if (isinf(x)) { digs[0] = 'I'; n = 1; }
    else if (x == 0) { digs[0] = '0'; n = 1; }
    else if (style == 0) {
        char buf[64];
        if (digits < 1) digits = 1;
        if (digits > 31) digits = 31;
        snprintf(buf, sizeof buf, "%.*e", digits - 1, fabs(x));
        char *e = strchr(buf, 'e');
        exp10 = atoi(e + 1);
        for (char *p = buf; p < e; p++) if (*p >= '0' && *p <= '9') digs[n++] = *p;
        exp10 -= n - 1;
        while (n > 1 && digs[n - 1] == '0') { n--; exp10++; }
    } else {
        char buf[400];
        if (digits < 0) digits = 0;
        if (digits > 30) digits = 30;
        snprintf(buf, sizeof buf, "%.*f", digits, fabs(x));
        const char *p = buf;
        for (; *p; p++) if (*p >= '0' && *p <= '9' && !(n == 0 && *p == '0')) digs[n++] = *p;
        if (!n) digs[n++] = '0';
        exp10 = -digits;
        while (n > 31) { n--; exp10++; }
    }
    out[2] = (uint8_t)((uint16_t)exp10 >> 8); out[3] = (uint8_t)exp10;
    out[4] = (uint8_t)n; memcpy(out + 5, digs, (size_t)n);
    return 0;
}

#define M(name, fn) {"MathLib", name, fn, NULL}
static const MacCfmHostImport imports[] = {
    M("ceil", m_ceil), M("floor", m_floor), M("fabs", m_fabs), M("sin", m_sin), M("cos", m_cos), M("tan", m_tan),
    M("asin", m_asin), M("acos", m_acos), M("atan", m_atan), M("exp2", m_exp2), M("round", m_round), M("exp", m_exp),
    M("log", m_log), M("log10", m_log10), M("trunc", m_trunc), M("rint", m_rint), M("pow", m_pow), M("fmod", m_fmod),
    M("atan2", m_atan2), M("modf", m_modf), M("roundtol", m_roundtol), M("__signbitd", m_signbit),
    M("__fpclassifyd", m_fpclassify), M("num2dec", m_num2dec),
};

const MacCfmHostImport *mac_ppc_math_imports(size_t *count) {
    *count = sizeof imports / sizeof imports[0];
    return imports;
}
