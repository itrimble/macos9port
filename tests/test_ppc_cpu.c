/* PowerPC interpreter: integer, logical, rotate, compare, CR/SPR, load/store,
 * branch and fault semantics. Every program is hand-encoded with ppc_asm.h and
 * every expected value was worked out by hand from the instruction definition. */
#include "ppc_asm.h"
#include "ppc_cpu.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define RAM_SIZE 0x10000u
#define CODE 0x1000u
static uint8_t ram[RAM_SIZE];
static PpcCpu cpu;

static void reset(void) {
    memset(ram, 0, sizeof ram);
    ppc_cpu_init(&cpu, ram, sizeof ram);
}

/* Places the words at CODE and executes exactly that many instructions (straight-line code). */
static void exec(const uint32_t *code, size_t count, size_t budget) {
    uint32_t at = CODE;
    for (size_t i = 0; i < count; i++) ppc_emit(ram, &at, code[i]);
    cpu.pc = CODE;
    ppc_cpu_run(&cpu, budget);
}
#define RUN(...) do { const uint32_t code_[] = {__VA_ARGS__}; exec(code_, sizeof code_ / 4, sizeof code_ / 4); } while (0)
#define RUN_BUDGET(budget, ...) do { const uint32_t code_[] = {__VA_ARGS__}; exec(code_, sizeof code_ / 4, budget); } while (0)
#define CR0 (cpu.cr >> 28)

static uint32_t arith(unsigned xo, uint32_t a, uint32_t b, unsigned rc) { /* rD=5, rA=3, rB=4 */
    cpu.gpr[3] = a; cpu.gpr[4] = b;
    RUN(ppc_x(31, 5, 3, 4, xo, rc));
    return cpu.gpr[5];
}

static void test_add_carry_overflow(void) {
    reset();
    CHECK(arith(266 + OE, 0x7fffffff, 1, 1) == 0x80000000u);                 /* addo. overflows into the sign */
    CHECK((cpu.xer & (PPC_XER_OV | PPC_XER_SO)) == (PPC_XER_OV | PPC_XER_SO));
    CHECK(CR0 == (PPC_CR_LT | PPC_CR_SO));
    CHECK(arith(266 + OE, 2, 3, 1) == 5);                                    /* OV clears, SO is sticky */
    CHECK(!(cpu.xer & PPC_XER_OV) && (cpu.xer & PPC_XER_SO));
    CHECK(CR0 == (PPC_CR_GT | PPC_CR_SO));
    reset();
    CHECK(arith(266, 0x7fffffff, 1, 0) == 0x80000000u && cpu.xer == 0 && cpu.cr == 0); /* plain add touches nothing */
    CHECK(arith(10, 0xffffffff, 1, 1) == 0 && (cpu.xer & PPC_XER_CA) && CR0 == PPC_CR_EQ); /* addc. */
    CHECK(arith(138, 1, 0, 0) == 2 && !(cpu.xer & PPC_XER_CA));              /* adde consumes and clears CA */
    cpu.xer = PPC_XER_CA;
    CHECK(arith(138, 0xffffffff, 0, 0) == 0 && (cpu.xer & PPC_XER_CA));
    cpu.xer = PPC_XER_CA;
    CHECK(arith(202, 0xffffffff, 99, 0) == 0 && (cpu.xer & PPC_XER_CA));     /* addze ignores rB */
    cpu.xer = 0;
    CHECK(arith(234, 0, 99, 0) == 0xffffffffu && !(cpu.xer & PPC_XER_CA));   /* addme: 0 + -1 + 0 */
    CHECK(arith(234, 1, 99, 0) == 0 && (cpu.xer & PPC_XER_CA));
    cpu.gpr[3] = 0xfffffff0u;
    RUN(ppc_d(12, 5, 3, 0x20), ppc_d(13, 6, 3, -16));                        /* addic, addic. */
    CHECK(cpu.gpr[5] == 0x10 && cpu.gpr[6] == 0xffffffe0u && (cpu.xer & PPC_XER_CA) && CR0 == PPC_CR_LT);
    cpu.gpr[3] = 100;
    RUN(ppc_addi(5, 3, -1), ppc_addi(6, 0, -1), ppc_d(15, 7, 3, 1), ppc_lis(8, -1)); /* rA = 0 reads as zero */
    CHECK(cpu.gpr[5] == 99 && cpu.gpr[6] == 0xffffffffu && cpu.gpr[7] == 0x10064u && cpu.gpr[8] == 0xffff0000u);
}

