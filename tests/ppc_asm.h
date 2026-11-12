#ifndef PPC_ASM_H
#define PPC_ASM_H

/* Hand encoders for the PowerPC instructions the tests execute. Written from
 * the public instruction formats; register and field numbers are passed as
 * plain integers. Every function returns one 32-bit instruction word. */

#include <stdint.h>

#define PPC_UNUSED __attribute__((unused))

static PPC_UNUSED uint32_t ppc_d(unsigned op, unsigned d, unsigned a, int32_t imm) {
    return (op << 26) | (d << 21) | (a << 16) | ((uint32_t)imm & 0xffffu);
}
static PPC_UNUSED uint32_t ppc_x(unsigned op, unsigned d, unsigned a, unsigned b, unsigned xo, unsigned rc) {
    return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (xo << 1) | rc;
}
/* Rotate (M form): rA = rotl(rS, sh) masked by mb..me. */
static PPC_UNUSED uint32_t ppc_m(unsigned op, unsigned s, unsigned a, unsigned sh, unsigned mb, unsigned me, unsigned rc) {
    return (op << 26) | (s << 21) | (a << 16) | (sh << 11) | (mb << 6) | (me << 1) | rc;
}
/* Floating-point A form: frD, frA, frB, frC. */
static PPC_UNUSED uint32_t ppc_a(unsigned op, unsigned d, unsigned a, unsigned b, unsigned c, unsigned xo, unsigned rc) {
    return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (xo << 1) | rc;
}

#define OE 512u /* add to an arithmetic extended opcode to request overflow recording */

static PPC_UNUSED uint32_t ppc_li(unsigned d, int32_t v) { return ppc_d(14, d, 0, v); }
static PPC_UNUSED uint32_t ppc_lis(unsigned d, int32_t v) { return ppc_d(15, d, 0, v); }
static PPC_UNUSED uint32_t ppc_addi(unsigned d, unsigned a, int32_t v) { return ppc_d(14, d, a, v); }
static PPC_UNUSED uint32_t ppc_ori(unsigned a, unsigned s, uint32_t v) { return ppc_d(24, s, a, (int32_t)v); }
static PPC_UNUSED uint32_t ppc_mr(unsigned a, unsigned s) { return ppc_x(31, s, a, s, 444, 0); }
static PPC_UNUSED uint32_t ppc_add(unsigned d, unsigned a, unsigned b) { return ppc_x(31, d, a, b, 266, 0); }
static PPC_UNUSED uint32_t ppc_lwz(unsigned d, int32_t off, unsigned a) { return ppc_d(32, d, a, off); }
static PPC_UNUSED uint32_t ppc_stw(unsigned s, int32_t off, unsigned a) { return ppc_d(36, s, a, off); }
static PPC_UNUSED uint32_t ppc_stwu(unsigned s, int32_t off, unsigned a) { return ppc_d(37, s, a, off); }
static PPC_UNUSED uint32_t ppc_mfspr(unsigned d, unsigned spr) { return ppc_x(31, d, spr & 31u, spr >> 5, 339, 0); }
static PPC_UNUSED uint32_t ppc_mtspr(unsigned spr, unsigned s) { return ppc_x(31, s, spr & 31u, spr >> 5, 467, 0); }
#define SPR_XER 1u
#define SPR_LR 8u
#define SPR_CTR 9u
/* Branches: displacements are in bytes relative to the branch itself. */
static PPC_UNUSED uint32_t ppc_b(int32_t disp, unsigned aa, unsigned lk) {
    return (18u << 26) | ((uint32_t)disp & 0x03fffffcu) | (aa << 1) | lk;
}
static PPC_UNUSED uint32_t ppc_bc(unsigned bo, unsigned bi, int32_t disp, unsigned aa, unsigned lk) {
    return (16u << 26) | (bo << 21) | (bi << 16) | ((uint32_t)disp & 0xfffcu) | (aa << 1) | lk;
}
static PPC_UNUSED uint32_t ppc_bclr(unsigned bo, unsigned bi, unsigned lk) { return ppc_x(19, bo, bi, 0, 16, lk); }
static PPC_UNUSED uint32_t ppc_bcctr(unsigned bo, unsigned bi, unsigned lk) { return ppc_x(19, bo, bi, 0, 528, lk); }
#define PPC_BLR ppc_bclr(20, 0, 0)
#define PPC_BCTR ppc_bcctr(20, 0, 0)
#define PPC_SC UINT32_C(0x44000002)
/* BO values and CR bit numbers (field * 4 + bit). */
#define BO_TRUE 12u
#define BO_FALSE 4u
#define BO_ALWAYS 20u
#define BO_DNZ 16u
#define BO_DZ 18u
#define BI_LT 0u
#define BI_GT 1u
#define BI_EQ 2u
#define BI_SO 3u

static PPC_UNUSED void ppc_emit(uint8_t *ram, uint32_t *at, uint32_t word) {
    ram[*at] = (uint8_t)(word >> 24);
    ram[*at + 1] = (uint8_t)(word >> 16);
    ram[*at + 2] = (uint8_t)(word >> 8);
    ram[*at + 3] = (uint8_t)word;
    *at += 4;
}

#endif
