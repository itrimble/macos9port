/* The PowerPC import bridge against a real MacRuntime. A hand-assembled PowerPC fragment in a hand-built PEF container
 * calls bridged imports of every calling style and the test checks what the guest can see afterwards: register-based
 * OS traps, a Pascal stack-based trap with value and pointer parameters, results returned through pointers, a Point
 * passed by value, routine descriptors called from PowerPC code, from 68K code and from the VBL poller, a trap that
 * blocks, and ExitToShell. Nothing here comes from a real program. */
#include "mac_ppc.h"
#include "pef_fixture.h"
#include "ppc_asm.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { I_NEWPTR, I_DISPOSEPTR, I_NEWHANDLE, I_GETHANDLESIZE, I_SETRECT, I_PTINRECT, I_GESTALT, I_ADDPT, I_MEMERROR,
       I_NEWRD, I_CALLUPP, I_VINSTALL, I_DELAY, I_EXIT, I_WEAK, IMPORTS };
/* Data section: four transition vectors, the import slots, then results. r2 points at its start. */
enum { TV_MAIN = 0, TV_MUL = 8, TV_VBL = 16, TV_TWICE = 24, SLOTS = 32, R = 0x80,
       R_PTR = R, R_HANDLE = R + 4, R_HSIZE = R + 8, R_RECT = R + 12, R_INSIDE = R + 20, R_OUTSIDE = R + 24,
       R_GESTALT = R + 28, R_GERR = R + 32, R_PT = R + 36, R_MEMERR = R + 40, R_UPP = R + 44, R_MUL = R + 48,
       R_VBLTASK = R + 52, R_VBLCOUNT = R + 68, R_VBLARG = R + 72, R_68K = R + 76, R_MIXED = R + 80, R_CBX = R + 84,
       R_CBPTR = R + 88, R_FINAL = R + 92, R_WEAK = R + 96, R_VERR = R + 100, R_VBLUPP = R + 104, DATA_BYTES = R + 112 };
enum { INFO_MUL = 0x3f0, INFO_TWICE = 0xf0, INFO_VBL = 0x9802 };

static uint32_t code[400];
static unsigned count;
static uint32_t here(void) { return 4 * count; }
static void op(uint32_t w) { code[count++] = w; }
static void li32(unsigned r, uint32_t v) { op(ppc_lis(r, (int32_t)(v >> 16))); op(ppc_ori(r, r, v & 0xffffu)); }
static void prologue(void) { op(ppc_mfspr(0, SPR_LR)); op(ppc_stw(0, 8, 1)); op(ppc_stwu(1, -64, 1)); }
static void epilogue(void) { op(ppc_addi(1, 1, 64)); op(ppc_lwz(0, 8, 1)); op(ppc_mtspr(SPR_LR, 0)); op(PPC_BLR); }
/* A cross-fragment call as a compiler emits it, with the glue inline. */
static void import(unsigned index) {
    op(ppc_lwz(12, SLOTS + 4 * (int32_t)index, 2)); op(ppc_stw(2, 20, 1)); op(ppc_lwz(0, 0, 12)); op(ppc_lwz(2, 4, 12));
    op(ppc_mtspr(SPR_CTR, 0)); op(ppc_bcctr(BO_ALWAYS, 0, 1)); op(ppc_lwz(2, 20, 1));
}
static void store(int32_t slot) { op(ppc_stw(3, slot, 2)); }

static uint32_t mul_at, vbl_at, twice_at;

