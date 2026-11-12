/* Classic application classification: every input is synthetic and authored here (no guest software). Headless. */
#include "app_classify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

#define FOURCC(a, b, c, d) ((uint32_t)(uint8_t)(a) << 24 | (uint32_t)(uint8_t)(b) << 16 | (uint32_t)(uint8_t)(c) << 8 | (uint32_t)(uint8_t)(d))
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p16(p, v >> 16); p16(p + 2, v & 0xffff); }

typedef struct { uint32_t type; int id; const uint8_t *data; size_t len; } Res;

/* A resource fork holding the given resources, one type list entry per resource (types may repeat; the reader copes). */
static size_t build_fork(uint8_t *out, size_t cap, const Res *r, size_t count) {
    size_t data = 256, datalen = 0;
    for (size_t i = 0; i < count; i++) datalen += 4 + ((r[i].len + 1) & ~(size_t)1);
    size_t map = data + datalen, tl = 28, maplen = tl + 2 + 8 * count + 12 * count;
    if (map + maplen > cap) return 0;
    memset(out, 0, map + maplen);
    p32(out, 256); p32(out + 4, (uint32_t)map); p32(out + 8, (uint32_t)datalen); p32(out + 12, (uint32_t)maplen);
    size_t at = data;
    for (size_t i = 0; i < count; i++) {
        p32(out + at, (uint32_t)r[i].len);
        if (r[i].len) memcpy(out + at + 4, r[i].data, r[i].len);
        p32(out + map + tl + 2 + 8 * count + 12 * i + 4, (uint32_t)(at - data) & 0xffffff);
        at += 4 + ((r[i].len + 1) & ~(size_t)1);
    }
    p16(out + map + 24, (unsigned)tl); p16(out + map + 26, (unsigned)maplen);
    p16(out + map + tl, (unsigned)count - 1);
    for (size_t i = 0; i < count; i++) {
        uint8_t *t = out + map + tl + 2 + 8 * i;
        p32(t, r[i].type); p16(t + 4, 0); p16(t + 6, (unsigned)(2 + 8 * count + 12 * i));
        uint8_t *ref = out + map + tl + 2 + 8 * count + 12 * i;
        p16(ref, (unsigned)r[i].id); p16(ref + 2, 0xffff);
    }
    return map + maplen;
}

static const uint8_t code0_good[24 + 8] = {0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0x10};  /* above 32, below 0, table 8 bytes at 16 */
static uint8_t size_clean[10], size_dirty[10];

static void init_blobs(void) {
    p16(size_clean, 0x58e0); p32(size_clean + 2, 0x100000); p32(size_clean + 6, 0x80000);   /* is32BitCompatible set */
    p16(size_dirty, 0x5860); p32(size_dirty + 2, 0x100000); p32(size_dirty + 6, 0x80000);   /* clear */
}

static size_t fork68k(uint8_t *out, size_t cap, int size_mode, const uint8_t *code1) {
    Res r[4]; size_t n = 0;
    r[n++] = (Res){FOURCC('C', 'O', 'D', 'E'), 0, code0_good, sizeof code0_good};
    r[n++] = (Res){FOURCC('C', 'O', 'D', 'E'), 1, code1, 4};
    if (size_mode) r[n++] = (Res){FOURCC('S', 'I', 'Z', 'E'), -1, size_mode > 0 ? size_clean : size_dirty, 10};
    return build_fork(out, cap, r, n);
}

/* PEF: 40-byte container header, one loader section, then the loader (56-byte header, 24-byte library entries, strings). */
static size_t build_pef(uint8_t *out, size_t cap, uint32_t arch, int has_main, const char *const *libs, size_t nlibs) {
    size_t strings = 0;
    for (size_t i = 0; i < nlibs; i++) strings += strlen(libs[i]) + 1;
    size_t loader = 56 + 24 * nlibs + strings, start = 40 + 28, total = start + loader;
    if (total > cap) return 0;
    memset(out, 0, total);
    memcpy(out, "Joy!peff", 8); p32(out + 8, arch); p32(out + 12, 1); p16(out + 32, 1); p16(out + 34, 1);
    uint8_t *s = out + 40;
    p32(s + 16, (uint32_t)loader); p32(s + 20, (uint32_t)start); s[24] = 4;
    uint8_t *l = out + start;
    p32(l, has_main ? 0 : 0xffffffffu); p32(l + 24, (uint32_t)nlibs); p32(l + 40, (uint32_t)(56 + 24 * nlibs));
    size_t off = 0;
    for (size_t i = 0; i < nlibs; i++) {
        p32(l + 56 + 24 * i, (uint32_t)off);
        memcpy(l + 56 + 24 * nlibs + off, libs[i], strlen(libs[i]) + 1);
        off += strlen(libs[i]) + 1;
    }
    return total;
}

