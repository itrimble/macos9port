/* Launch policy: argv per profile, refusals, saved overrides and their invalidation. Synthetic inputs only; headless. */
#include "launch_profile.h"
#include "native_policy.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define FOURCC(a, b, c, d) ((uint32_t)(uint8_t)(a) << 24 | (uint32_t)(uint8_t)(b) << 16 | (uint32_t)(uint8_t)(c) << 8 | (uint32_t)(uint8_t)(d))
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p16(p, v >> 16); p16(p + 2, v & 0xffff); }

typedef struct { uint32_t type; int id; const uint8_t *data; size_t len; } Res;
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

static const uint8_t code0[32] = {0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0x10};
static size_t fork68k(uint8_t *out, size_t cap, int flags, uint32_t preferred, uint8_t tag) {
    uint8_t size[10], body[4] = {0x4e, 0x75, 0x4e, tag};
    p16(size, (unsigned)flags); p32(size + 2, preferred); p32(size + 6, preferred);
    Res r[3] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0, sizeof code0}, {FOURCC('C', 'O', 'D', 'E'), 1, body, 4}, {FOURCC('S', 'I', 'Z', 'E'), -1, size, 10}};
    return build_fork(out, cap, r, flags < 0 ? 2 : 3);
}

static AppClassification classify68k(int flags, uint32_t preferred) {
    uint8_t f[1024];
    size_t n = fork68k(f, sizeof f, flags, preferred, 0x75);
    AppClassification c; AppClassifyCaps caps = {0, 0};
    app_classify(NULL, 0, f, n, 0, &caps, &c);
    return c;
}

static int has_arg(const LaunchPolicy *p, const char *a, const char *v) {
    for (int i = 0; i + 1 < p->nargs; i++) if (!strcmp(p->args[i], a) && !strcmp(p->args[i + 1], v)) return 1;
    return 0;
}

static void argv_per_profile(void) {
    LaunchPolicy p;
    LaunchSession def = {0, 0}, s32 = {32, 256}, s32big = {32, 512};
    AppClassification b = classify68k(0x58e0, 0x200000), a = classify68k(0x5860, 0x200000), none = classify68k(-1, 0);
    launch_policy_decide(&b, NULL, &def, &p);   /* a classic 32-bit-clean 68K app on the default machine: unchanged argv */
    CHECK(!p.refuse && p.profile == APP_PROFILE_B_CLEAN_68K && p.nargs == 0 && p.addr == 0 && !strcmp(p.source, "classifier"));
    launch_policy_decide(&b, NULL, &s32, &p);   /* and on a 32-bit session machine: also unchanged */
    CHECK(!p.refuse && p.nargs == 0);
    launch_policy_decide(&a, NULL, &def, &p);   /* legacy program on the default machine: unchanged */
    CHECK(p.profile == APP_PROFILE_A_LEGACY_68K && p.nargs == 0);
    launch_policy_decide(&a, NULL, &s32, &p);   /* legacy program, session set to 32-bit: ask for 24-bit for this launch only */
    CHECK(p.nargs == 2 && has_arg(&p, "--addressing", "24") && strstr(p.reason, "session default"));
    launch_policy_decide(&none, NULL, &def, &p);   /* no SIZE: legacy, conservative */
    CHECK(p.profile == APP_PROFILE_A_LEGACY_68K && p.nargs == 0);
    AppClassification big = classify68k(0x58e0, 20u * 1024 * 1024);
    launch_policy_decide(&big, NULL, &def, &p);   /* large partition: needs the 32-bit machine */
    CHECK(p.nargs == 4 && has_arg(&p, "--addressing", "32") && has_arg(&p, "--ram-mb", "256"));
    launch_policy_decide(&big, NULL, &s32big, &p);   /* session already satisfies it */
    CHECK(p.nargs == 0);
    AppClassification huge = classify68k(0x58e0, 300u * 1024 * 1024);
    launch_policy_decide(&huge, NULL, &def, &p);
    CHECK(has_arg(&p, "--ram-mb", "332"));
}

