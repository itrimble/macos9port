#include "mac_runtime.h"
#include <string.h>

static int valid(MacCpu *cpu, uint32_t address, size_t size) {
    if ((address & 1) || address > mac_cpu_ram_size(cpu) || size > mac_cpu_ram_size(cpu) - address) {
        mac_cpu_halt(cpu, "Event Manager guest range exceeds RAM or is unaligned.");
        return 0;
    }
    return 1;
}

void mac_runtime_post_event(MacRuntime *rt, uint16_t what, uint32_t message,
                            uint16_t modifiers, int16_t x, int16_t y) {
    if (!rt) return;
    rt->mouse_x = x; rt->mouse_y = y; rt->modifiers = modifiers;
    if (what==3 || what==4 || what==5) {
        unsigned key=(message>>8)&255;
        if(key<128) {
            uint8_t bit=(uint8_t)(1u<<(key&7));
            if(what==4)rt->keys[key/8]&=(uint8_t)~bit;
            else rt->keys[key/8]|=bit;
        }
    }
    if (rt->event_count == 64) return; /* Bound host work and guest storage. */
    unsigned slot = (rt->event_head + rt->event_count) % 64;
    rt->events[slot] = (MacEvent){what, modifiers, message, rt->ticks, x, y};
    rt->event_count++;
}

static int event_record(MacRuntime *rt, uint32_t address, const MacEvent *e) {
    MacCpu *cpu = rt->cpu;
    if (!valid(cpu, address, 16)) return -1;
    mac_cpu_write(cpu, address, 2, e->what);
    mac_cpu_write(cpu, address + 2, 4, e->message);
    mac_cpu_write(cpu, address + 6, 4, e->when);
    mac_cpu_write(cpu, address + 10, 2, (uint16_t)e->y);
    mac_cpu_write(cpu, address + 12, 2, (uint16_t)e->x);
    mac_cpu_write(cpu, address + 14, 2, e->modifiers);
    return 0;
}

static int take_event(MacRuntime *rt, uint16_t mask, int consume, MacEvent *out) {
    *out = (MacEvent){0, rt->modifiers, 0, rt->ticks, rt->mouse_x, rt->mouse_y};
    for (unsigned i = 0; i < rt->event_count; i++) {
        unsigned slot = (rt->event_head + i) % 64;
        MacEvent e = rt->events[slot];
        if (e.what >= 16 || !(mask & (1u << e.what))) continue;
        *out = e;
        if (consume) {
            for (unsigned j = i; j + 1 < rt->event_count; j++)
                rt->events[(rt->event_head + j) % 64] = rt->events[(rt->event_head + j + 1) % 64];
            rt->event_count--;
        }
        return 1;
    }
    if (mac_dlgmgr_update_event(rt, mask, out)) return 1;
    return mac_window_update_event(rt, mask, out);
}

