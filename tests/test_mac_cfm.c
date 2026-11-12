/* End to end: a synthetic big-endian PowerPC PEF fragment is parsed, placed
 * in guest RAM (pattern-initialized data expanded), relocated, its imports
 * bound to host functions, and its entry points run in the interpreter. The
 * fragment is assembled here by hand; nothing in it comes from a real program. */
#include "mac_cfm.h"
#include "pef_fixture.h"
#include "ppc_asm.h"

#include <stdio.h>
#include <stdlib.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define RAM_SIZE 0x100000u
static uint8_t *ram;
static PpcCpu cpu;
static MacCfmFragment fragment;
static uint32_t heap_next, heap_limit;
static int add7_calls, user_token;

static uint32_t bump(void *ctx, uint32_t size) {
    (void)ctx;
    uint32_t at = heap_next, aligned = (size + 3u) & ~3u;
    if (heap_next > heap_limit || aligned > heap_limit - heap_next) return 0;
    heap_next += aligned ? aligned : 4;
    return at;
}

static uint32_t wild(void *ctx, uint32_t size) { (void)ctx; (void)size; return RAM_SIZE - 8; }

static int host_add7(PpcCpu *c, void *user) {
    add7_calls++;
    if (user != &user_token) return -1;
    c->gpr[3] += 7;
    return 0;
}
static int host_fails(PpcCpu *c, void *user) { (void)c; (void)user; return -1; }

/* Data section layout (offsets from the TOC base, which is the start of the section). */
enum { MAIN_TV = 0x00, TOC_STRLEN = 0x08, TOC_ADD7 = 0x0c, TOC_STRING = 0x10, TOC_FIVE = 0x14, STRING = 0x18,
       ISQRT_TV = 0x20, TOC_SQRT = 0x28, TOC_NEWWINDOW = 0x2c, SIXTEEN = 0x30, CRASH_TV = 0x38, RESULT = 0x40,
       DATA_UNPACKED = 0x40, DATA_TOTAL = 0x50 };

static uint32_t code[80];
static unsigned code_count;
static uint32_t here(void) { return 4 * code_count; }
static void op(uint32_t word) { code[code_count++] = word; }
static void call(uint32_t target) { op(ppc_b((int32_t)(target - here()), 0, 1)); }
static void prologue(void) { op(ppc_mfspr(0, SPR_LR)); op(ppc_stw(0, 8, 1)); op(ppc_stwu(1, -64, 1)); }
static void epilogue(void) { op(ppc_addi(1, 1, 64)); op(ppc_lwz(0, 8, 1)); op(ppc_mtspr(SPR_LR, 0)); op(PPC_BLR); }
/* Cross-fragment glue as a compiler emits it: fetch the transition vector from the TOC, save our TOC, jump. */
static uint32_t glue(int32_t toc_slot) {
    uint32_t at = here();
    op(ppc_lwz(12, toc_slot, 2)); op(ppc_stw(2, 20, 1)); op(ppc_lwz(0, 0, 12)); op(ppc_lwz(2, 4, 12));
    op(ppc_mtspr(SPR_CTR, 0)); op(PPC_BCTR);
    return at;
}

static uint32_t isqrt_at, crash_at;

static void assemble(void) {
    code_count = 0;
    const uint32_t glue_strlen = 4 * 19, glue_add7 = glue_strlen + 24, glue_sqrt = glue_add7 + 24,
                   glue_newwindow = glue_sqrt + 24;
    /* main: (strlen("hello") + 5 + 7) * 3, also stored in the zero-initialized tail of the data section. */
    prologue();
    op(ppc_stw(31, 56, 1));
    op(ppc_lwz(3, TOC_STRING, 2));
    call(glue_strlen);
    op(ppc_lwz(2, 20, 1));
    op(ppc_mr(31, 3));
    op(ppc_lwz(4, TOC_FIVE, 2));
    op(ppc_add(3, 31, 4));
    call(glue_add7);
    op(ppc_lwz(2, 20, 1));
    op(ppc_d(7, 3, 3, 3)); /* mulli r3,r3,3 */
    op(ppc_stw(3, RESULT, 2));
    op(ppc_lwz(31, 56, 1));
    epilogue();
    if (here() != glue_strlen || glue(TOC_STRLEN) != glue_strlen || glue(TOC_ADD7) != glue_add7 ||
        glue(TOC_SQRT) != glue_sqrt || glue(TOC_NEWWINDOW) != glue_newwindow)
        failures++;
    /* isqrt: (int)sqrt(16.0) through a host function that takes and returns f1. */
    isqrt_at = here();
    prologue();
    op(ppc_d(50, 1, 2, SIXTEEN)); /* lfd f1 */
    call(glue_sqrt);
    op(ppc_lwz(2, 20, 1));
    op(ppc_x(63, 2, 0, 1, 15, 0)); /* fctiwz f2,f1 */
    op(ppc_d(54, 2, 1, 24));       /* stfd f2,24(r1) */
    op(ppc_lwz(3, 28, 1));
    epilogue();
    /* crash: calls an import nobody implements. */
    crash_at = here();
    prologue();
    op(ppc_li(3, 1));
    call(glue_newwindow);
    op(ppc_lwz(2, 20, 1));
    epilogue();
}