/* cfrg with one member of the given architecture (kApplicationCFrag, data fork). */
static size_t build_cfrg(uint8_t *out, uint32_t arch) {
    memset(out, 0, 32 + 60);
    p16(out + 30, 1);
    uint8_t *m = out + 32;
    p32(m, arch); m[22] = 1; m[23] = 1; p16(m + 40, 60); m[42] = 4; memcpy(m + 43, "Test", 4);
    return 32 + 60;
}

static AppClassification run(const void *d, size_t dn, const void *r, size_t rn, uint32_t type, int ppc) {
    AppClassification c;
    AppClassifyCaps caps = {ppc, 0};
    CHECK(app_classify(d, dn, r, rn, type, &caps, &c) == 0);
    return c;
}

static void m68k_profiles(void) {
    static const uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75};
    uint8_t f[2048];
    size_t n = fork68k(f, sizeof f, 1, body);
    CHECK(n > 0);
    AppClassification c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.format == APP_FMT_CODE68K && c.profile == APP_PROFILE_B_CLEAN_68K && c.runnable && c.is_application);
    CHECK(c.confidence == APP_CONF_MEDIUM && c.size_declares_32bit && c.has_size && c.code_resources == 2 && c.code0_layout_ok);
    CHECK(strstr(c.uncertainty, "declaration") && c.size_preferred == 0x100000 && c.size_minimum == 0x80000);
    n = fork68k(f, sizeof f, -1, body);
    c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.profile == APP_PROFILE_A_LEGACY_68K && c.confidence == APP_CONF_HIGH && !c.size_declares_32bit);
    n = fork68k(f, sizeof f, 0, body);   /* no SIZE: the missing flag is not evidence of 32-bit cleanliness */
    c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.profile == APP_PROFILE_A_LEGACY_68K && c.confidence == APP_CONF_LOW && !c.has_size);
    CHECK(strstr(c.uncertainty, "not proof"));
    c = run(NULL, 0, f, n, FOURCC('A', 'P', 'P', 'L'), 0);   /* the Finder type APPL changes nothing */
    CHECK(c.profile == APP_PROFILE_A_LEGACY_68K && c.is_application);
    c = run(NULL, 0, f, n, FOURCC('I', 'N', 'I', 'T'), 0);   /* a known non-application type is refused */
    CHECK(c.profile == APP_PROFILE_D_UNSUPPORTED && !c.is_application && !c.runnable && strstr(c.reason, "INIT"));
    /* a SIZE resource of the wrong length is ignored, not trusted */
    Res bad[3] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0_good, sizeof code0_good}, {FOURCC('S', 'I', 'Z', 'E'), -1, size_clean, 8}};
    n = build_fork(f, sizeof f, bad, 2);
    c = run(NULL, 0, f, n, 0, 0);
    CHECK(!c.has_size && c.profile == APP_PROFILE_A_LEGACY_68K);
}

static void m68k_rejections(void) {
    uint8_t f[2048];
    static const uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75};
    Res no0[1] = {{FOURCC('C', 'O', 'D', 'E'), 1, body, 4}};
    size_t n = build_fork(f, sizeof f, no0, 1);
    AppClassification c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.format == APP_FMT_MALFORMED && c.profile == APP_PROFILE_D_UNSUPPORTED && strstr(c.reason, "CODE 0"));
    uint8_t bad0[32]; memcpy(bad0, code0_good, sizeof bad0); p32(bad0 + 8, 7);   /* table length not a multiple of 8 */
    Res b[1] = {{FOURCC('C', 'O', 'D', 'E'), 0, bad0, sizeof bad0}};
    n = build_fork(f, sizeof f, b, 1);
    c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.format == APP_FMT_MALFORMED && c.profile == APP_PROFILE_D_UNSUPPORTED && strstr(c.reason, "layout"));
    Res short0[1] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0_good, 10}};
    n = build_fork(f, sizeof f, short0, 1);
    c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.profile == APP_PROFILE_D_UNSUPPORTED);
    Res none[1] = {{FOURCC('S', 'T', 'R', ' '), 128, body, 4}};   /* data only */
    n = build_fork(f, sizeof f, none, 1);
    c = run(NULL, 0, f, n, 0, 0);
    CHECK(c.format == APP_FMT_NONE && c.profile == APP_PROFILE_D_UNSUPPORTED && !c.runnable && c.identity == 0);
}