static void assemble(void) {
    count = 0;
    prologue();
    op(ppc_li(3, 100)); import(I_NEWPTR); store(R_PTR);                         /* register-based OS traps */
    op(ppc_li(3, 40)); import(I_NEWHANDLE); store(R_HANDLE);
    import(I_GETHANDLESIZE); store(R_HSIZE);
    op(ppc_addi(3, 2, R_RECT)); op(ppc_li(4, 10)); op(ppc_li(5, 20)); op(ppc_li(6, 30)); op(ppc_li(7, 40));
    import(I_SETRECT);                                                           /* Pascal stack: pointer and values */
    li32(3, 0x0019000f); op(ppc_addi(4, 2, R_RECT)); import(I_PTINRECT); store(R_INSIDE);   /* Point (v 25, h 15) by value */
    li32(3, 0x0005000f); op(ppc_addi(4, 2, R_RECT)); import(I_PTINRECT); store(R_OUTSIDE);
    li32(3, 0x73797376); op(ppc_addi(4, 2, R_GESTALT)); import(I_GESTALT); store(R_GERR);   /* result through a pointer */
    li32(3, 0x00010002); op(ppc_addi(4, 2, R_PT)); import(I_ADDPT);
    op(ppc_lwz(3, R_PTR, 2)); import(I_DISPOSEPTR); import(I_MEMERROR); store(R_MEMERR);
    op(ppc_addi(3, 2, TV_MUL)); op(ppc_li(4, INFO_MUL)); op(ppc_li(5, 1)); import(I_NEWRD); store(R_UPP);
    op(ppc_li(4, INFO_MUL)); op(ppc_li(5, 5)); op(ppc_li(6, 7)); import(I_CALLUPP); store(R_MUL); /* PowerPC to PowerPC */
    op(ppc_addi(3, 2, TV_VBL)); li32(4, INFO_VBL); op(ppc_li(5, 1)); import(I_NEWRD); store(R_VBLUPP);
    op(ppc_stw(3, R_VBLTASK + 6, 2));                                            /* vblAddr */
    op(ppc_li(0, 1)); op(ppc_d(44, 0, 2, R_VBLTASK + 4)); op(ppc_d(44, 0, 2, R_VBLTASK + 10)); /* sth: qType, vblCount */
    op(ppc_addi(3, 2, R_VBLTASK)); import(I_VINSTALL); store(R_VERR);
    uint32_t wait = here();                                                      /* spin until the VBL task has run */
    op(ppc_lwz(3, R_VBLCOUNT, 2)); op(ppc_d(11, 0, 3, 0)); /* cmpwi r3,0 */
    op(ppc_bc(BO_TRUE, BI_EQ, (int32_t)(wait - here()), 0, 0));
    op(ppc_lwz(3, R_68K, 2)); op(ppc_li(4, INFO_TWICE)); op(ppc_li(5, 20)); import(I_CALLUPP); store(R_MIXED);
    op(ppc_li(3, 3)); op(ppc_addi(4, 2, R_FINAL)); import(I_DELAY);              /* a trap that blocks */
    op(ppc_lwz(3, SLOTS + 4 * I_WEAK, 2)); store(R_WEAK);
    import(I_EXIT);
    epilogue();
    mul_at = here();                                                             /* long mul(long a, long b): a * b + 1 */
    op(ppc_x(31, 3, 3, 4, 235, 0)); op(ppc_addi(3, 3, 1)); op(PPC_BLR);
    vbl_at = here();                                                             /* VBL task: A0 = the task record */
    op(ppc_stw(3, R_VBLARG, 2)); op(ppc_lwz(5, R_VBLCOUNT, 2)); op(ppc_addi(5, 5, 1)); op(ppc_stw(5, R_VBLCOUNT, 2)); op(PPC_BLR);
    twice_at = here();                                                           /* long twice(long x): allocates, returns 2x */
    prologue();
    op(ppc_stw(3, R_CBX, 2)); op(ppc_li(3, 8)); import(I_NEWPTR); store(R_CBPTR);
    op(ppc_lwz(3, R_CBX, 2)); op(ppc_add(3, 3, 3));
    epilogue();
}

