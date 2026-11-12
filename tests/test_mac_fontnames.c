/* GetFNum, GetFontName and RealFont against a hand-built fork with one FOND and one FONT name holder. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#include "fork_builder.h"
int mac_fontnames_service(MacRuntime *rt, uint16_t opcode);
enum { SP = 0x9000, NAME = 0x5000, OUT = 0x6000 };
static uint8_t bytes[FB_BYTES];
static void pstr(MacRuntime *r, uint32_t at, const char *s) { mac_cpu_ram(r->cpu)[at] = (uint8_t)strlen(s); memcpy(mac_cpu_ram(r->cpu) + at + 1, s, strlen(s)); }
static int fnum(MacRuntime *r, const char *name) {
    pstr(r, NAME, name); mac_cpu_set(r->cpu, MAC_A7, SP); mac_cpu_write(r->cpu, SP, 4, OUT); mac_cpu_write(r->cpu, SP + 4, 4, NAME); mac_cpu_write(r->cpu, OUT, 2, 0x7777);
    CHECK(mac_fontnames_service(r, 0xa900) == 1 && mac_cpu_get(r->cpu, MAC_A7) == SP + 8);
    return (int16_t)mac_cpu_read(r->cpu, OUT, 2);
}
static const char *fname(MacRuntime *r, int family) {
    static char out[256];
    memset(mac_cpu_ram(r->cpu) + OUT, 0x5a, 256); mac_cpu_set(r->cpu, MAC_A7, SP); mac_cpu_write(r->cpu, SP, 4, OUT); mac_cpu_write(r->cpu, SP + 4, 2, (uint16_t)family);
    CHECK(mac_fontnames_service(r, 0xa8ff) == 1 && mac_cpu_get(r->cpu, MAC_A7) == SP + 6);
    unsigned n = mac_cpu_ram(r->cpu)[OUT]; memcpy(out, mac_cpu_ram(r->cpu) + OUT + 1, n); out[n] = 0;
    return out;
}
static int real(MacRuntime *r, int family, int size) {
    mac_cpu_set(r->cpu, MAC_A7, SP); mac_cpu_write(r->cpu, SP, 2, (uint16_t)size); mac_cpu_write(r->cpu, SP + 2, 2, (uint16_t)family); mac_cpu_write(r->cpu, SP + 4, 2, 0x7777);
    CHECK(mac_fontnames_service(r, 0xa902) == 1 && mac_cpu_get(r->cpu, MAC_A7) == SP + 4);
    return (int)mac_cpu_read(r->cpu, SP + 4, 2);
}
int main(void) {
    FbFork f = {0};
    uint8_t fond[60] = {0};
    /* a one-glyph strike for family 200 size 9 (FONT 200*128+9): 'A' only, 1 row, plus the missing glyph */
    uint8_t strike[26 + 2 + 6 + 6] = {0};
    fb_put16(strike, 0x9000); fb_put16(strike + 2, 65); fb_put16(strike + 4, 65); fb_put16(strike + 6, 2); fb_put16(strike + 12, 2); fb_put16(strike + 14, 1);
    fb_put16(strike + 16, (26 + 2 + 6 - 16) / 2); fb_put16(strike + 18, 1); fb_put16(strike + 24, 1);
    fb_put16(strike + 28, 0); fb_put16(strike + 30, 1); fb_put16(strike + 32, 2);          /* location table */
    fb_put16(strike + 34, 0x0002); fb_put16(strike + 36, 0x0002); fb_put16(strike + 38, 0xffff);
    fb_add(&f, 0x464f4e44, 150, "Test Sans", fond, sizeof fond);
    fb_add(&f, 0x464f4e54, 200 * 128, "Name Holder", "", 0);
    fb_add(&f, 0x464f4e54, 200 * 128 + 9, NULL, strike, sizeof strike);
    fb_add(&f, 0x464f4e54, 200 * 128 + 12, "Not A Family", "", 0);
    size_t n = fb_build(&f, bytes);
    MacRuntime r = {0}; r.cpu = mac_cpu_create(0x10000); CHECK(r.cpu != NULL);
    CHECK(mac_resource_open(&r.fork, bytes, n, NULL, 0) == 0);
    CHECK(mac_fontnames_service(&r, 0xa901) == 0 && mac_fontnames_service(&r, 0xa8fe) == 0);
    CHECK(fnum(&r, "Test Sans") == 150 && fnum(&r, "test SANS") == 150 && fnum(&r, "Name Holder") == 200);
    CHECK(fnum(&r, "Not A Family") == 0 && fnum(&r, "Missing") == 0 && fnum(&r, "") == 0 && fnum(&r, "Test San") == 0);
    CHECK(!strcmp(fname(&r, 150), "Test Sans") && !strcmp(fname(&r, 200), "Name Holder") && !strcmp(fname(&r, 3), "") && !strcmp(fname(&r, -1), ""));
    CHECK(mac_cpu_ram(r.cpu)[OUT + 1] == 0x5a);                                             /* an empty name writes only the length */
    CHECK(real(&r, 200, 9) == 0x0100 && real(&r, 200, 10) == 0 && real(&r, 150, 9) == 0 && real(&r, 200, 0) == 0 && real(&r, -3, 9) == 0);
    mac_cpu_set(r.cpu, MAC_A7, SP); mac_cpu_write(r.cpu, SP, 4, 0); mac_cpu_write(r.cpu, SP + 4, 4, NAME);
    CHECK(mac_fontnames_service(&r, 0xa900) == -1 && mac_cpu_halted(r.cpu));              /* nil result pointer */
    mac_cpu_destroy(r.cpu);
    if (!fails) puts("Font name checks passed");
    return fails ? 1 : 0;
}