int mac_event_service(MacRuntime *rt, uint16_t opcode) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (opcode == 0xa970 || opcode == 0xa971 || opcode == 0xa860) {
        unsigned args = opcode == 0xa860 ? 14 : 6;
        if (!valid(cpu, sp, args + 2)) return -1;
        uint32_t ptr = mac_cpu_read(cpu, sp + args - 6, 4);
        uint16_t mask = (uint16_t)mac_cpu_read(cpu, sp + args - 2, 2);
        if (!valid(cpu, ptr, 16)) return -1;
        MacEvent e;
        int found = take_event(rt, mask, opcode != 0xa971, &e);
        if (found < 0) return -1;
        if (event_record(rt, ptr, &e)) return -1;
        mac_cpu_write(cpu, sp + args, 1, found);
        mac_cpu_set(cpu, MAC_A7, sp + args);
        return 1;
    }
    if (opcode == 0xa976) { /* GetKeys: packed virtual-key bits, low bit first.
        * Public API: dev.os9.ca/techpubs/mac/Toolbox/Toolbox-77.html */
        if (!valid(cpu,sp,4)) return -1;
        uint32_t ptr=mac_cpu_read(cpu,sp,4);
        if (!ptr) {mac_cpu_halt(cpu,"GetKeys requires a non-NIL KeyMap.");return -1;}
        if (!valid(cpu,ptr,16)) return -1;
        uint8_t keys[16];memcpy(keys,rt->keys,sizeof keys);
        /* Existing host input combines left/right modifier states. Publish the
         * classic five modifier positions; no layout or keypad completeness claim. */
        for(unsigned i=0;i<5;i++) {
            unsigned key=55+i,bit=1u<<(key&7);
            keys[key/8]=(uint8_t)((keys[key/8]&~bit)|((rt->modifiers&(0x100u<<i))?bit:0));
        }
        memcpy(mac_cpu_ram(cpu)+ptr,keys,sizeof keys);
        mac_cpu_set(cpu,MAC_A7,sp+4);return 1;
    }
    if (opcode == 0xa975) {
        if (!valid(cpu, sp, 4)) return -1;
        mac_cpu_write(cpu, sp, 4, rt->ticks); return 1;
    }
    if (opcode == 0xa972) {
        if (!valid(cpu, sp, 4)) return -1;
        uint32_t ptr = mac_cpu_read(cpu, sp, 4);
        if (!valid(cpu, ptr, 4)) return -1;
        int x = rt->mouse_x, y = rt->mouse_y;
        MacWindowState *window=mac_window_find(rt,rt->current_port);
        if (window) { x -= window->bounds[1]; y -= window->bounds[0]; }
        mac_cpu_write(cpu, ptr, 2, (uint16_t)y);
        mac_cpu_write(cpu, ptr + 2, 2, (uint16_t)x);
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        return mac_cpu_halted(cpu) ? -1 : 1;
    }
    if (opcode == 0xa974) {
        if (!valid(cpu, sp, 2)) return -1;
        mac_cpu_write(cpu, sp, 1, !(rt->modifiers & 0x80));
        return 1;
    }
    if (opcode < 0xa800 && (opcode & 0xff) == 0x2f) { /* PostEvent (from memory of the ABI): A0 = event code, D0 = message, result in D0 (evQErr -1 when full) */
        uint32_t code = mac_cpu_get(cpu, MAC_A0);   /* the whole register: a value above the word range is refused, not truncated */
        if (code == 0 || code >= 16) { mac_cpu_set(cpu, MAC_D0, (uint32_t)-1); return 1; } /* not a postable event: refused with an error, nothing queued */
        if (rt->event_count == 64) { mac_cpu_set(cpu, MAC_D0, (uint32_t)-1); return 1; }
        unsigned slot = (rt->event_head + rt->event_count) % 64;
        rt->events[slot] = (MacEvent){(uint16_t)code, rt->modifiers, mac_cpu_get(cpu, MAC_D0), rt->ticks, rt->mouse_x, rt->mouse_y};
        rt->event_count++;
        mac_cpu_set(cpu, MAC_D0, 0);
        return 1;
    }
    if (opcode < 0xa800 && (opcode & 0xff) == 0x32) { /* FlushEvents */
        uint16_t mask = (uint16_t)mac_cpu_get(cpu, MAC_D0);
        uint16_t stop = (uint16_t)(mac_cpu_get(cpu, MAC_D0) >> 16);
        unsigned keep = 0;
        int stopped = 0;
        uint16_t stop_event = 0;
        for (unsigned i = 0; i < rt->event_count; i++) {
            MacEvent e = rt->events[(rt->event_head + i) % 64];
            unsigned bit = e.what < 16 ? 1u << e.what : 0;
            if (!stopped && (stop & bit)) { stopped = 1; stop_event = e.what; }
            if (stopped || !(mask & bit)) rt->events[(rt->event_head + keep++) % 64] = e;
        }
        rt->event_count = keep;
        mac_cpu_set(cpu, MAC_D0, (mac_cpu_get(cpu, MAC_D0) & 0xffff0000u) | stop_event);
        return 1;
    }
    return 0;
}
