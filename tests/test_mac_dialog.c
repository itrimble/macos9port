#include "mac_runtime.h"
#include "mac_handles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define SP 0x8000u
#define RAM 0x10000u

static MacRuntime fresh(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(RAM); CHECK(rt.cpu);
    mac_cpu_set(rt.cpu, MAC_A7, SP);
    for (unsigned i = 0; i < 4; i++) {
        uint32_t p = 0x1001 + 256 * i; /* Byte-aligned strings are legal. */
        mac_cpu_write(rt.cpu, SP + (3 - i) * 4, 4, p);
        mac_cpu_write(rt.cpu, p, 1, 1); mac_cpu_write(rt.cpu, p + 1, 1, 'A' + i);
    }
    return rt;
}
static void snapshots(void) {
    MacRuntime rt = fresh();
    CHECK(mac_dialog_service(&rt, 0xa98b) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 16);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(rt.dialog_params[i][0] == 1 && (unsigned char)rt.dialog_params[i][1] == 'A' + i);
        CHECK(rt.dialog_params[i][2] == 0);
    }
    mac_cpu_write(rt.cpu, 0x1002, 1, 'Z');
    CHECK(rt.dialog_params[0][1] == 'A'); /* Detached from guest storage. */
    mac_cpu_set(rt.cpu, MAC_A7, SP);
    mac_cpu_write(rt.cpu, 0x1001, 1, 255);
    for (unsigned i = 1; i <= 255; i++) mac_cpu_write(rt.cpu, 0x1001 + i, 1, i - 1);
    mac_cpu_write(rt.cpu, 0x1101, 1, 0); /* Empty string. */
    CHECK(mac_dialog_service(&rt, 0xa98b) == 1);
    CHECK((unsigned char)rt.dialog_params[0][0] == 255);
    CHECK(rt.dialog_params[0][1] == 0 && (unsigned char)rt.dialog_params[0][255] == 254);
    CHECK(rt.dialog_params[1][0] == 0 && rt.dialog_params[1][1] == 0);
    CHECK(mac_dialog_service(&rt, 0xafff) == 0);
    mac_cpu_destroy(rt.cpu);
}
/* A NIL parameter leaves that parameter as it was (the public text covers only empty strings; see mac_dialog.c). */
static void nil_leaves_unchanged(void) {
    MacRuntime rt = fresh();
    memset(rt.dialog_params, 0x35, sizeof rt.dialog_params);
    mac_cpu_write(rt.cpu, SP, 4, 0); mac_cpu_write(rt.cpu, SP + 4, 4, 0); mac_cpu_write(rt.cpu, SP + 8, 4, 0); mac_cpu_write(rt.cpu, SP + 12, 4, 0);
    CHECK(mac_dialog_service(&rt, 0xa98b) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SP + 16);
    for (unsigned i = 0; i < sizeof rt.dialog_params; i++) CHECK(((unsigned char *)rt.dialog_params)[i] == 0x35);
    mac_cpu_destroy(rt.cpu);
}
static void bad_pointer(uint32_t p) {
    MacRuntime rt = fresh();
    memset(rt.dialog_params, 0x35, sizeof rt.dialog_params);
    mac_cpu_write(rt.cpu, SP, 4, p); /* Validate three good args before bad fourth. */
    if (p == RAM - 1) mac_cpu_write(rt.cpu, p, 1, 255);
    CHECK(mac_dialog_service(&rt, 0xa98b) == -1 && mac_cpu_halted(rt.cpu));
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP);
    for (unsigned i = 0; i < sizeof rt.dialog_params; i++)
        CHECK(((unsigned char *)rt.dialog_params)[i] == 0x35);
    mac_cpu_destroy(rt.cpu);
}
/* Alert and its kin moved to mac_alert.c (tested in test_mac_alert): this service no longer claims them and touches nothing. */
static void alerts_are_not_ours(void) {
    for (unsigned op = 0xa985; op <= 0xa988; op++) {
        MacRuntime rt = fresh();
        mac_cpu_write(rt.cpu, SP, 4, 0); mac_cpu_write(rt.cpu, SP + 4, 2, 503); mac_cpu_write(rt.cpu, SP + 6, 2, 0xbeef);
        CHECK(mac_dialog_service(&rt, (uint16_t)op) == 0 && !mac_cpu_halted(rt.cpu));
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP);
        CHECK(mac_cpu_ram(rt.cpu)[SP + 6] == 0xbe && mac_cpu_ram(rt.cpu)[SP + 7] == 0xef);
        mac_cpu_destroy(rt.cpu);
    }
}
/* ParamText also leaves its four strings in guest handles whose addresses are the low-memory array DAStrings ($0AA0): a program may read them from there
 * (the QuickTime installer builds its own alert text that way, then disposes the handles and zeroes the array). */
