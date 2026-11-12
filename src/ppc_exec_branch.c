#include "ppc_int.h"

static uint32_t cr_bit(const PpcCpu *c, unsigned index) {
    return (c->cr >> (31u - index)) & 1u;
}

static void set_cr_bit(PpcCpu *c, unsigned index, uint32_t value) {
    uint32_t m = UINT32_C(0x80000000) >> index;
    c->cr = (value & 1u) ? (c->cr | m) : (c->cr & ~m);
}

/* BO: 0x10 ignore the condition, 0x08 condition sense, 0x04 leave CTR alone, 0x02 branch when CTR reaches zero. */
static int branch_taken(PpcCpu *c, unsigned bo, unsigned bi, int uses_ctr) {
    int ctr_ok = 1;
    if (uses_ctr && !(bo & 4u)) {
        c->ctr--;
        ctr_ok = (c->ctr != 0) != ((bo & 2u) != 0);
    }
    int cond_ok = (bo & 0x10u) || cr_bit(c, bi) == ((bo >> 3) & 1u);
    return ctr_ok && cond_ok;
}

static int opcode19(PpcCpu *c, uint32_t w) {
    unsigned d = PPC_RD(w), a = PPC_RA(w), b = PPC_RB(w);
    uint32_t x = cr_bit(c, a), y = cr_bit(c, b);
    switch (PPC_XO(w)) {
    case 16: {                                               /* bclr */
        uint32_t target = c->lr & ~UINT32_C(3);
        int taken = branch_taken(c, d, a, 1);
        if (w & 1u) c->lr = c->pc + 4;
        if (taken) c->npc = target;
        return 1;
    }
    case 528: {                                              /* bcctr: CTR is the target, never a counter */
        int taken = branch_taken(c, d, a, 0);
        if (w & 1u) c->lr = c->pc + 4;
        if (taken) c->npc = c->ctr & ~UINT32_C(3);
        return 1;
    }
    case 0:                                                  /* mcrf */
        ppc_set_crf(c, PPC_CRFD(w), c->cr >> (28u - 4u * ((w >> 18) & 7u)));
        return 1;
    case 257: set_cr_bit(c, d, x & y); return 1;             /* crand */
    case 449: set_cr_bit(c, d, x | y); return 1;             /* cror */
    case 193: set_cr_bit(c, d, x ^ y); return 1;             /* crxor */
    case 225: set_cr_bit(c, d, ~(x & y)); return 1;          /* crnand */
    case 33: set_cr_bit(c, d, ~(x | y)); return 1;           /* crnor */
    case 289: set_cr_bit(c, d, ~(x ^ y)); return 1;          /* creqv */
    case 129: set_cr_bit(c, d, x & ~y); return 1;            /* crandc */
    case 417: set_cr_bit(c, d, x | ~y); return 1;            /* crorc */
    case 150: return 1;                                      /* isync: nothing to synchronise */
    default: return 0;
    }
}

int ppc_exec_branch(PpcCpu *c, uint32_t w) {
    switch (PPC_OP(w)) {
    case 18: {                                               /* b, ba, bl, bla */
        uint32_t li = w & UINT32_C(0x03fffffc);
        if (li & UINT32_C(0x02000000)) li |= UINT32_C(0xfc000000);
        if (w & 1u) c->lr = c->pc + 4;
        c->npc = (w & 2u) ? li : c->pc + li;
        return 1;
    }
    case 16: {                                               /* bc and its AA/LK forms */
        uint32_t bd = (uint32_t)(int32_t)(int16_t)(w & 0xfffcu);
        int taken = branch_taken(c, PPC_RD(w), PPC_RA(w), 1);
        if (w & 1u) c->lr = c->pc + 4;
        if (taken) c->npc = (w & 2u) ? bd : c->pc + bd;
        return 1;
    }
    case 17:                                                 /* sc: only meaningful inside a host call gate */
        if (!(w & 2u)) return 0;
        if (!c->syscall || c->syscall(c, c->pc, c->syscall_user) != 0)
            ppc_cpu_halt(c, "PowerPC sc at pc %08x is not a host call gate.", c->pc);
        return 1;
    case 19:
        return opcode19(c, w);
    default:
        return 0;
    }
}
