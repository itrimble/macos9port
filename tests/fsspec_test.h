/* Shared helpers for the FSSpec tests: guest stack frames for HighLevelFSDispatch ($AA52), specs, host files. */
#ifndef FSSPEC_TEST_H
#define FSSPEC_TEST_H
#include "mac_runtime.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
enum { RAM = 0x10000, S = 0xf000, SPEC = 0x1000, SPEC2 = 0x1100, NAMEBUF = 0x1200, NAME2 = 0x1300,
       REFBUF = 0x1400, INFO = 0x1500, PB = 0x1600, DATA = 0x1800, GUARD = 0x2000 };

static void w16(MacRuntime *rt, uint32_t a, uint32_t v) { CHECK(!mac_cpu_write(rt->cpu, a, 2, v)); }
static void w32(MacRuntime *rt, uint32_t a, uint32_t v) { CHECK(!mac_cpu_write(rt->cpu, a, 4, v)); }
static void pstr(MacRuntime *rt, uint32_t at, const char *s) {
    size_t n = strlen(s);
    mac_cpu_ram(rt->cpu)[at] = (uint8_t)n;
    memcpy(mac_cpu_ram(rt->cpu) + at + 1, s, n);
}

/* One HighLevelFSDispatch call: the stack words were prepared by the caller; returns the OSErr. */
static int dispatch(MacRuntime *rt, unsigned selector, unsigned args) {
    mac_cpu_set(rt->cpu, MAC_D0, 0x12340000u | selector); /* only the low word is the selector */
    mac_cpu_set(rt->cpu, MAC_A7, S);
    CHECK(mac_fsspec_service(rt, 0xaa52) == 1);
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == S + args);
    int result = (int16_t)mac_cpu_read(rt->cpu, S + args, 2);
    CHECK((int32_t)mac_cpu_get(rt->cpu, MAC_D0) == result);
    return result;
}
static int make_at(MacRuntime *rt, int vref, uint32_t dir, uint32_t name, uint32_t spec) {
    w32(rt, S, spec); w32(rt, S + 4, name); w32(rt, S + 8, dir); w16(rt, S + 12, (uint16_t)vref);
    return dispatch(rt, 1, 14);
}
static int make(MacRuntime *rt, int vref, uint32_t dir, const char *name, uint32_t spec) {
    pstr(rt, NAMEBUF, name);
    return make_at(rt, vref, dir, NAMEBUF, spec);
}
/* A valid spec for a file in the root, built by hand (as an app could). */
static void spec_of(MacRuntime *rt, uint32_t at, int vref, uint32_t parent, const char *name) {
    memset(mac_cpu_ram(rt->cpu) + at, 0, 70);
    w16(rt, at, (uint16_t)vref); w32(rt, at + 2, parent);
    pstr(rt, at + 6, name);
}
static int open_df(MacRuntime *rt, uint32_t spec, int perm) {
    w32(rt, S, REFBUF); mac_cpu_ram(rt->cpu)[S + 4] = (uint8_t)perm; mac_cpu_ram(rt->cpu)[S + 5] = 0xee;
    w32(rt, S + 6, spec);
    w16(rt, REFBUF, 0);
    int error = dispatch(rt, 2, 10);
    return error ? error : (int16_t)mac_cpu_read(rt->cpu, REFBUF, 2);
}
static int create(MacRuntime *rt, uint32_t spec, uint32_t creator, uint32_t type) {
    w16(rt, S, 0); w32(rt, S + 2, type); w32(rt, S + 6, creator); w32(rt, S + 10, spec);
    return dispatch(rt, 4, 14);
}
static int one_spec(MacRuntime *rt, unsigned selector, uint32_t spec) { w32(rt, S, spec); return dispatch(rt, selector, 4); }
static int finfo(MacRuntime *rt, unsigned selector, uint32_t spec, uint32_t info) {
    w32(rt, S, info); w32(rt, S + 4, spec); return dispatch(rt, selector, 8);
}
static int rename_to(MacRuntime *rt, uint32_t spec, uint32_t name) { w32(rt, S, name); w32(rt, S + 4, spec); return dispatch(rt, 11, 8); }

static int pb_call(MacRuntime *rt, uint16_t op, int ref, int mode, int32_t offset, uint32_t count) {
    memset(mac_cpu_ram(rt->cpu) + PB, 0, 128);
    w16(rt, PB + 24, (uint16_t)ref); w32(rt, PB + 32, DATA); w32(rt, PB + 36, count);
    w16(rt, PB + 44, (uint16_t)mode); w32(rt, PB + 46, (uint32_t)offset);
    mac_cpu_set(rt->cpu, MAC_A0, PB);
    CHECK(mac_file_service(rt, op) == 1);
    return (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
}

static long host_size(const char *dir, const char *name) {
    char path[1024]; struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return lstat(path, &st) ? -1 : (long)st.st_size;
}
static void host_write(const char *dir, const char *name, const char *bytes) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, bytes, strlen(bytes)) == (ssize_t)strlen(bytes) && close(fd) == 0);
}
static int spec_is(MacRuntime *rt, uint32_t at, int vref, uint32_t parent, const char *name) {
    const uint8_t *p = mac_cpu_ram(rt->cpu) + at;
    return (int16_t)mac_cpu_read(rt->cpu, at, 2) == vref && mac_cpu_read(rt->cpu, at + 2, 4) == parent &&
           p[6] == strlen(name) && !memcmp(p + 7, name, strlen(name));
}
static MacRuntime fresh(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(RAM);
    rt.heap_next = 0x800; rt.heap_limit = 0xe00; /* drive-queue records for the mounted volume */
    CHECK(rt.cpu != NULL);
    return rt;
}
/* Expect a halt whose message names the selector; then discard the runtime. */
static void expect_halt(MacRuntime *rt, unsigned selector, const char *needle) {
    mac_cpu_set(rt->cpu, MAC_D0, selector);
    mac_cpu_set(rt->cpu, MAC_A7, S);
    CHECK(mac_fsspec_service(rt, 0xaa52) == -1);
    CHECK(mac_cpu_halted(rt->cpu));
    CHECK(strstr(mac_cpu_error(rt->cpu), needle) != NULL);
}


#endif