static void test_subtract(void) {
    reset();
    CHECK(arith(40, 5, 3, 0) == 0xfffffffeu && cpu.xer == 0);                /* subf: rB - rA */
    CHECK(arith(8, 5, 3, 0) == 0xfffffffeu && !(cpu.xer & PPC_XER_CA));      /* subfc: borrow clears CA */
    CHECK(arith(8, 3, 5, 0) == 2 && (cpu.xer & PPC_XER_CA));
    CHECK(arith(8, 7, 7, 0) == 0 && (cpu.xer & PPC_XER_CA));
    cpu.xer = 0;
    CHECK(arith(136, 0, 0, 0) == 0xffffffffu && !(cpu.xer & PPC_XER_CA));    /* subfe: ~0 + 0 + 0 */
    cpu.xer = PPC_XER_CA;
    CHECK(arith(136, 3, 10, 0) == 7 && (cpu.xer & PPC_XER_CA));
    cpu.xer = PPC_XER_CA;
    CHECK(arith(200, 0xffffffff, 99, 0) == 1 && !(cpu.xer & PPC_XER_CA));    /* subfze: ~rA + CA */
    cpu.xer = PPC_XER_CA;
    CHECK(arith(232, 0, 99, 0) == 0xffffffffu && (cpu.xer & PPC_XER_CA));    /* subfme: ~rA - 1 + CA */
    CHECK(arith(40 + OE, 1, 0x80000000, 0) == 0x7fffffffu && (cpu.xer & PPC_XER_OV)); /* min - 1 */
    cpu.xer = 0;
    CHECK(arith(104, 5, 0, 1) == 0xfffffffbu && CR0 == PPC_CR_LT);           /* neg. */
    CHECK(arith(104 + OE, 0x80000000, 0, 0) == 0x80000000u && (cpu.xer & PPC_XER_OV));
    cpu.xer = 0; cpu.gpr[3] = 3;
    RUN(ppc_d(8, 5, 3, 10));                                                 /* subfic: 10 - 3 */
    CHECK(cpu.gpr[5] == 7 && (cpu.xer & PPC_XER_CA));
    cpu.gpr[3] = 11;
    RUN(ppc_d(8, 5, 3, 10));
    CHECK(cpu.gpr[5] == 0xffffffffu && !(cpu.xer & PPC_XER_CA));
}

static void test_multiply_divide(void) {
    reset();
    CHECK(arith(235, 0xfffffffd, 7, 0) == 0xffffffebu);                      /* -3 * 7 */
    CHECK(arith(235 + OE, 0x10000, 0x10000, 1) == 0 && (cpu.xer & PPC_XER_OV) && CR0 == (PPC_CR_EQ | PPC_CR_SO));
    CHECK(arith(235 + OE, 0xffffffff, 0xffffffff, 0) == 1 && !(cpu.xer & PPC_XER_OV));
    CHECK(arith(75, 0xfffffffe, 3, 0) == 0xffffffffu);                       /* mulhw: -6 >> 32 */
    CHECK(arith(11, 0xfffffffe, 3, 0) == 2);                                 /* mulhwu */
    cpu.gpr[3] = 0xfffffffbu;
    RUN(ppc_d(7, 5, 3, 7));                                                  /* mulli: -5 * 7 */
    CHECK(cpu.gpr[5] == 0xffffffddu);
    cpu.xer = 0;
    CHECK(arith(491, 0xfffffff9, 2, 0) == 0xfffffffdu);                      /* divw truncates toward zero: -7 / 2 */
    CHECK(arith(459, 0xfffffff9, 2, 0) == 0x7ffffffcu);                      /* divwu */
    CHECK(arith(491 + OE, 7, 0, 0) == 0 && (cpu.xer & PPC_XER_OV));          /* divide by zero */
    CHECK(arith(491 + OE, 7, 7, 0) == 1 && !(cpu.xer & PPC_XER_OV) && (cpu.xer & PPC_XER_SO));
    CHECK(arith(491 + OE, 0x80000000, 0xffffffff, 0) == 0 && (cpu.xer & PPC_XER_OV));
    CHECK(arith(459 + OE, 7, 0, 0) == 0 && (cpu.xer & PPC_XER_OV));
}

static uint32_t logic(unsigned xo, uint32_t s, uint32_t b, unsigned rc) { /* rS=3, rA=5, rB=4 */
    cpu.gpr[3] = s; cpu.gpr[4] = b;
    RUN(ppc_x(31, 3, 5, 4, xo, rc));
    return cpu.gpr[5];
}

