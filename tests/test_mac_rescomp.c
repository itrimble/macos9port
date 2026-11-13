/* Compressed resources, a program's own resource load hook, truncated CODE 0 tables and the one-process Process Manager. Every fork, every piece of guest code
 * (a toy decompressor, a toy load hook) and every byte pattern is authored here; nothing comes from a real program or from Apple. Headless. */
#include "mac_runtime.h"
#include "mac_codefmt.h"
#include "mac_rescomp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#include "fork_builder.h"

enum { SETUP = 0x10000, DCMP_ID = 128, TYPE_TEST = 0x54455354, TYPE_DCMP = 0x64636d70 };
static uint8_t fork_bytes[FB_BYTES];

static uint32_t e16(MacCpu *c, uint32_t p, uint16_t w) { CHECK(mac_cpu_write(c, p, 2, w) == 0); return p + 2; }
static uint32_t e32(MacCpu *c, uint32_t p, uint32_t v) { CHECK(mac_cpu_write(c, p, 4, v) == 0); return p + 4; }
/* mac_cpu_read refuses once the machine has halted (ExitToShell halts it), so results are read from the RAM image. */
static uint32_t g(MacCpu *c, uint32_t a, unsigned n) { const uint8_t *r = mac_cpu_ram(c) + a; uint32_t v = 0; while (n--) v = v << 8 | *r++; return v; }
static void put16(uint8_t *p, unsigned x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, x >> 16); put16(p + 2, x & 0xffff); }

/* The toy decompressor: dcmp(src, dst, header, work): OSErr. It copies 8 source bytes to the destination with every byte inverted; the rest of the
 * destination is the zeros the runtime cleared. Pascal convention: it pops its 16 bytes of arguments and leaves 0 in the result slot. */
static const uint16_t toy_dcmp[] = {
    0x206f, 0x0010,         /* movea.l 16(sp),a0   src */
    0x226f, 0x000c,         /* movea.l 12(sp),a1   dst */
    0x7007,                 /* moveq #7,d0 */
    0x1218,                 /* move.b (a0)+,d1 */
    0x0a01, 0x00ff,         /* eori.b #$ff,d1 */
    0x12c1,                 /* move.b d1,(a1)+ */
    0x51c8, 0xfff6,         /* dbra d0,loop */
    0x205f,                 /* movea.l (sp)+,a0    return address */
    0x4fef, 0x0010,         /* lea 16(sp),sp       pop the arguments */
    0x4257,                 /* clr.w (sp)          noErr */
    0x4ed0                  /* jmp (a0) */
};

/* The Resource Manager's extended header, version 8, as the public layout gives it. */
static size_t apple_resource(uint8_t *out, uint32_t expanded, int dcmp_id, const uint8_t *body, size_t body_n) {
    memset(out, 0, 18);
    out[0] = 0xa8; out[1] = 0x9f; out[2] = 0x65; out[3] = 0x72;
    put16(out + 4, 18); out[6] = 8; out[7] = 1; put32(out + 8, expanded); out[12] = 0x10; out[13] = 1; put16(out + 14, (unsigned)dcmp_id);
    memcpy(out + 18, body, body_n);
    return 18 + body_n;
}

/* CODE 0: one entry that names CODE 1 at `offset`; `declared` bytes of table are claimed but only one entry is stored. */
static void code0(FbFork *f, uint32_t offset, uint32_t declared, int with_packed_tail) {
    uint8_t z[16 + 16] = {0};
    put32(z, 0x100); put32(z + 4, 0x100); put32(z + 8, declared); put32(z + 12, 32);
    put16(z + 16, offset); put16(z + 18, 0x3f3c); put16(z + 20, 1); put16(z + 22, 0xa9f0);
    if (with_packed_tail) { put16(z + 24, 0xa89f); put16(z + 26, 0x00a6); put32(z + 28, 0x01020304); }
    fb_add(f, MAC_CODE_TYPE, 0, NULL, z, with_packed_tail ? 32 : 24);
}

static int open_rt(const FbFork *f, MacResourceFork *fork, MacRuntime *rt, char *err, size_t n) {
    size_t len = fb_build(f, fork_bytes);
    return mac_resource_open(fork, fork_bytes, len, err, n) || mac_runtime_init(rt, fork, err, n);
}
static uint32_t emit_exit(MacCpu *c, uint32_t p) { return e16(c, p, 0xa9f4); }

