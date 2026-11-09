#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
enum { SP = 0x1000, SRC = 0x200, DST = 0x400, RAM = 65536 };
static void init(MacRuntime *r) { memset(r, 0, sizeof *r); r->cpu = mac_cpu_create(RAM); CHECK(r->cpu != NULL); }
static void reinit(MacRuntime *r) { mac_cpu_destroy(r->cpu); init(r); }
/* Register interface: D0 = number, A0 = string pointer; only the selector is on the stack.
 * Garbage above the selector proves nothing else is consumed. */
static void stack(MacRuntime *r, unsigned sel, uint32_t d0, uint32_t a0) {
    mac_cpu_write(r->cpu, SP, 2, sel); mac_cpu_write(r->cpu, SP + 2, 4, 0xdeadbeef);
    mac_cpu_set(r->cpu, MAC_D0, d0); mac_cpu_set(r->cpu, MAC_A0, a0); mac_cpu_set(r->cpu, MAC_A7, SP);
    mac_cpu_set(r->cpu, MAC_D1, 0x11111111); mac_cpu_set(r->cpu, MAC_A1, 0x22222222);
}
static int call(MacRuntime *r) { return mac_numconv_service(r, 0xa9ee); }
static int num2str(MacRuntime *r, int32_t v, char *out) {
    uint8_t *ram = mac_cpu_ram(r->cpu);
    memset(ram + DST, 0xee, 16);
    stack(r, 0, (uint32_t)v, DST);
    int rc = call(r);
    if (rc != 1) return rc;
    CHECK(mac_cpu_get(r->cpu, MAC_A7) == SP + 2 && mac_cpu_get(r->cpu, MAC_A0) == DST);
    CHECK(mac_cpu_get(r->cpu, MAC_D0) == (uint32_t)v && mac_cpu_get(r->cpu, MAC_D1) == 0x11111111);
    memcpy(out, ram + DST + 1, ram[DST]); out[ram[DST]] = 0;
    CHECK(ram[DST + 1 + ram[DST]] == 0xee); /* no write past the string */
    return 1;
}
static int str2num(MacRuntime *r, const char *s, int32_t *v) {
    uint8_t *ram = mac_cpu_ram(r->cpu);
    size_t n = strlen(s);
    ram[SRC] = (uint8_t)n; memcpy(ram + SRC + 1, s, n);
    memset(ram + DST, 0x55, 8);
    stack(r, 1, 0x7777, SRC);
    int rc = call(r);
    if (rc != 1) return rc;
    CHECK(mac_cpu_get(r->cpu, MAC_A7) == SP + 2 && mac_cpu_get(r->cpu, MAC_A0) == SRC);
    CHECK(mac_cpu_get(r->cpu, MAC_D1) == 0x11111111 && mac_cpu_get(r->cpu, MAC_A1) == 0x22222222);
    CHECK(ram[DST] == 0x55 && ram[SRC] == n); /* nothing written to guest memory */
    *v = (int32_t)mac_cpu_get(r->cpu, MAC_D0);
    return 1;
}
static void formatting(void) {
    MacRuntime r; init(&r); char b[32];
    const struct { int32_t v; const char *s; } c[] = {
        {0, "0"}, {1, "1"}, {-1, "-1"}, {10, "10"}, {-10, "-10"}, {99999, "99999"},
        {2147483647, "2147483647"}, {-2147483647, "-2147483647"},
        {(int32_t)0x80000000, "-2147483648"}, {1000000000, "1000000000"}};
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        CHECK(num2str(&r, c[i].v, b) == 1); CHECK(!strcmp(b, c[i].s));
    }
    mac_cpu_destroy(r.cpu);
}
static void parsing(void) {
    MacRuntime r; init(&r); int32_t v;
    const struct { const char *s; int32_t v; } c[] = {
        {"0", 0}, {"7", 7}, {"-1", -1}, {"+5", 5}, {"-0", 0}, {"007", 7}, {"", 0}, {"-", 0}, {"+", 0},
        {"2147483647", 2147483647}, {"-2147483648", (int32_t)0x80000000}, {"-2147483647", -2147483647},
        {"4294967296", 0}, {"2147483648", (int32_t)0x80000000}, {"4294967297", 1},
        {"12abc", 12}, {"abc", 0}, {" 12", 0}, {"1 2", 1}, {"--1", 0}, {"1-", 1}, {"-+1", 0}};
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        v = 0x7778; CHECK(str2num(&r, c[i].s, &v) == 1);
        if (v != c[i].v) fprintf(stderr, "  case '%s' got %d want %d\n", c[i].s, v, c[i].v);
        CHECK(v == c[i].v);
    }
    char big[256]; memset(big, '0', 255); big[255] = 0; big[254] = '9'; /* 255 chars */
    v = 1; CHECK(str2num(&r, big, &v) == 1); CHECK(v == 9);
    memset(big, '1', 255); big[255] = 0;
    CHECK(str2num(&r, big, &v) == 1); /* wraps modulo 2^32 */
    uint32_t w = 0; for (int i = 0; i < 255; i++) w = w * 10 + 1; CHECK((uint32_t)v == w);
    mac_cpu_destroy(r.cpu);
}
static void round_trip(void) {
    MacRuntime r; init(&r); char b[32]; int32_t v;
    uint32_t x = 12345;
    for (int i = 0; i < 20000; i++) {
        x = x * 1664525u + 1013904223u;
        int32_t n = (int32_t)(i % 5 == 0 ? x : x >> (i % 31));
        CHECK(num2str(&r, n, b) == 1);
        CHECK(b[0] == '-' ? b[1] != '0' && b[1] : (b[0] != '0' || !b[1]));
        CHECK(str2num(&r, b, &v) == 1); CHECK(v == n);
    }
    mac_cpu_destroy(r.cpu);
}
static void faults(void) {
    MacRuntime r; init(&r); uint8_t *ram;
    /* unknown selectors halt with the selector named, stack untouched */
    for (unsigned sel = 2; sel < 8; sel += 5) {
        reinit(&r); stack(&r, sel, 0, DST);
        CHECK(call(&r) == -1 && mac_cpu_halted(r.cpu));
        char want[32]; snprintf(want, sizeof want, "$%04x", sel);
        CHECK(strstr(mac_cpu_error(r.cpu), want) != NULL);
        CHECK(mac_cpu_get(r.cpu, MAC_A7) == SP);
    }
    reinit(&r); stack(&r, 0xffff, 0, 0);
    CHECK(call(&r) == -1 && strstr(mac_cpu_error(r.cpu), "$ffff"));
    reinit(&r); CHECK(mac_numconv_service(&r, 0xa9eb) == 0 && !mac_cpu_halted(r.cpu));
    /* NumToString destination: last byte fits, one short faults, far outside faults */
    reinit(&r); ram = mac_cpu_ram(r.cpu);
    stack(&r, 0, 12345, RAM - 6); CHECK(call(&r) == 1);
    CHECK(ram[RAM - 6] == 5 && ram[RAM - 1] == '5');
    reinit(&r); ram = mac_cpu_ram(r.cpu);
    ram[RAM - 1] = 0x99; stack(&r, 0, 12345, RAM - 5);
    CHECK(call(&r) == -1 && mac_cpu_halted(r.cpu));
    CHECK(ram[RAM - 1] == 0x99 && ram[RAM - 5] == 0 && mac_cpu_get(r.cpu, MAC_A7) == SP);
    reinit(&r); stack(&r, 0, 1, 0xfffffff0u); CHECK(call(&r) == -1 && mac_cpu_halted(r.cpu));
    reinit(&r); stack(&r, 0, (uint32_t)-5, RAM); CHECK(call(&r) == -1);
    reinit(&r); stack(&r, 0, (uint32_t)-5, RAM - 1); CHECK(call(&r) == -1);
    reinit(&r); stack(&r, 0, 0x80000000u, RAM - 11); CHECK(call(&r) == -1); /* needs 12 bytes */
    reinit(&r); stack(&r, 0, 0x80000000u, RAM - 12); CHECK(call(&r) == 1);
    /* StringToNum: string at the very end of RAM, claimed length runs past it */
    reinit(&r); ram = mac_cpu_ram(r.cpu);
    ram[RAM - 1] = 200; stack(&r, 1, 0x1234, RAM - 1);
    CHECK(call(&r) == -1 && mac_cpu_halted(r.cpu) && mac_cpu_get(r.cpu, MAC_D0) == 0x1234);
    reinit(&r); ram = mac_cpu_ram(r.cpu);
    ram[RAM - 3] = 2; ram[RAM - 2] = '4'; ram[RAM - 1] = '2'; stack(&r, 1, 0, RAM - 3);
    CHECK(call(&r) == 1 && mac_cpu_get(r.cpu, MAC_D0) == 42);
    reinit(&r); ram = mac_cpu_ram(r.cpu); ram[RAM - 1] = 0; stack(&r, 1, 9, RAM - 1);
    CHECK(call(&r) == 1 && mac_cpu_get(r.cpu, MAC_D0) == 0); /* empty string in the last byte */
    reinit(&r); ram = mac_cpu_ram(r.cpu); ram[RAM - 2] = 2; ram[RAM - 1] = '7'; /* one byte short */
    stack(&r, 1, 0x1234, RAM - 2); CHECK(call(&r) == -1 && mac_cpu_halted(r.cpu));
    reinit(&r); ram = mac_cpu_ram(r.cpu); ram[RAM - 2] = 1; ram[RAM - 1] = '7';
    stack(&r, 1, 0, RAM - 2); CHECK(call(&r) == 1 && mac_cpu_get(r.cpu, MAC_D0) == 7);
    reinit(&r); stack(&r, 0, 5, DST); mac_cpu_halt(r.cpu, "x"); /* already halted: no work */
    CHECK(call(&r) == -1 && mac_cpu_read(r.cpu, DST, 1) == 0 && mac_cpu_get(r.cpu, MAC_A7) == SP);
    reinit(&r); stack(&r, 1, 0, RAM); CHECK(call(&r) == -1);
    reinit(&r); stack(&r, 1, 0, 0xfffffff0u); CHECK(call(&r) == -1);
    /* stack at the end of RAM / odd */
    reinit(&r); mac_cpu_set(r.cpu, MAC_A7, RAM - 1); CHECK(call(&r) == -1);
    reinit(&r); mac_cpu_set(r.cpu, MAC_A7, RAM); CHECK(call(&r) == -1);
    reinit(&r); mac_cpu_set(r.cpu, MAC_A7, SP + 1); CHECK(call(&r) == -1);
    mac_cpu_destroy(r.cpu);
}
int main(void) {
    formatting(); parsing(); round_trip(); faults();
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("numconv ok"); return 0;
}