static void test_logical_and_shifts(void) {
    reset();
    const uint32_t s = 0xff00ff00u, b = 0x0ff00ff0u;
    CHECK(logic(28, s, b, 0) == 0x0f000f00u);   /* and */
    CHECK(logic(60, s, b, 0) == 0xf000f000u);   /* andc */
    CHECK(logic(444, s, b, 0) == 0xfff0fff0u);  /* or */
    CHECK(logic(412, s, b, 0) == 0xff0fff0fu);  /* orc */
    CHECK(logic(316, s, b, 0) == 0xf0f0f0f0u);  /* xor */
    CHECK(logic(476, s, b, 0) == 0xf0fff0ffu);  /* nand */
    CHECK(logic(124, s, b, 0) == 0x000f000fu);  /* nor */
    CHECK(logic(284, s, b, 1) == 0x0f0f0f0fu && CR0 == PPC_CR_GT); /* eqv. */
    CHECK(logic(316, s, s, 1) == 0 && CR0 == PPC_CR_EQ);
    cpu.gpr[3] = 0x12345678u;
    RUN(ppc_d(28, 3, 5, 0xff00), ppc_d(24, 3, 6, 0x8001), ppc_d(25, 3, 7, 0x8001), ppc_d(26, 3, 8, 0xffff),
        ppc_d(27, 3, 9, 0xffff));                /* andi. ori oris xori xoris: immediates are zero-extended */
    CHECK(cpu.gpr[5] == 0x5600 && CR0 == PPC_CR_GT && cpu.gpr[6] == 0x1234d679u && cpu.gpr[7] == 0x92355678u);
    CHECK(cpu.gpr[8] == 0x1234a987u && cpu.gpr[9] == 0xedcb5678u);
    cpu.gpr[3] = 0x80000000u;
    RUN(ppc_d(29, 3, 5, 0x8000));               /* andis. */
    CHECK(cpu.gpr[5] == 0x80000000u && CR0 == PPC_CR_LT);
    CHECK(logic(954, 0x1280, 0, 0) == 0xffffff80u && logic(954, 0x7f, 0, 0) == 0x7f);     /* extsb */
    CHECK(logic(922, 0x18000, 0, 0) == 0xffff8000u && logic(922, 0x7fff, 0, 1) == 0x7fff); /* extsh */
    CHECK(logic(26, 0, 0, 0) == 32 && logic(26, 1, 0, 0) == 31 && logic(26, 0x80000000, 0, 0) == 0 &&
          logic(26, 0x00010000, 0, 0) == 15);   /* cntlzw */
    CHECK(logic(24, 0x12345678, 4, 0) == 0x23456780u && logic(24, 0x12345678, 32, 0) == 0 &&
          logic(24, 0x12345678, 64, 0) == 0x12345678u); /* slw uses six bits of the count */
    CHECK(logic(536, 0x12345678, 4, 0) == 0x01234567u && logic(536, 0x80000000, 31, 0) == 1 &&
          logic(536, 0x80000000, 33, 0) == 0);  /* srw */
    CHECK(logic(792, 0x80000000, 4, 0) == 0xf8000000u && !(cpu.xer & PPC_XER_CA));  /* sraw: no one bits lost */
    CHECK(logic(792, 0xffffffff, 1, 0) == 0xffffffffu && (cpu.xer & PPC_XER_CA));
    CHECK(logic(792, 7, 1, 0) == 3 && !(cpu.xer & PPC_XER_CA));                    /* positive never carries */
    CHECK(logic(792, 0x80000000, 40, 0) == 0xffffffffu && (cpu.xer & PPC_XER_CA));
    CHECK(logic(792, 0x7fffffff, 40, 1) == 0 && !(cpu.xer & PPC_XER_CA) && CR0 == PPC_CR_EQ);
    cpu.gpr[3] = 0x80000001u;
    RUN(ppc_x(31, 3, 5, 1, 824, 0), ppc_x(31, 3, 6, 0, 824, 0));                   /* srawi 1, srawi 0 */
    CHECK(cpu.gpr[5] == 0xc0000000u && cpu.gpr[6] == 0x80000001u && !(cpu.xer & PPC_XER_CA));
    RUN(ppc_x(31, 3, 5, 1, 824, 0));
    CHECK(cpu.xer & PPC_XER_CA);
}

