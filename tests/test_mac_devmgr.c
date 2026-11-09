/* Device Manager Control ($A004 family): drivers are not implemented, so Control answers a result and changes nothing. */
#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define PB 0x5000u
static MacRuntime rig(int ref, int cs) {
    MacRuntime r = {0}; r.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(r.cpu);
    memset(mac_cpu_ram(r.cpu) + PB, 0x77, 64);
    mac_cpu_write(r.cpu, PB + 24, 2, (uint16_t)ref); mac_cpu_write(r.cpu, PB + 26, 2, (uint16_t)cs); mac_cpu_set(r.cpu, MAC_A0, PB);
    mac_cpu_set(r.cpu, MAC_SR, 0x2004);
    return r;
}
int main(void) {
    static const struct { int ref; int err; } cases[] = { {-4, -17}, {-1, -17}, {-32, -17}, {-33, -21}, {0, -21}, {3, -21}, {-32768, -21} };
    static const uint16_t ops[] = { 0xa004, 0xa204, 0xa404, 0xa604 };
    for (unsigned o = 0; o < 4; o++) for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        MacRuntime r = rig(cases[i].ref, 2); uint8_t before[64]; memcpy(before, mac_cpu_ram(r.cpu) + PB, 64);
        CHECK(mac_devmgr_service(&r, ops[o]) == 1);
        CHECK((int16_t)mac_cpu_read(r.cpu, PB + 16, 2) == cases[i].err && (int32_t)mac_cpu_get(r.cpu, MAC_D0) == cases[i].err && (mac_cpu_get(r.cpu, MAC_SR) & 15) == 8);
        uint8_t after[64]; memcpy(after, mac_cpu_ram(r.cpu) + PB, 64); memcpy(before + 16, after + 16, 2);
        CHECK(!memcmp(before, after, 64));                                    /* only ioResult changed */
        mac_cpu_destroy(r.cpu);
    }
    /* not ours: other traps, and the flag bits that make it a different call */
    MacRuntime r = rig(-4, 2);
    CHECK(mac_devmgr_service(&r, 0xa005) == 0 && mac_devmgr_service(&r, 0xa000) == 0 && mac_devmgr_service(&r, 0xa104) == 0 && mac_devmgr_service(&r, 0xa804) == 0 && mac_devmgr_service(&r, 0xa003) == 0);
    mac_cpu_destroy(r.cpu);
    /* bad parameter blocks halt without writing */
    static const uint32_t bad[] = { 0, 0x5001, 0x100, MAC_RUNTIME_RAM - 10, MAC_RUNTIME_RAM + 2 };
    for (unsigned i = 0; i < 5; i++) { r = rig(-4, 2); mac_cpu_set(r.cpu, MAC_A0, bad[i]); CHECK(mac_devmgr_service(&r, 0xa004) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu); }
    r = rig(-4, 2); mac_cpu_halt(r.cpu, "x"); CHECK(mac_devmgr_service(&r, 0xa004) == -1); mac_cpu_destroy(r.cpu);
    if (!fails) puts("Device Manager Control checks passed");
    return fails ? 1 : 0;
}