/* Pattern-initialized data for the layout above. */
static size_t pack_data(uint8_t *p) {
    uint8_t *start = p;
    *p++ = 0x10;                                   /* zero 16: two vectors' worth of relocated words */
    *p++ = 0x30;                                   /* block copy 16 */
    fix32(p, STRING); fix32(p + 4, 5); memcpy(p + 8, "hello\0\0", 8); p += 16;
    *p++ = 0x24; fix32(p, isqrt_at); p += 4;       /* block copy 4: code offset of isqrt */
    *p++ = 0x0c;                                   /* zero 12: its TOC word and two import slots */
    *p++ = 0x28; fix32(p, 0x40300000); fix32(p + 4, 0); p += 8; /* block copy 8: 16.0 */
    *p++ = 0x24; fix32(p, crash_at); p += 4;
    *p++ = 0x04;                                   /* zero 4 */
    return (size_t)(p - start);
}

static const FixLibrary libraries[] = {{"StdCLib", 0, 1}, {"TestLib", 1, 1}, {"MathLib", 2, 1}, {"InterfaceLib", 3, 1}};
static const FixImport imports[] = {{"strlen", 2}, {"add7", 2}, {"sqrt", 2}, {"NewWindow", 2}};
static const FixExport exports[] = {{"isqrt", ISQRT_TV, 1, 2}, {"crash", CRASH_TV, 1, 2}, {"result", RESULT, 1, 1}};
static const uint16_t relocations[] = {
    0x4600,          /* main's transition vector: code address, TOC */
    0x4a01,          /* TOC slots for imports 0 and 1 */
    0x4200,          /* pointer to the string */
    0xa000, 0x0020,  /* move to the second vector */
    0x4600, 0x4a01,  /* isqrt's vector, then TOC slots for imports 2 and 3 */
    0xa000, 0x0038, 0x4600,
};

static uint8_t image[2048];
static size_t image_size;
static MacCfmHostImport table[8];
static size_t table_count;

static void build_image(void) {
    uint8_t bytes[sizeof code], packed[128];
    PefFixture f = {0};
    assemble();
    for (unsigned i = 0; i < code_count; i++) fix32(bytes + 4 * i, code[i]);
    f.code = bytes; f.code_size = 4 * code_count;
    f.data = packed; f.data_packed = (uint32_t)pack_data(packed); f.data_unpacked = DATA_UNPACKED; f.data_total = DATA_TOTAL;
    f.data_kind = MAC_PEF_PATTERN_DATA;
    f.libraries = libraries; f.library_count = 4; f.imports = imports; f.import_count = 4;
    f.reloc = relocations; f.reloc_count = sizeof relocations / 2; f.reloc_section = 1;
    f.main_section = 1; f.main_offset = MAIN_TV;
    f.exports = exports; f.export_count = 3;
    image_size = pef_fixture_build(&f, image, sizeof image, NULL);
    size_t builtin;
    const MacCfmHostImport *host = mac_ppc_host_imports(&builtin);
    table_count = 0;
    table[table_count++] = (MacCfmHostImport){"TestLib", "add7", host_add7, NULL};
    for (size_t i = 0; i < builtin && table_count < 8; i++) table[table_count++] = host[i];
}

static int load(void) {
    mac_cfm_unload(&fragment);
    memset(ram, 0, RAM_SIZE);
    ppc_cpu_init(&cpu, ram, RAM_SIZE);
    heap_next = 0x10000; heap_limit = RAM_SIZE;
    int rc = mac_cfm_load(&fragment, &cpu, image, image_size, table, table_count, bump, NULL, &user_token);
    uint32_t stack = bump(NULL, 0x10000);
    if (!rc && mac_cfm_stack(&fragment, stack, 0x10000)) rc = -3;
    return rc;
}