static void test_rotates(void) {
    reset();
    cpu.gpr[3] = 0x12345678u; cpu.gpr[6] = 4;
    RUN(ppc_m(21, 3, 4, 8, 0, 31, 0),   /* rotlwi 8 */
        ppc_m(21, 3, 5, 0, 24, 31, 0),  /* low byte */
        ppc_m(21, 3, 7, 4, 0, 27, 0),   /* slwi 4 */
        ppc_m(21, 3, 8, 28, 4, 31, 0),  /* srwi 4 */
        ppc_m(21, 3, 9, 0, 28, 3, 0),   /* wrapped mask: bits 28..31 and 0..3 */
        ppc_m(23, 3, 10, 6, 0, 31, 0),  /* rlwnm by r6 */
        ppc_m(21, 3, 11, 0, 0, 0, 1),   /* single top bit, record: zero result */
        ppc_m(21, 3, 12, 31, 31, 31, 0),/* rotate left 31, keep bit 31: the old bit 30 */
        ppc_m(23, 3, 13, 6, 28, 31, 0));
    CHECK(cpu.gpr[4] == 0x34567812u && cpu.gpr[5] == 0x78 && cpu.gpr[7] == 0x23456780u && cpu.gpr[8] == 0x01234567u);
    CHECK(cpu.gpr[9] == 0x10000008u && cpu.gpr[10] == 0x23456781u && cpu.gpr[11] == 0 && CR0 == PPC_CR_EQ);
    CHECK(cpu.gpr[12] == 0 && cpu.gpr[13] == 1);
    cpu.gpr[3] = 0xab; cpu.gpr[4] = 0x11111111u; cpu.gpr[5] = 0xffffffffu; cpu.gpr[6] = 0x80000001u;
    RUN(ppc_m(20, 3, 4, 8, 16, 23, 0),  /* rlwimi: insert a byte */
        ppc_m(20, 6, 5, 4, 28, 3, 1));  /* wrapped insert, record */
    CHECK(cpu.gpr[4] == 0x1111ab11u);
    CHECK(cpu.gpr[5] == 0x0ffffff8u && CR0 == PPC_CR_GT); /* rotl(0x80000001,4)=0x00000018, mask 0xf000000f */
}

static void test_compare_cr_spr(void) {
    reset();
    cpu.gpr[3] = 0xffffffffu; cpu.gpr[4] = 1;
    RUN(ppc_d(11, 7 << 2, 3, 0),              /* cmpwi cr7: -1 < 0 */
        ppc_d(10, 6 << 2, 3, 0),              /* cmplwi cr6: 0xffffffff > 0 */
        ppc_x(31, 5 << 2, 3, 4, 0, 0),        /* cmpw cr5 */
        ppc_x(31, 4 << 2, 3, 4, 32, 0),       /* cmplw cr4 */
        ppc_x(31, 3 << 2, 4, 4, 0, 0),        /* cmpw cr3: equal */
        ppc_d(11, 2 << 2, 4, -1),             /* cmpwi cr2: 1 > -1 (immediate sign-extended) */
        ppc_d(10, 1 << 2, 4, -1));            /* cmplwi cr1: 1 < 0xffff (immediate zero-extended) */
    CHECK(cpu.cr == 0x08424848u);
    cpu.xer = PPC_XER_SO;
    RUN(ppc_x(31, 0, 4, 4, 0, 0));
    CHECK(cpu.cr == 0x38424848u);             /* SO is copied into the field */
    cpu.gpr[3] = 0x12345678u; cpu.xer = 0;
    RUN(ppc_x(31, 3, 0, 0xff << 1, 144, 0), ppc_x(31, 9, 0, 0, 19, 0)); /* mtcrf 0xff; mfcr */
    CHECK(cpu.cr == 0x12345678u && cpu.gpr[9] == 0x12345678u);
    cpu.gpr[3] = 0xffffffffu;
    RUN(ppc_x(31, 3, 0, 0x81 << 1, 144, 0));  /* only fields 0 and 7 */
    CHECK(cpu.cr == 0xf234567fu);
    RUN(ppc_x(19, 1 << 2, 6 << 2, 0, 0, 0));  /* mcrf cr1, cr6 */
    CHECK(cpu.cr == 0xf734567fu);
    cpu.cr = 0x50000000u;                     /* bits: 0=0 1=1 2=0 3=1 */
    RUN(ppc_x(19, 4, 1, 3, 257, 0),           /* crand 4 = 1 & 1 */
        ppc_x(19, 5, 0, 2, 449, 0),           /* cror 5 = 0 | 0 */
        ppc_x(19, 6, 1, 3, 193, 0),           /* crxor 6 = 1 ^ 1 */
        ppc_x(19, 7, 1, 3, 225, 0),           /* crnand 7 = !(1 & 1) */
        ppc_x(19, 8, 0, 2, 33, 0),            /* crnor 8 = !(0 | 0) */
        ppc_x(19, 9, 0, 1, 289, 0),           /* creqv 9 = !(0 ^ 1) */
        ppc_x(19, 10, 1, 2, 129, 0),          /* crandc 10 = 1 & !0 */
        ppc_x(19, 11, 0, 3, 417, 0),          /* crorc 11 = 0 | !1 */
        ppc_x(19, 12, 12, 12, 289, 0));       /* crset 12 */
    CHECK(cpu.cr == 0x58a80000u);
    cpu.xer = PPC_XER_SO | PPC_XER_OV | PPC_XER_CA | 5;
    RUN(ppc_x(31, 7 << 2, 0, 0, 512, 0));     /* mcrxr cr7 */
    CHECK((cpu.cr & 0xf) == 0xe && cpu.xer == 5);
    cpu.gpr[3] = 0xdeadbee0u; cpu.gpr[4] = 0x1234; cpu.gpr[5] = 0xffffffffu;
    RUN(ppc_mtspr(SPR_LR, 3), ppc_mtspr(SPR_CTR, 4), ppc_mtspr(SPR_XER, 5), ppc_mfspr(6, SPR_LR), ppc_mfspr(7, SPR_CTR),
        ppc_mfspr(8, SPR_XER));
    CHECK(cpu.lr == 0xdeadbee0u && cpu.ctr == 0x1234 && cpu.gpr[6] == 0xdeadbee0u && cpu.gpr[7] == 0x1234);
    CHECK(cpu.gpr[8] == 0xe000007fu);         /* XER keeps only its defined bits */
    RUN(ppc_mfspr(6, 272));                   /* SPRG0 is supervisor state */
    CHECK(cpu.halted && strstr(cpu.error, "special register 272") && cpu.pc == CODE);
}

