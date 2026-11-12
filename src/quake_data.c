#include "quake_data.h"
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

/* Original Quake III Arena 1.11 demo pak0.pk3. */
#define DEMO_SIZE ((off_t)46853694)
#define DEMO_SHA "e77abad2466f45a0a7ea018445528f9b95a0fe7789fa1abc1a7718bbf0754b08"

int quake_data_check(const char *path, off_t size, const char *sha256_hex) {
#ifdef __APPLE__
    if (!path || !path[0] || !sha256_hex) return 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size != size) { close(fd); return 0; }
    CC_SHA256_CTX ctx; CC_SHA256_Init(&ctx);
    unsigned char buf[16384], digest[CC_SHA256_DIGEST_LENGTH];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) CC_SHA256_Update(&ctx, buf, (CC_LONG)n);
    close(fd);
    if (n < 0) return 0;
    CC_SHA256_Final(digest, &ctx);
    char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return !strcmp(hex, sha256_hex);
#else
    (void)path; (void)size; (void)sha256_hex;
    return 0;
#endif
}

int quake_data_demo_ok(const char *path) { return quake_data_check(path, DEMO_SIZE, DEMO_SHA); }

#ifdef __APPLE__
/* <home>/Library/Application Support/OS999/<root>/<game>; dir receives the path. */
static int make_dirs(const char *home, const char *root, const char *game, char *dir, size_t n) {
    char sub[2][80];
    if (snprintf(sub[0], sizeof sub[0], "/%s", root) >= (int)sizeof sub[0] ||
        snprintf(sub[1], sizeof sub[1], "/%s", game) >= (int)sizeof sub[1]) return -1;
    const char *const parts[] = {"/Library", "/Application Support", "/OS999", sub[0], sub[1]};
    size_t len = (size_t)snprintf(dir, n, "%s", home);
    if (len >= n) return -1;
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
        int k = snprintf(dir + len, n - len, "%s", parts[i]);
        if (k < 0 || (size_t)k >= n - len) return -1;
        len += (size_t)k;
        if (mkdir(dir, 0700) && errno != EEXIST) return -1;
    }
    return 0;
}

static int copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY | O_NOFOLLOW), out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (in < 0 || out < 0) { if (in >= 0) close(in); if (out >= 0) close(out); return -1; }
    static char buf[1 << 20];
    ssize_t n; int bad = 0;
    while (!bad && (n = read(in, buf, sizeof buf)) > 0) {
        for (ssize_t done = 0; done < n;) {
            ssize_t w = write(out, buf + done, (size_t)(n - done));
            if (w <= 0) { bad = 1; break; }
            done += w;
        }
    }
    if (!bad && n < 0) bad = 1;
    close(in);
    if (close(out)) bad = 1;
    return bad ? -1 : 0;
}
#endif

int quake_data_import(const char *home, const char *const dirs[], int ndirs, off_t size, const char *sha256_hex) {
#ifdef __APPLE__
    char dir[1100], dest[1200], part[1210];
    if (!home || !home[0] || make_dirs(home, "Quake3", "demoq3", dir, sizeof dir) ||
        snprintf(dest, sizeof dest, "%s/pak0.pk3", dir) >= (int)sizeof dest) return -1;
    if (quake_data_check(dest, size, sha256_hex)) return 1;
    snprintf(part, sizeof part, "%s.part", dest);
    static const char *const sub[] = {"", "/demoq3"};
    for (int i = 0; i < ndirs; i++) {
        if (!dirs[i] || !dirs[i][0]) continue;
        for (int j = 0; j < 2; j++) {
            char src[1200];
            if (snprintf(src, sizeof src, "%s%s/pak0.pk3", dirs[i], sub[j]) >= (int)sizeof src) continue;
            if (!quake_data_check(src, size, sha256_hex)) continue;
            /* Copy beside the destination, re-verify, then rename: a half copy is never used. */
            if (copy_file(src, part) || !quake_data_check(part, size, sha256_hex)) { unlink(part); return -1; }
            if (rename(part, dest)) { unlink(part); return -1; }
            return 0;
        }
    }
#else
    (void)home; (void)dirs; (void)ndirs; (void)size; (void)sha256_hex;
#endif
    return -1;
}

int quake_data_import_demo(const char *home, const char *volume) {
    char dl[1100], docs[1100], hdl[1100], desk[1100];
    snprintf(dl, sizeof dl, "%s/Downloads", volume ? volume : "");
    snprintf(docs, sizeof docs, "%s/Documents", volume ? volume : "");
    snprintf(hdl, sizeof hdl, "%s/Downloads", home ? home : "");
    snprintf(desk, sizeof desk, "%s/Desktop", home ? home : "");
    /* One-time carry-over from builds that kept the data in the Mac's Library. */
    char old[1100]; const char *mac = getenv("HOME");
    snprintf(old, sizeof old, "%s/Library/Application Support/OS999/Quake3/demoq3", mac ? mac : "");
    const char *const dirs[] = {dl, docs, volume, hdl, desk, old};
    return quake_data_import(home, dirs, 6, DEMO_SIZE, DEMO_SHA);
}

/* Retail baseq3 point-release set, measured from the user-supplied files. The same size + SHA-256 rule as the
 * demo applies to every file; a partial or different set is never installed or launched. */
