#ifndef PPC_INT_H
#define PPC_INT_H

#include "ppc_cpu.h"

/* Instruction fields. Bit 0 is the most significant bit in PowerPC numbering. */
#define PPC_OP(w) ((w) >> 26)
#define PPC_RD(w) (((w) >> 21) & 31u) /* also rS, BO, crbD, frD */
#define PPC_RA(w) (((w) >> 16) & 31u) /* also BI, crbA */
#define PPC_RB(w) (((w) >> 11) & 31u) /* also SH, crbB */
#define PPC_RC(w) (((w) >> 6) & 31u)  /* also MB, frC */
#define PPC_ME(w) (((w) >> 1) & 31u)
#define PPC_XO(w) (((w) >> 1) & 0x3ffu)
#define PPC_SIMM(w) ((uint32_t)(int32_t)(int16_t)((w) & 0xffffu))
#define PPC_UIMM(w) ((w) & 0xffffu)
#define PPC_CRFD(w) (((w) >> 23) & 7u)

/* Each group returns 1 when it executed (or faulted) the instruction and 0 when the encoding is not its own. */
int ppc_exec_int(PpcCpu *c, uint32_t w);      /* immediate arithmetic/logical, rotates, opcode 31 arithmetic */
int ppc_exec_logic(PpcCpu *c, uint32_t w);    /* opcode 31 logical, shifts, compares, SPR and CR moves */
int ppc_exec_branch(PpcCpu *c, uint32_t w);   /* b, bc, sc, opcode 19 */
int ppc_exec_mem(PpcCpu *c, uint32_t w);      /* integer loads and stores, D and X forms */
int ppc_exec_fp(PpcCpu *c, uint32_t w);       /* opcodes 59 and 63 */
int ppc_exec_fp_mem(PpcCpu *c, uint32_t w);   /* floating-point loads and stores, D and X forms */

static inline void ppc_set_crf(PpcCpu *c, unsigned field, uint32_t value) {
    unsigned shift = 28u - 4u * field;
    c->cr = (c->cr & ~(UINT32_C(0xf) << shift)) | ((value & 0xfu) << shift);
}

static inline void ppc_record(PpcCpu *c, uint32_t result) {
    uint32_t v = (int32_t)result < 0 ? PPC_CR_LT : result ? PPC_CR_GT : PPC_CR_EQ;
    if (c->xer & PPC_XER_SO) v |= PPC_CR_SO;
    ppc_set_crf(c, 0, v);
}

static inline void ppc_set_ca(PpcCpu *c, int carry) {
    c->xer = carry ? (c->xer | PPC_XER_CA) : (c->xer & ~PPC_XER_CA);
}

static inline void ppc_set_ov(PpcCpu *c, int overflow) {
    if (overflow) c->xer |= PPC_XER_OV | PPC_XER_SO;
    else c->xer &= ~PPC_XER_OV;
}

/* Bits mb..me inclusive in PowerPC numbering, wrapping when mb > me. */
static inline uint32_t ppc_mask(unsigned mb, unsigned me) {
    uint32_t head = UINT32_C(0xffffffff) >> mb, tail = UINT32_C(0xffffffff) << (31u - me);
    return mb <= me ? (head & tail) : (head | tail);
}

static inline uint32_t ppc_rotl(uint32_t v, unsigned n) {
    n &= 31u;
    return n ? (v << n) | (v >> (32u - n)) : v;
}

#endif
