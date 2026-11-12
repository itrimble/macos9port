/* A custom menu definition procedure called from PowerPC code. A hand-assembled PowerPC fragment in a hand-built PEF container calls GetMenu,
 * CalcMenuSize, InsertMenu and PopUpMenuSelect on menus whose MENU resources name a synthetic 68K MDEF (tests/mdef_fixture.h). Each call makes the
 * 68K side run the definition's code (a mixed-mode round trip from inside a Toolbox service) and, for the pop-up, track a mouse-up that was queued
 * before the run. Nothing here comes from a real program. */
#include "mac_ppc.h"
#include "mac_mdef.h"
#include "pef_fixture.h"
#include "ppc_asm.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#include "fork_builder.h"
#include "mdef_fixture.h"

enum { I_INITMENUS, I_GETMENU, I_CALC, I_INSERT, I_POPUP, I_EXIT, IMPORTS };
enum { TV_MAIN = 0, SLOTS = 8, R = 0x80, R_MENU = R, R_POP = R + 4, R_AFTER = R + 8, DATA_BYTES = R + 16 };
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
    import(I_INITMENUS);
    op(ppc_li(3, 140)); import(I_GETMENU); store(R_MENU);                                /* the size message, inside GetMenu */
    op(ppc_lwz(3, R_MENU, 2)); import(I_CALC);                                           /* and again */
    op(ppc_lwz(3, R_MENU, 2)); op(ppc_li(4, -1)); import(I_INSERT);                      /* InsertMenu(menu, -1): a pop-up, not in the bar */
    op(ppc_lwz(3, R_MENU, 2)); op(ppc_li(4, 100)); op(ppc_li(5, 200)); op(ppc_li(6, 2));
    import(I_POPUP); store(R_POP);                                                       /* size, pop-up, draw, choose */
    op(ppc_li(3, 77)); store(R_AFTER);
    import(I_EXIT);
    epilogue();
}
static const FixLibrary libraries[] = {{"InterfaceLib", 0, IMPORTS}};
static const FixImport imports[IMPORTS] = {{"InitMenus", 2}, {"GetMenu", 2}, {"CalcMenuSize", 2}, {"InsertMenu", 2}, {"PopUpMenuSelect", 2}, {"ExitToShell", 2}};
static const uint16_t relocations[] = {0x4600, 0x4a00 | (IMPORTS - 1)};

static uint8_t image[4096], fork_bytes[FB_BYTES];
static FbFork fork_def;
static size_t fork_len;

static size_t build_image(void) {
    static uint8_t bytes[sizeof code], data[DATA_BYTES];
    PefFixture f = {0};
    assemble();
    for (unsigned i = 0; i < count; i++) fix32(bytes + 4 * i, code[i]);
    memset(data, 0, sizeof data);
    f.code = bytes; f.code_size = 4 * count;
    f.data = data; f.data_packed = f.data_unpacked = f.data_total = DATA_BYTES; f.data_kind = 1;
    f.libraries = libraries; f.library_count = 1; f.imports = imports; f.import_count = IMPORTS;
    f.reloc = relocations; f.reloc_count = 2; f.reloc_section = 1; f.main_section = 1; f.main_offset = TV_MAIN;
    return pef_fixture_build(&f, image, sizeof image, NULL);
}
/* A fork with no CODE (what a PowerPC-only program has): the MDEF and one MENU that names it. */
static void build_fork(void) {
    uint8_t mdef[512], res[256];
    static const char *const items[] = {"Alpha", "Beta", "Gamma", 0};
    size_t n = mdef_image(mdef);
    CHECK(n > 0);
    fb_add(&fork_def, 0x4d444546, 128, 0, mdef, n);
    n = mdef_menu_res(res, 140, 128, "Custom", items, 0);
    fb_add(&fork_def, 0x4d454e55, 140, 0, res, n);
    fork_len = fb_build(&fork_def, fork_bytes);
}
static uint32_t peek(MacRuntime *rt, uint32_t address, unsigned width) {
    const uint8_t *p = mac_cpu_ram(rt->cpu) + address;
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | p[i];
    return v;
}
static uint32_t log_at;
static uint32_t lg(MacRuntime *rt, int message, unsigned off, unsigned n) { return peek(rt, MDEF_SLOT(log_at, message) + off, n); }
static int16_t lgs(MacRuntime *rt, int message, unsigned off) { return (int16_t)lg(rt, message, off, 2); }

