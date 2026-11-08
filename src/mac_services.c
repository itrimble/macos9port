#include "mac_runtime.h"
#include "mac_sound.h"
#include "mac_sound_double.h"
#include "mac_time.h"
#include "mac_system_volume.h"

#include <stdio.h>

static unsigned trap_key(uint16_t opcode) {
    return opcode & 0x800 ? 0x800u | (opcode & 0x3ff) : opcode & 0xff;
}

static uint32_t native_stub(unsigned key) { return 0x2000u + key * 8; }

static unsigned requested_key(uint16_t instruction, uint16_t requested) {
    if (instruction & 0x200)
        return instruction & 0x400 ? 0x800u | (requested & 0x3ff) : requested & 0xff;
    return trap_key(requested);
}

static int implemented(unsigned key) {
    if (key < 0x800) {
        switch (key) {
        case 0x58: case 0x59: case 0x5a: return 1;
        case 0x13: case 0x11: case 0x00: case 0x01: case 0x02: case 0x07: case 0x14: case 0x33: case 0x34: return 1;
        case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x21: case 0x22:
        case 0x23: case 0x24: case 0x25: case 0x29: case 0x2a: case 0x2d: case 0x2e:
        case 0x32: case 0x3b: case 0x40: case 0x46: case 0x47: case 0x49: case 0x4a:
        case 0x4c: case 0x4d: case 0x55: case 0x60: case 0x61: case 0x63: case 0x64: case 0x69: case 0x6a:
        case 0x80: case 0x90: case 0xad: case 0xbd: return 1;
        default: return 0;
        }
    }
    switch (0xa000 | key) {
    case 0xa800:
    case 0xa801: case 0xa803: case 0xa804: case 0xa807:
    case 0xa9eb: case 0xa9ee: case 0xa830: case 0xa816:
    case 0xa8f6:
    case 0xa997: case 0xa994: case 0xa998: case 0xa99a: case 0xa9a4:
    case 0xaa52:
    case 0xaa91: case 0xaa92: case 0xaa93: case 0xaa94: case 0xaa95: case 0xaa96: case 0xaaa2:
    case 0xa931: case 0xa932: case 0xa933: case 0xa934: case 0xa935: case 0xa936: case 0xa93c: case 0xa9c0:
    case 0xa939: case 0xa93a: case 0xa945: case 0xa949: case 0xa94d:
    case 0xa950: case 0xa9bf: case 0xa9bc:
    case 0xa9f0: case 0xa9f4: case 0xa9a0: case 0xa81f: case 0xa99d:
    case 0xa99c: case 0xa9a5: case 0xa9a6: case 0xa9a8: case 0xa9af:
    case 0xa9a2: case 0xa9a3: case 0xa992: case 0xa970: case 0xa971:
    case 0xa976: case 0xa860: case 0xa975: case 0xa972: case 0xa974: case 0xa86e: case 0xa870: case 0xa871: case 0xa878:
    case 0xa86d: case 0xa86f: case 0xa873: case 0xa874: case 0xa875: case 0xa87d:
    case 0xa8aa: case 0xa880: case 0xa937: case 0xa8a8: case 0xa8a7: case 0xa850: case 0xa8fe: case 0xa912: case 0xa930:
    case 0xa97b: case 0xa9cc: case 0xa910: case 0xa9b4:
    case 0xa87c: case 0xa879: case 0xa87a: case 0xa87b:
    case 0xa893: case 0xa891: case 0xa89b: case 0xa89d: case 0xa89a:
    case 0xa8a1: case 0xa8a2: case 0xa8a3: case 0xa8a4: case 0xa8a5:
    case 0xa89c: case 0xa896: case 0xa897: case 0xa89e: case 0xa8ad:
    case 0xa9b9: case 0xa851: case 0xa852: case 0xa853: case 0xa856: case 0xa98b:
    case 0xa8df: case 0xa8d8: case 0xa8d9:
    case 0xa91b: case 0xa91d: case 0xa909: case 0xaa46: case 0xa90d: case 0xaa45: case 0xa924: case 0xa922: case 0xa923: case 0xa915:
    case 0xa916: case 0xa91f: case 0xa920: case 0xa92d: case 0xa914:
    case 0xa917: case 0xa918: case 0xa919: case 0xa92c: case 0xa80a:
    case 0xab1d: case 0xaa03: case 0xaa04: case 0xaa24:
    case 0xaa00: case 0xaa01: case 0xaa02: case 0xa8ec:
    case 0xaa14: case 0xaa15: case 0xaa18: case 0xaa3e: case 0xaa40: case 0xaa3f:
    case 0xaa29: case 0xaa2a: case 0xaa2b: case 0xaa2c: case 0xaa31: case 0xaa32:
    case 0xaa1b: case 0xaa1c: case 0xaa1d: case 0xaa26: return 1;
    default: return 0;
    }
}

