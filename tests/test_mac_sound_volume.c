/* _SoundDispatch GetDefaultOutputVolume: selectors, results, and guest-pointer validation. */
#include "mac_sound_volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define RAM 0x10000u
#define SP 0xe000u
static MacRuntime fresh(void) { MacRuntime rt = {0}; rt.cpu = mac_cpu_create(RAM); CHECK(rt.cpu != NULL); return rt; }
static uint32_t rd(MacRuntime *r, uint32_t a, unsigned n) { const uint8_t *m = mac_cpu_ram(r->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | m[a + i]; return v; }   /* raw, so it still reads after a halt */
static void frame(MacRuntime *r, uint32_t sel, uint32_t ptr) {
    memset(mac_cpu_ram(r->cpu) + 0x2000, 0xee, 64);
    mac_cpu_set(r->cpu, MAC_A7, SP); mac_cpu_set(r->cpu, MAC_D0, sel);
    mac_cpu_write(r->cpu, SP, 4, ptr); mac_cpu_write(r->cpu, SP + 4, 2, 0x7777);
}
int main(void) {
    static const uint32_t sels[] = { 0x022c0018u, 0x022c0024u };
    for (unsigned i = 0; i < 2; i++) {
        MacRuntime r = fresh(); frame(&r, sels[i], 0x2000);
        CHECK(mac_sound_volume_service(&r, 0xa800) == 1 && rd(&r, 0x2000, 4) == 0x01000100u && rd(&r, 0x2004, 1) == 0xee);   /* level written, nothing past it */
        CHECK(rd(&r, SP + 4, 2) == 0 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4 && !mac_cpu_halted(r.cpu));                      /* noErr in the result slot, argument popped */
        mac_cpu_destroy(r.cpu);
    }
    for (unsigned i = 0; i < 2; i++) {   /* SetDefaultOutputVolume: accepted, argument popped, noErr, nothing written anywhere */
        MacRuntime r = fresh(); frame(&r, i ? 0x02300024u : 0x02300018u, 0x00800080u);
        CHECK(mac_sound_volume_service(&r, 0xa800) == 1 && rd(&r, SP + 4, 2) == 0 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4 && rd(&r, 0x2000, 1) == 0xee && !mac_cpu_halted(r.cpu));
        mac_cpu_destroy(r.cpu);
    }
    {   /* not ours: another selector, another trap, the neighbouring routines */
        static const uint32_t other[] = { 0x00200008u, 0x02240024u, 0x02280024u, 0x022c0000u, 0x012c0018u, 0x022c0019u, 0 };
        for (unsigned i = 0; i < sizeof other / sizeof *other; i++) {
            MacRuntime r = fresh(); frame(&r, other[i], 0x2000);
            CHECK(mac_sound_volume_service(&r, 0xa800) == 0 && rd(&r, 0x2000, 1) == 0xee && mac_cpu_get(r.cpu, MAC_A7) == SP);
            mac_cpu_destroy(r.cpu);
        }
        MacRuntime r = fresh(); frame(&r, 0x022c0018u, 0x2000);
        CHECK(mac_sound_volume_service(&r, 0xa801) == 0 && mac_sound_volume_service(&r, 0xa000) == 0 && rd(&r, 0x2000, 1) == 0xee);
        mac_cpu_destroy(r.cpu);
    }
    {   /* the last bytes of RAM: a level pointer at RAM-4 and a stack exactly 6 bytes from the end are fine */
        MacRuntime r = fresh(); frame(&r, 0x022c0018u, RAM - 4);
        CHECK(mac_sound_volume_service(&r, 0xa800) == 1 && rd(&r, RAM - 4, 4) == 0x01000100u); mac_cpu_destroy(r.cpu);
        r = fresh(); frame(&r, 0x022c0018u, 0x2000); mac_cpu_write(r.cpu, RAM - 6, 4, 0x2000); mac_cpu_set(r.cpu, MAC_A7, RAM - 6);
        CHECK(mac_sound_volume_service(&r, 0xa800) == 1 && rd(&r, 0x2000, 4) == 0x01000100u && mac_cpu_get(r.cpu, MAC_A7) == RAM - 2); mac_cpu_destroy(r.cpu);
    }
    {   /* corrupt arguments halt and write nothing */
        static const uint32_t bad[] = { 0, 0x3ff, RAM - 3, RAM, 0xffffffffu, 0x01000000u };
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
            MacRuntime r = fresh(); frame(&r, 0x022c0018u, bad[i]);
            CHECK(mac_sound_volume_service(&r, 0xa800) == -1 && mac_cpu_halted(r.cpu) && rd(&r, 0x2000, 1) == 0xee && rd(&r, SP + 4, 2) == 0x7777);
            mac_cpu_destroy(r.cpu);
        }
        MacRuntime r = fresh(); frame(&r, 0x022c0018u, 0x2000); mac_cpu_set(r.cpu, MAC_A7, SP + 1);      /* odd stack */
        CHECK(mac_sound_volume_service(&r, 0xa800) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
        r = fresh(); frame(&r, 0x022c0018u, 0x2000); mac_cpu_write(r.cpu, RAM - 4, 4, 0x2000); mac_cpu_set(r.cpu, MAC_A7, RAM - 4);   /* no room for the result slot */
        CHECK(mac_sound_volume_service(&r, 0xa800) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
        r = fresh(); frame(&r, 0x022c0018u, 0x2000); mac_cpu_set(r.cpu, MAC_A7, 0x100);                  /* stack in low memory */
        CHECK(mac_sound_volume_service(&r, 0xa800) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
        r = fresh(); frame(&r, 0x022c0018u, 0x2000); mac_cpu_halt(r.cpu, "x");                           /* already halted */
        CHECK(mac_sound_volume_service(&r, 0xa800) == -1); mac_cpu_destroy(r.cpu);
    }
    if (!fails) puts("Sound volume checks passed");
    return fails ? 1 : 0;
}
