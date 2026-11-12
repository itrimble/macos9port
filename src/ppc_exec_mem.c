#include "ppc_int.h"

#include <string.h>

typedef struct {
    uint8_t width;  /* 1, 2 or 4 bytes */
    uint8_t store, sign, update, reversed;
} Access;

static uint32_t swap(uint32_t v, unsigned width) {
    if (width == 2) return ((v & 0xffu) << 8) | ((v >> 8) & 0xffu);
    return (v << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
}

/* Performs one access; the base register is updated only after the access succeeded. */
static int transfer(PpcCpu *c, uint32_t w, uint32_t ea, Access a) {
    unsigned d = PPC_RD(w), ra = PPC_RA(w);
    if (a.update && !ra) return 0; /* invalid form: reported as an unimplemented instruction */
    if (a.store) {
        uint32_t v = c->gpr[d];
        if (a.reversed) v = swap(v, a.width);
        if (!ppc_mem_write(c, ea, a.width, v)) return 1;
    } else {
        uint32_t v;
        if (!ppc_mem_read(c, ea, a.width, &v)) return 1;
        if (a.reversed) v = swap(v, a.width);
        if (a.sign) v = (uint32_t)(int32_t)(int16_t)v;
        c->gpr[d] = v;
    }
    if (a.update) c->gpr[ra] = ea;
    return 1;
}

/* String word moves: `count` bytes between memory at ea and successive registers from rD/rS,
 * four bytes per register, most significant first, wrapping from r31 to r0. A load clears the
 * unused low bytes of the last register. */
static int string_move(PpcCpu *c, uint32_t w, uint32_t ea, unsigned count, int store) {
    unsigned r = PPC_RD(w);
    for (unsigned i = 0; i < count; i++, ea++) {
        unsigned shift = 24u - 8u * (i & 3u);
        if (store) {
            if (!ppc_mem_write(c, ea, 1, (c->gpr[r] >> shift) & 0xffu)) return 1;
        } else {
            uint32_t byte;
            if (!ppc_mem_read(c, ea, 1, &byte)) return 1;
            if ((i & 3u) == 0) c->gpr[r] = 0;
            c->gpr[r] |= byte << shift;
        }
        if ((i & 3u) == 3u) r = (r + 1u) & 31u;
    }
    return 1;
}

static int indexed(PpcCpu *c, uint32_t w) {
    unsigned ra = PPC_RA(w);
    uint32_t ea = (ra ? c->gpr[ra] : 0u) + c->gpr[PPC_RB(w)];
    switch (PPC_XO(w)) {
    case 533: return string_move(c, w, ea, c->xer & 0x7fu, 0);                                  /* lswx */
    case 661: return string_move(c, w, ea, c->xer & 0x7fu, 1);                                  /* stswx */
    case 597: return string_move(c, w, ra ? c->gpr[ra] : 0u, PPC_RB(w) ? PPC_RB(w) : 32u, 0);  /* lswi */
    case 725: return string_move(c, w, ra ? c->gpr[ra] : 0u, PPC_RB(w) ? PPC_RB(w) : 32u, 1);  /* stswi */
    default: break;
    }
    Access a = {0, 0, 0, 0, 0};
    switch (PPC_XO(w)) {
    case 23: a.width = 4; break;                             /* lwzx */
    case 55: a.width = 4; a.update = 1; break;               /* lwzux */
    case 87: a.width = 1; break;                             /* lbzx */
    case 119: a.width = 1; a.update = 1; break;              /* lbzux */
    case 279: a.width = 2; break;                            /* lhzx */
    case 311: a.width = 2; a.update = 1; break;              /* lhzux */
    case 343: a.width = 2; a.sign = 1; break;                /* lhax */
    case 375: a.width = 2; a.sign = 1; a.update = 1; break;  /* lhaux */
    case 151: a.width = 4; a.store = 1; break;               /* stwx */
    case 183: a.width = 4; a.store = 1; a.update = 1; break; /* stwux */
    case 215: a.width = 1; a.store = 1; break;               /* stbx */
    case 247: a.width = 1; a.store = 1; a.update = 1; break; /* stbux */
    case 407: a.width = 2; a.store = 1; break;               /* sthx */
    case 439: a.width = 2; a.store = 1; a.update = 1; break; /* sthux */
    case 534: a.width = 4; a.reversed = 1; break;            /* lwbrx */
    case 790: a.width = 2; a.reversed = 1; break;            /* lhbrx */
    case 662: a.width = 4; a.store = 1; a.reversed = 1; break; /* stwbrx */
    case 918: a.width = 2; a.store = 1; a.reversed = 1; break; /* sthbrx */
    case 1014: {                                             /* dcbz: clear the 32-byte block holding ea */
        uint8_t *p = ppc_mem_span(c, ea & ~UINT32_C(31), 32);
        if (!p) ppc_cpu_halt(c, "PowerPC dcbz outside guest RAM at %08x (pc %08x).", ea, c->pc);
        else memset(p, 0, 32);
        return 1;
    }
    case 54: case 86: case 246: case 278: case 598: case 982: /* dcbst dcbf dcbtst dcbt sync icbi: no cache model */
        return 1;
    default:
        return 0;
    }
    return transfer(c, w, ea, a);
}

int ppc_exec_mem(PpcCpu *c, uint32_t w) {
    unsigned op = PPC_OP(w), ra = PPC_RA(w);
    if (op == 31) return indexed(c, w);
    uint32_t ea = (ra ? c->gpr[ra] : 0u) + PPC_SIMM(w);
    Access a = {0, 0, 0, 0, 0};
    switch (op) {
    case 32: case 33: a.width = 4; break;                    /* lwz, lwzu */
    case 34: case 35: a.width = 1; break;                    /* lbz, lbzu */
    case 36: case 37: a.width = 4; a.store = 1; break;       /* stw, stwu */
    case 38: case 39: a.width = 1; a.store = 1; break;       /* stb, stbu */
    case 40: case 41: a.width = 2; break;                    /* lhz, lhzu */
    case 42: case 43: a.width = 2; a.sign = 1; break;        /* lha, lhau */
    case 44: case 45: a.width = 2; a.store = 1; break;       /* sth, sthu */
    case 46:                                                 /* lmw */
        for (unsigned r = PPC_RD(w); r < 32; r++, ea += 4)
            if (!ppc_mem_read(c, ea, 4, &c->gpr[r])) break;
        return 1;
    case 47:                                                 /* stmw */
        for (unsigned r = PPC_RD(w); r < 32; r++, ea += 4)
            if (!ppc_mem_write(c, ea, 4, c->gpr[r])) break;
        return 1;
    default:
        return 0;
    }
    a.update = (uint8_t)(op & 1u);
    return transfer(c, w, ea, a);
}
