/* Library glue called from PowerPC code: EqualString, TESetClickLoop (through a TextEdit record the host makes) and MathLib's num2dec. A hand-assembled
 * fragment makes the calls; the test reads what the guest stored. */
#include "mac_ppc.h"
#include "pef_fixture.h"
#include "ppc_asm.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { I_EQUAL, I_EXIT, I_PROPEN, I_PRERROR, I_PRCLOSE, I_NUM2DEC, I_DEC2NUM, IMPORTS };
enum { TV_MAIN = 0, SLOTS = 8, R = 0x80, R_EQ1 = R, R_EQ2 = R + 4, R_S1 = R + 8, R_S2 = R + 16, R_F1 = R + 24, R_F2 = R + 28, R_D1 = R + 32, R_D2 = R + 40,
       R_D3 = R + 48, R_O1 = R + 56, R_O2 = R + 96, R_O3 = R + 136, R_PE1 = R + 176, R_PE2 = R + 180, R_BACK = R + 184, DATA_BYTES = R + 200 };
static uint32_t code[200];
static unsigned count;
static void op(uint32_t w) { code[count++] = w; }
static void prologue(void) { op(ppc_mfspr(0, SPR_LR)); op(ppc_stw(0, 8, 1)); op(ppc_stwu(1, -64, 1)); }
static void epilogue(void) { op(ppc_addi(1, 1, 64)); op(ppc_lwz(0, 8, 1)); op(ppc_mtspr(SPR_LR, 0)); op(PPC_BLR); }
static void import(unsigned index) {
    op(ppc_lwz(12, SLOTS + 4 * (int32_t)index, 2)); op(ppc_stw(2, 20, 1)); op(ppc_lwz(0, 0, 12)); op(ppc_lwz(2, 4, 12));
    op(ppc_mtspr(SPR_CTR, 0)); op(ppc_bcctr(BO_ALWAYS, 0, 1)); op(ppc_lwz(2, 20, 1));
}
static void store(int32_t slot) { op(ppc_stw(3, slot, 2)); }
static void num2dec(int32_t form, int32_t value, int32_t out) {
    op(ppc_addi(3, 2, form)); op(ppc_addi(4, 2, out)); op(ppc_d(50, 1, 2, value)); import(I_NUM2DEC);          /* lfd f1,value(r2) */
}
static void assemble(void) {
    count = 0;
    prologue();
    op(ppc_addi(3, 2, R_S1)); op(ppc_addi(4, 2, R_S2)); op(ppc_li(5, 0)); op(ppc_li(6, 0)); import(I_EQUAL); store(R_EQ1);       /* not case sensitive */
    op(ppc_addi(3, 2, R_S1)); op(ppc_addi(4, 2, R_S2)); op(ppc_li(5, 1)); op(ppc_li(6, 0)); import(I_EQUAL); store(R_EQ2);       /* case sensitive */
    num2dec(R_F1, R_D1, R_O1); num2dec(R_F2, R_D2, R_O2); num2dec(R_F1, R_D3, R_O3);
    op(ppc_addi(3, 2, R_O1)); import(I_DEC2NUM); op(ppc_d(54, 1, 2, R_BACK));                           /* stfd f1,R_BACK(r2) */
    import(I_PROPEN); import(I_PRERROR); store(R_PE1);                                              /* no printer: PrOpen reports it through PrError */
    import(I_PRCLOSE); import(I_PRERROR); store(R_PE2);
    import(I_EXIT);
    epilogue();
}
static const FixLibrary libraries[] = {{"InterfaceLib", 0, 5}, {"MathLib", 5, 2}};
static const FixImport imports[IMPORTS] = {{"EqualString", 2}, {"ExitToShell", 2}, {"PrOpen", 2}, {"PrError", 2}, {"PrClose", 2}, {"num2dec", 2}, {"dec2num", 2}};
static const uint16_t relocations[] = {0x4600, 0x4a00 | (IMPORTS - 1)};
static uint8_t image[4096], fork_bytes[512];

