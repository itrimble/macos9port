/* PEF container parser: structure, loader tables, pattern-initialized data and
 * rejection of malformed input. All containers are synthetic (pef_fixture.h). */
#include "mac_pef.h"
#include "pef_fixture.h"

#include <stdio.h>
#include <stdlib.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const uint8_t code_bytes[8] = {0x38, 0x60, 0x00, 0x2a, 0x4e, 0x80, 0x00, 0x20}; /* li r3,42; blr */
static const uint8_t data_bytes[12] = {0, 0, 0, 0, 0, 0, 0, 0, 'd', 'a', 't', 'a'};
static const FixLibrary libraries[] = {{"InterfaceLib", 0, 2}, {"MathLib", 2, 1}};
static const FixImport imports[] = {{"TickCount", 2}, {"qd", 1}, {"sqrt", 0x82}};
static const uint16_t relocs[] = {0x4600, 0x4a00};
static const FixExport exports[] = {{"main", 0, 1, 2}, {"version", 0x1234, MAC_PEF_EXPORT_ABSOLUTE, 1}};

static size_t build(uint8_t *out, size_t cap, uint32_t *loader_at) {
    PefFixture f = {0};
    f.code = code_bytes; f.code_size = sizeof code_bytes;
    f.data = data_bytes; f.data_packed = f.data_unpacked = sizeof data_bytes; f.data_total = 32; f.data_kind = 1;
    f.libraries = libraries; f.library_count = 2;
    f.imports = imports; f.import_count = 3;
    f.reloc = relocs; f.reloc_count = 2; f.reloc_section = 1;
    f.main_section = 1; f.main_offset = 0;
    f.exports = exports; f.export_count = 2;
    return pef_fixture_build(&f, out, cap, loader_at);
}

static void test_valid_container(void) {
    uint8_t image[1024], buffer[64];
    char err[160] = "";
    uint32_t loader_at;
    size_t size = build(image, sizeof image, &loader_at);
    MacPef pef;
    CHECK(size && mac_pef_open(&pef, image, size, err, sizeof err) == 0 && !err[0]);
    CHECK(pef.architecture == MAC_PEF_ARCH_PPC && pef.section_count == 3 && pef.inst_section_count == 2);
    CHECK(pef.current_version == 0x01008000 && pef.loader_index == 2 && pef.loader == image + loader_at);
    CHECK(pef.sections[0].kind == MAC_PEF_CODE && pef.sections[0].total_size == 8 && pef.sections[0].alignment == 2);
    CHECK(pef.sections[1].kind == MAC_PEF_DATA && pef.sections[1].total_size == 32 && pef.sections[1].unpacked_size == 12);
    CHECK(pef.sections[1].name_offset == -1 && pef.sections[2].kind == MAC_PEF_LOADER);
    CHECK(pef.main_section == 1 && pef.main_offset == 0 && pef.init_section == -1 && pef.term_section == -1);
    CHECK(pef.library_count == 2 && pef.import_count == 3 && pef.reloc_section_count == 1 && pef.export_count == 2);
    CHECK(!strcmp(mac_pef_kind_name(2), "pattern-data") && !strcmp(mac_pef_kind_name(99), "unknown"));
    CHECK(mac_pef_instantiated(MAC_PEF_CONSTANT) && !mac_pef_instantiated(MAC_PEF_LOADER));
    MacPefLibrary library;
    CHECK(mac_pef_library(&pef, 0, &library) == 0 && !strcmp(library.name, "InterfaceLib") && library.symbol_count == 2);
    CHECK(mac_pef_library(&pef, 1, &library) == 0 && !strcmp(library.name, "MathLib") && library.first_symbol == 2);
    CHECK(mac_pef_library(&pef, 2, &library) == -1);
    MacPefImport import;
    CHECK(mac_pef_import(&pef, 0, &import) == 0 && !strcmp(import.name, "TickCount") && import.library == 0 &&
          import.symbol_class == MAC_PEF_SYM_TVECTOR && !import.weak);
    CHECK(mac_pef_import(&pef, 1, &import) == 0 && !strcmp(import.name, "qd") && import.symbol_class == MAC_PEF_SYM_DATA);
    CHECK(mac_pef_import(&pef, 2, &import) == 0 && !strcmp(import.name, "sqrt") && import.library == 1 && import.weak &&
          import.symbol_class == MAC_PEF_SYM_TVECTOR);
    CHECK(mac_pef_import(&pef, 3, &import) == -1);
    MacPefRelocations r;
    CHECK(mac_pef_relocations(&pef, 0, &r) == 0 && r.section == 1 && r.count == 2 && r.words[0] == 0x46 &&
          r.words[3] == 0x00 && mac_pef_relocations(&pef, 1, &r) == -1);
    MacPefExport e;
    CHECK(mac_pef_export_find(&pef, "main", &e) == 0 && e.section == 1 && e.value == 0 && e.symbol_class == 2);
    CHECK(mac_pef_export_find(&pef, "version", &e) == 0 && e.section == MAC_PEF_EXPORT_ABSOLUTE && e.value == 0x1234);
    CHECK(mac_pef_export_find(&pef, "mai", &e) == 1 && mac_pef_export_find(&pef, "main2", &e) == 1);
    CHECK(mac_pef_export(&pef, 2, &e) == -1);
    memset(buffer, 0xee, sizeof buffer);
    CHECK(mac_pef_unpack(&pef, 0, buffer, sizeof buffer, err, sizeof err) == 0 && !memcmp(buffer, code_bytes, 8) &&
          buffer[8] == 0xee);
    CHECK(mac_pef_unpack(&pef, 1, buffer, sizeof buffer, err, sizeof err) == 0 && !memcmp(buffer + 8, "data", 4) &&
          buffer[12] == 0 && buffer[31] == 0 && buffer[32] == 0xee); /* zero-filled up to the total size only */
    CHECK(mac_pef_unpack(&pef, 1, buffer, 31, err, sizeof err) == -1);  /* destination too small */
    CHECK(mac_pef_unpack(&pef, 2, buffer, sizeof buffer, err, sizeof err) == -1); /* the loader is not instantiated */
    CHECK(mac_pef_unpack(&pef, 3, buffer, sizeof buffer, err, sizeof err) == -1);
    /* A container without a loader section is valid; loader accessors then fail. */
    image[FIX_SECTION_HEADER(2) + 24] = MAC_PEF_DEBUG;
    CHECK(mac_pef_open(&pef, image, size, err, sizeof err) == 0 && pef.loader_index == -1);
    CHECK(mac_pef_library(&pef, 0, &library) == -1 && mac_pef_export_find(&pef, "main", &e) == -1);
}