static void pef_cases(void) {
    uint8_t d[1024];
    const char *classic[] = {"InterfaceLib", "MathLib"};
    size_t n = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, classic, 2);
    AppClassification c = run(d, n, NULL, 0, 0, 0);
    CHECK(c.format == APP_FMT_PEF && c.profile == APP_PROFILE_C_CLASSIC_PPC && c.confidence == APP_CONF_HIGH);
    CHECK(!c.runnable && strstr(c.missing, "PowerPC") && c.pef_valid && c.pef_lib_total == 2 && !strcmp(c.pef_libs[1], "MathLib"));
    c = run(d, n, NULL, 0, 0, 1);
    CHECK(c.runnable && c.missing[0] == 0);   /* recognition did not change; only availability did */
    const char *own[] = {"InterfaceLib", "MyPlugInLib"};
    n = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, own, 2);
    c = run(d, n, NULL, 0, 0, 1);
    CHECK(c.profile == APP_PROFILE_C_CLASSIC_PPC && c.confidence == APP_CONF_MEDIUM && c.pef_lib_unlisted == 1 && strstr(c.uncertainty, "unverified"));
    const char *carbon[] = {"CarbonLib"};
    n = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, carbon, 1);
    c = run(d, n, NULL, 0, 0, 1);
    CHECK(c.pef_needs_carbon && strstr(c.missing, "CarbonLib"));
    n = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 0, classic, 2);   /* no main symbol: a shared library */
    c = run(d, n, NULL, 0, 0, 1);
    CHECK(c.profile == APP_PROFILE_D_UNSUPPORTED && !c.is_application && strstr(c.reason, "shared library"));
    n = build_pef(d, sizeof d, FOURCC('m', '6', '8', 'k'), 1, classic, 2);   /* CFM-68K */
    c = run(d, n, NULL, 0, 0, 1);
    CHECK(c.profile == APP_PROFILE_D_UNSUPPORTED && (c.arch & APP_ARCH_PEF_M68K) && strstr(c.missing, "CFM-68K"));
    n = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, classic, 2);
    d[34 + 6] = 0; d[40 + 16 + 3] = 0xff;   /* section size beyond the file */
    c = run(d, n, NULL, 0, 0, 1);
    CHECK(c.format == APP_FMT_MALFORMED && c.profile == APP_PROFILE_D_UNSUPPORTED && !c.pef_valid);
}

static void macho_cases(void) {
    uint8_t d[64] = {0};
    p32(d, 0xfeedfaceu); p32(d + 4, 18);   /* ppc */
    AppClassification c = run(d, sizeof d, NULL, 0, 0, 1);
    CHECK(c.format == APP_FMT_MACHO && c.profile == APP_PROFILE_D_UNSUPPORTED && (c.arch & APP_ARCH_MACHO_PPC) && strstr(c.reason, "OS X"));
    memset(d, 0, sizeof d); p32(d, 0xcffaedfeu); d[4] = 7; d[5] = 0; d[6] = 0; d[7] = 1;   /* little-endian x86_64 */
    c = run(d, sizeof d, NULL, 0, 0, 1);
    CHECK(c.format == APP_FMT_MACHO && (c.arch & APP_ARCH_MACHO_X86) && !c.runnable && strstr(c.missing, "no x86"));
    memset(d, 0, sizeof d); p32(d, 0xcafebabeu); p32(d + 4, 2); p32(d + 8, 7); p32(d + 12, 3); p32(d + 16, 48); p32(d + 28, 0x01000007); p32(d + 36, 56);
    c = run(d, sizeof d, NULL, 0, 0, 1);   /* a real fat table */
    CHECK(c.format == APP_FMT_MACHO && (c.arch & APP_ARCH_MACHO_X86));
    memset(d, 0, sizeof d); p32(d, 0xcafebabeu); p32(d + 4, 0x34);   /* a Java class file: same magic */
    c = run(d, sizeof d, NULL, 0, 0, 1);
    CHECK(c.format == APP_FMT_NONE && c.arch == 0);
    uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75}, f[2048];
    size_t n = fork68k(f, sizeof f, 1, body);
    memset(d, 0, sizeof d); p32(d, 0xfeedfaceu); p32(d + 4, 7);
    c = run(d, sizeof d, f, n, 0, 0);   /* a Mach-O data fork wins over CODE: it is what the file is */
    CHECK(c.format == APP_FMT_MACHO && c.profile == APP_PROFILE_D_UNSUPPORTED);
}

