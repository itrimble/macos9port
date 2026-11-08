#include "native_launch.h"
#include "runtime_bridge.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }

static int write_fork(const char *path, const char *type) {
    uint8_t b[326] = {0};
    put32(b, 256); put32(b + 4, 276); put32(b + 8, 20); put32(b + 12, 50);
    put32(b + 256, 16);
    put16(b + 276 + 24, 28); put16(b + 276 + 26, 50);
    memcpy(b + 306, type, 4); put16(b + 312, 10);
    put16(b + 316, 0xffff);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(b, 1, sizeof b, f) == sizeof b;
    return fclose(f) || !ok ? -1 : 0;
}

static void check_log(const char *message) {
    const char *p = strstr(message, "Log: ");
    CHECK(p != NULL);
    if (!p) return;
    struct stat st;
    CHECK(stat(p + 5, &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    CHECK(unlink(p + 5) == 0); /* This test owns this exact temporary log. */
}

static int helper(int argc, char **argv) {
    if (argc != 9 || strcmp(argv[1], "--window") || strcmp(argv[2], "--indexed") ||
        strcmp(argv[3], "--interactive") || strcmp(argv[4], "--bridge") ||
        strcmp(argv[6], "--saves")) return 30;
    const char *perf = getenv("OSPI_PERF_TRACE");
    if (!perf || strcmp(perf, "1")) return 32;
    const char *expected = getenv("TEST_FORK"), *saves = getenv("TEST_SAVES");
    if (!expected || strcmp(argv[8], expected)) return 31;
    if (!saves || strcmp(argv[7], saves)) return 34;
    RuntimeBridge *bridge = runtime_bridge_open(argv[5]);
    if (!bridge) return 33;
    runtime_bridge_set_state(bridge, RUNTIME_BRIDGE_CLOSED, "test child finished");
    runtime_bridge_close(bridge);
    const char *mode = getenv("TEST_MODE");
    if (mode && !strcmp(mode, "fail")) {
        fputs("Unsupported synthetic trap abcd\n", stderr);
        puts("instructions=12");
        return 3;
    }
    if (mode && !strcmp(mode, "late")) {
        struct timespec delay = {2, 0};
        nanosleep(&delay, NULL);
        return 3;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) return helper(argc, argv);
    char directory[] = "/tmp/ospi-native-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    /* The volume sits one level down so the launcher's "Saves" folder lands
     * beside it, inside this test's own temporary directory. */
    char root[PATH_MAX], saves[PATH_MAX], self[PATH_MAX], app[PATH_MAX], fork[PATH_MAX], canonical[PATH_MAX], err[384];
    snprintf(root, sizeof root, "%s/Volume", directory);
    CHECK(mkdir(root, 0700) == 0);
    CHECK(realpath(directory, canonical) != NULL);
    snprintf(saves, sizeof saves, "%s/Saves/Volume", canonical);
    setenv("TEST_SAVES", saves, 1);
    CHECK(realpath(argv[0], self) != NULL);
    CHECK(setenv("OSPI_RUNTIME_PATH", self, 1) == 0);
    unsetenv("OSPI_PERF_TRACE");
    snprintf(app, sizeof app, "%s/Odd ' ; $(touch should-not-exist)", root);
    snprintf(fork, sizeof fork, "%s.rsrc", app);
    CHECK(native_open(root, app, err, sizeof err) == 1);
    CHECK(write_fork(fork, "CODE") == 0);
    CHECK(realpath(fork, canonical) != NULL);
    setenv("TEST_FORK", canonical, 1);
    setenv("TEST_MODE", "fail", 1);
    CHECK(native_open(root, app, err, sizeof err) == -1);
    CHECK(strstr(err, "Unsupported synthetic trap abcd") != NULL);
    check_log(err);
    setenv("TEST_MODE", "quote", 1);
    CHECK(native_open(root, fork, err, sizeof err) == 0);
    check_log(err);
    native_surface_close();
    CHECK(access("should-not-exist", F_OK) != 0);
    CHECK(write_fork(fork, "STR ") == 0);
    CHECK(native_open(root, app, err, sizeof err) == 1);
    CHECK(write_fork(fork, "CODE") == 0);
    setenv("OSPI_RUNTIME_PATH", "/missing/ospi-runtime", 1);
    CHECK(native_open(root, app, err, sizeof err) == -1);
    CHECK(strstr(err, "runtime is missing") != NULL);
    setenv("OSPI_RUNTIME_PATH", self, 1);
    char link[PATH_MAX];
    snprintf(link, sizeof link, "%s/escape.rsrc", root);
    CHECK(symlink(self, link) == 0);
    CHECK(native_open(root, link, err, sizeof err) == -1);
    CHECK(strstr(err, "outside") != NULL);
    CHECK(unlink(link) == 0);
#if defined(__APPLE__)
    char native[PATH_MAX], named[PATH_MAX];
    snprintf(native, sizeof native, "%s/native-app", root);
    FILE *empty = fopen(native, "wb");
    CHECK(empty != NULL);
    if (empty) fclose(empty);
    snprintf(named, sizeof named, "%s/..namedfork/rsrc", native);
    CHECK(write_fork(named, "CODE") == 0);
    if (realpath(native, canonical)) {
        snprintf(named, sizeof named, "%s/..namedfork/rsrc", canonical);
        setenv("TEST_FORK", named, 1);
        CHECK(native_open(root, native, err, sizeof err) == 0);
        check_log(err);
    } else CHECK(0);
    CHECK(unlink(native) == 0);
    CHECK(realpath(fork, canonical) != NULL);
    setenv("TEST_FORK", canonical, 1);
#endif
    setenv("TEST_MODE", "late", 1);
    int late = native_open(root, app, err, sizeof err);
    if (late != 0) fprintf(stderr, "late launch: %s\n", err);
    CHECK(late == 0);
    struct timespec delay = {1, 300000000};
    nanosleep(&delay, NULL);
    check_log(err);
    native_surface_close();
    int status;
    errno = 0;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    CHECK(unlink(fork) == 0);
    CHECK(rmdir(root) == 0);
    /* The launcher created only the Saves parent; the runtime owns the leaf. */
    snprintf(saves, sizeof saves, "%s/Saves", directory);
    CHECK(rmdir(saves) == 0);
    CHECK(rmdir(directory) == 0);
    printf("Native launch checks: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
