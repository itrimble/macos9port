#include "mac_sound_volume.h"

/* GetDefaultOutputVolume: Apple's Sound Manager reference (2-141 of the chapter) gives the selector $022C0024, the form FUNCTION
 * GetDefaultOutputVolume(VAR level: LongInt): OSErr, and says the level is a long whose high and low words are the right and left volumes, each
 * 0 (silence) to $0100 (full). Glider PRO calls the same routine number with the low word $0018 (not $0024); I could not find why in the document,
 * and the call shape (one VAR long, an OSErr result, the level divided by 36 to make a 0-7 setting) matches, so both low words are answered alike.
 * SetDefaultOutputVolume (same document, $02300024; the app uses $02300018) is accepted and ignored. The runtime has no system volume setting: Get reports full volume on both channels and noErr. Nothing is written if the pointer is unusable (halt). */
int mac_sound_volume_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa800) return 0;
    uint32_t selector = mac_cpu_get(rt->cpu, MAC_D0);
    int set = selector == 0x02300018u || selector == 0x02300024u;
    if (selector != 0x022c0018u && selector != 0x022c0024u && !set) return 0;
    if (mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), z = (uint32_t)mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 6) { mac_cpu_halt(rt->cpu, "GetDefaultOutputVolume stack is invalid."); return -1; }
    if (set) {   /* SetDefaultOutputVolume(level: LongInt): OSErr ($02300024 in the document, $02300018 from Glider PRO): accepted and ignored, noErr; the level is not kept */
        mac_cpu_write(rt->cpu, sp + 4, 2, 0);
        mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
        return 1;
    }
    uint32_t level = mac_cpu_read(rt->cpu, sp, 4);
    if (level < 0x400 || level > z || z - level < 4) { mac_cpu_halt(rt->cpu, "GetDefaultOutputVolume level pointer is outside guest RAM."); return -1; }
    if (mac_cpu_write(rt->cpu, level, 4, 0x01000100u)) return -1;
    mac_cpu_write(rt->cpu, sp + 4, 2, 0);                 /* noErr */
    mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
    return 1;
}
