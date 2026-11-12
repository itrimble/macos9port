/* A stub that puts its jump-table entry back to the unloaded form and re-enters it: LoadSeg halts by name instead of spinning. A normal second
 * segment still loads once and runs. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#include "fork_builder.h"
static uint8_t bytes[FB_BYTES];
static size_t app(const uint8_t *code1, size_t n1, int two) {
    static FbFork f; memset(&f, 0, sizeof f);
    uint8_t zero[32] = {0}, seg[64] = {0, 0, 0, 1}, seg2[8] = {0, 8, 0, 1, 0xa9, 0xf4};
    fb_put32(zero, 0x100); fb_put32(zero + 4, 0x100); fb_put32(zero + 8, two ? 16 : 8); fb_put32(zero + 12, 32);
    fb_put16(zero + 16, 0); fb_put16(zero + 18, 0x3f3c); fb_put16(zero + 20, 1); fb_put16(zero + 22, 0xa9f0);
    fb_put16(zero + 24, 0); fb_put16(zero + 26, 0x3f3c); fb_put16(zero + 28, 2); fb_put16(zero + 30, 0xa9f0);
    memcpy(seg + 4, code1, n1);
    fb_add(&f, 0x434f4445, 0, NULL, zero, two ? 32 : 24); fb_add(&f, 0x434f4445, 1, NULL, seg, 4 + n1);
    if (two) fb_add(&f, 0x434f4445, 2, NULL, seg2, 6);
    return fb_build(&f, bytes);
}
int main(void) {
    static const uint8_t loop[] = { 0x41, 0xed, 0x00, 0x20, 0x30, 0xfc, 0x00, 0x00, 0x30, 0xfc, 0x3f, 0x3c, 0x30, 0xfc, 0x00, 0x01, 0x30, 0xbc, 0xa9, 0xf0, 0x4e, 0xed, 0x00, 0x22 };
    static const uint8_t chain[] = { 0x4e, 0xed, 0x00, 0x2a };   /* JMP 42(A5): the second entry, still unloaded */
    char err[200]; MacResourceFork fork; MacRuntime rt;
    size_t n = app(loop, sizeof loop, 0);
    CHECK(mac_resource_open(&fork, bytes, n, err, sizeof err) == 0 && mac_runtime_init(&rt, &fork, err, sizeof err) == 0);
    mac_runtime_run(&rt, 100000);
    CHECK(mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "re-entered") && strstr(mac_cpu_error(rt.cpu), "CODE 1") && rt.traps < 200 && !rt.exited);
    mac_runtime_destroy(&rt);
    n = app(chain, sizeof chain, 1);
    CHECK(mac_resource_open(&fork, bytes, n, err, sizeof err) == 0 && mac_runtime_init(&rt, &fork, err, sizeof err) == 0);
    mac_runtime_run(&rt, 100000);
    CHECK(rt.exited && rt.traps == 2);                       /* LoadSeg(2), then the second segment's ExitToShell */
    mac_runtime_destroy(&rt);
    if (!fails) puts("LoadSeg re-entry checks passed");
    return fails ? 1 : 0;
}
