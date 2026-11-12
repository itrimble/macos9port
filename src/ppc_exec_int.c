#include "ppc_int.h"

/* x + y + carry_in with the carry out of bit 0 and the signed overflow. */
static uint32_t add3(uint32_t x, uint32_t y, uint32_t carry_in, int *carry, int *overflow) {
    uint64_t wide = (uint64_t)x + y + carry_in;
    uint32_t r = (uint32_t)wide;
    *carry = (int)(wide >> 32);
    *overflow = (int)(((x ^ r) & (y ^ r)) >> 31);
    return r;
}

/* Opcode 31 arithmetic with the OE bit: the extended opcode is nine bits wide here. */
static int arithmetic(PpcCpu *c, uint32_t w) {
    uint32_t a = c->gpr[PPC_RA(w)], b = c->gpr[PPC_RB(w)], r = 0;
    uint32_t cin = (c->xer & PPC_XER_CA) ? 1u : 0u;
    int carry = 0, overflow = 0, sets_carry = 0, has_oe = 1;
    switch ((w >> 1) & 0x1ffu) {
    case 266: r = add3(a, b, 0, &carry, &overflow); break;                                  /* add */
    case 10: r = add3(a, b, 0, &carry, &overflow); sets_carry = 1; break;                   /* addc */
    case 138: r = add3(a, b, cin, &carry, &overflow); sets_carry = 1; break;                /* adde */
    case 202: r = add3(a, 0, cin, &carry, &overflow); sets_carry = 1; break;                /* addze */
    case 234: r = add3(a, UINT32_C(0xffffffff), cin, &carry, &overflow); sets_carry = 1; break; /* addme */
    case 40: r = add3(~a, b, 1, &carry, &overflow); break;                                  /* subf */
    case 8: r = add3(~a, b, 1, &carry, &overflow); sets_carry = 1; break;                   /* subfc */
    case 136: r = add3(~a, b, cin, &carry, &overflow); sets_carry = 1; break;               /* subfe */
    case 200: r = add3(~a, 0, cin, &carry, &overflow); sets_carry = 1; break;               /* subfze */
    case 232: r = add3(~a, UINT32_C(0xffffffff), cin, &carry, &overflow); sets_carry = 1; break; /* subfme */
    case 104: r = add3(~a, 0, 1, &carry, &overflow); break;                                 /* neg */
    case 235: {                                                                             /* mullw */
        int64_t p = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
        r = (uint32_t)p;
        overflow = p != (int64_t)(int32_t)r;
        break;
    }
    case 75: r = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32); has_oe = 0; break; /* mulhw */
    case 11: r = (uint32_t)(((uint64_t)a * b) >> 32); has_oe = 0; break;                    /* mulhwu */
    case 491:                                                                               /* divw */
        if (!b || (a == UINT32_C(0x80000000) && b == UINT32_C(0xffffffff))) overflow = 1; /* quotient undefined: 0 */
        else r = (uint32_t)((int32_t)a / (int32_t)b);
        break;
    case 459:                                                                               /* divwu */
        if (!b) overflow = 1;
        else r = a / b;
        break;
    default:
        return 0;
    }
    c->gpr[PPC_RD(w)] = r;
    if (sets_carry) ppc_set_ca(c, carry);
    if (has_oe && (w & 0x400u)) ppc_set_ov(c, overflow);
    if (w & 1u) ppc_record(c, r);
    return 1;
}

static void compare_signed(PpcCpu *c, unsigned field, int32_t a, int32_t b) {
    uint32_t v = a < b ? PPC_CR_LT : a > b ? PPC_CR_GT : PPC_CR_EQ;
    ppc_set_crf(c, field, v | ((c->xer & PPC_XER_SO) ? PPC_CR_SO : 0u));
}

static void compare_unsigned(PpcCpu *c, unsigned field, uint32_t a, uint32_t b) {
    uint32_t v = a < b ? PPC_CR_LT : a > b ? PPC_CR_GT : PPC_CR_EQ;
    ppc_set_crf(c, field, v | ((c->xer & PPC_XER_SO) ? PPC_CR_SO : 0u));
}

