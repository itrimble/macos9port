#include "mac_runtime.h"
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
static void alert_diagnostic(void) {
    MacRuntime rt = fresh();
    const char text[] = "Synthetic compatibility requirement";
    rt.dialog_params[0][0] = sizeof text - 1;
    memcpy(rt.dialog_params[0] + 1, text, sizeof text - 1);
    mac_cpu_write(rt.cpu, SP, 4, 0); mac_cpu_write(rt.cpu, SP + 4, 2, 503);
    mac_cpu_write(rt.cpu, SP + 6, 2, 0xbeef);
    CHECK(mac_dialog_service(&rt, 0xa985) == -1 && mac_cpu_halted(rt.cpu));
    CHECK(strstr(mac_cpu_error(rt.cpu), "Alert 503 unsupported"));
    CHECK(strstr(mac_cpu_error(rt.cpu), text));
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP);
    CHECK(mac_cpu_ram(rt.cpu)[SP + 6] == 0xbe && mac_cpu_ram(rt.cpu)[SP + 7] == 0xef);
    mac_cpu_destroy(rt.cpu);
}
int main(void) {
    snapshots(); bad_pointer(0); bad_pointer(RAM - 1); bad_pointer(0xffffffff);
    const uint32_t bad_sp[] = {SP + 1, RAM - 14, 0xfffffff8};
    for (unsigned i = 0; i < 3; i++) {
        MacRuntime rt = fresh(); mac_cpu_set(rt.cpu, MAC_A7, bad_sp[i]);
        CHECK(mac_dialog_service(&rt, 0xa98b) == -1 && mac_cpu_halted(rt.cpu));
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == bad_sp[i]); mac_cpu_destroy(rt.cpu);
    }
    alert_diagnostic(); puts("mac dialog ABI tests passed"); return 0;
}