static void refusals(void) {
    LaunchPolicy p; LaunchSession def = {0, 0};
    uint8_t macho[64] = {0};
    p32(macho, 0xfeedfaceu); p32(macho + 4, 7);
    AppClassification c; AppClassifyCaps caps = {0, 0};
    app_classify(macho, sizeof macho, NULL, 0, 0, &caps, &c);
    launch_policy_decide(&c, NULL, &def, &p);
    CHECK(p.refuse && p.nargs == 0 && strstr(p.message, "Mach-O") && strstr(p.message, "does not start it in a different mode"));
    /* PowerPC: refused on this build with the missing requirement named, runnable once the host says so */
    uint8_t pef[200] = {0};
    memcpy(pef, "Joy!peffpwpc", 12); p32(pef + 12, 1); p16(pef + 32, 1); p16(pef + 34, 1);
    p32(pef + 40 + 16, 56); p32(pef + 40 + 20, 68); pef[40 + 24] = 4; p32(pef + 68, 0); p32(pef + 68 + 40, 56);
    app_classify(pef, 68 + 56, NULL, 0, 0, &caps, &c);
    launch_policy_decide(&c, NULL, &def, &p);
    CHECK(c.profile == APP_PROFILE_C_CLASSIC_PPC && p.refuse && strstr(p.message, "PowerPC interpreter"));
    caps.ppc_available = 1;
    app_classify(pef, 68 + 56, NULL, 0, 0, &caps, &c);
    launch_policy_decide(&c, NULL, &def, &p);
    CHECK(!p.refuse && p.profile == APP_PROFILE_C_CLASSIC_PPC && has_arg(&p, "--addressing", "32") && has_arg(&p, "--ram-mb", "256"));
}

static char root[256];
static void make_root(void) {
    const char *t = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/lp-test-XXXXXX", t && *t ? t : "/tmp");
    if (!mkdtemp(root)) { perror("mkdtemp"); exit(2); }
}
static void write_file(const char *name, const void *bytes, size_t n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(bytes, 1, n, f); fclose(f); }
}

static void overrides(void) {
    LaunchOverride o = {0x1111, 0xaaaa, APP_PROFILE_B_CLEAN_68K, 32, 128}, got;
    uint64_t stale = 0;
    CHECK(launch_override_find(root, 0x1111, 0xaaaa, &got, &stale) == 0);
    CHECK(launch_override_set(root, &o) == 0);
    CHECK(launch_override_find(root, 0x1111, 0xaaaa, &got, &stale) == 1 && got.profile == APP_PROFILE_B_CLEAN_68K && got.addr == 32 && got.ram_mb == 128);
    CHECK(launch_override_find(root, 0x1111, 0xbbbb, &got, &stale) == 1);   /* moved or renamed: identity still finds it */
    LaunchOverride o2 = {0x2222, 0xbbbb, APP_PROFILE_A_LEGACY_68K, 0, 0};
    CHECK(launch_override_set(root, &o2) == 0);
    CHECK(launch_override_find(root, 0x2222, 0xbbbb, &got, &stale) == 1 && got.profile == APP_PROFILE_A_LEGACY_68K);
    CHECK(launch_override_find(root, 0x1111, 0xaaaa, &got, &stale) == 1);   /* both coexist */
    CHECK(launch_override_find(root, 0x3333, 0xaaaa, &got, &stale) == 2 && stale == 0x1111);   /* same place, new executable */
    CHECK(launch_override_find(root, 0x1111, 0xaaaa, &got, &stale) == 0);   /* the stale entry is gone for good */
    CHECK(launch_override_clear(root, 0x2222) == 0 && launch_override_find(root, 0x2222, 0xbbbb, &got, &stale) == 0);
    LaunchOverride bad = {1, 2, APP_PROFILE_D_UNSUPPORTED, 0, 0};
    CHECK(launch_override_set(root, &bad) == -1);
    bad = (LaunchOverride){1, 2, APP_PROFILE_B_CLEAN_68K, 24, 64};
    CHECK(launch_override_set(root, &bad) == -1);   /* RAM only goes with 32-bit */
    /* the store is in the private folder, not beside the user's files */
    char path[512], old[512];
    snprintf(path, sizeof path, "%s/.os999/launch-profiles", root);
    snprintf(old, sizeof old, "%s/.os999-launch-profiles", root);
    CHECK(access(path, F_OK) == 0 && access(old, F_OK) != 0);
    write_file(".os999/launch-profiles", "garbage\nv1 zz\nv2 0000000000000001 0000000000000002 B 0 0\nv1 0000000000000007 0000000000000008 B 32 64\nv1 0000000000000009 000000000000000a Z 0 0\n", 140);
    CHECK(launch_override_find(root, 7, 8, &got, &stale) == 1 && got.ram_mb == 64);   /* damaged lines dropped, good one kept */
    CHECK(launch_override_find(root, 9, 10, &got, &stale) == 0);
    unlink(path);
    char target[512];
    snprintf(target, sizeof target, "%s/elsewhere", root);
    write_file("elsewhere", "v1 0000000000000007 0000000000000008 B 32 64\n", 47);
    CHECK(symlink(target, path) == 0);
    CHECK(launch_override_find(root, 7, 8, &got, &stale) == -1);   /* a symlinked store is never followed */
    unlink(path);
    /* an old-location file is read once, copied into the private folder and removed */
    write_file(".os999-launch-profiles", "v1 000000000000000b 000000000000000c A 0 0\nnonsense\n", 52);
    CHECK(launch_override_find(root, 0xb, 0xc, &got, &stale) == 1 && got.profile == APP_PROFILE_A_LEGACY_68K);
    CHECK(access(old, F_OK) != 0 && access(path, F_OK) == 0);
    CHECK(launch_override_find(root, 0xb, 0xc, &got, &stale) == 1);   /* now served from the new place */
    /* when both exist the private folder wins and the old file is left alone */
    write_file(".os999-launch-profiles", "v1 000000000000000d 000000000000000e B 0 0\n", 43);
    CHECK(launch_override_find(root, 0xd, 0xe, &got, &stale) == 0 && access(old, F_OK) == 0);
    unlink(old);
    /* a symlinked old file is not followed and nothing is migrated from it */
    unlink(path);
    CHECK(symlink(target, old) == 0);
    CHECK(launch_override_find(root, 7, 8, &got, &stale) == -1);
    unlink(old);
    CHECK(launch_override_find("", 7, 8, &got, &stale) == -1 && launch_override_set(NULL, &o) == -1);
}