static const FixLibrary libraries[] = {{"InterfaceLib", 0, I_WEAK}, {"AbsentLib", I_WEAK, 1}};
static const FixImport imports[IMPORTS] = {
    {"NewPtr", 2}, {"DisposePtr", 2}, {"NewHandle", 2}, {"GetHandleSize", 2}, {"SetRect", 2}, {"PtInRect", 2},
    {"Gestalt", 2}, {"AddPt", 2}, {"MemError", 2}, {"NewRoutineDescriptor", 2}, {"CallUniversalProc", 2},
    {"VInstall", 2}, {"Delay", 2}, {"ExitToShell", 2}, {"NotInstalled", 0x82}};
static const uint16_t relocations[] = {0x4603, 0x4a00 | (IMPORTS - 1)};

static uint8_t image[4096], fork_bytes[512];

static size_t build_image(void) {
    static uint8_t bytes[sizeof code], data[DATA_BYTES];
    PefFixture f = {0};
    assemble();
    for (unsigned i = 0; i < count; i++) fix32(bytes + 4 * i, code[i]);
    memset(data, 0, sizeof data);
    fix32(data + TV_MUL, mul_at); fix32(data + TV_VBL, vbl_at); fix32(data + TV_TWICE, twice_at);
    fix16(data + R_PT, 10); fix16(data + R_PT + 2, 20); /* Point v 10, h 20 */
    f.code = bytes; f.code_size = 4 * count;
    f.data = data; f.data_packed = f.data_unpacked = f.data_total = DATA_BYTES; f.data_kind = 1;
    f.libraries = libraries; f.library_count = 2; f.imports = imports; f.import_count = IMPORTS;
    f.reloc = relocations; f.reloc_count = 2; f.reloc_section = 1; f.main_section = 1; f.main_offset = TV_MAIN;
    return pef_fixture_build(&f, image, sizeof image, NULL);
}

/* A resource fork with one small resource and no CODE: what a PowerPC-only application has. */
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

/* Guest memory as the guest left it; mac_cpu_read refuses once the CPU has halted, and it has (ExitToShell). */
static uint32_t peek(MacRuntime *rt, uint32_t address, unsigned width) {
    const uint8_t *p = mac_cpu_ram(rt->cpu) + address;
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | p[i];
    return v;
}
static uint32_t word(MacRuntime *rt, uint32_t base, uint32_t slot) { return peek(rt, base + slot, 4); }

static int start(MacRuntime *rt, MacResourceFork *fork, size_t image_size) {
    char err[240];
    if (mac_resource_open(fork, fork_bytes, build_fork(), err, sizeof err) ||
        mac_ppc_runtime_init(rt, fork, image, image_size, err, sizeof err)) {
        fprintf(stderr, "init: %s\n", err);
        return -1;
    }
    return 0;
}

