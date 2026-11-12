/* PEF relocation instructions, one form at a time, against a synthetic data
 * section. C is where the code section landed, D where the data section
 * landed, I(k) the address bound to import k. */
#include "mac_cfm.h"
#include "pef_fixture.h"

#include <stdio.h>
#include <stdlib.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define RAM_SIZE 0x100000u
#define WORDS 16u
static uint8_t *ram;
static PpcCpu cpu;
static MacCfmFragment fragment;
static uint32_t heap_next;
static uint32_t C, D;

static uint32_t bump(void *ctx, uint32_t size) {
    (void)ctx;
    uint32_t at = heap_next, aligned = (size + 3u) & ~3u;
    if (aligned > RAM_SIZE - heap_next) return 0;
    heap_next += aligned ? aligned : 4;
    return at;
}

static const uint8_t code_bytes[8] = {0x38, 0x60, 0x00, 0x2a, 0x4e, 0x80, 0x00, 0x20};
static const FixLibrary libraries[] = {{"TestLib", 0, 4}};
static const FixImport imports[] = {{"a", 2}, {"b", 2}, {"c", 2}, {"d", 2}};

/* Loads a fragment whose data section holds WORDS words, word k initialised to k (so addends are visible). */
static int load_at(const uint16_t *reloc, uint32_t count, uint32_t default_address, uint16_t reloc_section) {
    uint8_t image[1024], data[4 * WORDS];
    PefFixture f = {0};
    mac_cfm_unload(&fragment);
    memset(ram, 0, RAM_SIZE);
    ppc_cpu_init(&cpu, ram, RAM_SIZE);
    heap_next = 0x10000;
    for (uint32_t k = 0; k < WORDS; k++) fix32(data + 4 * k, k);
    f.code = code_bytes; f.code_size = sizeof code_bytes;
    f.data = data; f.data_packed = f.data_unpacked = f.data_total = sizeof data; f.data_kind = 1;
    f.data_default_address = default_address;
    f.libraries = libraries; f.library_count = 1; f.imports = imports; f.import_count = 4;
    f.reloc = reloc; f.reloc_count = count; f.reloc_section = reloc_section; f.main_section = -1;
    size_t size = pef_fixture_build(&f, image, sizeof image, NULL);
    int rc = size ? mac_cfm_load(&fragment, &cpu, image, size, NULL, 0, bump, NULL, NULL) : -2;
    C = fragment.section_address[0];
    D = fragment.section_address[1];
    return rc;
}
#define LOAD(...) (load_at((const uint16_t[]){__VA_ARGS__}, sizeof((const uint16_t[]){__VA_ARGS__}) / 2, 0, 1))

static uint32_t I(uint32_t k) { return fragment.gate_base + k * MAC_CFM_GATE_BYTES; }
/* Word k of the data section minus its initial value k: what relocation added. */
static uint32_t added(uint32_t k) { return fix_get32(ram + D + 4 * k) - k; }
static int only(uint32_t mask) { /* every word outside `mask` is untouched */
    for (uint32_t k = 0; k < WORDS; k++)
        if (!(mask & (1u << k)) && added(k)) return 0;
    return 1;
}

static void test_run_forms(void) {
    CHECK(LOAD(0x0083) == 0);                       /* by section D with skip: skip 2 words, relocate 3 */
    CHECK(C && D && C != D && added(2) == D && added(3) == D && added(4) == D && only(0x1c));
    CHECK(LOAD(0x4001) == 0 && added(0) == C && added(1) == C && only(0x3));           /* by section C, run 2 */
    CHECK(LOAD(0x4202) == 0 && added(0) == D && added(2) == D && only(0x7));           /* by section D, run 3 */
    CHECK(LOAD(0x4401) == 0);                       /* 12-byte transition vectors, run 2 */
    CHECK(added(0) == C && added(1) == D && added(3) == C && added(4) == D && only(0x1b));
    CHECK(LOAD(0x4601) == 0);                       /* 8-byte transition vectors, run 2 */
    CHECK(added(0) == C && added(1) == D && added(2) == C && added(3) == D && only(0xf));
    CHECK(LOAD(0x4801) == 0 && added(0) == D && added(2) == D && only(0x5));           /* vtable entries, run 2 */
    CHECK(LOAD(0x4a02) == 0 && added(0) == I(0) && added(1) == I(1) && added(2) == I(2) && only(0x7)); /* import run */
    CHECK(fragment.gate_base && fragment.import_count == 4 && fragment.missing == 4);
    CHECK(!strcmp(fragment.first_missing, "TestLib::a"));
}