static void test_loads_stores(void) {
    reset();
    cpu.gpr[3] = 0x11223344u; cpu.gpr[4] = 0x2000; cpu.gpr[5] = 0xffff8081u; cpu.gpr[6] = 8;
    RUN(ppc_stw(3, 0, 4), ppc_d(44, 5, 4, 4), ppc_d(38, 5, 4, 6),     /* stw, sth, stb */
        ppc_lwz(7, 0x2000, 0),                                         /* rA = 0: absolute */
        ppc_d(40, 8, 4, 4), ppc_d(42, 9, 4, 4), ppc_d(34, 10, 4, 6),   /* lhz, lha, lbz */
        ppc_lwz(11, 1, 4),                                             /* unaligned word */
        ppc_x(31, 12, 4, 6, 23, 0));                                   /* lwzx: empty memory */
    CHECK(!memcmp(ram + 0x2000, "\x11\x22\x33\x44\x80\x81\x81", 7));  /* big-endian byte order */
    CHECK(cpu.gpr[7] == 0x11223344u && cpu.gpr[8] == 0x8081 && cpu.gpr[9] == 0xffff8081u && cpu.gpr[10] == 0x81);
    CHECK(cpu.gpr[11] == 0x22334480u && cpu.gpr[12] == 0 && !cpu.halted);
    cpu.gpr[4] = 0x2000; cpu.gpr[13] = 0x2100; cpu.gpr[14] = 0x2100; cpu.gpr[15] = 0x2000; cpu.gpr[16] = 0x2000;
    RUN(ppc_d(33, 7, 4, 0),             /* lwzu r7,0(r4) */
        ppc_d(35, 8, 4, 1),             /* lbzu r8,1(r4): r4 becomes 0x2001 */
        ppc_stwu(3, -16, 13),           /* stwu: r13 becomes 0x20f0 */
        ppc_d(45, 3, 14, 2),            /* sthu */
        ppc_d(39, 3, 14, 3),            /* stbu */
        ppc_d(41, 9, 15, 4),            /* lhzu */
        ppc_d(43, 10, 16, 4));          /* lhau */
    CHECK(cpu.gpr[7] == 0x11223344u && cpu.gpr[8] == 0x22 && cpu.gpr[4] == 0x2001 && cpu.gpr[13] == 0x20f0);
    CHECK(!memcmp(ram + 0x20f0, "\x11\x22\x33\x44", 4) && cpu.gpr[14] == 0x2105 && ram[0x2102] == 0x33 &&
          ram[0x2103] == 0x44 && ram[0x2105] == 0x44);
    CHECK(cpu.gpr[9] == 0x8081 && cpu.gpr[15] == 0x2004 && cpu.gpr[10] == 0xffff8081u && cpu.gpr[16] == 0x2004);
    cpu.gpr[4] = 0x2000; cpu.gpr[6] = 4; cpu.gpr[17] = 0x2200; cpu.gpr[18] = 0x2000;
    RUN(ppc_x(31, 7, 4, 6, 343, 0),     /* lhax */
        ppc_x(31, 8, 4, 6, 279, 0),     /* lhzx */
        ppc_x(31, 9, 4, 6, 87, 0),      /* lbzx */
        ppc_x(31, 3, 17, 6, 151, 0),    /* stwx at 0x2204 */
        ppc_x(31, 3, 17, 6, 183, 0),    /* stwux: r17 = 0x2204 */
        ppc_x(31, 3, 17, 6, 407, 0),    /* sthx at 0x2208 */
        ppc_x(31, 3, 17, 6, 215, 0),    /* stbx at 0x2208 */
        ppc_x(31, 10, 18, 6, 55, 0),    /* lwzux: r18 = 0x2004 */
        ppc_x(31, 11, 4, 0, 534, 0),    /* lwbrx r11,r4,r0 with r0 = 0 */
        ppc_x(31, 12, 4, 0, 790, 0),    /* lhbrx */
        ppc_x(31, 3, 17, 6, 662, 0),    /* stwbrx at 0x2208 */
        ppc_x(31, 3, 17, 0, 918, 0));   /* sthbrx at 0x2204 */
    CHECK(cpu.gpr[7] == 0xffff8081u && cpu.gpr[8] == 0x8081 && cpu.gpr[9] == 0x80 && cpu.gpr[17] == 0x2204);
    CHECK(cpu.gpr[10] == 0x80818100u && cpu.gpr[18] == 0x2004 && cpu.gpr[11] == 0x44332211u && cpu.gpr[12] == 0x2211);
    CHECK(!memcmp(ram + 0x2204, "\x44\x33\x33\x44\x44\x33\x22\x11", 8));
    for (unsigned r = 29; r < 32; r++) cpu.gpr[r] = 0xa0000000u + r;
    cpu.gpr[4] = 0x2300;
    RUN(ppc_d(47, 29, 4, 4), ppc_li(29, 0), ppc_li(30, 0), ppc_li(31, 0), ppc_d(46, 30, 4, 8)); /* stmw r29; lmw r30 */
    CHECK(!memcmp(ram + 0x2304, "\xa0\x00\x00\x1d\xa0\x00\x00\x1e\xa0\x00\x00\x1f", 12));
    CHECK(cpu.gpr[29] == 0 && cpu.gpr[30] == 0xa000001eu && cpu.gpr[31] == 0xa000001fu);
    memset(ram + 0x2400, 0xee, 96);
    cpu.gpr[4] = 0x2425;
    RUN(ppc_x(31, 0, 0, 4, 1014, 0));   /* dcbz clears the aligned 32-byte block */
    CHECK(ram[0x241f] == 0xee && ram[0x2420] == 0 && ram[0x243f] == 0 && ram[0x2440] == 0xee);
    /* String moves, as real compiler block copies use them (first seen in DesignWorkshop Lite 1.8.5). */
    memcpy(ram + 0x2500, "ABCDEFGHIJK", 11);
    memset(ram + 0x2520, 0xee, 16);
    cpu.gpr[4] = 0x2500; cpu.gpr[5] = 0x2520; cpu.gpr[6] = 0; cpu.xer = 7;
    cpu.gpr[30] = cpu.gpr[31] = cpu.gpr[0] = 0xffffffffu;
    RUN(ppc_x(31, 30, 4, 11, 597, 0),   /* lswi r30,r4,11: fills r30, r31 and wraps into r0 */
        ppc_x(31, 30, 5, 6, 661, 0),    /* stswx r30,r5,r6: XER count 7 */
        ppc_x(31, 20, 4, 6, 533, 0),    /* lswx r20,r4,r6: 7 bytes */
        ppc_x(31, 20, 5, 3, 725, 0));   /* stswi r20,r5,3 */
    CHECK(cpu.gpr[30] == 0x41424344u && cpu.gpr[31] == 0x45464748u && cpu.gpr[0] == 0x494a4b00u);
    CHECK(cpu.gpr[20] == 0x41424344u && cpu.gpr[21] == 0x45464700u);
    CHECK(!memcmp(ram + 0x2520, "ABCDEFG", 7) && ram[0x2527] == 0xee);
}

