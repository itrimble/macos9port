/* PowerPC interpreter: floating-point loads/stores, arithmetic, compares,
 * conversion and the FPSCR subset. Expected bit patterns are IEEE-754 values
 * worked out by hand, not produced by the code under test. */
#include "ppc_asm.h"
#include "ppc_cpu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CODE 0x1000u
#define DATA 0x2000u
static uint8_t ram[0x10000];
static PpcCpu cpu;

static void reset(void) {
    memset(ram, 0, sizeof ram);
    ppc_cpu_init(&cpu, ram, sizeof ram);
    cpu.gpr[4] = DATA;
}

static void exec(const uint32_t *code, size_t count) {
    uint32_t at = CODE;
    for (size_t i = 0; i < count; i++) ppc_emit(ram, &at, code[i]);
    cpu.pc = CODE;
    ppc_cpu_run(&cpu, count);
}
#define RUN(...) do { const uint32_t code_[] = {__VA_ARGS__}; exec(code_, sizeof code_ / 4); } while (0)

static void poke64(uint32_t at, uint64_t v) { for (int i = 0; i < 8; i++) ram[at + i] = (uint8_t)(v >> (56 - 8 * i)); }
static void poke32(uint32_t at, uint32_t v) { for (int i = 0; i < 4; i++) ram[at + i] = (uint8_t)(v >> (24 - 8 * i)); }
static uint64_t peek64(uint32_t at) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | ram[at + i]; return v; }
static uint32_t peek32(uint32_t at) { uint32_t v = 0; for (int i = 0; i < 4; i++) v = (v << 8) | ram[at + i]; return v; }

#define FPRF ((cpu.fpscr >> 12) & 0x1fu)
/* Double arithmetic: opcode 63, single: opcode 59. frD=3, frA=1, frB=2, frC=2 for multiplies. */
static uint64_t binary(unsigned op, unsigned xo, double a, double b, unsigned rc) {
    ppc_fpr_set(&cpu, 1, a); ppc_fpr_set(&cpu, 2, b);
    RUN(xo == 25 ? ppc_a(op, 3, 1, 0, 2, xo, rc) : ppc_a(op, 3, 1, 2, 0, xo, rc));
    return cpu.fpr[3];
}