int ppc_exec_int(PpcCpu *c, uint32_t w) {
    unsigned d = PPC_RD(w), ra = PPC_RA(w);
    uint32_t a = c->gpr[ra], s = c->gpr[d], r;
    int carry, overflow;
    switch (PPC_OP(w)) {
    case 7: c->gpr[d] = (uint32_t)((int64_t)(int32_t)a * (int64_t)(int32_t)PPC_SIMM(w)); return 1; /* mulli */
    case 8: c->gpr[d] = add3(~a, PPC_SIMM(w), 1, &carry, &overflow); ppc_set_ca(c, carry); return 1; /* subfic */
    case 10: compare_unsigned(c, PPC_CRFD(w), a, PPC_UIMM(w)); return 1;                       /* cmpli */
    case 11: compare_signed(c, PPC_CRFD(w), (int32_t)a, (int32_t)PPC_SIMM(w)); return 1;       /* cmpi */
    case 12: case 13:                                                                        /* addic, addic. */
        r = add3(a, PPC_SIMM(w), 0, &carry, &overflow);
        c->gpr[d] = r;
        ppc_set_ca(c, carry);
        if (PPC_OP(w) == 13) ppc_record(c, r);
        return 1;
    case 14: c->gpr[d] = (ra ? a : 0u) + PPC_SIMM(w); return 1;                               /* addi / li */
    case 15: c->gpr[d] = (ra ? a : 0u) + (PPC_UIMM(w) << 16); return 1;                       /* addis / lis */
    case 20: {                                                                               /* rlwimi */
        uint32_t m = ppc_mask(PPC_RC(w), PPC_ME(w));
        r = (ppc_rotl(s, PPC_RB(w)) & m) | (a & ~m);
        c->gpr[ra] = r;
        if (w & 1u) ppc_record(c, r);
        return 1;
    }
    case 21:                                                                                 /* rlwinm */
        r = ppc_rotl(s, PPC_RB(w)) & ppc_mask(PPC_RC(w), PPC_ME(w));
        c->gpr[ra] = r;
        if (w & 1u) ppc_record(c, r);
        return 1;
    case 23:                                                                                 /* rlwnm */
        r = ppc_rotl(s, c->gpr[PPC_RB(w)] & 31u) & ppc_mask(PPC_RC(w), PPC_ME(w));
        c->gpr[ra] = r;
        if (w & 1u) ppc_record(c, r);
        return 1;
    case 24: c->gpr[ra] = s | PPC_UIMM(w); return 1;                                         /* ori */
    case 25: c->gpr[ra] = s | (PPC_UIMM(w) << 16); return 1;                                 /* oris */
    case 26: c->gpr[ra] = s ^ PPC_UIMM(w); return 1;                                         /* xori */
    case 27: c->gpr[ra] = s ^ (PPC_UIMM(w) << 16); return 1;                                 /* xoris */
    case 28: r = s & PPC_UIMM(w); c->gpr[ra] = r; ppc_record(c, r); return 1;                /* andi. */
    case 29: r = s & (PPC_UIMM(w) << 16); c->gpr[ra] = r; ppc_record(c, r); return 1;        /* andis. */
    case 31: return arithmetic(c, w);
    default: return 0;
    }
}

static int spr_move(PpcCpu *c, uint32_t w, int to_spr) {
    unsigned spr = PPC_RA(w) | (PPC_RB(w) << 5);
    uint32_t *reg = spr == 1 ? &c->xer : spr == 8 ? &c->lr : spr == 9 ? &c->ctr : NULL;
    if (!reg) {
        ppc_cpu_halt(c, "PowerPC %s of special register %u is not available in user mode (instruction %08x at pc %08x).",
                     to_spr ? "mtspr" : "mfspr", spr, w, c->pc);
        return 1;
    }
    if (to_spr) *reg = spr == 1 ? (c->gpr[PPC_RD(w)] & UINT32_C(0xe000007f)) : c->gpr[PPC_RD(w)];
    else c->gpr[PPC_RD(w)] = *reg;
    return 1;
}

int ppc_exec_logic(PpcCpu *c, uint32_t w) {
    unsigned d = PPC_RD(w), ra = PPC_RA(w);
    uint32_t s = c->gpr[d], b = c->gpr[PPC_RB(w)], r;
    switch (PPC_XO(w)) {
    case 0: compare_signed(c, PPC_CRFD(w), (int32_t)c->gpr[ra], (int32_t)b); return 1;       /* cmp */
    case 32: compare_unsigned(c, PPC_CRFD(w), c->gpr[ra], b); return 1;                      /* cmpl */
    case 28: r = s & b; break;                                                               /* and */
    case 60: r = s & ~b; break;                                                              /* andc */
    case 124: r = ~(s | b); break;                                                           /* nor */
    case 284: r = ~(s ^ b); break;                                                           /* eqv */
    case 316: r = s ^ b; break;                                                              /* xor */
    case 412: r = s | ~b; break;                                                             /* orc */
    case 444: r = s | b; break;                                                              /* or / mr */
    case 476: r = ~(s & b); break;                                                           /* nand */
    case 24: r = (b & 0x20u) ? 0u : s << (b & 31u); break;                                   /* slw */
    case 536: r = (b & 0x20u) ? 0u : s >> (b & 31u); break;                                  /* srw */
    case 792: case 824: {                                                                    /* sraw, srawi */
        unsigned n = PPC_XO(w) == 824 ? PPC_RB(w) : (b & 63u);
        int negative = (int32_t)s < 0;
        if (n >= 32) { r = negative ? UINT32_C(0xffffffff) : 0u; ppc_set_ca(c, negative); }
        else {
            uint32_t lost = n ? (s & ((UINT32_C(1) << n) - 1u)) : 0u;
            r = n ? ((s >> n) | (negative ? ~(UINT32_C(0xffffffff) >> n) : 0u)) : s;
            ppc_set_ca(c, negative && lost);
        }
        break;
    }
    case 26: for (r = 0; r < 32 && !(s & (UINT32_C(0x80000000) >> r)); r++) {} break;        /* cntlzw */
    case 922: r = (uint32_t)(int32_t)(int16_t)s; break;                                      /* extsh */
    case 954: r = (uint32_t)(int32_t)(int8_t)s; break;                                       /* extsb */
    case 19: c->gpr[d] = c->cr; return 1;                                                    /* mfcr */
    case 144: {                                                                              /* mtcrf */
        uint32_t m = 0;
        for (unsigned i = 0; i < 8; i++)
            if (w & (UINT32_C(0x80000) >> i)) m |= UINT32_C(0xf0000000) >> (4u * i);
        c->cr = (c->cr & ~m) | (s & m);
        return 1;
    }
    case 339: return spr_move(c, w, 0);                                                      /* mfspr */
    case 467: return spr_move(c, w, 1);                                                      /* mtspr */
    case 512:                                                                                /* mcrxr */
        ppc_set_crf(c, PPC_CRFD(w), c->xer >> 28);
        c->xer &= UINT32_C(0x0fffffff);
        return 1;
    default:
        return 0;
    }
    c->gpr[ra] = r;
    if (w & 1u) ppc_record(c, r);
    return 1;
}