static void test_faults(void) {
    reset();
    cpu.gpr[4] = RAM_SIZE - 2; cpu.gpr[7] = 0x55;
    RUN(ppc_li(3, 1), ppc_lwz(7, 0, 4), ppc_li(3, 2));      /* the word crosses the end of RAM */
    CHECK(cpu.halted && cpu.pc == CODE + 4 && cpu.gpr[3] == 1 && cpu.instructions == 1);
    CHECK(strstr(cpu.error, "load") && strstr(cpu.error, "0000fffe") && strstr(cpu.error, "00001004"));
    CHECK(ppc_cpu_run(&cpu, 10) == 0);                      /* a halted CPU does not run */
    reset();
    cpu.gpr[4] = 0xfffffff0u; cpu.gpr[3] = 0x77;
    RUN(ppc_stwu(3, 0, 4));
    CHECK(cpu.halted && strstr(cpu.error, "store") && cpu.gpr[4] == 0xfffffff0u); /* no update after a fault */
    reset();
    cpu.gpr[4] = RAM_SIZE - 3;
    RUN(ppc_d(34, 3, 4, 2), ppc_d(34, 3, 4, 3));            /* last byte is readable, one past is not */
    CHECK(cpu.halted && cpu.pc == CODE + 4);
    reset();
    RUN(0x00000000u);
    CHECK(cpu.halted && strstr(cpu.error, "Unimplemented") && strstr(cpu.error, "00000000 at pc 00001000"));
    reset();
    RUN(ppc_x(31, 3, 4, 5, 20, 0));                         /* lwarx is not in this slice */
    CHECK(cpu.halted && strstr(cpu.error, "extended 20"));
    reset();
    RUN(ppc_d(33, 3, 0, 8));                                /* lwzu with rA = 0 is an invalid form */
    CHECK(cpu.halted);
    reset();
    cpu.pc = RAM_SIZE; ppc_cpu_run(&cpu, 1);
    CHECK(cpu.halted && strstr(cpu.error, "fetch"));
    reset();
    cpu.pc = CODE + 2; ppc_cpu_run(&cpu, 1);
    CHECK(cpu.halted && strstr(cpu.error, "misaligned"));
    reset();
    RUN(PPC_SC);                                            /* sc with no host gate installed */
    CHECK(cpu.halted && strstr(cpu.error, "host call gate"));
    PpcCpu empty;
    ppc_cpu_init(&empty, NULL, 100);
    empty.pc = 0;
    CHECK(empty.ram_size == 0 && ppc_cpu_run(&empty, 1) == 1 && empty.halted && !ppc_mem_span(&empty, 0, 0));
}