void mac_traps_init(MacRuntime *rt) {
    for (unsigned key = 0; key < 4096; key++) {
        /* HFSDispatch's shared OS vector needs its hierarchical flag when
         * invoked through a saved predecessor address. Other flags stay local
         * to their services; do not normalize every File Manager instruction. */
        mac_cpu_write(rt->cpu, native_stub(key), 2, key == 0x60 ? 0xa260u : key == 0x07 ? 0xa207u : 0xa000u | key);
        mac_cpu_write(rt->cpu, native_stub(key) + 2, 2, 0x4e75); /* RTS */
        rt->trap_addresses[key] = native_stub(key);
    }
}

static int dispatch(MacRuntime *rt, uint16_t opcode, uint32_t pc, uint32_t continuation) {
    MacCpu *cpu = rt->cpu;
    if (opcode < 0xa800 && (opcode & 0xff) == 0x46) { /* GetTrapAddress */
        unsigned key = requested_key(opcode, (uint16_t)mac_cpu_get(cpu, MAC_D0));
        uint32_t address = rt->trap_addresses[key];
        if (address == native_stub(key) && !implemented(key)) address = native_stub(0x89f);
        mac_cpu_set(cpu, MAC_A0, address);
        return 0;
    }
    if (opcode < 0xa800 && (opcode & 0xff) == 0x47) { /* SetTrapAddress */
        unsigned key = requested_key(opcode, (uint16_t)mac_cpu_get(cpu, MAC_D0));
        uint32_t address = mac_cpu_get(cpu, MAC_A0);
        if ((address & 1) || address >= mac_cpu_ram_size(cpu) - 1) {
            mac_cpu_halt(cpu, "Invalid guest trap-handler address.");
            return -1;
        }
        rt->trap_addresses[key] = address;
        return 0;
    }
    if (opcode == 0xa9f0) { /* LoadSeg, direct or chained through a guest patch. */
        uint32_t sp = mac_cpu_get(cpu, MAC_A7);
        int16_t id = (int16_t)mac_cpu_read(cpu, sp, 2);
        if (mac_runtime_load_segment(rt, id)) {
            mac_cpu_halt(cpu, "LoadSeg could not load the requested CODE resource.");
            return -1;
        }
        mac_cpu_set(cpu, MAC_A7, sp + 2);
        mac_cpu_set(cpu, MAC_PC, continuation ? continuation - 6 : pc - 4);
        return 0;
    }
    int timer = mac_time_service(rt, opcode);
    if (timer) return timer > 0 ? 0 : -1;
    int delay = mac_delay_service(rt, opcode);
    if (delay) return delay > 0 ? 0 : -1;
    int double_sound = mac_sound_double_service(rt, opcode);
    if (double_sound) return double_sound > 0 ? 0 : -1;
    int sound = mac_sound_service(rt, opcode);
    if (sound) return sound > 0 ? 0 : -1;
    int sane = mac_sane_service(rt, opcode);
    if (sane) return sane > 0 ? 0 : -1;
    int numconv = mac_numconv_service(rt, opcode);
    if (numconv) return numconv > 0 ? 0 : -1;
    int help = mac_help_service(rt, opcode);
    if (help) return help > 0 ? 0 : -1;
    int appleevents = mac_appleevents_service(rt, opcode);
    if (appleevents) return appleevents > 0 ? 0 : -1;
    int memory = mac_memory_service(rt, opcode);
    if (memory) return memory > 0 ? 0 : -1;
    int resource = mac_resource_service(rt, opcode);
    if (resource) return resource > 0 ? 0 : -1;
    int event = mac_event_service(rt, opcode);
    if (event) return event > 0 ? 0 : -1;
    int quickdraw = mac_quickdraw_service(rt, opcode);
    if (quickdraw) return quickdraw > 0 ? 0 : -1;
    int menu = mac_menu_service(rt, opcode);
    if (menu) return menu > 0 ? 0 : -1;
    int toolbox = mac_toolbox_service(rt, opcode);
    if (toolbox) return toolbox > 0 ? 0 : -1;
    int color_raster = mac_color_raster_service(rt, opcode);
    if (color_raster) return color_raster > 0 ? 0 : -1;
    int color_pen = mac_color_pen_service(rt, opcode);
    if (color_pen) return color_pen > 0 ? 0 : -1;
    int raster = mac_raster_service(rt, opcode);
    if (raster) return raster > 0 ? 0 : -1;
    int system = mac_system_service(rt, opcode);
    if (system) return system > 0 ? 0 : -1;
    int geometry = mac_geometry_service(rt, opcode);
    if (geometry) return geometry > 0 ? 0 : -1;
    int system_file = mac_system_volume_service(rt, rt->system_volume, opcode);
    if (system_file) return system_file > 0 ? 0 : -1;
    int file = mac_file_service(rt, opcode);
    if (file) return file > 0 ? 0 : -1;
    int cursor = mac_cursor_service(rt, opcode);
    if (cursor) return cursor > 0 ? 0 : -1;
    int ccursor = mac_ccursor_service(rt, opcode);
    if (ccursor) return ccursor > 0 ? 0 : -1;
    int dialog = mac_dialog_service(rt, opcode);
    if (dialog) return dialog > 0 ? 0 : -1;
    int color_port = mac_color_port_service(rt, opcode);
    if (color_port) return color_port > 0 ? 0 : -1;
    int color = mac_color_service(rt, opcode);
    if (color) return color > 0 ? 0 : -1;
    int picture = mac_picture_service(rt, opcode);
    if (picture) return picture > 0 ? 0 : -1;
    int copy = mac_copybits_service(rt, opcode);
    if (copy) return copy > 0 ? 0 : -1;
    int palette = mac_palette_service(rt, opcode);
    if (palette) return palette > 0 ? 0 : -1;
    int video_config = mac_video_config_service(rt, opcode);
    if (video_config) return video_config > 0 ? 0 : -1;
    int regions = mac_regions_service(rt, opcode);
    if (regions) return regions > 0 ? 0 : -1;
    int palette_manager = mac_palette_manager_service(rt, opcode);
    if (palette_manager) return palette_manager > 0 ? 0 : -1;
    int resource_window = mac_window_resource_service(rt, opcode);
    if (resource_window) return resource_window > 0 ? 0 : -1;
    int window = mac_window_service(rt, opcode);
    if (window) return window > 0 ? 0 : -1;
    int pixels = mac_pixels_service(rt, opcode);
    if (pixels) return pixels > 0 ? 0 : -1;
    int pixmap = mac_pixmap_service(rt, opcode);
    if (pixmap) return pixmap > 0 ? 0 : -1;
    int vbl = mac_vbl_service(rt, opcode);
    if (vbl) return vbl > 0 ? 0 : -1;
    if (opcode == 0xa9f4) {
        rt->exited = 1;
        mac_cpu_halt(cpu, "Guest called ExitToShell.");
        return 0;
    }
    return -1;
}