static const char *da_string(MacRuntime *rt, unsigned slot, unsigned *length) {
    static char text[257];
    uint32_t h = mac_cpu_read(rt->cpu, 0xaa0 + slot * 4, 4), p, n;
    if (!h || !mac_handle_block(rt, h, &p, &n)) return NULL;
    unsigned len = mac_cpu_read(rt->cpu, p, 1);
    CHECK(len + 1 <= n);
    for (unsigned i = 0; i < len; i++) text[i] = (char)mac_cpu_read(rt->cpu, p + 1 + i, 1);
    text[len] = 0; *length = len;
    return text;
}
static void put_pascal(MacRuntime *rt, uint32_t at, const char *s) { mac_cpu_write(rt->cpu, at, 1, (uint32_t)strlen(s)); for (size_t i = 0; s[i]; i++) mac_cpu_write(rt->cpu, at + 1 + i, 1, (unsigned char)s[i]); }
static void param_text(MacRuntime *rt, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    mac_cpu_write(rt->cpu, SP + 12, 4, a); mac_cpu_write(rt->cpu, SP + 8, 4, b); mac_cpu_write(rt->cpu, SP + 4, 4, c); mac_cpu_write(rt->cpu, SP, 4, d);
    CHECK(mac_dialog_service(rt, 0xa98b) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 16);
}
static void da_strings(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(rt.cpu);
    rt.heap_next = 0x100000; rt.heap_limit = 0x600000;
    put_pascal(&rt, 0x1000, "OS999 HD"); put_pascal(&rt, 0x1100, ""); put_pascal(&rt, 0x1200, "second"); put_pascal(&rt, 0x1300, "x");
    CHECK(mac_cpu_read(rt.cpu, 0xaa0, 4) == 0 && da_string(&rt, 0, NULL) == NULL);
    unsigned n;
    param_text(&rt, 0x1000, 0x1100, 0x1200, 0x1300);
    CHECK(!strcmp(da_string(&rt, 0, &n), "OS999 HD") && n == 8 && !strcmp(da_string(&rt, 1, &n), "") && n == 0 && !strcmp(da_string(&rt, 2, &n), "second") && !strcmp(da_string(&rt, 3, &n), "x"));
    CHECK(rt.dialog_params[0][0] == 8 && !memcmp(rt.dialog_params[0] + 1, "OS999 HD", 8));                       /* the host copy dialogs draw from is the same text */
    uint32_t h0 = mac_cpu_read(rt.cpu, 0xaa0, 4), h2 = mac_cpu_read(rt.cpu, 0xaa8, 4);
    /* a NIL parameter leaves its slot (and its handle) alone; a new string replaces the old handle, which is disposed */
    put_pascal(&rt, 0x1400, "Disk");
    param_text(&rt, 0, 0x1400, 0, 0);
    CHECK(mac_cpu_read(rt.cpu, 0xaa0, 4) == h0 && mac_cpu_read(rt.cpu, 0xaa8, 4) == h2 && !strcmp(da_string(&rt, 0, &n), "OS999 HD") && !strcmp(da_string(&rt, 1, &n), "Disk"));
    put_pascal(&rt, 0x1500, "Other");
    param_text(&rt, 0x1500, 0x1100, 0x1200, 0x1300);
    CHECK(!strcmp(da_string(&rt, 0, &n), "Other") && n == 5 && !strcmp(da_string(&rt, 1, &n), ""));
    /* the program disposes the handles and zeroes the array itself, as the installer does; the next ParamText starts again without touching them */
    for (unsigned i = 0; i < 4; i++) { mac_handle_dispose(&rt, mac_cpu_read(rt.cpu, 0xaa0 + i * 4, 4)); mac_cpu_write(rt.cpu, 0xaa0 + i * 4, 4, 0); }
    param_text(&rt, 0x1000, 0x1100, 0x1200, 0x1300);
    CHECK(!strcmp(da_string(&rt, 0, &n), "OS999 HD") && !strcmp(da_string(&rt, 3, &n), "x"));
    /* a slot that holds an address that is no handle is overwritten, never disposed */
    mac_cpu_write(rt.cpu, 0xaa4, 4, 0x1234);
    param_text(&rt, 0, 0x1200, 0, 0);
    CHECK(!strcmp(da_string(&rt, 1, &n), "second"));
    mac_cpu_destroy(rt.cpu);
}
int main(void) {
    da_strings(); snapshots(); nil_leaves_unchanged(); bad_pointer(RAM - 1); bad_pointer(0xffffffff);
    const uint32_t bad_sp[] = {SP + 1, RAM - 14, 0xfffffff8};
    for (unsigned i = 0; i < 3; i++) {
        MacRuntime rt = fresh(); mac_cpu_set(rt.cpu, MAC_A7, bad_sp[i]);
        CHECK(mac_dialog_service(&rt, 0xa98b) == -1 && mac_cpu_halted(rt.cpu));
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == bad_sp[i]); mac_cpu_destroy(rt.cpu);
    }
    alerts_are_not_ours(); puts("mac dialog ABI tests passed"); return 0;
}
