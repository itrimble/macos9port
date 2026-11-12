#include "ppc_int.h"

#include <math.h>
#include <string.h>

/* Floating point follows the host's IEEE-754 doubles in round-to-nearest. The
 * FPSCR rounding-mode field is honoured only by fctiw; exception enables are
 * ignored (no floating-point exceptions are delivered to the guest). */

static uint32_t result_class(double v) {
    int negative = signbit(v) != 0;
    switch (fpclassify(v)) {
    case FP_NAN: return 0x11;
    case FP_INFINITE: return negative ? 0x09 : 0x05;
    case FP_ZERO: return negative ? 0x12 : 0x02;
    case FP_SUBNORMAL: return negative ? 0x18 : 0x14;
    default: return negative ? 0x08 : 0x04;
    }
}

static void raise_flag(PpcCpu *c, uint32_t flag) {
    if (!(c->fpscr & flag)) c->fpscr |= PPC_FPSCR_FX;
    c->fpscr |= flag;
    if (flag & (PPC_FPSCR_VXSNAN | PPC_FPSCR_VXVC | PPC_FPSCR_VXCVI)) c->fpscr |= PPC_FPSCR_VX;
}

static void record_cr1(PpcCpu *c, uint32_t w) {
    if (w & 1u) ppc_set_crf(c, 1, c->fpscr >> 28);
}

static void arithmetic_result(PpcCpu *c, uint32_t w, double v, int single) {
    if (single) v = (double)(float)v;
    ppc_fpr_set(c, PPC_RD(w), v);
    c->fpscr = (c->fpscr & ~PPC_FPSCR_FPRF) | (result_class(v) << 12);
    record_cr1(c, w);
}

static int is_signalling(uint64_t bits) {
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) && !(bits & UINT64_C(0x0008000000000000));
}

static void compare(PpcCpu *c, uint32_t w, int ordered) {
    double a = ppc_fpr_get(c, PPC_RA(w)), b = ppc_fpr_get(c, PPC_RB(w));
    uint32_t v = isnan(a) || isnan(b) ? PPC_CR_SO : a < b ? PPC_CR_LT : a > b ? PPC_CR_GT : PPC_CR_EQ;
    if (v == PPC_CR_SO) {
        if (is_signalling(c->fpr[PPC_RA(w)]) || is_signalling(c->fpr[PPC_RB(w)])) raise_flag(c, PPC_FPSCR_VXSNAN);
        else if (ordered) raise_flag(c, PPC_FPSCR_VXVC);
    }
    c->fpscr = (c->fpscr & ~UINT32_C(0x0000f000)) | (v << 12);
    ppc_set_crf(c, PPC_CRFD(w), v);
}

static void to_integer(PpcCpu *c, uint32_t w, int truncate) {
    double v = ppc_fpr_get(c, PPC_RB(w));
    uint32_t r;
    if (isnan(v)) { r = UINT32_C(0x80000000); raise_flag(c, PPC_FPSCR_VXCVI); }
    else {
        unsigned mode = truncate ? 1u : (c->fpscr & 3u);
        v = mode == 1 ? trunc(v) : mode == 2 ? ceil(v) : mode == 3 ? floor(v) : nearbyint(v);
        if (v > 2147483647.0) { r = UINT32_C(0x7fffffff); raise_flag(c, PPC_FPSCR_VXCVI); }
        else if (v < -2147483648.0) { r = UINT32_C(0x80000000); raise_flag(c, PPC_FPSCR_VXCVI); }
        else r = (uint32_t)(int32_t)v;
    }
    c->fpr[PPC_RD(w)] = UINT64_C(0xfff8000000000000) | r; /* the upper word is undefined by the architecture */
    record_cr1(c, w);
}

static int fpscr_move(PpcCpu *c, uint32_t w) {
    unsigned d = PPC_RD(w);
    switch (PPC_XO(w)) {
    case 583: c->fpr[d] = UINT64_C(0xfff8000000000000) | c->fpscr; break;        /* mffs */
    case 711: {                                                                  /* mtfsf */
        uint32_t m = 0, v = (uint32_t)c->fpr[PPC_RB(w)];
        for (unsigned i = 0; i < 8; i++)
            if (w & (UINT32_C(0x01000000) >> i)) m |= UINT32_C(0xf0000000) >> (4u * i);
        c->fpscr = (c->fpscr & ~m) | (v & m);
        break;
    }
    case 134: {                                                                  /* mtfsfi */
        unsigned shift = 28u - 4u * PPC_CRFD(w);
        c->fpscr = (c->fpscr & ~(UINT32_C(0xf) << shift)) | (((w >> 12) & 0xfu) << shift);
        break;
    }
    case 70: c->fpscr &= ~(UINT32_C(0x80000000) >> d); break;                    /* mtfsb0 */
    case 38: c->fpscr |= UINT32_C(0x80000000) >> d; break;                       /* mtfsb1 */
    default: return 0;
    }
    record_cr1(c, w);
    return 1;
}

