/* Offscreen graphics worlds called from PowerPC code: a hand-assembled fragment calls NewGWorld, GetGWorldPixMap, GetPixBaseAddr, GetGWorld,
 * SetGWorld and DisposeGWorld through the import bridge, writes a byte into the pixels it was given, and the test then checks what the guest left. */
#include "mac_ppc.h"
#include "pef_fixture.h"
#include "ppc_asm.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { I_NEW, I_PIXMAP, I_BASE, I_GET, I_SET, I_DISPOSE, I_EXIT, IMPORTS };
enum { TV_MAIN = 0, SLOTS = 8, R = 0x80, R_ERR = R, R_GW = R + 4, R_PM = R + 8, R_BASE = R + 12, R_P1 = R + 16, R_D1 = R + 20, R_P2 = R + 24, R_D2 = R + 28,
       R_RECT = R + 32, R_ROW = R + 40, R_DEPTH = R + 44, R_PIX = R + 48, R_PMH2 = R + 52, DATA_BYTES = R + 64 };
static uint32_t code[100];
static unsigned count;
static void op(uint32_t w) { code[count++] = w; }
static void prologue(void) { op(ppc_mfspr(0, SPR_LR)); op(ppc_stw(0, 8, 1)); op(ppc_stwu(1, -64, 1)); }
static void epilogue(void) { op(ppc_addi(1, 1, 64)); op(ppc_lwz(0, 8, 1)); op(ppc_mtspr(SPR_LR, 0)); op(PPC_BLR); }
static void import(unsigned index) {
    op(ppc_lwz(12, SLOTS + 4 * (int32_t)index, 2)); op(ppc_stw(2, 20, 1)); op(ppc_lwz(0, 0, 12)); op(ppc_lwz(2, 4, 12));
    op(ppc_mtspr(SPR_CTR, 0)); op(ppc_bcctr(BO_ALWAYS, 0, 1)); op(ppc_lwz(2, 20, 1));
}
static void store(int32_t slot) { op(ppc_stw(3, slot, 2)); }
static void assemble(void) {
    count = 0;
    prologue();
    op(ppc_addi(3, 2, R_GW)); op(ppc_li(4, 8)); op(ppc_addi(5, 2, R_RECT)); op(ppc_li(6, 0)); op(ppc_li(7, 0)); op(ppc_li(8, 0));
    import(I_NEW); store(R_ERR);
    op(ppc_lwz(3, R_GW, 2)); import(I_PIXMAP); store(R_PM);
    op(ppc_lwz(3, R_PM, 2)); import(I_BASE); store(R_BASE);
    op(ppc_lwz(9, R_BASE, 2)); op(ppc_li(10, 0x55)); op(ppc_d(38, 10, 9, 0));            /* stb r10,0(r9): a pixel */
    op(ppc_addi(3, 2, R_P1)); op(ppc_addi(4, 2, R_D1)); import(I_GET);                  /* the port we started with */
    op(ppc_lwz(3, R_GW, 2)); op(ppc_li(4, 0)); import(I_SET);
    op(ppc_addi(3, 2, R_P2)); op(ppc_addi(4, 2, R_D2)); import(I_GET);                  /* now the GWorld */
    op(ppc_lwz(3, R_P1, 2)); op(ppc_lwz(4, R_D1, 2)); import(I_SET);
    op(ppc_lwz(9, R_PM, 2)); op(ppc_lwz(9, 0, 9));                                       /* the PixMap record, read before the world goes away */
    op(ppc_d(40, 10, 9, 4)); op(ppc_stw(10, R_ROW, 2)); op(ppc_d(40, 10, 9, 32)); op(ppc_stw(10, R_DEPTH, 2));
    op(ppc_lwz(11, 0, 9)); op(ppc_d(34, 12, 11, 0)); op(ppc_stw(12, R_PIX, 2));
    op(ppc_lwz(9, R_GW, 2)); op(ppc_lwz(10, 2, 9)); op(ppc_stw(10, R_PMH2, 2));          /* portPixMap */
    op(ppc_lwz(3, R_GW, 2)); import(I_DISPOSE);
    import(I_EXIT);
    epilogue();
}
static const FixLibrary libraries[] = {{"InterfaceLib", 0, IMPORTS}};
static const FixImport imports[IMPORTS] = {{"NewGWorld", 2}, {"GetGWorldPixMap", 2}, {"GetPixBaseAddr", 2}, {"GetGWorld", 2}, {"SetGWorld", 2}, {"DisposeGWorld", 2}, {"ExitToShell", 2}};
static const uint16_t relocations[] = {0x4600, 0x4a00 | (IMPORTS - 1)};
static uint8_t image[4096], fork_bytes[512];