int mac_runtime_trap(MacCpu *cpu, uint16_t opcode, uint32_t pc, void *user) {
    MacRuntime *rt = user;
    rt->traps++;
    rt->last_trap = opcode;
    rt->last_trap_pc = pc;
    if (rt->trace) fprintf(stderr, "trap %04x pc=%08x sp=%08x d0=%08x a0=%08x\n",
                           opcode, pc, mac_cpu_get(cpu, MAC_A7),
                           mac_cpu_get(cpu, MAC_D0), mac_cpu_get(cpu, MAC_A0));
    unsigned key = trap_key(opcode);
    uint32_t stub = native_stub(key);
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (pc == stub) {
        /* A guest JMP/JSR to its predecessor handler has a real continuation
         * on the stack. Native services expose only Pascal arguments. */
        uint32_t continuation = mac_cpu_read(cpu, sp, 4);
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        int rc = dispatch(rt, opcode, pc, continuation);
        if (!rc && !mac_cpu_halted(cpu) && opcode != 0xa9f0)
            mac_cpu_set(cpu, MAC_PC, continuation);
        return rc;
    }
    if (rt->trap_addresses[key] != stub) {
        if (sp < 4 || mac_cpu_write(cpu, sp - 4, 4, pc + 2)) return -1;
        mac_cpu_set(cpu, MAC_A7, sp - 4);
        mac_cpu_set(cpu, MAC_PC, rt->trap_addresses[key]);
        return 0;
    }
    return dispatch(rt, opcode, pc, 0);
}