static int gate_calls;
static int gate(PpcCpu *c, uint32_t pc, void *user) {
    gate_calls++;
    c->gpr[3] = pc + *(uint32_t *)user;
    return 0;
}

static void test_branches(void) {
    reset();
    RUN_BUDGET(3, ppc_li(3, 1), ppc_b(8, 0, 1), ppc_li(3, 2), ppc_li(4, 9)); /* bl skips one instruction */
    CHECK(cpu.gpr[3] == 1 && cpu.gpr[4] == 9 && cpu.lr == CODE + 8 && cpu.pc == CODE + 16);
    RUN_BUDGET(2, ppc_b(0x100c, 1, 0), ppc_li(3, 2), ppc_li(3, 3), ppc_li(5, 7));  /* ba to an absolute address */
    CHECK(cpu.gpr[5] == 7 && cpu.gpr[3] == 1);
    RUN_BUDGET(2, ppc_b(8, 0, 0), ppc_li(5, 1), ppc_b(-4, 0, 0));           /* backward displacement */
    CHECK(cpu.pc == CODE + 4);
    /* Sum 5+4+3+2+1 with bdnz. */
    RUN_BUDGET(100, ppc_li(3, 0), ppc_li(4, 5), ppc_mtspr(SPR_CTR, 4), ppc_mfspr(5, SPR_CTR), ppc_add(3, 3, 5),
               ppc_bc(BO_DNZ, 0, -8, 0, 0), ppc_b(0, 0, 0) | 0); /* final b . spins until the budget ends */
    CHECK(cpu.gpr[3] == 15 && cpu.ctr == 0 && cpu.pc == CODE + 24);
    cpu.ctr = 1; cpu.gpr[3] = 0;
    RUN_BUDGET(2, ppc_bc(BO_DZ, 0, 8, 0, 0), ppc_li(3, 1), ppc_li(4, 2));   /* bdz taken when CTR hits zero */
    CHECK(cpu.gpr[3] == 0 && cpu.ctr == 0 && cpu.pc == CODE + 12);
    cpu.ctr = 2; cpu.cr = 0x20000000u;                                      /* EQ of cr0 */
    RUN_BUDGET(2, ppc_bc(8, BI_EQ, 8, 0, 0), ppc_li(3, 1), ppc_li(3, 2));   /* decrement, branch if CTR != 0 and EQ */
    CHECK(cpu.gpr[3] == 2 && cpu.ctr == 1);
    cpu.gpr[3] = 0;
    RUN_BUDGET(2, ppc_bc(8, BI_EQ, 8, 0, 0), ppc_li(3, 1), ppc_li(3, 2));   /* CTR reaches zero: falls through */
    CHECK(cpu.gpr[3] == 1 && cpu.ctr == 0);
    cpu.cr = 0x02000000u;                                                   /* EQ of cr1 */
    RUN_BUDGET(4, ppc_bc(BO_TRUE, 4 + BI_EQ, 8, 0, 0), ppc_li(3, 1), ppc_bc(BO_FALSE, 4 + BI_EQ, 8, 0, 1), ppc_li(4, 7),
               ppc_li(5, 8));
    CHECK(cpu.gpr[3] == 1 && cpu.gpr[4] == 7 && cpu.gpr[5] == 8 && cpu.lr == CODE + 12); /* untaken bcl still links */
    cpu.lr = CODE + 12; cpu.gpr[3] = 0; cpu.cr = 0x80000000u;
    RUN_BUDGET(2, ppc_bclr(BO_TRUE, BI_LT, 1), ppc_li(3, 1), ppc_li(3, 2), ppc_li(4, 3)); /* bltlrl: old LR is the target */
    CHECK(cpu.gpr[4] == 3 && cpu.gpr[3] == 0 && cpu.lr == CODE + 4);
    cpu.ctr = CODE + 12 + 3; cpu.gpr[3] = 0;
    RUN_BUDGET(2, ppc_bcctr(BO_ALWAYS, 0, 1), ppc_li(3, 1), ppc_li(3, 2), ppc_li(4, 4)); /* bctrl ignores the low bits */
    CHECK(cpu.gpr[4] == 4 && cpu.gpr[3] == 0 && cpu.lr == CODE + 4 && cpu.ctr == CODE + 15);
    cpu.cr = 0;
    RUN_BUDGET(2, ppc_bclr(BO_TRUE, BI_EQ, 0), ppc_li(3, 5));               /* beqlr not taken */
    CHECK(cpu.gpr[3] == 5);
    /* ppc_cpu_call: a routine that doubles r3 through a helper it calls with bl. */
    reset();
    uint32_t at = 0x3000;
    const uint32_t routine[] = {ppc_mfspr(0, SPR_LR), ppc_stw(0, 8, 1), ppc_stwu(1, -64, 1), ppc_b(24, 0, 1),
                                ppc_addi(1, 1, 64), ppc_lwz(0, 8, 1), ppc_mtspr(SPR_LR, 0), PPC_BLR,
                                PPC_BLR /* padding */, ppc_add(3, 3, 3), PPC_BLR};
    for (size_t i = 0; i < sizeof routine / 4; i++) ppc_emit(ram, &at, routine[i]);
    cpu.gpr[1] = 0x8000; cpu.gpr[3] = 21;
    CHECK(ppc_cpu_call(&cpu, 0x3000, 0x1234, 100) == 0);
    CHECK(cpu.gpr[3] == 42 && cpu.gpr[1] == 0x8000 && cpu.gpr[2] == 0x1234 && cpu.pc == PPC_STOP_PC && cpu.instructions == 10);
    CHECK(ppc_cpu_call(&cpu, 0x3000, 0, 3) == 1);                           /* budget exhausted mid-routine */
    at = 0x4000;
    ppc_emit(ram, &at, PPC_SC); ppc_emit(ram, &at, PPC_BLR);
    uint32_t bias = 5;
    cpu.syscall = gate; cpu.syscall_user = &bias;
    CHECK(ppc_cpu_call(&cpu, 0x4000, 0, 10) == 0 && gate_calls == 1 && cpu.gpr[3] == 0x4005);
}

int main(void) {
    test_add_carry_overflow();
    test_subtract();
    test_multiply_divide();
    test_logical_and_shifts();
    test_rotates();
    test_compare_cr_spr();
    test_loads_stores();
    test_faults();
    test_branches();
    if (failures) { fprintf(stderr, "test_ppc_cpu: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_ppc_cpu: all %d checks passed\n", checks);
    return 0;
}