static void test_fragment(size_t image_size) {
    MacRuntime rt;
    MacResourceFork fork;
    CHECK(start(&rt, &fork, image_size) == 0);
    if (!rt.cpu) return;
    MacPpcState *st = rt.ppc;
    uint32_t d = st->fragment.section_address[1];
    CHECK(st && d > rt.a5 && d < rt.screen_base && st->fragment.missing == 0 && st->fragment.absent == 1);
    CHECK(mac_cpu_read(rt.cpu, 0x904, 4) == rt.a5 && mac_runtime_resource(&rt, MAC_RESOURCE_TYPE('T', 'E', 'S', 'T'), 128));
    /* 68K code for the mixed-mode round trip: FUNCTION f(x: LONGINT): LONGINT calls a PowerPC routine through a
     * routine descriptor (written here byte by byte) and adds one to its result. */
    uint32_t desc = mac_runtime_alloc(&rt, 32), routine = mac_runtime_alloc(&rt, 32), p = routine;
    static const uint16_t head[] = {0x202f, 0x0004, 0x598f, 0x2f00, 0x4eb9}, tail[] = {0x201f, 0x5280, 0x205f, 0x588f, 0x2e80, 0x4ed0};
    mac_cpu_write(rt.cpu, desc, 2, 0xaafe); mac_cpu_write(rt.cpu, desc + 2, 1, 7);
    mac_cpu_write(rt.cpu, desc + 12, 4, INFO_TWICE); mac_cpu_write(rt.cpu, desc + 17, 1, 1);
    mac_cpu_write(rt.cpu, desc + 20, 4, d + TV_TWICE);
    for (unsigned i = 0; i < 5; i++, p += 2) mac_cpu_write(rt.cpu, p, 2, head[i]);
    mac_cpu_write(rt.cpu, p, 4, desc); p += 4;
    for (unsigned i = 0; i < 6; i++, p += 2) mac_cpu_write(rt.cpu, p, 2, tail[i]);
    mac_cpu_write(rt.cpu, d + R_68K, 4, routine);
    uint32_t a7 = mac_cpu_get(rt.cpu, MAC_A7), r1 = st->cpu.gpr[1];
    unsigned slices = 0;
    while (slices < 2000 && !mac_cpu_halted(rt.cpu)) {
        rt.ticks++;
        slices++;
        if (!mac_runtime_run(&rt, 10000)) break;
    }
    CHECK(rt.exited && strstr(mac_cpu_error(rt.cpu), "ExitToShell"));
    uint32_t ptr = word(&rt, d, R_PTR), handle = word(&rt, d, R_HANDLE);
    CHECK(ptr >= 0x100000 && ptr < rt.a5 && handle && peek(&rt, handle, 4) != 0);
    CHECK(word(&rt, d, R_HSIZE) == 40);
    CHECK(peek(&rt, d + R_RECT, 2) == 20 && peek(&rt, d + R_RECT + 2, 2) == 10 &&
          peek(&rt, d + R_RECT + 4, 2) == 40 && peek(&rt, d + R_RECT + 6, 2) == 30);
    CHECK(word(&rt, d, R_INSIDE) == 1 && word(&rt, d, R_OUTSIDE) == 0);
    CHECK(word(&rt, d, R_GERR) == 0 && word(&rt, d, R_GESTALT) == MAC_PROFILE_SYSTEM_VERSION);
    CHECK(peek(&rt, d + R_PT, 2) == 11 && peek(&rt, d + R_PT + 2, 2) == 22);
    CHECK(word(&rt, d, R_MEMERR) == 0);
    uint32_t upp = word(&rt, d, R_UPP);
    CHECK(upp && peek(&rt, upp, 2) == 0xaafe && peek(&rt, upp + 12, 4) == INFO_MUL &&
          peek(&rt, upp + 17, 1) == 1 && peek(&rt, upp + 20, 4) == d + TV_MUL);
    CHECK(word(&rt, d, R_MUL) == 36);
    CHECK(word(&rt, d, R_VERR) == 0 && word(&rt, d, R_VBLCOUNT) == 1 && word(&rt, d, R_VBLARG) == d + R_VBLTASK);
    CHECK(word(&rt, d, R_MIXED) == 41 && word(&rt, d, R_CBX) == 20);
    uint32_t inner = word(&rt, d, R_CBPTR);
    CHECK(inner >= 0x100000 && inner < rt.a5 && inner != ptr);
    CHECK(word(&rt, d, R_FINAL) != 0 && slices > 3);
    CHECK(word(&rt, d, R_WEAK) == 0);
    CHECK(st->depth == 0 && st->calls == 18 && mac_cpu_get(rt.cpu, MAC_A7) == a7 && st->cpu.gpr[1] == r1 - 64);
    mac_runtime_destroy(&rt);
    CHECK(rt.ppc == NULL);
}