static void test_menu_calls(size_t image_size) {
    MacRuntime rt;
    MacResourceFork fork;
    char err[240];
    CHECK(!mac_resource_open(&fork, fork_bytes, fork_len, err, sizeof err));
    CHECK(!mac_ppc_runtime_init(&rt, &fork, image, image_size, err, sizeof err));
    if (!rt.cpu) return;
    MacPpcState *st = rt.ppc;
    uint32_t d = st->fragment.section_address[1];
    CHECK(mac_color_init(&rt) == 1 && rt.screen_depth == 8);                             /* the 8-bit screen menu tracking draws on */
    log_at = mac_runtime_alloc(&rt, 160);
    uint32_t marker = mac_runtime_alloc(&rt, 8);
    CHECK(log_at && marker);
    for (unsigned i = 0; i < 160; i += 4) mac_cpu_write(rt.cpu, log_at + i, 4, 0);
    mac_cpu_write(rt.cpu, marker, 4, 0);
    mac_cpu_write(rt.cpu, 0xa78, 4, log_at);
    mac_cpu_write(rt.cpu, 0xa7c, 4, marker);
    mac_runtime_post_event(&rt, 2, 0, 0x80, 220, 120);                                   /* the button is already up, over the third item */
    uint32_t a7 = mac_cpu_get(rt.cpu, MAC_A7);
    unsigned slices = 0;
    while (slices < 2000 && !mac_cpu_halted(rt.cpu)) {
        rt.ticks++;
        slices++;
        if (!mac_runtime_run(&rt, 10000)) break;
    }
    if (!rt.exited) fprintf(stderr, "stopped: %s | %s\n", mac_cpu_error(rt.cpu), rt.ppc->cpu.error);
    CHECK(rt.exited && strstr(mac_cpu_error(rt.cpu), "ExitToShell"));
    uint32_t menu = peek(&rt, d + R_MENU, 4);
    CHECK(menu && !(menu & 1));
    CHECK(lg(&rt, MDEF_SIZE, 0, 2) == 3 && lg(&rt, MDEF_SIZE, 14, 4) == menu);           /* GetMenu, CalcMenuSize, then the pop-up's own */
    CHECK(lg(&rt, MDEF_POPUP, 0, 2) == 1 && lgs(&rt, MDEF_POPUP, 2) == 100 && lgs(&rt, MDEF_POPUP, 4) == 200 && lgs(&rt, MDEF_POPUP, 18) == 2);
    CHECK(lg(&rt, MDEF_DRAW, 0, 2) == 1 && lgs(&rt, MDEF_DRAW, 6) == 84 && lgs(&rt, MDEF_DRAW, 8) == 200 &&
          lgs(&rt, MDEF_DRAW, 10) == 132 && lgs(&rt, MDEF_DRAW, 12) == 280 && peek(&rt, marker, 1) == 0x55);
    CHECK(lg(&rt, MDEF_CHOOSE, 0, 2) == 1 && lgs(&rt, MDEF_CHOOSE, 2) == 120 && lgs(&rt, MDEF_CHOOSE, 4) == 220 && lgs(&rt, MDEF_CHOOSE, 18) == 2);
    CHECK(peek(&rt, d + R_POP, 4) == ((140u << 16) | 3));                                /* the item the 68K procedure answered */
    CHECK(peek(&rt, d + R_AFTER, 4) == 77);                                              /* PowerPC code carried on after the round trips */
    uint32_t record = peek(&rt, menu, 4);
    CHECK(peek(&rt, record + 2, 2) == MDEF_W && peek(&rt, record + 4, 2) == MDEF_H && peek(&rt, record + 6, 4) != 0);
    CHECK(st->depth == 0 && mac_cpu_get(rt.cpu, MAC_A7) == a7 && slices > 1);
    mac_runtime_destroy(&rt);
}

int main(void) {
    size_t image_size = build_image();
    CHECK(image_size != 0);
    build_fork();
    test_menu_calls(image_size);
    if (failures) { fprintf(stderr, "test_mac_ppc_mdef: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_ppc_mdef: all %d checks passed\n", checks);
    return 0;
}