static void test_loads_stores(void) {
    reset();
    poke64(DATA, UINT64_C(0x7ff0000000000001));            /* signalling NaN: must survive lfd/stfd untouched */
    poke64(DATA + 8, UINT64_C(0x400921fb54442d18));        /* pi */
    poke32(DATA + 16, 0x3fc00000u);                        /* 1.5f */
    poke32(DATA + 20, 0xc1200000u);                        /* -10.0f */
    cpu.gpr[5] = 8; cpu.gpr[6] = DATA; cpu.gpr[7] = DATA;
    RUN(ppc_d(50, 1, 4, 0), ppc_d(54, 1, 4, 0x40),         /* lfd f1; stfd f1 */
        ppc_d(50, 2, 4, 8), ppc_d(48, 3, 4, 16),           /* lfd f2; lfs f3 */
        ppc_d(49, 5, 6, 20),                               /* lfsu f5,20(r6) */
        ppc_d(51, 6, 7, 8),                                /* lfdu f6,8(r7) */
        ppc_x(31, 7, 4, 5, 599, 0),                        /* lfdx f7,r4,r5 */
        ppc_d(52, 2, 4, 0x48),                             /* stfs f2: pi rounded to single */
        ppc_d(54, 3, 4, 0x50));                            /* stfd f3: 1.5 widened */
    CHECK(cpu.fpr[1] == UINT64_C(0x7ff0000000000001) && peek64(DATA + 0x40) == UINT64_C(0x7ff0000000000001));
    CHECK(ppc_fpr_get(&cpu, 3) == 1.5 && ppc_fpr_get(&cpu, 5) == -10.0 && cpu.gpr[6] == DATA + 20);
    CHECK(cpu.fpr[6] == UINT64_C(0x400921fb54442d18) && cpu.gpr[7] == DATA + 8 && cpu.fpr[7] == cpu.fpr[6]);
    CHECK(peek32(DATA + 0x48) == 0x40490fdbu && peek64(DATA + 0x50) == UINT64_C(0x3ff8000000000000));
    cpu.gpr[5] = 0x60; cpu.gpr[6] = DATA; cpu.gpr[7] = DATA; cpu.gpr[8] = DATA; cpu.gpr[9] = DATA;
    cpu.fpr[9] = UINT64_C(0xfff80000deadbeef);
    RUN(ppc_x(31, 3, 4, 5, 663, 0),                        /* stfsx at +0x60 */
        ppc_x(31, 2, 6, 5, 759, 0),                        /* stfdux: r6 = DATA+0x60 */
        ppc_d(53, 3, 7, 0x70),                             /* stfsu */
        ppc_d(55, 3, 8, 0x78),                             /* stfdu */
        ppc_x(31, 9, 4, 0, 983, 0),                        /* stfiwx at DATA (rB = r0 = 0) */
        ppc_x(31, 10, 4, 5, 535, 0),                       /* lfsx: reads the top half of pi as a float */
        ppc_x(31, 11, 9, 5, 631, 0));                      /* lfdux */
    CHECK(peek64(DATA + 0x60) == UINT64_C(0x400921fb54442d18) && cpu.gpr[6] == DATA + 0x60);
    CHECK(peek32(DATA + 0x70) == 0x3fc00000u && cpu.gpr[7] == DATA + 0x70 && cpu.gpr[8] == DATA + 0x78);
    CHECK(peek64(DATA + 0x78) == UINT64_C(0x3ff8000000000000) && peek32(DATA) == 0xdeadbeefu);
    CHECK(cpu.fpr[11] == UINT64_C(0x400921fb54442d18) && cpu.gpr[9] == DATA + 0x60 && !cpu.halted);
    cpu.gpr[4] = 0xfffc;
    RUN(ppc_d(50, 1, 4, 0));                               /* a double straddling the end of RAM */
    CHECK(cpu.halted && strstr(cpu.error, "8 byte"));
}