static size_t build_image(void) {
    static uint8_t bytes[sizeof code], data[DATA_BYTES];
    PefFixture f = {0};
    assemble();
    for (unsigned i = 0; i < count; i++) fix32(bytes + 4 * i, code[i]);
    memset(data, 0, sizeof data);
    data[R_S1] = 3; memcpy(data + R_S1 + 1, "Abc", 3); data[R_S2] = 3; memcpy(data + R_S2 + 1, "aBC", 3);
    fix16(data + R_F1, 0); fix16(data + R_F1 + 2, 5);                                  /* FLOATDECIMAL, 5 digits */
    fix16(data + R_F2, 1); fix16(data + R_F2 + 2, 2);                                  /* FIXEDDECIMAL, 2 places */
    fix32(data + R_D1, 0x405edd2f); fix32(data + R_D1 + 4, 0x1a9fbe77);                /* 123.456 */
    fix32(data + R_D2, 0xc00921f9); fix32(data + R_D2 + 4, 0xf01b866e);                /* -3.14159 */
    f.code = bytes; f.code_size = 4 * count;
    f.data = data; f.data_packed = f.data_unpacked = f.data_total = DATA_BYTES; f.data_kind = 1;
    f.libraries = libraries; f.library_count = 2; f.imports = imports; f.import_count = IMPORTS;
    f.reloc = relocations; f.reloc_count = 2; f.reloc_section = 1; f.main_section = 1; f.main_offset = TV_MAIN;
    return pef_fixture_build(&f, image, sizeof image, NULL);
}
static size_t build_fork(void) {
    enum { DATA = 256, MAP = 384 };
    uint8_t *b = fork_bytes;
    memset(b, 0, sizeof fork_bytes);
    fix32(b, DATA); fix32(b + 4, MAP); fix32(b + 8, 8); fix32(b + 12, 50);
    fix32(b + DATA, 4); memcpy(b + DATA + 4, "data", 4);
    fix16(b + MAP + 24, 28); fix16(b + MAP + 26, 50);
    memcpy(b + MAP + 30, "TEST", 4); fix16(b + MAP + 36, 10);
    fix16(b + MAP + 38, 128); fix16(b + MAP + 40, 0xffff);
    return MAP + 50;
}
static uint32_t peek(MacRuntime *rt, uint32_t address, unsigned width) {
    const uint8_t *p = mac_cpu_ram(rt->cpu) + address;
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | p[i];
    return v;
}
static int sig_is(MacRuntime *rt, uint32_t at, int sgn, int exp, const char *digits) {   /* decimal record: sgn byte, exp short at 2, Pascal string at 4 */
    size_t n = strlen(digits);
    return (int)peek(rt, at, 1) == sgn && (int16_t)peek(rt, at + 2, 2) == exp && peek(rt, at + 4, 1) == n && !memcmp(mac_cpu_ram(rt->cpu) + at + 5, digits, n);
}

int main(void) {
    size_t image_size = build_image();
    MacRuntime rt;
    MacResourceFork fork;
    char err[240];
    CHECK(image_size != 0);
    CHECK(!mac_resource_open(&fork, fork_bytes, build_fork(), err, sizeof err));
    CHECK(!mac_ppc_runtime_init(&rt, &fork, image, image_size, err, sizeof err));
    if (!rt.cpu) return 1;
    uint32_t d = rt.ppc->fragment.section_address[1];
    unsigned slices = 0;
    while (slices < 2000 && !mac_cpu_halted(rt.cpu)) {
        rt.ticks++;
        slices++;
        if (!mac_runtime_run(&rt, 10000)) break;
    }
    if (!rt.exited) fprintf(stderr, "stopped: %s\n", mac_cpu_error(rt.cpu));
    CHECK(rt.exited && strstr(mac_cpu_error(rt.cpu), "ExitToShell"));
    CHECK(peek(&rt, d + R_EQ1, 4) == 1 && peek(&rt, d + R_EQ2, 4) == 0);
    CHECK(sig_is(&rt, d + R_O1, 0, -2, "12346"));            /* 123.456 to 5 significant digits */
    CHECK(sig_is(&rt, d + R_O2, 1, -2, "314"));              /* -3.14159 to 2 places, negative */
    CHECK(sig_is(&rt, d + R_O3, 0, 0, "0"));                 /* zero */
    CHECK(peek(&rt, d + R_BACK, 4) == 0x405edd70u && peek(&rt, d + R_BACK + 4, 4) == 0xa3d70a3du);   /* dec2num of 12346e-2 is exactly the double 123.46 */
    CHECK((int32_t)peek(&rt, d + R_PE1, 4) == -4101);           /* PrOpen with no printer: PrError says so */
    CHECK(peek(&rt, d + R_PE2, 4) == 0);               /* PrClose clears it */
    mac_runtime_destroy(&rt);
    if (failures) { fprintf(stderr, "test_mac_ppc_libs: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_ppc_libs: all %d checks passed\n", checks);
    return 0;
}