/* --- pure layout readers -------------------------------------------------------------------------------------------------------------------------- */
static void formats(void) {
    uint8_t h[18 + 8]; uint8_t body[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    MacComp c;
    size_t n = apple_resource(h, 0x40, 5, body, 8);
    CHECK(mac_comp_parse(h, n, &c) == 1 && c.kind == MAC_COMP_SYSTEM && c.version == 8 && c.expanded == 0x40 && c.dcmp_id == 5 && c.header_len == 18);
    CHECK(mac_comp_size(h, n) == 0x40);
    h[6] = 7; CHECK(mac_comp_parse(h, n, &c) == -1);                      /* a version this reader does not know */
    h[6] = 8; put16(h + 4, 400); CHECK(mac_comp_parse(h, n, &c) == -1);   /* header longer than the resource */
    put16(h + 4, 18); put32(h + 8, 0x7fffffff); CHECK(mac_comp_parse(h, n, &c) == -1);   /* absurd expanded size */
    CHECK(mac_comp_parse(h, 3, &c) == 0 && mac_comp_parse(NULL, 0, &c) == 0);
    uint8_t m[16] = {0xa8, 0x9f, 0, 0x0c, 0, 0, 0, 0, 0, 0, 0x1b, 0x4d, 0, 0, 0x18, 0xc7};
    CHECK(mac_comp_parse(m, 16, &c) == 1 && c.kind == MAC_COMP_MARKED && c.expanded == 0x1b4d && mac_comp_size(m, 16) == 0x1b4d);
    uint8_t plain[16] = {0, 0, 0, 1}; CHECK(mac_comp_parse(plain, 16, &c) == 0 && mac_comp_size(plain, 16) == 16);
    uint8_t z[40] = {0}; MacCode0 z0;
    put32(z, 0x100); put32(z + 4, 0x100); put32(z + 8, 0x40); put32(z + 12, 0x20);
    CHECK(mac_code0_parse(z, 40, &z0) == 0 && z0.truncated && z0.present == 24 && z0.length == 0x40);   /* 24 bytes stored of 64 declared */
    CHECK(mac_code0_parse(z, 24, &z0) == 0 && z0.present == 8);
    CHECK(mac_code0_parse(z, 23, &z0) == -1 && mac_code0_parse(z, 16, &z0) == -1);
    put32(z + 8, 0x100000); CHECK(mac_code0_parse(z, 40, &z0) == -1);   /* claims more than the A5 world holds */
    put32(z + 8, 8); put32(z + 12, 0x21); CHECK(mac_code0_parse(z, 40, &z0) == -1);   /* odd offset */
}

/* --- a compressed CODE resource: the loader checks it at its expanded size, LoadSeg expands it with the program's own decompressor ---------------------- */
static void compressed_code(int with_dcmp, int entry_beyond_stored) {
    FbFork *f = calloc(1, sizeof *f);
    uint8_t seg[8] = {0x00, 0x08, 0x00, 0x01, 0x70, 0x0b, 0x4e, 0x75}, inverted[8], res[64];
    for (int i = 0; i < 8; i++) inverted[i] = (uint8_t)~seg[i];
    size_t n = apple_resource(res, entry_beyond_stored ? 0x80 : 8, DCMP_ID, inverted, 8);
    code0(f, entry_beyond_stored ? 0x40 : 0, 32, 0);
    fb_add(f, MAC_CODE_TYPE, 1, NULL, res, n);
    uint8_t code[sizeof toy_dcmp];
    for (unsigned i = 0; i < sizeof toy_dcmp / 2; i++) put16(code + 2 * i, toy_dcmp[i]);
    if (with_dcmp) fb_add(f, TYPE_DCMP, DCMP_ID, NULL, code, sizeof code);
    MacResourceFork fork; char err[240]; MacRuntime rt;
    int opened = open_rt(f, &fork, &rt, err, sizeof err);
    if (opened) fprintf(stderr, "open: %s\n", err);
    CHECK(opened == 0);   /* the entry at offset 0x40 lies past the 26 stored bytes but inside the 0x80 expanded ones */
    if (!entry_beyond_stored) {
        MacCpu *c = rt.cpu; uint32_t p = SETUP;
        p = e16(c, p, 0x4eb9); p = e32(c, p, rt.jump_table + 2); emit_exit(c, p);   /* JSR entry0; ExitToShell */
        mac_cpu_set(c, MAC_PC, SETUP);
        mac_runtime_run(&rt, 2000);
        if (with_dcmp) CHECK(rt.exited && (mac_cpu_get(c, MAC_D0) & 0xff) == 11);   /* the expanded segment's code (MOVEQ #11,D0; RTS) ran */
        else {
            CHECK(mac_cpu_halted(c) && !rt.exited);
            CHECK(strstr(mac_rescomp_why(&rt), "'dcmp' 128") != NULL && strstr(mac_rescomp_why(&rt), "System file") != NULL);   /* named precisely */
        }
    }
    mac_runtime_destroy(&rt);
    free(f);
}

/* --- GetResource, SizeResource, MaxSizeRsrc, GetHandleSize, ReleaseResource on a compressed non-CODE resource ------------------------------------------- */
static void compressed_data(int with_dcmp) {
    FbFork *f = calloc(1, sizeof *f);
    uint8_t body[8] = {0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7}, res[64];   /* inverts to 01 02 03 04 05 06 07 08 */
    size_t n = apple_resource(res, 0x30, DCMP_ID, body, 8);
    code0(f, 0, 32, 0);
    uint8_t seg[8] = {0x00, 0x08, 0x00, 0x01, 0x70, 0x0b, 0x4e, 0x75};
    fb_add(f, MAC_CODE_TYPE, 1, NULL, seg, 8);
    fb_add(f, TYPE_TEST, 7, NULL, res, n);
    uint8_t code[sizeof toy_dcmp];
    for (unsigned i = 0; i < sizeof toy_dcmp / 2; i++) put16(code + 2 * i, toy_dcmp[i]);
    if (with_dcmp) fb_add(f, TYPE_DCMP, DCMP_ID, NULL, code, sizeof code);
    MacResourceFork fork; char err[240]; MacRuntime rt;
    CHECK(open_rt(f, &fork, &rt, err, sizeof err) == 0);
    MacCpu *c = rt.cpu; uint32_t p = SETUP;
    /* h = GetResource('TEST',7); D4 = h; D5 = GetHandleSize(h); D6 = SizeResource(h); D7 = MaxSizeRsrc(h); A2 = *h; D3 = GetResError */
    p = e16(c, p, 0x598f); p = e16(c, p, 0x2f3c); p = e32(c, p, TYPE_TEST); p = e16(c, p, 0x3f3c); p = e16(c, p, 7); p = e16(c, p, 0xa9a0);
    p = e16(c, p, 0x281f);                                                   /* move.l (sp)+,d4 */
    p = e16(c, p, 0x554f); p = e16(c, p, 0xa9af); p = e16(c, p, 0x361f);   /* GetResError -> d3 (word result popped into d3.w) */
    if (with_dcmp) {
        p = e16(c, p, 0x2044); p = e16(c, p, 0xa025); p = e16(c, p, 0x2a00);   /* movea.l d4,a0; _GetHandleSize; move.l d0,d5 */
        p = e16(c, p, 0x598f); p = e16(c, p, 0x2f04); p = e16(c, p, 0xa9a5); p = e16(c, p, 0x2c1f);   /* SizeResource */
        p = e16(c, p, 0x598f); p = e16(c, p, 0x2f04); p = e16(c, p, 0xa821); p = e16(c, p, 0x2e1f);   /* MaxSizeRsrc */
        p = e16(c, p, 0x2044); p = e16(c, p, 0x2450);                          /* movea.l d4,a0; movea.l (a0),a2 */
    }
    emit_exit(c, p);
    mac_cpu_set(c, MAC_PC, SETUP);
    mac_runtime_run(&rt, 5000);
    CHECK(rt.exited);
    if (with_dcmp) {
        CHECK(mac_cpu_get(c, MAC_D4) != 0 && (mac_cpu_get(c, MAC_D3) & 0xffff) == 0);
        CHECK(mac_cpu_get(c, MAC_D5) == 0x30);                          /* the handle holds the expanded bytes */
        CHECK(mac_cpu_get(c, MAC_D6) == n);                             /* SizeResource: the size on disk, header included */
        CHECK(mac_cpu_get(c, MAC_D7) == 0x30);                          /* MaxSizeRsrc: the size once loaded */
        uint32_t a2 = mac_cpu_get(c, MAC_A2);
        CHECK(g(c, a2, 4) == 0x01020304 && g(c, a2 + 4, 4) == 0x05060708 && g(c, a2 + 8, 4) == 0);
    } else {
        CHECK(mac_cpu_get(c, MAC_D4) == 0);                              /* NIL, not the packed bytes */
        CHECK((int16_t)(mac_cpu_get(c, MAC_D3) & 0xffff) == -192);       /* resNotFound */
        CHECK(strstr(mac_rescomp_why(&rt), "'dcmp' 128") != NULL);
    }
    mac_runtime_destroy(&rt);
    free(f);
}

/* --- a program's own load hook (OS trap $A0FC), run over its marked resources when it is installed ------------------------------------------------------ */
static void load_hook(void) {
    FbFork *f = calloc(1, sizeof *f);
    uint8_t marked[24] = {0xa8, 0x9f, 0x00, 0x0c, 0xde, 0xad, 0xbe, 0xef, 0, 0, 0, 0x18, 0, 0, 0, 0x18, 0x80, 0, 0, 1, 0x99, 0x98, 0x97, 0x96};
    uint8_t plain[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    code0(f, 0, 8, 0);
    uint8_t seg[8] = {0x00, 0x08, 0x00, 0x01, 0x70, 0x0b, 0x4e, 0x75};
    fb_add(f, MAC_CODE_TYPE, 1, NULL, seg, 8);
    fb_add(f, TYPE_TEST, 1, NULL, marked, sizeof marked);
    fb_add(f, TYPE_TEST, 2, NULL, plain, sizeof plain);
    MacResourceFork fork; char err[240]; MacRuntime rt;
    CHECK(open_rt(f, &fork, &rt, err, sizeof err) == 0);
    MacCpu *c = rt.cpu; uint32_t hook = SETUP + 0x200, p = hook;
    /* The hook the way an installer engine writes one: chain to the previous routine first (a native stub reached by JSR inside a guest call), then, for a
     * handle whose contents start with A89F 000C, overwrite the first long and append nothing. A2 is the map entry; its handle is the long at +8. */
    p = e16(c, p, 0x4eb9); p = e32(c, p, mac_native_stub(0xfc));            /* jsr <previous $A0FC> */
    p = e16(c, p, 0x206a); p = e16(c, p, 0x0008);                           /* movea.l 8(a2),a0   the handle */
    p = e16(c, p, 0x2050);                                                  /* movea.l (a0),a0    its contents */
    p = e16(c, p, 0x0c90); p = e32(c, p, 0xa89f000c);                       /* cmpi.l #$a89f000c,(a0) */
    p = e16(c, p, 0x6606);                                                  /* bne.s end */
    p = e16(c, p, 0x20bc); p = e32(c, p, 0x11223344);                       /* move.l #$11223344,(a0) */
    p = e16(c, p, 0x4e75);                                                  /* end: rts */
    p = SETUP;
    p = e16(c, p, 0x207c); p = e32(c, p, hook); p = e16(c, p, 0x303c); p = e16(c, p, 0xa0fc); p = e16(c, p, 0xa247);   /* SetTrapAddress($A0FC) */
    p = e16(c, p, 0x598f); p = e16(c, p, 0x2f3c); p = e32(c, p, TYPE_TEST); p = e16(c, p, 0x3f3c); p = e16(c, p, 1); p = e16(c, p, 0xa9a0);   /* GetResource */
    p = e16(c, p, 0x205f); p = e16(c, p, 0x2050); p = e16(c, p, 0x2a10);   /* movea.l (sp)+,a0; movea.l (a0),a0; move.l (a0),d5 */
    p = e16(c, p, 0x598f); p = e16(c, p, 0x2f3c); p = e32(c, p, TYPE_TEST); p = e16(c, p, 0x3f3c); p = e16(c, p, 2); p = e16(c, p, 0xa9a0);
    p = e16(c, p, 0x205f); p = e16(c, p, 0x2050); p = e16(c, p, 0x2c10);   /* the unmarked resource: its first long into d6 */
    emit_exit(c, p);
    mac_cpu_set(c, MAC_D3, 0xcafe0003); mac_cpu_set(c, MAC_D5, 0); mac_cpu_set(c, MAC_PC, SETUP);
    mac_runtime_run(&rt, 200000);
    CHECK(rt.exited && strstr(mac_cpu_error(c), "ExitToShell") != NULL);   /* it ended by the program's own ExitToShell, not by a halt */
    CHECK(mac_cpu_get(c, MAC_D5) == 0x11223344);                            /* the hook ran on the marked resource */
    CHECK(mac_cpu_get(c, MAC_D6) == 0x01020304);                            /* and left the plain one alone */
    CHECK(mac_cpu_get(c, MAC_D3) == 0xcafe0003);                            /* registers the hook may have used are back as the trap's caller left them */
    /* A native reader (it reads rt->fork directly, as the dialog and menu services do) sees the expanded bytes too. */
    MacResource r;
    CHECK(mac_resource_find(&rt.fork, TYPE_TEST, 1, &r) == 0 && r.size == sizeof marked && mac_cf_be32(r.data) == 0x11223344 && mac_cf_be32(r.data + 20) == 0x99989796);
    CHECK(mac_resource_find(&rt.fork, TYPE_TEST, 2, &r) == 0 && r.size == 8 && r.data[0] == 1);
    CHECK(mac_resource_find(&rt.fork, MAC_CODE_TYPE, 0, &r) == 0 && r.size == 24);   /* everything else survives the rebuilt fork */
    mac_runtime_destroy(&rt);
    free(f);
}

/* --- CODE 0 stored shorter than it declares, the rest being an installer's packed payload ---------------------------------------------------------------- */
static void truncated_code0(void) {
    FbFork *f = calloc(1, sizeof *f);
    uint8_t seg[8] = {0x00, 0x08, 0x00, 0x01, 0x70, 0x0b, 0x4e, 0x75};
    code0(f, 0, 0x60, 1);                                                   /* 96 bytes declared, 16 stored, the second "entry" is packed data */
    fb_add(f, MAC_CODE_TYPE, 1, NULL, seg, 8);
    MacResourceFork fork; char err[240]; MacRuntime rt;
    CHECK(open_rt(f, &fork, &rt, err, sizeof err) == 0);
    MacCpu *c = rt.cpu; uint32_t p = SETUP;
    p = e16(c, p, 0x4eb9); p = e32(c, p, rt.jump_table + 2); emit_exit(c, p);
    mac_cpu_set(c, MAC_PC, SETUP);
    mac_runtime_run(&rt, 2000);
    CHECK(rt.exited && (mac_cpu_get(c, MAC_D0) & 0xff) == 11);
    CHECK(g(c, rt.jump_table + 0x28, 4) == 0 && rt.jump_size == 0x60);   /* the entries past what was stored are zero */
    mac_runtime_destroy(&rt);
    free(f);
    /* A table with no whole entry stored is still refused. */
    f = calloc(1, sizeof *f);
    uint8_t z[20] = {0}; put32(z, 0x100); put32(z + 4, 0x100); put32(z + 8, 8); put32(z + 12, 32);
    fb_add(f, MAC_CODE_TYPE, 0, NULL, z, 20);
    CHECK(open_rt(f, &fork, &rt, err, sizeof err) != 0 && strstr(err, "CODE 0") != NULL);
    free(f);
}

/* --- the one-process Process Manager ----------------------------------------------------------------------------------------------------------------- */
static void process_case(int which) {
    FbFork *f = calloc(1, sizeof *f);
    uint8_t seg[8] = {0x00, 0x08, 0x00, 0x01, 0x70, 0x0b, 0x4e, 0x75};
    code0(f, 0, 8, 0);
    fb_add(f, MAC_CODE_TYPE, 1, NULL, seg, 8);
    MacResourceFork fork; char err[240]; MacRuntime rt;
    CHECK(open_rt(f, &fork, &rt, err, sizeof err) == 0);
    MacCpu *c = rt.cpu; uint32_t psn = SETUP + 0x800, psn2 = SETUP + 0x810, flag = SETUP + 0x820, p = SETUP;
    for (int i = 0; i < 2; i++) { mac_cpu_write(c, psn + 4 * i, 4, 0xffffffff); mac_cpu_write(c, psn2 + 4 * i, 4, 0); }
    if (which == 0) {   /* GetCurrentProcess(psn) -> d3.w; GetNextProcess(psn2) from kNoProcess -> d4.w, and again -> d5.w */
        p = e16(c, p, 0x554f); p = e16(c, p, 0x4879); p = e32(c, p, psn); p = e16(c, p, 0x3f3c); p = e16(c, p, 0x37); p = e16(c, p, 0xa88f); p = e16(c, p, 0x361f);
        p = e16(c, p, 0x554f); p = e16(c, p, 0x4879); p = e32(c, p, psn2); p = e16(c, p, 0x3f3c); p = e16(c, p, 0x38); p = e16(c, p, 0xa88f); p = e16(c, p, 0x381f);
        p = e16(c, p, 0x554f); p = e16(c, p, 0x4879); p = e32(c, p, psn2); p = e16(c, p, 0x3f3c); p = e16(c, p, 0x38); p = e16(c, p, 0xa88f); p = e16(c, p, 0x3a1f);
    } else if (which == 1) {   /* SameProcess(psn, kCurrentProcess, flag) */
        mac_cpu_write(c, psn, 4, 0); mac_cpu_write(c, psn + 4, 4, 0x00010002); mac_cpu_write(c, psn2 + 4, 4, 2); mac_cpu_write(c, flag, 1, 0);
        p = e16(c, p, 0x554f); p = e16(c, p, 0x4879); p = e32(c, p, psn); p = e16(c, p, 0x4879); p = e32(c, p, psn2); p = e16(c, p, 0x4879); p = e32(c, p, flag);
        p = e16(c, p, 0x3f3c); p = e16(c, p, 0x3d); p = e16(c, p, 0xa88f); p = e16(c, p, 0x361f);
    } else { p = e16(c, p, 0x3f3c); p = e16(c, p, 0x7f); p = e16(c, p, 0xa88f); }   /* an unknown selector */
    emit_exit(c, p);
    mac_cpu_set(c, MAC_PC, SETUP);
    uint32_t sp = mac_cpu_get(c, MAC_A7);
    mac_runtime_run(&rt, 2000);
    if (getenv("RC_DEBUG")) fprintf(stderr, "case %d: exited=%d halted=%d err=%s d3=%08x d4=%08x d5=%08x psn=%08x/%08x flag=%u\n", which, rt.exited, mac_cpu_halted(c), mac_cpu_error(c) ? mac_cpu_error(c) : "", mac_cpu_get(c, MAC_D3), mac_cpu_get(c, MAC_D4), mac_cpu_get(c, MAC_D5), g(c, psn, 4), g(c, psn + 4, 4), g(c, flag, 1));
    if (which == 0) {
        CHECK(rt.exited && mac_cpu_get(c, MAC_A7) == sp);                       /* arguments and results all popped */
        CHECK((mac_cpu_get(c, MAC_D3) & 0xffff) == 0 && g(c, psn, 4) == 0 && g(c, psn + 4, 4) == 0x00010002);
        CHECK((mac_cpu_get(c, MAC_D4) & 0xffff) == 0);                          /* the first process after kNoProcess is the program */
        CHECK((int16_t)(mac_cpu_get(c, MAC_D5) & 0xffff) == -600);              /* and there is no second: procNotFound */
    } else if (which == 1) {
        CHECK(rt.exited && (mac_cpu_get(c, MAC_D3) & 0xffff) == 0 && g(c, flag, 1) == 1 && mac_cpu_get(c, MAC_A7) == sp);
    } else {
        CHECK(mac_cpu_halted(c) && !rt.exited);                                 /* names the selector rather than answering */
        CHECK(strstr(mac_cpu_error(c), "$7F") != NULL);
    }
    mac_runtime_destroy(&rt);
    free(f);
}

int main(void) {
    formats();
    compressed_code(1, 0);
    compressed_code(0, 0);
    compressed_code(1, 1);
    compressed_data(1);
    compressed_data(0);
    load_hook();
    truncated_code0();
    process_case(0);
    process_case(1);
    process_case(2);
    if (fails) { fprintf(stderr, "test_mac_rescomp: %d failures\n", fails); return 1; }
    printf("test_mac_rescomp: all checks passed\n");
    return 0;
}