static const QuakePak RETAIL[] = {
    {"pak0.pk3", 479493658, "7ce8b3910620cd50a09e4f1100f426e8c6180f68895d589f80e6bd95af54bcae"},
    {"pak1.pk3", 374405, "d4ffd60b4b414c3419499e321b6f5c2e933cf082df85823ad2d6ae2f803e1682"},
    {"pak2.pk3", 7511182, "ccae938a2f13a03b24902d675181d516a431699701ed88023a307f34b5bcd58c"},
    {"pak3.pk3", 276305, "d03c0a0e06b99f9ecca2be7389f57faed406e85f7c09b9c56afdfa53ba25e312"},
    {"pak4.pk3", 9600350, "af5f6d5c82fe4440ae0bb660f0648d1fa1731a9e8305a9eb652aa243428697f1"},
    {"pak5.pk3", 191872, "69f87070ca7719e252a3ba97e6483f6663939c987ede550d1268d4d9a07b45bc"},
    {"pak6.pk3", 7346884, "bb4f0ae2bf603b050fb665436d3178ce7c1c20360e67bacf7c14d93daff38daf"},
    {"pak7.pk3", 320873, "de6283ce23e3486a2622c5dbf73d3721a59f24debd380e90f43a97d952fea283"},
    {"pak8.pk3", 454478, "812c9e97f231e89cefede3848c6110b7bd34245093af6f22c2cacde3e6b15663"},
};

const QuakePak *quake_data_retail_table(int *count) {
    if (count) *count = (int)(sizeof RETAIL / sizeof *RETAIL);
    return RETAIL;
}

/* 1 when every pak in the table verifies as <dir>/<name> (dir holds the pk3 files themselves). */
static int set_ok_in(const char *dir, const QuakePak *t, int n) {
    for (int i = 0; i < n; i++) {
        char p[1200];
        if (snprintf(p, sizeof p, "%s/%s", dir, t[i].name) >= (int)sizeof p) return 0;
        if (!quake_data_check(p, t[i].size, t[i].sha256)) return 0;
    }
    return n > 0;
}

int quake_data_retail_set_ok(const char *datapath, const QuakePak *t, int n) {
    char dir[1200];
    if (!datapath || !datapath[0] || snprintf(dir, sizeof dir, "%s/baseq3", datapath) >= (int)sizeof dir) return 0;
    return set_ok_in(dir, t, n);
}

int quake_data_retail_ok(const char *datapath) {
    int n; const QuakePak *t = quake_data_retail_table(&n);
    return quake_data_retail_set_ok(datapath, t, n);
}

#ifdef __APPLE__
/* Look for a complete verified set in dir, dir/baseq3, and the same under each immediate subfolder
 * (so an extracted "Quake III Arena (Complete)/baseq3" folder works). Writes the folder holding the paks. */
static int find_set(const char *dir, const QuakePak *t, int n, char *found, size_t fn) {
    char c[1200];
    if (!dir || !dir[0]) return 0;
    if (snprintf(c, sizeof c, "%s/baseq3", dir) < (int)sizeof c && set_ok_in(c, t, n)) goto hit;
    if (set_ok_in(dir, t, n)) { snprintf(c, sizeof c, "%s", dir); goto hit; }
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    for (int seen = 0; seen < 64 && (e = readdir(d)); ) {
        if (e->d_name[0] == '.' || e->d_type == DT_LNK || (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN)) continue;
        seen++;
        char sub[1200];
        if (snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name) >= (int)sizeof sub) continue;
        char b[1300];
        if (snprintf(b, sizeof b, "%s/baseq3", sub) < (int)sizeof b && set_ok_in(b, t, n)) { snprintf(c, sizeof c, "%s", b); closedir(d); goto hit; }
        if (set_ok_in(sub, t, n)) { snprintf(c, sizeof c, "%s", sub); closedir(d); goto hit; }
    }
    closedir(d);
    return 0;
hit:
    return snprintf(found, fn, "%s", c) < (int)fn;
}
#endif

int quake_data_import_set(const char *home, const char *const dirs[], int ndirs, const QuakePak *t, int n) {
#ifdef __APPLE__
    char dest[1100], src[1200];
    if (!home || !home[0] || n <= 0 || make_dirs(home, "Quake3-Full", "baseq3", dest, sizeof dest)) return -1;
    if (set_ok_in(dest, t, n)) return 1;
    for (int i = 0; i < ndirs; i++) {
        if (!find_set(dirs[i], t, n, src, sizeof src)) continue;
        for (int k = 0; k < n; k++) {
            char from[1300], to[1300], part[1310];
            snprintf(from, sizeof from, "%s/%s", src, t[k].name);
            snprintf(to, sizeof to, "%s/%s", dest, t[k].name);
            if (quake_data_check(to, t[k].size, t[k].sha256)) continue;
            snprintf(part, sizeof part, "%s.part", to);
            if (copy_file(from, part) || !quake_data_check(part, t[k].size, t[k].sha256)) { unlink(part); return -1; }
            if (rename(part, to)) { unlink(part); return -1; }
        }
        return 0;
    }
#else
    (void)home; (void)dirs; (void)ndirs; (void)t; (void)n;
#endif
    return -1;
}

int quake_data_import_retail(const char *home, const char *volume) {
    char dl[1100], docs[1100], hdl[1100], desk[1100];
    snprintf(dl, sizeof dl, "%s/Downloads", volume ? volume : "");
    snprintf(docs, sizeof docs, "%s/Documents", volume ? volume : "");
    snprintf(hdl, sizeof hdl, "%s/Downloads", home ? home : "");
    snprintf(desk, sizeof desk, "%s/Desktop", home ? home : "");
    const char *env = getenv("OS999_QUAKE3_IMPORT"); /* a folder the user points at */
    const char *const dirs[] = {env && env[0] ? env : "", dl, docs, volume, hdl, desk};
    int n; const QuakePak *t = quake_data_retail_table(&n);
    return quake_data_import_set(home, dirs, 6, t, n);
}
