#include "quake_data.h"
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
static int make_dirs(const char *home, char *dest, size_t n) {
    static const char *const parts[] = {"/Library", "/Application Support", "/OS999", "/Quake3", "/demoq3"};
    char dir[1100]; size_t len = (size_t)snprintf(dir, sizeof dir, "%s", home);
    if (len >= sizeof dir) return -1;
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
        int k = snprintf(dir + len, sizeof dir - len, "%s", parts[i]);
        if (k < 0 || (size_t)k >= sizeof dir - len) return -1;
        len += (size_t)k;
        if (mkdir(dir, 0700) && errno != EEXIST) return -1;
    }
    return snprintf(dest, n, "%s/pak0.pk3", dir) < (int)n ? 0 : -1;
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
    char dest[1200], part[1210];
    if (!home || !home[0] || make_dirs(home, dest, sizeof dest)) return -1;
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