static void test_pattern_data(void) {
    uint8_t out[512];
    static const uint8_t program[] = {
        0x03,                               /* zero 3 */
        0x22, 0xaa, 0xbb,                   /* block copy 2 */
        0x42, 0x02, 0x11, 0x22,             /* repeat a 2-byte block 2+1 times */
        0x61, 0x02, 0x02, 0xcc, 0xa1, 0xa2, 0xb1, 0xb2, /* interleave: common 1, custom 2, 2 repeats */
        0x82, 0x01, 0x02, 0xe1, 0xe2,       /* interleave with zero: 2 zeros, custom 1, 2 repeats */
        0x00, 0x81, 0x48,                   /* zero with a two-byte count: 200 */
        0x20, 0x01, 0x7f,                   /* block copy with an argument count of 1 */
    };
    static const uint8_t expected[] = {0, 0, 0, 0xaa, 0xbb, 0x11, 0x22, 0x11, 0x22, 0x11, 0x22,
                                       0xcc, 0xa1, 0xa2, 0xcc, 0xb1, 0xb2, 0xcc, 0, 0, 0xe1, 0, 0, 0xe2, 0, 0};
    memset(out, 0xee, sizeof out);
    long n = mac_pef_pattern_expand(program, sizeof program, out, sizeof out);
    CHECK(n == (long)sizeof expected + 201 && !memcmp(out, expected, sizeof expected));
    int zeros = 1;
    for (int i = 0; i < 200; i++) zeros &= out[sizeof expected + i] == 0;
    CHECK(zeros && out[sizeof expected + 200] == 0x7f && out[sizeof expected + 201] == 0xee);
    CHECK(mac_pef_pattern_expand(program, sizeof program, out, (size_t)n) == n);     /* exact fit */
    CHECK(mac_pef_pattern_expand(program, sizeof program, out, (size_t)n - 1) == -1); /* one byte short */
    CHECK(mac_pef_pattern_expand(program, 0, out, sizeof out) == 0);
    static const uint8_t truncated_copy[] = {0x24, 1, 2, 3};          /* wants four literal bytes */
    static const uint8_t truncated_count[] = {0x00, 0x81};            /* count argument never ends */
    static const uint8_t long_count[] = {0x00, 0x81, 0x81, 0x81, 0x81, 0x81, 0x01};
    static const uint8_t missing_repeat[] = {0x42};
    static const uint8_t missing_custom[] = {0x61, 0x02, 0x02, 0xcc, 0xa1};
    static const uint8_t unknown[] = {0xa1, 0x00};                    /* opcode 5 does not exist */
    static const uint8_t huge_repeat[] = {0x41, 0x8f, 0xff, 0xff, 0xff, 0x7f, 0x55}; /* 1 byte, ~4 billion times */
    static const uint8_t empty_repeat[] = {0x40, 0x00, 0x8f, 0xff, 0xff, 0xff, 0x7f}; /* empty block, huge repeat */
    static const uint8_t empty_interleave[] = {0x80, 0x00, 0x00, 0x8f, 0xff, 0xff, 0xff, 0x7f};
    CHECK(mac_pef_pattern_expand(truncated_copy, sizeof truncated_copy, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(truncated_count, sizeof truncated_count, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(long_count, sizeof long_count, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(missing_repeat, sizeof missing_repeat, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(missing_custom, sizeof missing_custom, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(unknown, sizeof unknown, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(huge_repeat, sizeof huge_repeat, out, sizeof out) == -1);
    CHECK(mac_pef_pattern_expand(empty_repeat, sizeof empty_repeat, out, sizeof out) == 0);     /* returns at once */
    CHECK(mac_pef_pattern_expand(empty_interleave, sizeof empty_interleave, out, sizeof out) == 0);
    /* The same stream as a section: the unpacked size bounds the expansion, the rest of the section is zero. */
    PefFixture f = {0};
    uint8_t image[512], section[300];
    char err[160];
    MacPef pef;
    f.code = code_bytes; f.code_size = 8;
    f.data = program; f.data_packed = sizeof program; f.data_unpacked = (uint32_t)n; f.data_total = (uint32_t)n + 16;
    f.data_kind = MAC_PEF_PATTERN_DATA; f.main_section = -1;
    size_t size = pef_fixture_build(&f, image, sizeof image, NULL);
    memset(section, 0xee, sizeof section);
    CHECK(size && mac_pef_open(&pef, image, size, err, sizeof err) == 0);
    CHECK(mac_pef_unpack(&pef, 1, section, sizeof section, err, sizeof err) == 0 && !memcmp(section, expected, sizeof expected));
    CHECK(section[n - 1] == 0x7f && section[n] == 0 && section[n + 15] == 0 && section[n + 16] == 0xee);
    fix32(image + FIX_SECTION_HEADER(1) + 12, (uint32_t)n - 1); /* claims to unpack to less than the stream produces */
    CHECK(mac_pef_open(&pef, image, size, err, sizeof err) == 0);
    CHECK(mac_pef_unpack(&pef, 1, section, sizeof section, err, sizeof err) == -1 && strstr(err, "pattern"));
}

/* Applies one corruption to a fresh copy and expects the parser to reject it with a reason. */
static void expect_reject(uint32_t offset, int relative_to_loader, uint32_t value, int width, const char *why) {
    uint8_t image[1024];
    char err[160] = "";
    uint32_t loader_at;
    MacPef pef;
    size_t size = build(image, sizeof image, &loader_at);
    uint8_t *p = image + (relative_to_loader ? loader_at : 0) + offset;
    if (width == 4) fix32(p, value); else if (width == 2) fix16(p, value); else *p = (uint8_t)value;
    checks++;
    if (mac_pef_open(&pef, image, size, err, sizeof err) != -1 || !err[0]) {
        failures++;
        fprintf(stderr, "FAIL: corrupt container accepted: %s\n", why);
    }
}

static void test_malformed(void) {
    expect_reject(0, 0, 0x4a6f7922, 4, "first tag");
    expect_reject(4, 0, 0x70656667, 4, "second tag");
    expect_reject(8, 0, 0x78383620, 4, "architecture");
    expect_reject(12, 0, 2, 4, "format version");
    expect_reject(32, 0, 0, 2, "no sections");
    expect_reject(32, 0, 33, 2, "too many sections");
    expect_reject(32, 0, 30, 2, "section headers past the end");
    expect_reject(34, 0, 4, 2, "more instantiated sections than sections");
    expect_reject(FIX_SECTION_HEADER(0) + 20, 0, 0x7fffffff, 4, "code offset past the end");
    expect_reject(FIX_SECTION_HEADER(0) + 16, 0, 0xfffffff0, 4, "code packed size wraps");
    expect_reject(FIX_SECTION_HEADER(1) + 12, 0, 33, 4, "unpacked larger than total");
    expect_reject(FIX_SECTION_HEADER(1) + 8, 0, 0x7fffffff, 4, "section too large");
    expect_reject(FIX_SECTION_HEADER(0) + 12, 0, 4, 4, "raw section stores more than it unpacks");
    expect_reject(FIX_SECTION_HEADER(1) + 24, 0, 9, 1, "unknown section kind");
    expect_reject(FIX_SECTION_HEADER(1) + 24, 0, 4, 1, "two loader sections");
    expect_reject(FIX_SECTION_HEADER(1) + 26, 0, 40, 1, "absurd alignment");
    expect_reject(FIX_SECTION_HEADER(2) + 16, 0, 40, 4, "loader shorter than its header");
    expect_reject(0, 1, 7, 4, "main section does not exist");
    expect_reject(8, 1, 3, 4, "init section does not exist");
    expect_reject(24, 1, 5000, 4, "library count");
    expect_reject(24, 1, 20, 4, "library table past the loader");
    expect_reject(28, 1, 0x00ffffff, 4, "import count");
    expect_reject(32, 1, 4, 4, "more relocated sections than sections");
    expect_reject(36, 1, 0x10000, 4, "relocation instructions offset");
    expect_reject(40, 1, 0x10000, 4, "string table offset");
    expect_reject(56, 1, 0x4000, 4, "library name outside the string table");
    expect_reject(56 + 12, 1, 4, 4, "library claims too many symbols");
    expect_reject(56 + 16, 1, 0xffffffff, 4, "library first symbol wraps");
    expect_reject(56 + 48, 1, 0x02004000, 4, "import name outside the string table");
    expect_reject(56 + 48 + 12, 1, 9, 2, "relocation section does not exist");
    expect_reject(56 + 48 + 12 + 4, 1, 0x7fffffff, 4, "relocation count past the loader");
    expect_reject(56 + 48 + 12 + 8, 1, 0xfffffffe, 4, "relocation offset past the loader");
    expect_reject(44, 1, 0x10000, 4, "export hash offset");
    expect_reject(48, 1, 30, 4, "export hash power");
    expect_reject(52, 1, 400, 4, "export count past the loader");
    /* The last string must be terminated inside the loader section. */
    uint8_t image[1024];
    char err[160];
    uint32_t loader_at;
    MacPef pef;
    PefFixture f = {0};
    f.code = code_bytes; f.code_size = 8; f.data_kind = 1; f.main_section = -1;
    f.libraries = libraries; f.library_count = 1; f.imports = imports; f.import_count = 2;
    size_t size = pef_fixture_build(&f, image, sizeof image, &loader_at);
    CHECK(size && mac_pef_open(&pef, image, size, err, sizeof err) == 0);
    uint32_t strings_at = fix_get32(image + loader_at + 40);
    uint32_t tight = loader_at + strings_at + (uint32_t)strlen("InterfaceLib") + 1 + (uint32_t)strlen("TickCount") + 1 + 2;
    fix32(image + FIX_SECTION_HEADER(2) + 16, tight - loader_at); /* loader now ends right after "qd", no NUL */
    CHECK(mac_pef_open(&pef, image, tight, err, sizeof err) == -1 && strstr(err, "symbol name"));
    /* Export name running past the loader. */
    size = build(image, sizeof image, &loader_at);
    uint32_t keys = fix_get32(image + loader_at + 44) + 4;
    fix32(image + loader_at + keys, 0xfff0u << 16);
    CHECK(mac_pef_open(&pef, image, size, err, sizeof err) == -1 && strstr(err, "exported symbol"));
    size = build(image, sizeof image, &loader_at);
    fix16(image + loader_at + keys + 8 + 8, 0x0040); /* export section 64 */
    CHECK(mac_pef_open(&pef, image, size, err, sizeof err) == -1);
    CHECK(mac_pef_open(&pef, NULL, 100, err, sizeof err) == -1 && mac_pef_open(NULL, image, size, err, sizeof err) == -1);
}

/* Every truncation and every single-byte corruption must be handled without touching memory outside the
 * buffer. Each trial uses an exact-size heap copy so a sanitizer or guard allocator sees any overrun. */
static void test_truncation_and_corruption_sweep(void) {
    uint8_t image[1024], scratch[4096];
    uint32_t loader_at;
    size_t size = build(image, sizeof image, &loader_at);
    int accepted_short = 0, survived = 0;
    for (size_t length = 0; length < size; length++) {
        uint8_t *copy = malloc(length ? length : 1);
        MacPef pef;
        memcpy(copy, image, length);
        if (mac_pef_open(&pef, copy, length, NULL, 0) == 0) accepted_short++;
        free(copy);
    }
    CHECK(accepted_short == 0); /* the loader section is last, so every proper prefix is incomplete */
    static const uint8_t patterns[] = {0xff, 0x00, 0x80, 0x01};
    for (size_t at = 0; at < size; at++) {
        for (size_t k = 0; k < sizeof patterns; k++) {
            uint8_t *copy = malloc(size);
            MacPef pef;
            memcpy(copy, image, size);
            copy[at] = patterns[k];
            if (mac_pef_open(&pef, copy, size, NULL, 0) == 0) {
                MacPefLibrary library; MacPefImport import; MacPefRelocations r; MacPefExport e;
                for (uint32_t i = 0; i < pef.library_count; i++) (void)mac_pef_library(&pef, i, &library);
                for (uint32_t i = 0; i < pef.import_count; i++)
                    if (!mac_pef_import(&pef, i, &import)) survived += (int)strlen(import.name) >= 0;
                for (uint32_t i = 0; i < pef.reloc_section_count; i++)
                    if (!mac_pef_relocations(&pef, i, &r) && r.count) survived += r.words[2 * r.count - 1] >= 0;
                (void)mac_pef_export_find(&pef, "no such symbol", &e);
                for (unsigned i = 0; i < pef.section_count; i++)
                    if (pef.sections[i].total_size <= sizeof scratch) (void)mac_pef_unpack(&pef, i, scratch, sizeof scratch, NULL, 0);
            }
            free(copy);
        }
    }
    CHECK(survived > 0);
}

static void test_cfrg(void) {
    uint8_t r[160];
    MacCfrgMember m;
    memset(r, 0, sizeof r);
    fix16(r + 30, 2);
    uint8_t *a = r + 32; /* member 0: a 68K library, 48 bytes */
    memcpy(a, "m68k", 4); a[22] = MAC_CFRG_LIBRARY; a[23] = MAC_CFRG_IN_RESOURCE; fix16(a + 40, 48); a[42] = 3; memcpy(a + 43, "Old", 3);
    uint8_t *b = a + 48; /* member 1: the PowerPC application in the data fork */
    memcpy(b, "pwpc", 4); b[22] = MAC_CFRG_APPLICATION; b[23] = MAC_CFRG_IN_DATA_FORK; fix32(b + 24, 0x200); fix32(b + 28, 0x4000);
    fix16(b + 40, 52); b[42] = 7; memcpy(b + 43, "Example", 7);
    size_t size = 32 + 48 + 52;
    CHECK(mac_cfrg_member(r, size, 0, &m) == 0 && m.architecture == MAC_PEF_ARCH_68K && !strcmp(m.name, "Old"));
    CHECK(mac_cfrg_member(r, size, 1, &m) == 0 && m.architecture == MAC_PEF_ARCH_PPC && m.usage == MAC_CFRG_APPLICATION &&
          m.where == MAC_CFRG_IN_DATA_FORK && m.offset == 0x200 && m.length == 0x4000 && !strcmp(m.name, "Example"));
    CHECK(mac_cfrg_member(r, size, 2, &m) == 1);
    /* Real resources overstate the member size (DesignWorkshop Lite 1.8.5: 68 declared, 56 present):
     * accepted while the fixed part and the name are there, rejected once the name is cut. */
    CHECK(mac_cfrg_member(r, size - 1, 1, &m) == 0 && !strcmp(m.name, "Example"));
    CHECK(mac_cfrg_member(r, 32 + 48 + 43 + 6, 1, &m) == -1 && mac_cfrg_member(r, 31, 0, &m) == -1);
    b[42] = 200; /* name longer than the member */
    CHECK(mac_cfrg_member(r, size, 1, &m) == -1);
    b[42] = 7; fix16(a + 40, 10); /* member size smaller than the fixed part */
    CHECK(mac_cfrg_member(r, size, 0, &m) == -1);
    fix16(a + 40, 48); fix16(r + 30, 9); /* claims more members than exist */
    CHECK(mac_cfrg_member(r, size, 5, &m) == -1);
}

int main(void) {
    test_valid_container();
    test_pattern_data();
    test_malformed();
    test_truncation_and_corruption_sweep();
    test_cfrg();
    if (failures) { fprintf(stderr, "test_mac_pef: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_pef: all %d checks passed\n", checks);
    return 0;
}