static void fat_cases(void) {
    uint8_t f[2048], d[1024], cf[128];
    static const uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75};
    Res r[4] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0_good, sizeof code0_good}, {FOURCC('C', 'O', 'D', 'E'), 1, body, 4},
                {FOURCC('S', 'I', 'Z', 'E'), -1, size_clean, 10}, {FOURCC('c', 'f', 'r', 'g'), 0, cf, 0}};
    r[3].len = build_cfrg(cf, FOURCC('p', 'w', 'p', 'c'));
    size_t n = build_fork(f, sizeof f, r, 4);
    const char *classic[] = {"InterfaceLib"};
    size_t dn = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, classic, 1);
    AppClassification c = run(d, dn, f, n, 0, 0);   /* host cannot run PowerPC: the 68K path, not "whichever is newer" */
    CHECK(c.format == APP_FMT_FAT && c.profile == APP_PROFILE_B_CLEAN_68K && c.alternate == APP_PROFILE_C_CLASSIC_PPC && c.runnable);
    CHECK(c.has_cfrg && c.cfrg_members == 1 && c.cfrg_app_members == 1 && (c.arch & APP_ARCH_M68K) && (c.arch & APP_ARCH_PEF_PPC));
    c = run(d, dn, f, n, 0, 1);   /* host runs PowerPC and every import is classic: take it */
    CHECK(c.format == APP_FMT_FAT && c.profile == APP_PROFILE_C_CLASSIC_PPC && c.alternate == APP_PROFILE_B_CLEAN_68K);
    const char *own[] = {"InterfaceLib", "SomeoneElsesLib"};
    dn = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, own, 2);
    c = run(d, dn, f, n, 0, 1);   /* unverified imports: stay on the 68K path that is known to be implemented */
    CHECK(c.profile == APP_PROFILE_B_CLEAN_68K && c.alternate == APP_PROFILE_C_CLASSIC_PPC);
    d[40 + 16 + 3] = 0xff;   /* PEF damaged but CODE intact: the program is still a 68K application */
    c = run(d, dn, f, n, 0, 1);
    CHECK(c.format == APP_FMT_CODE68K && c.profile == APP_PROFILE_B_CLEAN_68K && strstr(c.uncertainty, "inconsistent"));
}

static void identity_cases(void) {
    uint8_t f1[2048], f2[2048], f3[2048];
    static const uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75}, other[4] = {0x4e, 0x75, 0x4e, 0x71};
    Res a[2] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0_good, sizeof code0_good}, {FOURCC('C', 'O', 'D', 'E'), 1, body, 4}};
    Res b[2] = {a[1], a[0]};   /* same resources, other map order */
    Res c3[2] = {a[0], {FOURCC('C', 'O', 'D', 'E'), 1, other, 4}};
    size_t n1 = build_fork(f1, sizeof f1, a, 2), n2 = build_fork(f2, sizeof f2, b, 2), n3 = build_fork(f3, sizeof f3, c3, 2);
    AppClassification x = run(NULL, 0, f1, n1, 0, 0), y = run(NULL, 0, f2, n2, 0, 0), z = run(NULL, 0, f3, n3, 0, 0);
    CHECK(x.identity && x.identity == y.identity);   /* map order does not matter */
    CHECK(x.identity != z.identity);                 /* one changed code byte does */
    Res d4[3] = {a[0], a[1], {FOURCC('S', 'T', 'R', ' '), 1, body, 4}};
    uint8_t f4[2048]; size_t n4 = build_fork(f4, sizeof f4, d4, 3);
    CHECK(run(NULL, 0, f4, n4, 0, 0).identity == x.identity);   /* non-executable resources (preferences, strings) do not */
}