static uint32_t word(uint32_t address) { return fix_get32(ram + address); }

static void test_end_to_end(void) {
    CHECK(image_size && load() == 0);
    uint32_t C = fragment.section_address[0], D = fragment.section_address[1], sp = cpu.gpr[1];
    CHECK(C >= 0x10000 && D > C && !(C & 3) && !(D & 3) && fragment.section_address[2] == 0);
    CHECK(fragment.import_count == 4 && fragment.missing == 1 && !strcmp(fragment.first_missing, "InterfaceLib::NewWindow"));
    CHECK(fragment.imports[0].resolved && fragment.imports[1].resolved && fragment.imports[2].resolved &&
          !fragment.imports[3].resolved && !strcmp(fragment.imports[2].library, "MathLib"));
    /* Relocation results: vectors point at code and TOC, TOC slots at import gates, the string pointer at the string. */
    CHECK(fragment.main_vector == D + MAIN_TV && word(D) == C && word(D + 4) == D);
    CHECK(word(D + TOC_STRLEN) == fragment.gate_base && word(D + TOC_ADD7) == fragment.gate_base + 16);
    CHECK(word(D + TOC_STRING) == D + STRING && word(D + TOC_FIVE) == 5 && !memcmp(ram + D + STRING, "hello", 6));
    CHECK(word(D + ISQRT_TV) == C + isqrt_at && word(D + ISQRT_TV + 4) == D && word(D + CRASH_TV) == C + crash_at);
    CHECK(word(D + TOC_SQRT) == fragment.gate_base + 32 && word(D + TOC_NEWWINDOW) == fragment.gate_base + 48);
    CHECK(word(D + SIXTEEN) == 0x40300000u && word(D + RESULT) == 0 && word(C) == ppc_mfspr(0, SPR_LR));
    /* The gate record: a transition vector whose code is `sc; blr`. */
    CHECK(word(fragment.gate_base) == fragment.gate_base + 8 && word(fragment.gate_base + 4) == 0 &&
          word(fragment.gate_base + 8) == PPC_SC && word(fragment.gate_base + 12) == PPC_BLR);
    cpu.gpr[31] = 0x31313131u;
    CHECK(mac_cfm_call(&fragment, fragment.main_vector, 1000) == 0);
    CHECK(cpu.gpr[3] == 51 && word(D + RESULT) == 51 && add7_calls == 1);
    CHECK(cpu.gpr[1] == sp && cpu.gpr[2] == D && cpu.gpr[31] == 0x31313131u && cpu.pc == PPC_STOP_PC && !cpu.halted);
    CHECK(cpu.instructions == 19 + 2 * (6 + 2)); /* main, plus glue and sc/blr for each of the two host calls */
    uint32_t isqrt, crash, result, missing;
    CHECK(mac_cfm_export(&fragment, "isqrt", &isqrt) == 0 && isqrt == D + ISQRT_TV);
    CHECK(mac_cfm_export(&fragment, "result", &result) == 0 && result == D + RESULT);
    CHECK(mac_cfm_export(&fragment, "nothing", &missing) == 1);
    CHECK(mac_cfm_call(&fragment, isqrt, 1000) == 0 && cpu.gpr[3] == 4 && ppc_fpr_get(&cpu, 1) == 4.0 && cpu.gpr[1] == sp);
    /* An import with no host implementation halts with a diagnostic naming it, like an unsupported 68K trap. */
    CHECK(mac_cfm_export(&fragment, "crash", &crash) == 0);
    CHECK(mac_cfm_call(&fragment, crash, 1000) == -1 && cpu.halted);
    CHECK(strstr(cpu.error, "Unsupported PowerPC import InterfaceLib::NewWindow"));
    CHECK(cpu.pc == fragment.gate_base + 48 + 8);                 /* stopped on the gate's sc */
    CHECK(mac_cfm_call(&fragment, fragment.main_vector, 1000) == -1); /* a halted CPU stays halted */
    CHECK(load() == 0 && mac_cfm_call(&fragment, fragment.main_vector, 5) == 1 && !cpu.halted); /* budget ran out */
}