static void test_indexed_and_position_forms(void) {
    CHECK(LOAD(0x6002, 0x4a00) == 0 && added(0) == I(2) && added(1) == I(3) && only(0x3)); /* by import sets the run index */
    CHECK(LOAD(0x6201, 0x4000) == 0 && added(0) == D && only(0x1));                    /* set section C := 1 */
    CHECK(LOAD(0x6400, 0x4200) == 0 && added(0) == C && only(0x1));                    /* set section D := 0 */
    CHECK(LOAD(0x6600, 0x6601) == 0 && added(0) == C && added(1) == D && only(0x3));   /* by section index */
    CHECK(LOAD(0x8007, 0x4200) == 0 && added(2) == D && only(0x4));                    /* increment position by 8 */
    CHECK(LOAD(0x8000, 0x8002, 0x4200) == 0 && added(1) == D && only(0x2));            /* 1 + 3 bytes */
    CHECK(LOAD(0xa000, 0x0020, 0x4000) == 0 && added(8) == C && only(0x100));          /* set position */
    CHECK(LOAD(0x4200, 0xa000, 0x0000, 0x4000) == 0 && added(0) == C + D && only(0x1)); /* back to the start: adds twice */
    CHECK(LOAD(0xa400, 0x0001, 0x4a00) == 0 && added(0) == I(1) && added(1) == I(2) && only(0x3)); /* large by import */
    CHECK(LOAD(0xb400, 0x0000, 0xb400, 0x0001) == 0 && added(0) == C && added(1) == D && only(0x3)); /* large by section */
    CHECK(LOAD(0xb440, 0x0001, 0x4000) == 0 && added(0) == D && only(0x1));            /* large set section C */
    CHECK(LOAD(0xb480, 0x0000, 0x4200) == 0 && added(0) == C && only(0x1));            /* large set section D */
    CHECK(LOAD(0x003f) == -1 && strstr(fragment.error, "outside its section"));        /* 63 words do not fit in 16 */
    /* Sections linked at a non-zero default address are relocated by the difference. */
    static const uint16_t by_d[] = {0x4200, 0x6601};
    CHECK(load_at(by_d, 2, 0x1000, 1) == 0 && added(0) == D - 0x1000 && added(1) == D - 0x1000 && only(0x3));
    /* Relocating the code section: position and bounds are the code section's. */
    static const uint16_t in_code[] = {0x8003, 0x4200};
    CHECK(load_at(in_code, 2, 0, 0) == 0 && fix_get32(ram + C + 4) == 0x4e800020u + D && fix_get32(ram + C) == 0x3860002au);
    CHECK(only(0));
}

static void test_repeats(void) {
    /* by D once, skip a word, then repeat those two instructions 3 more times: words 0, 2, 4, 6. */
    CHECK(LOAD(0x4200, 0x8003, 0x9102) == 0);
    CHECK(added(0) == D && added(2) == D && added(4) == D && added(6) == D && only(0x55));
    CHECK(LOAD(0x4200, 0x9000) == 0 && added(0) == D && added(1) == D && only(0x3));   /* one block, one more time */
    CHECK(LOAD(0x4200, 0xb000, 0x0002) == 0 && added(0) == D && added(1) == D && added(2) == D && only(0x7)); /* large repeat */
    CHECK(LOAD(0x4200, 0xb000, 0x0000, 0x4000) == 0 && added(0) == D && added(1) == C && only(0x3)); /* zero repeats */
    /* Two separate repeats in one stream each keep their own count. */
    CHECK(LOAD(0x4200, 0x9000, 0x4000, 0x9001) == 0);
    CHECK(added(0) == D && added(1) == D && added(2) == C && added(3) == C && added(4) == C && only(0x1f));
    /* A repeat whose block is a two-word instruction. */
    CHECK(LOAD(0xa400, 0x0000, 0x9101) == 0 && added(0) == I(0) && added(1) == I(0) && added(2) == I(0) && only(0x7));
    /* The longest legal repeat of a harmless block still ends (about four million iterations). */
    CHECK(LOAD(0x6200, 0xb03f, 0xffff, 0x4000) == 0 && added(0) == C && only(0x1));
}

static void test_rejected_streams(void) {
    CHECK(LOAD(0x4a04) == -1 && strstr(fragment.error, "import"));                     /* fifth import does not exist */
    CHECK(fragment.imports == NULL);                                                   /* a failed load releases its tables */
    CHECK(LOAD(0x61ff) == -1 && LOAD(0xa7ff, 0xffff) == -1);                           /* import index out of range */
    CHECK(LOAD(0x6602) == -1 && strstr(fragment.error, "not in memory"));              /* the loader section has no address */
    CHECK(LOAD(0x6209) == -1 && LOAD(0x6409) == -1 && LOAD(0xb440, 0x0009) == -1);     /* section does not exist */
    CHECK(LOAD(0x4200, 0xa000) == -1 && strstr(fragment.error, "two-word"));           /* truncated large form */
    CHECK(LOAD(0xc000) == -1 && LOAD(0xe123) == -1 && LOAD(0xa800, 0) == -1 && LOAD(0xbc00, 0) == -1); /* unknown opcodes */
    CHECK(LOAD(0x4c00) == -1 && LOAD(0x6800) == -1 && LOAD(0xb4c0, 0) == -1);          /* unknown sub-opcodes */
    CHECK(LOAD(0x9000) == -1 && strstr(fragment.error, "before the start"));           /* nothing to repeat */
    CHECK(LOAD(0x4200, 0x9100) == -1);                                                 /* block longer than the stream so far */
    CHECK(LOAD(0x4000, 0x9000, 0x9100) == -1 && strstr(fragment.error, "Nested"));     /* repeat inside a repeat */
    CHECK(LOAD(0xa000, 0x0041) == -1 && LOAD(0x8fff) == -1 && LOAD(0x3fc0) == -1);     /* position leaves the section */
    CHECK(LOAD(0xa000, 0x003d, 0x4000) == -1);                                         /* word straddles the section end */
    CHECK(LOAD(0xa000, 0x003c, 0x4000) == 0 && added(15) == C);                        /* the last word is fine */
    CHECK(LOAD(0x4200, 0x90ff) == -1 && strstr(fragment.error, "outside its section")); /* a repeat runs off the end */
    static const uint16_t one[] = {0x4200};
    CHECK(load_at(one, 1, 0, 2) == -1 && strstr(fragment.error, "not in memory"));     /* relocations for the loader */
}

int main(void) {
    ram = malloc(RAM_SIZE);
    if (!ram) return 2;
    test_run_forms();
    test_indexed_and_position_forms();
    test_repeats();
    test_rejected_streams();
    mac_cfm_unload(&fragment);
    free(ram);
    if (failures) { fprintf(stderr, "test_mac_cfm_reloc: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_cfm_reloc: all %d checks passed\n", checks);
    return 0;
}