static void test_arithmetic(void) {
    reset();
    CHECK(binary(63, 21, 1.5, 2.25, 0) == UINT64_C(0x400e000000000000) && FPRF == 0x04); /* 3.75, +normal */
    CHECK(binary(63, 20, 1.5, 2.25, 0) == UINT64_C(0xbfe8000000000000) && FPRF == 0x08); /* -0.75, -normal */
    CHECK(binary(63, 25, 3.0, 0.5, 0) == UINT64_C(0x3ff8000000000000));                  /* fmul uses frC */
    CHECK(binary(63, 18, 1.0, 3.0, 0) == UINT64_C(0x3fd5555555555555));                  /* fdiv */
    CHECK(binary(59, 18, 1.0, 3.0, 0) == UINT64_C(0x3fd5555560000000));                  /* fdivs: rounded to single */
    CHECK(binary(59, 21, 0.1, 0.2, 0) == UINT64_C(0x3fd3333340000000));                  /* fadds: 0.3f */
    CHECK(binary(59, 20, 16777216.0, 1.0, 0) == UINT64_C(0x416fffffe0000000));           /* fsubs: 16777215 is exact */
    CHECK(binary(59, 25, 16777217.0, 1.0, 0) == UINT64_C(0x4170000000000000));           /* fmuls: ties to even */
    CHECK(binary(63, 20, 2.0, 2.0, 0) == 0 && FPRF == 0x02);                             /* +0 */
    CHECK(binary(63, 25, -1.0, 0.0, 0) == UINT64_C(0x8000000000000000) && FPRF == 0x12); /* -0 */
    cpu.fpscr = 0;
    CHECK(binary(63, 18, 1.0, 0.0, 1) == UINT64_C(0x7ff0000000000000) && FPRF == 0x05);  /* divide by zero */
    CHECK((cpu.fpscr & (PPC_FPSCR_ZX | PPC_FPSCR_FX)) == (PPC_FPSCR_ZX | PPC_FPSCR_FX));
    CHECK(((cpu.cr >> 24) & 0xf) == 0x8);                                                /* fdiv. copies FX.. into cr1 */
    CHECK(binary(63, 18, -1.0, 0.0, 0) == UINT64_C(0xfff0000000000000) && FPRF == 0x09);
    binary(63, 18, 0.0, 0.0, 0);
    CHECK(isnan(ppc_fpr_get(&cpu, 3)) && FPRF == 0x11);
    CHECK(binary(63, 25, 4e-320, 0.5, 0) != 0 && FPRF == 0x14);                          /* +denormal */
    ppc_fpr_set(&cpu, 1, 2.0); ppc_fpr_set(&cpu, 2, 1.0); ppc_fpr_set(&cpu, 4, 3.0);
    RUN(ppc_a(63, 5, 1, 2, 4, 29, 0),  /* fmadd: 2*3+1 */
        ppc_a(63, 6, 1, 2, 4, 28, 0),  /* fmsub: 2*3-1 */
        ppc_a(63, 7, 1, 2, 4, 31, 0),  /* fnmadd */
        ppc_a(63, 8, 1, 2, 4, 30, 0),  /* fnmsub */
        ppc_a(59, 9, 1, 2, 4, 29, 0)); /* fmadds */
    CHECK(ppc_fpr_get(&cpu, 5) == 7.0 && ppc_fpr_get(&cpu, 6) == 5.0 && ppc_fpr_get(&cpu, 7) == -7.0 &&
          ppc_fpr_get(&cpu, 8) == -5.0 && ppc_fpr_get(&cpu, 9) == 7.0);
    cpu.fpr[2] = 0; cpu.fpr[1] = UINT64_C(0xc00921fb54442d18);
    RUN(ppc_x(63, 3, 0, 1, 72, 0),     /* fmr */
        ppc_x(63, 5, 0, 1, 40, 0),     /* fneg */
        ppc_x(63, 6, 0, 1, 264, 0),    /* fabs */
        ppc_x(63, 7, 0, 6, 136, 0),    /* fnabs of the positive value */
        ppc_x(63, 8, 0, 2, 40, 0),     /* fneg +0 */
        ppc_x(63, 9, 0, 1, 12, 0));    /* frsp */
    CHECK(cpu.fpr[3] == UINT64_C(0xc00921fb54442d18) && cpu.fpr[5] == UINT64_C(0x400921fb54442d18));
    CHECK(cpu.fpr[6] == UINT64_C(0x400921fb54442d18) && cpu.fpr[7] == UINT64_C(0xc00921fb54442d18));
    CHECK(cpu.fpr[8] == UINT64_C(0x8000000000000000) && cpu.fpr[9] == UINT64_C(0xc00921fb60000000));
    RUN(ppc_a(59, 3, 1, 2, 0, 22, 0)); /* fsqrts is not in this slice */
    CHECK(cpu.halted && strstr(cpu.error, "Unimplemented"));
}

static uint32_t compare(unsigned xo, uint64_t a, uint64_t b) { /* into cr2 */
    cpu.fpr[1] = a; cpu.fpr[2] = b; cpu.cr = 0;
    RUN(ppc_x(63, 2 << 2, 1, 2, xo, 0));
    return (cpu.cr >> 20) & 0xf;
}