static void test_host_imports_and_gate(void) {
    /* A fragment that only imports: call each built-in host function through its own transition vector. */
    static const FixLibrary libs[] = {{"StdCLib", 0, 4}, {"MathLib", 4, 1}, {"TestLib", 5, 2}};
    static const FixImport syms[] = {{"strlen", 2}, {"memcpy", 2}, {"memset", 2}, {"abs", 0}, {"sqrt", 2}, {"fails", 2},
                                     {"table", 1}};
    static const uint16_t relocs[] = {0x4a06};
    uint8_t small[512], data[28] = {0};
    PefFixture f = {0};
    f.data = data; f.data_packed = f.data_unpacked = f.data_total = sizeof data; f.data_kind = 1;
    f.libraries = libs; f.library_count = 3; f.imports = syms; f.import_count = 7;
    f.reloc = relocs; f.reloc_count = 1; f.reloc_section = 1; f.main_section = -1;
    size_t size = pef_fixture_build(&f, small, sizeof small, NULL);
    MacCfmHostImport hosts[8];
    size_t n;
    const MacCfmHostImport *builtin = mac_ppc_host_imports(&n);
    memcpy(hosts, builtin, n * sizeof *hosts);
    hosts[n] = (MacCfmHostImport){NULL, "fails", host_fails, NULL}; /* a NULL library matches any */
    hosts[n + 1] = (MacCfmHostImport){"TestLib", "table", host_fails, NULL}; /* functions never satisfy data imports */
    mac_cfm_unload(&fragment);
    memset(ram, 0, RAM_SIZE);
    ppc_cpu_init(&cpu, ram, RAM_SIZE);
    heap_next = 0x10000; heap_limit = RAM_SIZE;
    CHECK(n == 5 && size && mac_cfm_load(&fragment, &cpu, small, size, hosts, n + 2, bump, NULL, NULL) == 0);
    CHECK(fragment.missing == 1 && !strcmp(fragment.first_missing, "TestLib::table") && !fragment.main_vector);
    uint32_t D = fragment.section_address[1], g = fragment.gate_base;
    CHECK(word(D + 24) == PPC_UNMAPPED_BASE + 6 * 16 && fragment.imports[6].address >= PPC_UNMAPPED_BASE); /* parked data import */
    CHECK(word(D) == g && word(D + 12) == g + 48 + 8); /* a vector import is the record, a code-class import its code */
    CHECK(mac_cfm_call(&fragment, g, 10) == -1 && cpu.halted && strstr(cpu.error, "without a stack"));
    cpu.halted = 0;
    CHECK(mac_cfm_stack(&fragment, 0x80000, 0x1000) == 0 && cpu.gpr[1] == 0x80fc0 && mac_cfm_stack(&fragment, RAM_SIZE - 16, 0x1000) == -1);
    memcpy(ram + 0x20000, "PowerPC", 8);
    cpu.gpr[3] = 0x20000;
    CHECK(mac_cfm_call(&fragment, g, 10) == 0 && cpu.gpr[3] == 7);                          /* strlen */
    cpu.gpr[3] = 0x20100; cpu.gpr[4] = 0x20000; cpu.gpr[5] = 8;
    CHECK(mac_cfm_call(&fragment, g + 16, 10) == 0 && cpu.gpr[3] == 0x20100 && !strcmp((char *)ram + 0x20100, "PowerPC")); /* memcpy */
    cpu.gpr[3] = 0x20101; cpu.gpr[4] = 0x12a; cpu.gpr[5] = 3;
    CHECK(mac_cfm_call(&fragment, g + 32, 10) == 0 && !memcmp(ram + 0x20100, "P***rPC", 8)); /* memset uses the low byte */
    cpu.gpr[3] = 0xfffffff9u;
    CHECK(mac_cfm_call(&fragment, g + 48, 10) == 0 && cpu.gpr[3] == 7);                     /* abs */
    ppc_fpr_set(&cpu, 1, 2.25);
    CHECK(mac_cfm_call(&fragment, g + 64, 10) == 0 && ppc_fpr_get(&cpu, 1) == 1.5);         /* sqrt */
    CHECK(cpu.instructions == 10);                                                          /* five calls of sc; blr */
    cpu.gpr[3] = RAM_SIZE - 4; memset(ram + RAM_SIZE - 4, 'x', 4);                          /* unterminated at the end of RAM */
    CHECK(mac_cfm_call(&fragment, g, 10) == -1 && strstr(cpu.error, "strlen") && strstr(cpu.error, "outside guest RAM"));
    cpu.halted = 0; cpu.gpr[3] = 0x20000; cpu.gpr[4] = RAM_SIZE - 2; cpu.gpr[5] = 8;
    CHECK(mac_cfm_call(&fragment, g + 16, 10) == -1 && strstr(cpu.error, "memcpy"));
    cpu.halted = 0;
    CHECK(mac_cfm_call(&fragment, g + 80, 10) == -1 && strstr(cpu.error, "TestLib::fails failed"));
    cpu.halted = 0;
    fix32(ram + 0x30000, PPC_SC); fix32(ram + 0x30004, PPC_BLR);
    CHECK(ppc_cpu_call(&cpu, 0x30000, 0, 10) == -1 && strstr(cpu.error, "not a host call gate")); /* sc outside the gates */
    cpu.halted = 0;
    CHECK(ppc_cpu_call(&cpu, g + 12, 0, 10) == 0 && cpu.instructions == 11); /* the blr half of a record is ordinary code */
    cpu.pc = g + 4; ppc_cpu_run(&cpu, 1);
    CHECK(cpu.halted);                                              /* the vector's TOC word (0) is not an instruction */
    mac_cfm_unload(&fragment);
    CHECK(cpu.syscall == NULL && fragment.imports == NULL);
}

