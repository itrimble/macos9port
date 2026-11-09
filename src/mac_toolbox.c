#include "mac_runtime.h"

/* Initialization of our own manager state. Full managers are separate gates;
 * recognizing these initializers does not advertise unimplemented operations. */
int mac_toolbox_service(MacRuntime *rt, uint16_t opcode) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (opcode == 0xa8fe) { /* InitFonts: select the independent default face. */
        rt->manager_flags |= 1;
        return 1;
    }
    if (opcode == 0xa912) { /* InitWindows: create the Window Manager GrafPort. */
        if (sp < 4 || sp > mac_cpu_ram_size(cpu)) return -1;
        if (!rt->wm_port) rt->wm_port = mac_runtime_alloc(rt, 108);
        if (!rt->wm_port) { mac_cpu_halt(cpu, "No memory for Window Manager port."); return -1; }
        mac_cpu_write(cpu, sp - 4, 4, rt->wm_port);
        mac_cpu_set(cpu, MAC_A7, sp - 4);
        int rc = mac_quickdraw_service(rt, 0xa86f);
        if (rc != 1) return -1;
        mac_cpu_write(cpu, 0x9d6, 4, 0); /* WindowList */
        mac_cpu_write(cpu, 0x9de, 4, rt->wm_port);
        if (!mac_cpu_read(cpu, 0x9ee, 4)) {   /* GrayRgn: the desktop, the screen below the menu bar (a plain rectangle: no rounded screen corners) */
            uint32_t gray = mac_region_new_rect(rt, 20, 0, rt->screen_height ? rt->screen_height : 480, rt->screen_width ? rt->screen_width : 640);
            mac_cpu_write(cpu, 0x9ee, 4, gray);
        }
        mac_cpu_write(cpu, 0x9da, 2, 1); /* SaveUpdate */
        mac_cpu_write(cpu, 0x9dc, 2, 1); /* PaintWhite */
        rt->manager_flags |= 2;
        return 1;
    }
    if (opcode == 0xa930) { /* InitMenus: empty MenuList handle. */
        if (!rt->menu_bar) {
            uint32_t d0 = mac_cpu_get(cpu, MAC_D0), a0 = mac_cpu_get(cpu, MAC_A0);
            mac_cpu_set(cpu, MAC_D0, 2);
            mac_memory_service(rt, 0xa122);
            rt->menu_bar = mac_cpu_get(cpu, MAC_A0);
            mac_cpu_set(cpu, MAC_D0, d0); mac_cpu_set(cpu, MAC_A0, a0);
        }
        if (!rt->menu_bar) { mac_cpu_halt(cpu, "No memory for Menu Manager."); return -1; }
        mac_cpu_write(cpu, 0xa1c, 4, rt->menu_bar);
        mac_cpu_write(cpu, 0xbaa, 2, 20); /* Authored default menu height. */
        rt->manager_flags |= 4;
        return mac_menu_service(rt, 0xa937);
    }
    if (opcode == 0xa97b) { /* InitDialogs: retain application resume procedure. */
        if ((sp & 1) || sp > mac_cpu_ram_size(cpu) - 4) return -1;
        rt->dialog_resume = mac_cpu_read(cpu, sp, 4);
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        rt->manager_flags |= 8;
        return 1;
    }
    if (opcode == 0xa9cc) { /* TEInit: empty application text-edit state. */
        rt->manager_flags |= 16;
        return 1;
    }
    if (opcode == 0xa910) { /* GetWMgrPort */
        uint32_t out = mac_cpu_read(cpu, sp, 4);
        mac_cpu_write(cpu, out, 4, rt->wm_port);
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        return mac_cpu_halted(cpu) ? -1 : 1;
    }
    if (opcode == 0xa9b4) { /* SystemTask: no desk accessories in this process. */
        return 1;
    }
    return 0;
}
