#include "mac_runtime.h"

/* QuickDraw's text state on the current port: TextFont ($A887), TextFace ($A888), TextMode ($A889) and TextSize ($A88A). Each
 * stores one field of the current GrafPort or CGrafPort (the two layouts agree from offset 66) and pops its argument:
 *   txFont  integer at +68    txFace  Style (a byte) at +70    txMode  integer at +72    txSize  integer at +74.
 * A Style is a byte-sized set, so Pascal pushes it as a byte in the high half of a two-byte stack slot, which is the byte at SP.
 * Nothing here draws: it only records the state that later text calls will read. The values are stored as given; the port must be
 * a plausible record in guest RAM (a current port exists and its text fields fit). */
int mac_text_state_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode < 0xa887 || opcode > 0xa88a) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    size_t z = mac_cpu_ram_size(rt->cpu);
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), port = rt->current_port;
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 2) { mac_cpu_halt(rt->cpu, "Text state call has an invalid stack."); return -1; }
    if (port < 0x400 || (port & 1) || port > z || z - port < 78) { mac_cpu_halt(rt->cpu, "Text state call needs a current port (InitGraf and a window or port first)."); return -1; }
    switch (opcode) {
    case 0xa887: mac_cpu_write(rt->cpu, port + 68, 2, mac_cpu_read(rt->cpu, sp, 2)); break;
    case 0xa888: mac_cpu_write(rt->cpu, port + 70, 1, mac_cpu_read(rt->cpu, sp, 1)); break;
    case 0xa889: mac_cpu_write(rt->cpu, port + 72, 2, mac_cpu_read(rt->cpu, sp, 2)); break;
    default:     mac_cpu_write(rt->cpu, port + 74, 2, mac_cpu_read(rt->cpu, sp, 2)); break;
    }
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}
