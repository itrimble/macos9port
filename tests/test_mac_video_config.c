#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_video_config_service(MacRuntime *, uint16_t);
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static MacRuntime fresh(uint32_t p) {
    MacRuntime r = {0}; r.cpu = mac_cpu_create(65536); CHECK(r.cpu);
    memset(mac_cpu_ram(r.cpu), 0xcc, 65536);
    mac_cpu_set(r.cpu, MAC_A0, p); mac_cpu_set(r.cpu, MAC_A7, 0x8000);
    mac_cpu_set(r.cpu, MAC_D0, 0x12345678); return r;
}
int main(void) {
    const uint32_t good[] = {0x1000, 0x1001, 65534};
    for (unsigned i = 0; i < 3; i++) {
        MacRuntime r = fresh(good[i]);
        CHECK(mac_video_config_service(&r, 0xa080) == 1 && !mac_cpu_halted(r.cpu));
        unsigned char *b = mac_cpu_ram(r.cpu);
        CHECK(b[good[i]] == 0 && b[good[i] + 1] == 0 && b[good[i] - 1] == 0xcc);
        if (good[i] + 2 < 65536) CHECK(b[good[i] + 2] == 0xcc);
        CHECK(mac_cpu_get(r.cpu, MAC_A0) == good[i]);
        CHECK(mac_cpu_get(r.cpu, MAC_D0) == 0x12345678 && mac_cpu_get(r.cpu, MAC_A7) == 0x8000);
        CHECK(mac_video_config_service(&r, 0xa081) == 0);
        mac_cpu_destroy(r.cpu);
    }
    const uint32_t bad[] = {0, 65535, 65536, 0xffffffff};
    for (unsigned i = 0; i < 4; i++) {
        MacRuntime r = fresh(bad[i]);
        CHECK(mac_video_config_service(&r, 0xa080) == -1 && mac_cpu_halted(r.cpu));
        for (unsigned j = 0; j < 65536; j++) CHECK(mac_cpu_ram(r.cpu)[j] == 0xcc);
        CHECK(mac_cpu_get(r.cpu, MAC_A0) == bad[i] && mac_cpu_get(r.cpu, MAC_A7) == 0x8000);
        mac_cpu_destroy(r.cpu);
    }
    puts("mac video config tests passed"); return 0;
}