static void container_and_json(void) {
    uint8_t f[2048], ad[2200];
    static const uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75};
    size_t n = fork68k(f, sizeof f, 1, body);
    memset(ad, 0, 38); p32(ad, 0x00051607); p32(ad + 4, 0x00020000); p16(ad + 24, 1);
    p32(ad + 26, 2); p32(ad + 30, 38); p32(ad + 34, (uint32_t)n);
    memcpy(ad + 38, f, n);
    AppClassification a = run(NULL, 0, ad, 38 + n, 0, 0), raw = run(NULL, 0, f, n, 0, 0);
    CHECK(a.profile == raw.profile && a.identity == raw.identity && a.format == APP_FMT_CODE68K);   /* AppleDouble wrapper */
    char json[4096];
    size_t need = app_classification_json(&raw, json, sizeof json);
    CHECK(need == strlen(json) && strstr(json, "\"profile\":\"B-clean-68k\"") && strstr(json, "\"demonstrated\":false"));
    CHECK(json[0] == '{' && json[need - 1] == '}');
    char tiny[16];
    CHECK(app_classification_json(&raw, tiny, sizeof tiny) == need && strlen(tiny) == sizeof tiny - 1);
    CHECK(app_classification_json(&raw, NULL, 0) == need);
    AppClassification q = raw;   /* hostile text must be escaped */
    snprintf(q.reason, sizeof q.reason, "quote \" backslash \\ newline \n tab \t");
    app_classification_json(&q, json, sizeof json);
    CHECK(strstr(json, "quote \\\" backslash \\\\ newline \\u000a tab \\u0009"));
    CHECK(!strcmp(app_profile_name(APP_PROFILE_C_CLASSIC_PPC), "C-classic-ppc") && !strcmp(app_format_name(APP_FMT_FAT), "fat-68k-pef"));
}

/* Every prefix and many single-byte corruptions of valid inputs: no crash, no read past the buffer (run under ASan to
 * prove the latter), and a coherent verdict. The buffers are exact-size heap blocks so a stray read is caught. */
static void coherent(const AppClassification *c) {
    CHECK(c->profile >= APP_PROFILE_A_LEGACY_68K && c->profile <= APP_PROFILE_D_UNSUPPORTED);
    CHECK(c->reason[0] != 0);
    CHECK(c->profile != APP_PROFILE_D_UNSUPPORTED || !c->runnable);
    CHECK(c->profile == APP_PROFILE_D_UNSUPPORTED || c->is_application);
}
static void malformed_input(void) {
    uint8_t f[2048], d[1024], cf[128];
    static const uint8_t body[4] = {0x4e, 0x75, 0x4e, 0x75};
    Res r[4] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0_good, sizeof code0_good}, {FOURCC('C', 'O', 'D', 'E'), 1, body, 4},
                {FOURCC('S', 'I', 'Z', 'E'), -1, size_clean, 10}, {FOURCC('c', 'f', 'r', 'g'), 0, cf, 0}};
    r[3].len = build_cfrg(cf, FOURCC('p', 'w', 'p', 'c'));
    size_t n = build_fork(f, sizeof f, r, 4);
    const char *libs[] = {"InterfaceLib", "CarbonLib"};
    size_t dn = build_pef(d, sizeof d, FOURCC('p', 'w', 'p', 'c'), 1, libs, 2);
    for (size_t cut = 0; cut <= n; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, f, cut);
        AppClassification c = run(NULL, 0, copy, cut, 0, 1);
        coherent(&c);
        free(copy);
    }
    for (size_t cut = 0; cut <= dn; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, d, cut);
        AppClassification c = run(copy, cut, NULL, 0, 0, 1);
        coherent(&c);
        free(copy);
    }
    uint32_t seed = 12345;
    for (int round = 0; round < 4000; round++) {
        uint8_t *fc = malloc(n), *dc = malloc(dn);
        memcpy(fc, f, n); memcpy(dc, d, dn);
        for (int k = 0; k < 3; k++) {
            seed = seed * 1664525u + 1013904223u;
            if (seed & 0x100) fc[(seed >> 9) % n] = (uint8_t)(seed >> 3); else dc[(seed >> 9) % dn] = (uint8_t)(seed >> 3);
        }
        AppClassification c = run(dc, dn, fc, n, 0, round & 1);
        coherent(&c);
        free(fc); free(dc);
    }
    AppClassification c = run(NULL, 0, NULL, 0, 0, 0);
    CHECK(c.format == APP_FMT_NONE && c.profile == APP_PROFILE_D_UNSUPPORTED);
    uint8_t junk[64]; memset(junk, 0xff, sizeof junk);
    c = run(junk, sizeof junk, junk, sizeof junk, 0, 1);
    coherent(&c);
    CHECK(app_classify(NULL, 0, NULL, 0, 0, NULL, NULL) == -1);
    CHECK(app_classify("Joy!peff", 8, NULL, 0, 0, NULL, &c) == 0 && c.profile == APP_PROFILE_D_UNSUPPORTED);
}

int main(void) {
    init_blobs();
    m68k_profiles(); m68k_rejections(); pef_cases(); macho_cases(); fat_cases(); identity_cases(); container_and_json(); malformed_input();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("app classify: all checks passed");
    return 0;
}