static void test_rejected_fragments(void) {
    uint8_t copy[sizeof image];
    memset(ram, 0, RAM_SIZE);
    ppc_cpu_init(&cpu, ram, RAM_SIZE);
    heap_next = 0x10000; heap_limit = RAM_SIZE;
    memcpy(copy, image, image_size);
    memcpy(copy + 8, "m68k", 4);
    CHECK(mac_cfm_load(&fragment, &cpu, copy, image_size, table, table_count, bump, NULL, NULL) == -1 &&
          strstr(fragment.error, "not PowerPC"));
    memcpy(copy, image, image_size);
    copy[FIX_SECTION_HEADER(2) + 24] = MAC_PEF_DEBUG;
    CHECK(mac_cfm_load(&fragment, &cpu, copy, image_size, table, table_count, bump, NULL, NULL) == -1 &&
          strstr(fragment.error, "no loader"));
    CHECK(mac_cfm_load(&fragment, &cpu, image, image_size - 1, table, table_count, bump, NULL, NULL) == -1 && fragment.error[0]);
    CHECK(mac_cfm_load(&fragment, &cpu, image, image_size, table, table_count, NULL, NULL, NULL) == -1);
    heap_limit = heap_next + 64; /* room for nothing but a sliver */
    CHECK(mac_cfm_load(&fragment, &cpu, image, image_size, table, table_count, bump, NULL, NULL) == -1 &&
          strstr(fragment.error, "Out of guest memory") && cpu.syscall == NULL);
    heap_limit = RAM_SIZE;
    uint32_t loader_at = fix_get32(image + FIX_SECTION_HEADER(2) + 20);
    memcpy(copy, image, image_size);
    fix32(copy + loader_at + 4, DATA_TOTAL - 4); /* main vector would straddle the end of its section */
    CHECK(mac_cfm_load(&fragment, &cpu, copy, image_size, table, table_count, bump, NULL, NULL) == -1 &&
          strstr(fragment.error, "entry point"));
    memcpy(copy, image, image_size);
    fix32(copy + loader_at, 2); /* main in the loader section */
    CHECK(mac_cfm_load(&fragment, &cpu, copy, image_size, table, table_count, bump, NULL, NULL) == -1);
    /* An allocator that hands back memory outside guest RAM is caught, not trusted. */
    CHECK(mac_cfm_load(&fragment, &cpu, image, image_size, table, table_count, wild, NULL, NULL) == -1 &&
          strstr(fragment.error, "Out of guest memory"));
    PpcCpu none;
    ppc_cpu_init(&none, NULL, 0);
    CHECK(mac_cfm_load(&fragment, &none, image, image_size, table, table_count, bump, NULL, NULL) == -1);
}

int main(void) {
    ram = malloc(RAM_SIZE);
    if (!ram) return 2;
    build_image();
    test_end_to_end();
    test_host_imports_and_gate();
    test_rejected_fragments();
    mac_cfm_unload(&fragment);
    free(ram);
    if (failures) { fprintf(stderr, "test_mac_cfm: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_cfm: all %d checks passed\n", checks);
    return 0;
}