static void test_compare_convert(void) {
    const uint64_t one = UINT64_C(0x3ff0000000000000), two = UINT64_C(0x4000000000000000);
    const uint64_t qnan = UINT64_C(0x7ff8000000000000), snan = UINT64_C(0x7ff0000000000001);
    reset();
    CHECK(compare(0, one, two) == PPC_CR_LT && FPRF == 0x08);  /* FPCC mirrors the result, C is untouched */
    CHECK(compare(0, two, one) == PPC_CR_GT && compare(0, two, two) == PPC_CR_EQ);
    CHECK(compare(0, 0, UINT64_C(0x8000000000000000)) == PPC_CR_EQ);          /* +0 equals -0 */
    CHECK(compare(0, UINT64_C(0xfff0000000000000), one) == PPC_CR_LT);        /* -infinity */
    CHECK(compare(0, qnan, one) == PPC_CR_SO && !(cpu.fpscr & PPC_FPSCR_VX)); /* quiet NaN: unordered, no flag */
    CHECK(compare(0, one, snan) == PPC_CR_SO && (cpu.fpscr & PPC_FPSCR_VXSNAN) && (cpu.fpscr & PPC_FPSCR_FX));
    cpu.fpscr = 0;
    CHECK(compare(32, qnan, one) == PPC_CR_SO && (cpu.fpscr & PPC_FPSCR_VXVC)); /* fcmpo flags quiet NaNs */
    CHECK(cpu.cr == 0x00100000u);                                             /* only the requested field changed */
    cpu.fpscr = 0;
    static const struct { double v; uint32_t z, n; int invalid; } cases[] = {
        {2.9, 2, 3, 0}, {-2.9, 0xfffffffeu, 0xfffffffdu, 0}, {2.5, 2, 2, 0}, {3.5, 3, 4, 0}, {-0.5, 0, 0, 0},
        {2147483647.0, 0x7fffffffu, 0x7fffffffu, 0}, {-2147483648.0, 0x80000000u, 0x80000000u, 0},
        {3e9, 0x7fffffffu, 0x7fffffffu, 1}, {-3e9, 0x80000000u, 0x80000000u, 1},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        cpu.fpscr = 0;
        ppc_fpr_set(&cpu, 1, cases[i].v);
        RUN(ppc_x(63, 2, 0, 1, 15, 0), ppc_x(63, 3, 0, 1, 14, 0)); /* fctiwz f2; fctiw f3 (round to nearest even) */
        CHECK((uint32_t)cpu.fpr[2] == cases[i].z && (uint32_t)cpu.fpr[3] == cases[i].n);
        CHECK(!(cpu.fpscr & PPC_FPSCR_VXCVI) == !cases[i].invalid);
    }
    cpu.fpscr = 0; cpu.fpr[1] = qnan;
    RUN(ppc_x(63, 2, 0, 1, 15, 0));
    CHECK((uint32_t)cpu.fpr[2] == 0x80000000u && (cpu.fpscr & PPC_FPSCR_VXCVI) && (cpu.fpscr & PPC_FPSCR_VX));
    cpu.fpscr = 0; ppc_fpr_set(&cpu, 1, 2.5);
    RUN(ppc_x(63, 31, 0, 0, 38, 0),                 /* mtfsb1 31: rounding mode 1 (toward zero) */
        ppc_x(63, 3, 0, 1, 14, 0),
        ppc_x(63, 7 << 2, 0, 2 << 1, 134, 0),       /* mtfsfi 7, 2: rounding toward +infinity */
        ppc_x(63, 5, 0, 1, 14, 0),
        ppc_x(63, 31, 0, 0, 38, 0),                 /* mtfsb1 31: mode 3 (toward -infinity) */
        ppc_x(63, 6, 0, 1, 14, 0),
        ppc_x(63, 30, 0, 0, 70, 0),                 /* mtfsb0 30: back to mode 1 */
        ppc_x(63, 7, 0, 0, 583, 0));                /* mffs f7 */
    CHECK((uint32_t)cpu.fpr[3] == 2 && (uint32_t)cpu.fpr[5] == 3 && (uint32_t)cpu.fpr[6] == 2);
    CHECK((uint32_t)cpu.fpr[7] == 1 && cpu.fpscr == 1);
    cpu.fpr[1] = UINT64_C(0x00000000a5a5a5a5);
    cpu.fpscr = 1;
    RUN(UINT32_C(0xfc000000) | (0x81u << 17) | (1u << 11) | (711u << 1)); /* mtfsf 0x81, f1: fields 0 and 7 */
    CHECK(cpu.fpscr == 0xa0000005u);
}

int main(void) {
    test_loads_stores();
    test_arithmetic();
    test_compare_convert();
    if (failures) { fprintf(stderr, "test_ppc_fp: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_ppc_fp: all %d checks passed\n", checks);
    return 0;
}
