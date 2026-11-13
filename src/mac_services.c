#include "mac_runtime.h"
#include "mac_sound.h"
#include "mac_sound_double.h"
#include "mac_sound_volume.h"
#include "mac_scrap.h"
#include "mac_time.h"
#include "mac_system_volume.h"
#include "mac_rescomp.h"

#include <stdio.h>

static unsigned trap_key(uint16_t opcode) {
    return opcode & 0x800 ? 0x800u | (opcode & 0x3ff) : opcode & 0xff;
}

static uint32_t native_stub(unsigned key) { return 0x2000u + key * 8; }
uint32_t mac_native_stub(unsigned key) { return native_stub(key); } /* the guest-callable address of a trap's stub */

static unsigned requested_key(uint16_t instruction, uint16_t requested) {
    if (instruction & 0x200)
        return instruction & 0x400 ? 0x800u | (requested & 0x3ff) : requested & 0xff;
    return trap_key(requested);
}

static int implemented(unsigned key) {
    if (mac_env_implemented(key)) return 1;
    if (key < 0x800) {
        switch (key) {
        case 0x58: case 0x59: case 0x5a: return 1;
        case 0x13: case 0x11: case 0x00: case 0x01: case 0x02: case 0x07: case 0x0a: case 0x14: case 0x15: case 0x3c: case 0x33: case 0x34: return 1;
        case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x21: case 0x22: case 0x28:
        case 0x23: case 0x24: case 0x25: case 0x29: case 0x2a: case 0x2d: case 0x2e: case 0x2f:
        case 0x32: case 0x3b: case 0x40: case 0x46: case 0x47: case 0x49: case 0x4a:
        case 0x4c: case 0x4d: case 0x55: case 0x60: case 0x61: case 0x63: case 0x64: case 0x69: case 0x6a:
        case 0x80: case 0x90: case 0xad: case 0xbd: case 0xfc: return 1;
        default: return 0;
        }
    }
    switch (0xa000 | key) {
    case 0xa800:
    case 0xa801: case 0xa803: case 0xa804: case 0xa807:
    case 0xa9fa: case 0xa9fb: case 0xa9eb: case 0xa9ee: case 0xa830: case 0xa816: case 0xa9fc: case 0xa9fd: case 0xa9fe: case 0xa99b: case 0xa9e1: case 0xa9e3: case 0xa9ef: case 0xa9b1: case 0xa823: case 0xa904: case 0xa953: case 0xa954: case 0xa955: case 0xa956: case 0xa957: case 0xa958: case 0xa959: case 0xa95a: case 0xa95b: case 0xa95c: case 0xa95d: case 0xa95e: case 0xa95f: case 0xa960: case 0xa961: case 0xa962: case 0xa963: case 0xa964: case 0xa965: case 0xa966: case 0xa967: case 0xa968: case 0xa969: case 0xa96d: case 0xa9e7: case 0xa83e: case 0xa9ed: case 0xa96a: case 0xa96b: case 0xa96c: case 0xa9be:
    case 0xa8f6: case 0xa826: case 0xa913: case 0xa9bd: case 0xa8ff: case 0xa900: case 0xa902: case 0xaa07: case 0xaa08: case 0xaa0c: case 0xaa0d: case 0xaa0e: case 0xaa0f: case 0xaa10:
    case 0xa9c4: case 0xa997: case 0xa994: case 0xa998: case 0xa99a: case 0xa9a4:
    case 0xaa52:
    case 0xa827: case 0xa828: case 0xa817: case 0xa8b0: case 0xa8b1: case 0xa8b2: case 0xa8b3: case 0xa8b4: case 0xa8b7: case 0xa8b8: case 0xa8b9: case 0xa8ba: case 0xa8bb: case 0xa004: case 0xa204: case 0xa404: case 0xa604: case 0xa97c: case 0xa991: case 0xa981: case 0xa982: case 0xa983: case 0xa98d: case 0xa990: case 0xa98f: case 0xa97e: case 0xa98c: case 0xaa68: case 0xa97d: case 0xa98e: case 0xa980: case 0xa97f: case 0xa984: case 0xa978: case 0xa979: case 0xa97a: case 0xa162: case 0xa05d: case 0xa858: case 0xa859: case 0xa85a: case 0xa85b: case 0xa85c: case 0xa85d: case 0xa85e: case 0xa85f: case 0xa86a: case 0xa86b: case 0xa861: case 0xa93d: case 0xa938: case 0xa93e: case 0xaa4e: case 0xaa97: case 0xaa98: case 0xa83d: case 0xa9ce: case 0xa9cb: case 0xa9cc: case 0xa9cd: case 0xa9cf: case 0xa9d1: case 0xa9d2: case 0xa9d3: case 0xa9d4: case 0xa9d8: case 0xa9d9: case 0xa9da: case 0xa9dc: case 0xa9df: case 0xa883: case 0xa884: case 0xa885: case 0xa886: case 0xa887: case 0xa888: case 0xa889: case 0xa88a: case 0xa88b: case 0xa88c: case 0xa88d: case 0xa88e: case 0xaa91: case 0xaa92: case 0xaa93: case 0xaa94: case 0xaa95: case 0xaa96: case 0xaa9a: case 0xaaa2:
    case 0xa931: case 0xa932: case 0xa933: case 0xa934: case 0xa935: case 0xa936: case 0xa93c: case 0xa9c0:
    case 0xa939: case 0xa93a: case 0xa945: case 0xa947: case 0xa949: case 0xa94d:
    case 0xa950: case 0xa9bf: case 0xa9bc: case 0xa9c8: case 0xaa1e: case 0xaa1f: case 0xaa25: case 0xa80b: case 0xa941: case 0xa946: case 0xa948: case 0xa84e: case 0xa943: case 0xa93f: case 0xaa64: case 0xa942: case 0xa944: case 0xa84f: case 0xa940:
    case 0xa985: case 0xa986: case 0xa987: case 0xa988: case 0xa9ea: case 0xa9f0: case 0xa9f1: case 0xa9f4: case 0xa9a0: case 0xa81f: case 0xa99d:
    case 0xa99c: case 0xa80d: case 0xa821: case 0xa9a5: case 0xa9a6: case 0xa9a8: case 0xa9af:
    case 0xa9ad: case 0xa9f6: case 0xa9f7: case 0xa993: case 0xa9c1: case 0xa810:
    case 0xa9a2: case 0xa9a3: case 0xa992: case 0xa970: case 0xa971:
    case 0xa976: case 0xa860: case 0xa975: case 0xa972: case 0xa974: case 0xa86e: case 0xa870: case 0xa871: case 0xa878: case 0xa876: case 0xa877:
    case 0xa86d: case 0xa86f: case 0xa873: case 0xa874: case 0xa875: case 0xa87d:
    case 0xa87e: case 0xa87f: case 0xa8aa: case 0xa8ab: case 0xa8a6: case 0xa880: case 0xa937: case 0xa8a8: case 0xa8a7: case 0xa850: case 0xa8fe: case 0xa912: case 0xa930:
    case 0xa97b: case 0xa910: case 0xa9b4:
    case 0xa87c: case 0xa879: case 0xa87a: case 0xa87b:
    case 0xa893: case 0xa891: case 0xa89b: case 0xa89d: case 0xa89a: case 0xa898: case 0xa899:
    case 0xa8a1: case 0xa8a2: case 0xa8a3: case 0xa8a4: case 0xa8a5:
    case 0xa89c: case 0xa896: case 0xa897: case 0xa89e: case 0xa8ad:
    case 0xa9b9: case 0xa851: case 0xa852: case 0xa853: case 0xa855: case 0xa856: case 0xa98b:
    case 0xa8df: case 0xa8dd: case 0xa8d8: case 0xa8d9: case 0xa8da: case 0xa8db: case 0xa8dc: case 0xa8de: case 0xa8e0: case 0xa8e1: case 0xa8e2: case 0xa8e3: case 0xa8e4: case 0xa8e5: case 0xa8e6: case 0xa8e7: case 0xa8e8: case 0xa8e9:
    case 0xa90a: case 0xa908: case 0xa927: case 0xa928: case 0xa929: case 0xa92a: case 0xa91b: case 0xa91d: case 0xa909: case 0xaa46: case 0xa90d: case 0xaa45: case 0xa924: case 0xa922: case 0xa923: case 0xa915:
    case 0xa916: case 0xa91f: case 0xa920: case 0xa92d: case 0xa914:
    case 0xa917: case 0xa918: case 0xa919: case 0xa91a: case 0xa91c: case 0xa92c: case 0xa80a:
    case 0xab1d: case 0xaa03: case 0xaa04: case 0xaa24:
    case 0xaa00: case 0xaa01: case 0xaa02: case 0xa8ec:
    case 0xaa14: case 0xaa15: case 0xaa33: case 0xaa34: case 0xaa19: case 0xaa1a: case 0xaa18: case 0xaa3e: case 0xaa40: case 0xaa3f:
    case 0xaa27: case 0xaa29: case 0xaa2a: case 0xaa2b: case 0xaa2c: case 0xaa31: case 0xaa32:
    case 0xaa1b: case 0xaa1c: case 0xaa1d: case 0xaa26: case 0xa8b5: case 0xa8fd: case 0xa88f: return 1;
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

/* LoadSeg, finished: the segment is runnable (any load hook has run), so the jump-table entries are rewritten and the call is retried. */
static struct { uint32_t sp, pc, continuation; int16_t id; } loadseg;
static int loadseg_finish(MacRuntime *rt, int error) {
    MacCpu *cpu = rt->cpu;
    if (error) {
        char msg[400];
        snprintf(msg, sizeof msg, "LoadSeg could not expand CODE %d: %s", loadseg.id, mac_rescomp_why(rt)[0] ? mac_rescomp_why(rt) : "the load hook failed.");
        mac_cpu_halt(cpu, msg);
        return -1;
    }
    if (mac_runtime_load_segment(rt, loadseg.id)) {
        mac_cpu_halt(cpu, "LoadSeg could not load the requested CODE resource.");
        return -1;
    }
    mac_cpu_set(cpu, MAC_A7, loadseg.sp + 2);
    mac_cpu_set(cpu, MAC_PC, loadseg.continuation ? loadseg.continuation - 6 : loadseg.pc - 4);
    return 0;
}
static int loadseg_done(MacRuntime *rt, void *ctx, int error) { (void)ctx; return loadseg_finish(rt, error); }

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
        if (rt->init_guard && (key == 0x46 || key == 0x47 || key == 0x9f0 || key == 0x9f4)) {
            rt->init_violation = 1; /* protected: refuse, tell the INIT paramErr */
            mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)-50);
            return 0;
        }
        if ((address & 1) || address >= mac_cpu_ram_size(cpu) - 1) {
            mac_cpu_halt(cpu, "Invalid guest trap-handler address.");
            return -1;
        }
        rt->trap_addresses[key] = address;
        if (key == 0xfc && address != native_stub(0xfc)) {
            int pre = mac_rescomp_prepass(rt);   /* the program just installed a resource load hook: run it over the marked resources now */
            if (pre < 0) return -1;
        }
        return 0;
    }
    if (opcode == 0xa9f0 && rt->init_guard) { /* Segments belong to the host, not to an INIT. */
        mac_cpu_halt(cpu, "An INIT called LoadSeg.");
        return -1;
    }
    if (opcode == 0xa9f0) { /* LoadSeg, direct or chained through a guest patch. */
        uint32_t sp = mac_cpu_get(cpu, MAC_A7);
        int16_t id = (int16_t)mac_cpu_read(cpu, sp, 2);
        /* The same jump-table entry asking for the same segment again and again, with no other trap between: the code it reached put the entry back and
         * re-entered it. A self-loading stub does that when it expects LoadSeg to fetch the segment through Resource Manager traps it patched; here the
         * segment comes straight from the fork, so nothing would ever change. */
        if (rt->loadseg.trap + 1 == rt->traps && rt->loadseg.pc == pc && rt->loadseg.id == id) rt->loadseg.repeats++; else rt->loadseg.repeats = 0;
        rt->loadseg.trap = rt->traps; rt->loadseg.pc = pc; rt->loadseg.id = id;
        if (rt->loadseg.repeats >= 64) {
            char msg[200]; snprintf(msg, sizeof msg, "LoadSeg for CODE %d is re-entered from the same jump-table entry without progress (a self-loading stub that expects its patched Resource Manager traps to supply the segment; not supported).", id);
            mac_cpu_halt(cpu, msg);
            return -1;
        }
        /* The segment's resource may need guest code before it is runnable (a compressed CODE resource, a program's own load hook). */
        uint32_t seg = mac_runtime_resource(rt, MAC_CODE_TYPE, id);
        if (seg) {
            loadseg.sp = sp; loadseg.pc = pc; loadseg.continuation = continuation; loadseg.id = id;
            int started = mac_rescomp_begin(rt, seg, loadseg_done, NULL);
            if (started) return started < 0 ? -1 : 0;
        }
        return loadseg_finish(rt, 0);
    }
    if (opcode == 0xa9f1) { /* UnloadSeg(routineAddr): a purgeability hint. Segments here stay resident, which the
                             * contract allows; a later call through the jump table keeps working unchanged. */
        uint32_t sp = mac_cpu_get(cpu, MAC_A7), address = mac_cpu_read(cpu, sp, 4);
        uint32_t delta = address - rt->jump_table - 2; /* routine addresses are jump-table entry + 2 */
        if (address < rt->jump_table + 2 || delta >= rt->jump_size || (delta & 7)) {
            mac_cpu_halt(cpu, "UnloadSeg requires the address of a jump-table routine entry.");
            return -1;
        }
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        return 0;
    }
    mac_rgn_record_observe(rt, opcode);
    /* first, so that FrontWindow and SetPort see an open dialog before the window and QuickDraw services do */
    int dlgmgr = mac_dlgmgr_service(rt, opcode);
    if (dlgmgr) return dlgmgr > 0 ? 0 : -1;
    int timer = mac_time_service(rt, opcode);
    if (timer) return timer > 0 ? 0 : -1;
    int delay = mac_delay_service(rt, opcode);
    if (delay) return delay > 0 ? 0 : -1;
    int tbox = mac_textbox_service(rt, opcode);
    if (tbox) return tbox > 0 ? 0 : -1;
    int beep = mac_sysbeep_service(rt, opcode);
    if (beep) return beep > 0 ? 0 : -1;
    int cicn = mac_cicn_service(rt, opcode);
    if (cicn) return cicn > 0 ? 0 : -1;
    int controls = mac_controls_service(rt, opcode);
    if (controls) return controls > 0 ? 0 : -1;
    int alias = mac_alias_service(rt, opcode);
    if (alias) return alias > 0 ? 0 : -1;
    int cmp = mac_cmpstring_service(rt, opcode);
    if (cmp) return cmp > 0 ? 0 : -1;
    int crf = mac_files_crf_service(rt, opcode);
    if (crf) return crf > 0 ? 0 : -1;
    int colorindex = mac_colorindex_service(rt, opcode);
    if (colorindex) return colorindex > 0 ? 0 : -1;
    int handcopy = mac_handcopy_service(rt, opcode);
    if (handcopy) return handcopy > 0 ? 0 : -1;
    int rescomp = mac_rescomp_service(rt, opcode);
    if (rescomp) return rescomp > 0 ? 0 : -1;
    int resload = mac_resload_service(rt, opcode);
    if (resload) return resload > 0 ? 0 : -1;
    int scrap = mac_scrap_service(rt, opcode);
    if (scrap) return scrap > 0 ? 0 : -1;
    int volume = mac_sound_volume_service(rt, opcode);
    if (volume) return volume > 0 ? 0 : -1;
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
    int menusel = mac_menuselect_service(rt, opcode);
    if (menusel) return menusel > 0 ? 0 : -1;
    int gworld = mac_gworld_service(rt, opcode);
    if (gworld) return gworld > 0 ? 0 : -1;
    int printing = mac_print_service(rt, opcode);
    if (printing) return printing > 0 ? 0 : -1;
    int intl = mac_intl_service(rt, opcode);
    if (intl) return intl > 0 ? 0 : -1;
    int list = mac_list_service(rt, opcode);
    if (list) return list > 0 ? 0 : -1;
    int script = mac_script_service(rt, opcode);
    if (script) return script > 0 ? 0 : -1;
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
    int appfork = mac_appfork_service(rt, opcode);
    if (appfork) return appfork > 0 ? 0 : -1;
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
    int stdfile = mac_stdfile_service(rt, opcode);
    if (stdfile) return stdfile > 0 ? 0 : -1;
    int alert = mac_alert_service(rt, opcode);
    if (alert) return alert > 0 ? 0 : -1;
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
    int inval = mac_window_inval_service(rt, opcode);
    if (inval) return inval > 0 ? 0 : -1;
    int pixels = mac_pixels_service(rt, opcode);
    if (pixels) return pixels > 0 ? 0 : -1;
    int pixmap = mac_pixmap_service(rt, opcode);
    if (pixmap) return pixmap > 0 ? 0 : -1;
    int vbl = mac_vbl_service(rt, opcode);
    if (vbl) return vbl > 0 ? 0 : -1;
    int env = mac_env_service(rt, opcode);
    if (env) return env > 0 ? 0 : -1;
    int process = mac_process_service(rt, opcode);
    if (process) return process > 0 ? 0 : -1;
    int random = mac_random_service(rt, opcode);
    if (random) return random > 0 ? 0 : -1;
    int text_state = mac_text_state_service(rt, opcode);
    if (text_state) return text_state > 0 ? 0 : -1;
    int text = mac_text_service(rt, opcode);
    if (text) return text > 0 ? 0 : -1;
    int shapes = mac_shapes_service(rt, opcode);
    if (shapes) return shapes > 0 ? 0 : -1;
    int devmgr = mac_devmgr_service(rt, opcode);
    if (devmgr) return devmgr > 0 ? 0 : -1;
    int toolutil = mac_toolutil_service(rt, opcode);
    if (toolutil) return toolutil > 0 ? 0 : -1;
    int stdprocs = mac_stdprocs_service(rt, opcode);
    if (stdprocs) return stdprocs > 0 ? 0 : -1;
    int textedit = mac_textedit_service(rt, opcode);
    if (textedit) return textedit > 0 ? 0 : -1;
    int pixpat = mac_pixpat_service(rt, opcode);
    if (pixpat) return pixpat > 0 ? 0 : -1;
    int fontnames = mac_fontnames_service(rt, opcode);
    if (fontnames) return fontnames > 0 ? 0 : -1;
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
    if (rt->guest.active && pc == native_stub(0xfe) && opcode == 0xa0fe) return mac_guest_return(rt);
    if (opcode == 0xaafe && rt->ppc) return mac_ppc_mixed_trap(rt, pc); /* a routine descriptor: switch to PowerPC */
    if (pc == stub) {
        /* A guest JMP/JSR to its predecessor handler has a real continuation
         * on the stack. Native services expose only Pascal arguments. */
        uint32_t continuation = mac_cpu_read(cpu, sp, 4);
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        int inside_guest_call = rt->guest.active;   /* a patch running as a guest call may chain to a native stub: that returns to its caller as usual */
        int rc = dispatch(rt, opcode, pc, continuation);
        if (!rc && !mac_cpu_halted(cpu) && opcode != 0xa9f0 && (inside_guest_call || !rt->guest.active))
            mac_cpu_set(cpu, MAC_PC, continuation);
        return rc;
    }
    if (rt->trap_addresses[key] != stub) {
        if (sp < 4 || mac_cpu_write(cpu, sp - 4, 4, pc + 2)) return -1;
        mac_cpu_set(cpu, MAC_A7, sp - 4);
        mac_cpu_set(cpu, MAC_PC, rt->trap_addresses[key]);
        return 0;
    }
    int rc = dispatch(rt, opcode, pc, 0);
    if (rt->trace && opcode < 0xa800 && !rt->guest.active) fprintf(stderr, "  result d0=%d\n", (int32_t)mac_cpu_get(cpu, MAC_D0));   /* an OS trap's error code */
    return rc;
}