static void prepare(void) {
    NativeSettings st = {"8", 1, 1, "", 0};
    LaunchPolicy p; char line[1024], err[512] = "";
    uint8_t f[1024], plain[16] = "just a document";
    size_t n = fork68k(f, sizeof f, 0x58e0, 0x200000, 0x75);
    write_file("Game.rsrc", f, n);
    CHECK(native_policy_prepare(root, "x", &st, &p, line, sizeof line, err, sizeof err) == 1);   /* missing file: not ours */
    char full[512];
    snprintf(full, sizeof full, "%s/Game", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0 && p.nargs == 0);   /* default argv unchanged */
    CHECK(strstr(line, "profile=B-clean-68k") && strstr(line, "source=classifier") && strstr(line, "args=[]"));
    snprintf(full, sizeof full, "%s/Game.rsrc", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);   /* the sidecar itself, same result */
    write_file("Doc", plain, sizeof plain);
    snprintf(full, sizeof full, "%s/Doc", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 1);   /* plain data: continue as before */
    uint8_t macho[64] = {0}; p32(macho, 0xfeedfaceu); p32(macho + 4, 18);
    write_file("OSXApp", macho, sizeof macho);
    snprintf(full, sizeof full, "%s/OSXApp", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == -1 && strstr(err, "Mach-O") && strstr(line, "REFUSED"));
    /* a saved override for this executable is applied and logged */
    AppClassification c; AppClassifyCaps caps = {0, 0};
    app_classify(NULL, 0, f, n, 0, &caps, &c);
    char rel[64] = "/Game";
    LaunchOverride o = {c.identity, launch_slot_key(rel), APP_PROFILE_B_CLEAN_68K, 32, 192};
    CHECK(launch_override_set(root, &o) == 0);
    snprintf(full, sizeof full, "%s/Game", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(strstr(line, "source=override") && p.nargs == 4 && has_arg(&p, "--ram-mb", "192"));
    /* the executable changes: the override is dropped, the launch is back to the profile, and the log says why */
    n = fork68k(f, sizeof f, 0x58e0, 0x200000, 0x71);
    write_file("Game.rsrc", f, n);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(strstr(line, "source=classifier") && p.nargs == 0 && strstr(line, "the executable at this place changed"));
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0 && !strstr(line, "changed"));   /* reported once */
    /* session default 32-bit with a legacy program: this launch asks for 24-bit; the next one is independent */
    n = fork68k(f, sizeof f, 0x5860, 0x200000, 0x75);
    write_file("Old.rsrc", f, n);
    NativeSettings s32 = {"8", 1, 1, "256", 32};
    snprintf(full, sizeof full, "%s/Old", root);
    CHECK(native_policy_prepare(root, full, &s32, &p, line, sizeof line, err, sizeof err) == 0 && has_arg(&p, "--addressing", "24"));
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0 && p.nargs == 0);
    /* a CODE 0 header the classifier does not accept is left to the runtime loader, exactly as before this layer existed */
    uint8_t odd[32]; memcpy(odd, code0, sizeof odd); p32(odd + 8, 7);
    Res oddres[1] = {{FOURCC('C', 'O', 'D', 'E'), 0, odd, sizeof odd}};
    n = build_fork(f, sizeof f, oddres, 1);
    write_file("Odd.rsrc", f, n);
    snprintf(full, sizeof full, "%s/Odd", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0 && p.nargs == 0 && strstr(line, "runtime-validated"));
}