static size_t build_image(void) {
    static uint8_t bytes[sizeof code], data[DATA_BYTES];
    PefFixture f = {0};
    assemble();
    for (unsigned i = 0; i < count; i++) fix32(bytes + 4 * i, code[i]);
    memset(data, 0, sizeof data);
    fix16(data + R_RECT + 4, 20); fix16(data + R_RECT + 6, 30);                        /* a 20 x 30 rectangle at the origin */
    f.code = bytes; f.code_size = 4 * count;
    f.data = data; f.data_packed = f.data_unpacked = f.data_total = DATA_BYTES; f.data_kind = 1;
    f.libraries = libraries; f.library_count = 1; f.imports = imports; f.import_count = IMPORTS;
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
    /* the host does what an application's start-up does: QuickDraw, colour QuickDraw, a first port */
    uint32_t globals = mac_runtime_alloc(&rt, 256), port = mac_runtime_alloc(&rt, 108), args[1] = {globals + 202};
    CHECK(mac_ppc_invoke(&rt, "InitGraf", 0xa86e, "-:l", args, 1, NULL) == 0);
    args[0] = port;
    CHECK(mac_ppc_invoke(&rt, "OpenPort", 0xa86f, "-:l", args, 1, NULL) == 0);
    CHECK(mac_color_init(&rt) == 1 && rt.screen_depth == 8);
    uint32_t a7 = mac_cpu_get(rt.cpu, MAC_A7);
    unsigned slices = 0;
    while (slices < 2000 && !mac_cpu_halted(rt.cpu)) {
        rt.ticks++;
        slices++;
        if (!mac_runtime_run(&rt, 10000)) break;
    }
    if (!rt.exited) fprintf(stderr, "stopped: %s\n", mac_cpu_error(rt.cpu));
    CHECK(rt.exited && strstr(mac_cpu_error(rt.cpu), "ExitToShell"));
    uint32_t gw = peek(&rt, d + R_GW, 4), pm = peek(&rt, d + R_PM, 4), base = peek(&rt, d + R_BASE, 4);
    CHECK(peek(&rt, d + R_ERR, 4) == 0 && gw && pm == peek(&rt, d + R_PMH2, 4));              /* noErr, a port, its PixMap handle */
    CHECK(base >= 0x100000 && peek(&rt, d + R_PIX, 4) == 0x55);   /* (the block is disposed by now, so only the copy taken before it is meaningful) */
    CHECK(peek(&rt, d + R_P1, 4) == port && peek(&rt, d + R_P2, 4) == gw);               /* GetGWorld saw the port change */
    CHECK(peek(&rt, d + R_D1, 4) == rt.main_device && peek(&rt, d + R_D2, 4) == rt.main_device);
    CHECK(peek(&rt, d + R_ROW, 4) == (0x8000u | 32) && peek(&rt, d + R_DEPTH, 4) == 8);   /* 30 pixels at 8 bits, rows of 32 bytes */
    CHECK(peek(&rt, globals + 202, 4) == port && mac_cpu_get(rt.cpu, MAC_A7) == a7);        /* thePort is back where it began */
    CHECK(rt.ppc->depth == 0);
    mac_runtime_destroy(&rt);
    if (failures) { fprintf(stderr, "test_mac_ppc_gworld: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_ppc_gworld: all %d checks passed\n", checks);
    return 0;
}