int ppc_exec_fp(PpcCpu *c, uint32_t w) {
    int single = PPC_OP(w) == 59;
    unsigned d = PPC_RD(w);
    double a = ppc_fpr_get(c, PPC_RA(w)), b = ppc_fpr_get(c, PPC_RB(w)), m = ppc_fpr_get(c, PPC_RC(w));
    unsigned xo5 = (w >> 1) & 31u;
    if (xo5 >= 18) { /* A-form: the extended opcode is five bits and frC is live */
        switch (xo5) {
        case 21: arithmetic_result(c, w, a + b, single); return 1;               /* fadd(s) */
        case 20: arithmetic_result(c, w, a - b, single); return 1;               /* fsub(s) */
        case 25: arithmetic_result(c, w, a * m, single); return 1;               /* fmul(s) */
        case 18:                                                                 /* fdiv(s) */
            if (b == 0.0 && isfinite(a) && a != 0.0) raise_flag(c, PPC_FPSCR_ZX);
            arithmetic_result(c, w, a / b, single);
            return 1;
        case 29: arithmetic_result(c, w, fma(a, m, b), single); return 1;        /* fmadd(s) */
        case 28: arithmetic_result(c, w, fma(a, m, -b), single); return 1;       /* fmsub(s) */
        case 31: arithmetic_result(c, w, -fma(a, m, b), single); return 1;       /* fnmadd(s) */
        case 30: arithmetic_result(c, w, -fma(a, m, -b), single); return 1;      /* fnmsub(s) */
        default: return 0;
        }
    }
    if (single) return 0;
    switch (PPC_XO(w)) {
    case 0: compare(c, w, 0); return 1;                                          /* fcmpu */
    case 32: compare(c, w, 1); return 1;                                         /* fcmpo */
    case 72: c->fpr[d] = c->fpr[PPC_RB(w)]; break;                               /* fmr */
    case 40: c->fpr[d] = c->fpr[PPC_RB(w)] ^ UINT64_C(0x8000000000000000); break; /* fneg */
    case 264: c->fpr[d] = c->fpr[PPC_RB(w)] & ~UINT64_C(0x8000000000000000); break; /* fabs */
    case 136: c->fpr[d] = c->fpr[PPC_RB(w)] | UINT64_C(0x8000000000000000); break; /* fnabs */
    case 12: arithmetic_result(c, w, b, 1); return 1;                            /* frsp */
    case 14: to_integer(c, w, 0); return 1;                                      /* fctiw */
    case 15: to_integer(c, w, 1); return 1;                                      /* fctiwz */
    default: return fpscr_move(c, w);
    }
    record_cr1(c, w);
    return 1;
}

static int transfer(PpcCpu *c, uint32_t w, uint32_t ea, int is_double, int store, int update) {
    unsigned d = PPC_RD(w), ra = PPC_RA(w);
    if (update && !ra) return 0;
    if (is_double) {
        if (store ? !ppc_mem_write64(c, ea, c->fpr[d]) : !ppc_mem_read64(c, ea, &c->fpr[d])) return 1;
    } else if (store) {
        float f = (float)ppc_fpr_get(c, d);
        uint32_t bits;
        memcpy(&bits, &f, sizeof bits);
        if (!ppc_mem_write(c, ea, 4, bits)) return 1;
    } else {
        uint32_t bits;
        float f;
        if (!ppc_mem_read(c, ea, 4, &bits)) return 1;
        memcpy(&f, &bits, sizeof f);
        ppc_fpr_set(c, d, (double)f);
    }
    if (update) c->gpr[ra] = ea;
    return 1;
}

int ppc_exec_fp_mem(PpcCpu *c, uint32_t w) {
    unsigned op = PPC_OP(w), ra = PPC_RA(w);
    uint32_t base = ra ? c->gpr[ra] : 0u;
    if (op != 31) { /* 48 lfs, 49 lfsu, 50 lfd, 51 lfdu, 52 stfs, 53 stfsu, 54 stfd, 55 stfdu */
        if (op < 48 || op > 55) return 0;
        return transfer(c, w, base + PPC_SIMM(w), (op & 2u) != 0, op >= 52, (int)(op & 1u));
    }
    uint32_t ea = base + c->gpr[PPC_RB(w)];
    switch (PPC_XO(w)) {
    case 535: return transfer(c, w, ea, 0, 0, 0);                                /* lfsx */
    case 567: return transfer(c, w, ea, 0, 0, 1);                                /* lfsux */
    case 599: return transfer(c, w, ea, 1, 0, 0);                                /* lfdx */
    case 631: return transfer(c, w, ea, 1, 0, 1);                                /* lfdux */
    case 663: return transfer(c, w, ea, 0, 1, 0);                                /* stfsx */
    case 695: return transfer(c, w, ea, 0, 1, 1);                                /* stfsux */
    case 727: return transfer(c, w, ea, 1, 1, 0);                                /* stfdx */
    case 759: return transfer(c, w, ea, 1, 1, 1);                                /* stfdux */
    case 983: ppc_mem_write(c, ea, 4, (uint32_t)c->fpr[PPC_RD(w)]); return 1;    /* stfiwx */
    default: return 0;
    }
}