/* A PEF container with one loader section listing `libs` (same layout the classifier reads). */
static size_t build_pef(uint8_t *out, size_t cap, const char *const *libs, size_t nlibs) {
    size_t strings = 0;
    for (size_t i = 0; i < nlibs; i++) strings += strlen(libs[i]) + 1;
    size_t loader = 56 + 24 * nlibs + strings, start = 40 + 28, total = start + loader;
    if (total > cap) return 0;
    memset(out, 0, total);
    memcpy(out, "Joy!peffpwpc", 12); p32(out + 12, 1); p16(out + 32, 1); p16(out + 34, 1);
    uint8_t *s = out + 40;
    p32(s + 16, (uint32_t)loader); p32(s + 20, (uint32_t)start); s[24] = 4;
    uint8_t *l = out + start;
    p32(l, 0); p32(l + 24, (uint32_t)nlibs); p32(l + 40, (uint32_t)(56 + 24 * nlibs));
    size_t off = 0;
    for (size_t i = 0; i < nlibs; i++) {
        p32(l + 56 + 24 * i, (uint32_t)off);
        memcpy(l + 56 + 24 * nlibs + off, libs[i], strlen(libs[i]) + 1);
        off += strlen(libs[i]) + 1;
    }
    return total;
}

/* Profile C through the real launch path: PowerPC fragments launch (the runtime has the PowerPC path), a CarbonLib
 * program or one the runtime could not locate a fragment for is refused with the requirement named, and a fat program
 * stays on its 68K half because the runtime keeps programs with CODE 0 on the 68K path. */
static void powerpc_launch(void) {
    NativeSettings st = {"8", 1, 1, "", 0};
    LaunchPolicy p; char line[1024], err[512] = "", full[512];
    uint8_t pef[512], cf[92] = {0}, f[2048];
    const char *classic[] = {"InterfaceLib", "MathLib"}, *carbon[] = {"InterfaceLib", "CarbonLib"};
    memset(cf, 0, sizeof cf);
    p16(cf + 30, 1); p32(cf + 32, FOURCC('p', 'w', 'p', 'c')); cf[32 + 22] = 1; cf[32 + 23] = 1; p16(cf + 32 + 40, 60);
    cf[32 + 42] = 4; memcpy(cf + 32 + 43, "Test", 4);
    Res cfrg[1] = {{FOURCC('c', 'f', 'r', 'g'), 0, cf, sizeof cf}};
    size_t rn = build_fork(f, sizeof f, cfrg, 1), pn = build_pef(pef, sizeof pef, classic, 2);
    write_file("PPCApp", pef, pn);
    write_file("PPCApp.rsrc", f, rn);
    snprintf(full, sizeof full, "%s/PPCApp", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(p.profile == APP_PROFILE_C_CLASSIC_PPC && has_arg(&p, "--addressing", "32") && has_arg(&p, "--ram-mb", "256") && strstr(line, "profile=C-classic-ppc"));
    /* CarbonLib: still refused, naming what is missing, with no fallback */
    pn = build_pef(pef, sizeof pef, carbon, 2);
    write_file("CarbonApp", pef, pn);
    write_file("CarbonApp.rsrc", f, rn);
    snprintf(full, sizeof full, "%s/CarbonApp", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == -1 && strstr(err, "CarbonLib") && strstr(line, "REFUSED"));
    CHECK(!strstr(err, "PowerPC interpreter"));   /* the interpreter is no longer what is missing */
    /* a bare PEF with no cfrg resource: the runtime has no member to load, so the refusal says so */
    pn = build_pef(pef, sizeof pef, classic, 2);
    write_file("BarePEF", pef, pn);
    Res other[1] = {{FOURCC('t', 'e', 'x', 't'), 128, pef, 4}};
    rn = build_fork(f, sizeof f, other, 1);
    write_file("BarePEF.rsrc", f, rn);
    snprintf(full, sizeof full, "%s/BarePEF", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == -1 && strstr(err, "cfrg"));
    /* fat: CODE 0 + cfrg + PEF takes the 68K half; no addressing arguments for a 24-bit legacy fat program */
    Res fat[4] = {{FOURCC('C', 'O', 'D', 'E'), 0, code0, sizeof code0}, {FOURCC('C', 'O', 'D', 'E'), 1, (const uint8_t *)"\x4e\x75\x4e\x75", 4},
                  {FOURCC('S', 'I', 'Z', 'E'), -1, (const uint8_t *)"\x58\xe0\0\x20\0\0\0\x20\0\0", 10}, {FOURCC('c', 'f', 'r', 'g'), 0, cf, sizeof cf}};
    rn = build_fork(f, sizeof f, fat, 4);
    write_file("FatApp", pef, pn);
    write_file("FatApp.rsrc", f, rn);
    snprintf(full, sizeof full, "%s/FatApp", root);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(p.profile == APP_PROFILE_B_CLEAN_68K || p.profile == APP_PROFILE_A_LEGACY_68K);
}

int main(void) {
    make_root();
    argv_per_profile(); refusals(); overrides(); prepare(); powerpc_launch();
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd)) fails++;
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("launch profile: all checks passed");
    return 0;
}