/* PortSize and MovePortTo, which this slice added to the QuickDraw service because the real application needs them. */
static void test_port_geometry(size_t image_size) {
    MacRuntime rt;
    MacResourceFork fork;
    CHECK(start(&rt, &fork, image_size) == 0);
    if (!rt.cpu) return;
    uint32_t globals = mac_runtime_alloc(&rt, 256), port = mac_runtime_alloc(&rt, 108), args[2];
    args[0] = globals + 202;
    CHECK(mac_ppc_invoke(&rt, "InitGraf", 0xa86e, "-:l", args, 1, NULL) == 0);
    args[0] = port;
    CHECK(mac_ppc_invoke(&rt, "OpenPort", 0xa86f, "-:l", args, 1, NULL) == 0);
    int top = (int16_t)peek(&rt, port + 16, 2), left = (int16_t)peek(&rt, port + 18, 2);
    args[0] = 100; args[1] = 50;
    CHECK(mac_ppc_invoke(&rt, "PortSize", 0xa876, "-:ww", args, 2, NULL) == 0);
    CHECK((int16_t)peek(&rt, port + 20, 2) == top + 50 && (int16_t)peek(&rt, port + 22, 2) == left + 100);
    CHECK((int16_t)peek(&rt, port + 16, 2) == top && (int16_t)peek(&rt, port + 18, 2) == left);
    args[0] = 30; args[1] = 40;
    CHECK(mac_ppc_invoke(&rt, "MovePortTo", 0xa877, "-:ww", args, 2, NULL) == 0);
    CHECK((int16_t)peek(&rt, port + 8, 2) == top - 40 && (int16_t)peek(&rt, port + 10, 2) == left - 30);
    CHECK(!mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

/* The bridge refuses instead of guessing: a signature that disagrees with the service, a trap nobody implements. */
static void test_refusals(size_t image_size) {
    MacRuntime rt;
    MacResourceFork fork;
    uint32_t args[6] = {0x1000, 1, 2, 3, 4, 5}, result = 0;
    CHECK(start(&rt, &fork, image_size) == 0);
    if (!rt.cpu) return;
    uint32_t a7 = mac_cpu_get(rt.cpu, MAC_A7);
    CHECK(mac_ppc_invoke(&rt, "SetRect", 0xa8a7, "-:lwwww", args, 5, &result) == 0 && mac_cpu_get(rt.cpu, MAC_A7) == a7);
    CHECK(mac_cpu_read(rt.cpu, 0x1000, 2) == 2 && mac_cpu_read(rt.cpu, 0x1006, 2) == 3);
    args[0] = 0x00101000; args[1] = 0x10; /* one parameter too many: the service pops 12 bytes of the 14 pushed */
    CHECK(mac_ppc_invoke(&rt, "SetRect", 0xa8a7, "-:lwwwww", args, 6, &result) == -1);
    CHECK(mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "Stack imbalance") && strstr(rt.ppc->cpu.error, "SetRect"));
    mac_runtime_destroy(&rt);
    CHECK(start(&rt, &fork, image_size) == 0);
    if (!rt.cpu) return;
    CHECK(mac_ppc_invoke(&rt, "NoSuchCall", 0xa89f, "-:", NULL, 0, &result) == -1);
    CHECK(strstr(mac_cpu_error(rt.cpu), "Toolbox trap $A89F behind PowerPC import NoSuchCall is not implemented."));
    CHECK(mac_runtime_run(&rt, 1000) == 0 && !rt.exited); /* halted: nothing runs afterwards */
    mac_runtime_destroy(&rt);
    /* A 68K application is untouched by all this: no CODE 0 is still an error for the 68K start path. */
    char err[200];
    CHECK(mac_runtime_init(&rt, &fork, err, sizeof err) == -1 && strstr(err, "CODE 0"));
}

int main(void) {
    size_t image_size = build_image();
    size_t table_count;
    CHECK(image_size != 0 && mac_ppc_imports(&table_count) && table_count > 300);
    test_fragment(image_size);
    test_port_geometry(image_size);
    test_refusals(image_size);
    if (failures) { fprintf(stderr, "test_mac_ppc_bridge: %d/%d checks failed\n", failures, checks); return 1; }
    printf("test_mac_ppc_bridge: all %d checks passed\n", checks);
    return 0;
}
