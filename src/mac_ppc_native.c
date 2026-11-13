#include "mac_ppc.h"

#include <string.h>

/* InterfaceLib symbols that have no A-line trap: low-memory accessors, string helpers, A5 access and small glue
 * routines the classic libraries implemented in code. Each is written directly against the same guest RAM and
 * low-memory globals the 68K services use. Low-memory addresses are public interface facts, written from memory. */

typedef struct { uint16_t address; uint8_t width, mode; } LowMem; /* mode: 0 read, 1 write, 2 address of the global */
#define LM_GET(name, address, width) {"InterfaceLib", name, low_memory, &(const LowMem){address, width, 0}}
#define LM_SET(name, address, width) {"InterfaceLib", name, low_memory, &(const LowMem){address, width, 1}}
#define LM_PTR(name, address) {"InterfaceLib", name, low_memory, &(const LowMem){address, 4, 2}}

static int low_memory(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    const LowMem *lm = rt->ppc->fragment.calling->data;
    if (lm->mode == 2) c->gpr[3] = lm->address;
    else if (lm->mode == 1) mac_cpu_write(rt->cpu, lm->address, lm->width, lm->width == 4 ? c->gpr[3] : c->gpr[3] & (lm->width == 2 ? 0xffffu : 0xffu));
    else {
        uint32_t v = mac_cpu_read(rt->cpu, lm->address, lm->width);
        c->gpr[3] = lm->width == 2 ? (uint32_t)(int32_t)(int16_t)v : v;
    }
    return 0;
}

static int set_current_a5(PpcCpu *c, void *user) { /* A5 := CurrentA5, returns the old A5 */
    MacRuntime *rt = user;
    c->gpr[3] = mac_cpu_get(rt->cpu, MAC_A5);
    mac_cpu_set(rt->cpu, MAC_A5, mac_cpu_read(rt->cpu, 0x904, 4));
    return 0;
}

static int set_a5(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    uint32_t old = mac_cpu_get(rt->cpu, MAC_A5);
    mac_cpu_set(rt->cpu, MAC_A5, c->gpr[3]);
    c->gpr[3] = old;
    return 0;
}

static int c2pstr(PpcCpu *c, void *user) { /* in place; longer than 255 characters is truncated, as the glue did */
    (void)user;
    uint8_t *s = ppc_mem_span(c, c->gpr[3], 1);
    const uint8_t *end = s ? memchr(s, 0, c->ram_size - c->gpr[3]) : NULL;
    if (!end) { ppc_cpu_halt(c, "c2pstr was given a string outside guest RAM at %08x.", c->gpr[3]); return 0; }
    size_t n = (size_t)(end - s) > 255 ? 255 : (size_t)(end - s);
    memmove(s + 1, s, n);
    s[0] = (uint8_t)n;
    return 0;
}

static int p2cstr(PpcCpu *c, void *user) {
    (void)user;
    uint8_t *s = ppc_mem_span(c, c->gpr[3], 256);
    if (!s) { ppc_cpu_halt(c, "p2cstr was given a string outside guest RAM at %08x.", c->gpr[3]); return 0; }
    size_t n = s[0];
    memmove(s, s + 1, n);
    s[n] = 0;
    return 0;
}

static int debugger(PpcCpu *c, void *user) {
    mac_ppc_fail(user, "The application called Debugger (from %08x).", c->lr - 4);
    return 0;
}

static int debug_str(PpcCpu *c, void *user) {
    char text[120] = "";
    const uint8_t *s = ppc_mem_span(c, c->gpr[3], 256);
    if (s) { size_t n = s[0] < sizeof text - 1 ? s[0] : sizeof text - 1; memcpy(text, s + 1, n); text[n] = 0; }
    mac_ppc_fail(user, "The application called DebugStr: %s", text);
    return 0;
}

/* NGetTrapAddress(trapNum, trapType): the OS or Toolbox form of GetTrapAddress. */
static int n_get_trap_address(PpcCpu *c, void *user) {
    uint32_t args[1] = {c->gpr[3]}, result = 0;
    if (!mac_ppc_invoke(user, "NGetTrapAddress", (c->gpr[4] & 0xffu) ? 0xa746 : 0xa346, "@D0>A0", args, 1, &result)) c->gpr[3] = result;
    return 0;
}

static int get_trap_address(PpcCpu *c, void *user) {
    uint32_t args[1] = {c->gpr[3]}, result = 0;
    if (!mac_ppc_invoke(user, "GetTrapAddress", 0xa146, "@D0>A0", args, 1, &result)) c->gpr[3] = result;
    return 0;
}

static int flush_events(PpcCpu *c, void *user) { /* D0 = stop mask in the high word, event mask in the low word */
    uint32_t args[1] = {((c->gpr[4] & 0xffffu) << 16) | (c->gpr[3] & 0xffffu)};
    mac_ppc_invoke(user, "FlushEvents", 0xa032, "@D0>", args, 1, NULL);
    return 0;
}

static int hlock_hi(PpcCpu *c, void *user) { /* MoveHHi then HLock */
    uint32_t args[1] = {c->gpr[3]};
    if (!mac_ppc_invoke(user, "HLockHi", 0xa064, "@A0>", args, 1, NULL)) mac_ppc_invoke(user, "HLockHi", 0xa029, "@A0>", args, 1, NULL);
    return 0;
}

static int os_event(PpcCpu *c, void *user, const char *name, uint16_t trap) { /* D0 comes back 0 when an event is returned */
    uint32_t args[2] = {c->gpr[3] & 0xffffu, c->gpr[4]}, result = 0;
    if (!mac_ppc_invoke(user, name, trap, "@D0,A0>D0b", args, 2, &result)) c->gpr[3] = result == 0;
    return 0;
}
static int get_os_event(PpcCpu *c, void *user) { return os_event(c, user, "GetOSEvent", 0xa031); }
static int os_event_avail(PpcCpu *c, void *user) { return os_event(c, user, "OSEventAvail", 0xa030); }

/* GetIndString(theString, strListID, index): library glue over GetResource('STR#'). An absent list or index gives
 * the empty string. */
static int get_ind_string(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    uint32_t args[2] = {UINT32_C(0x53545223), c->gpr[4]}, handle = 0, out = c->gpr[3];
    int index = (int16_t)c->gpr[5];
    uint8_t *dst = ppc_mem_span(c, out, 256);
    if (!dst) { ppc_cpu_halt(c, "GetIndString was given a string outside guest RAM at %08x.", out); return 0; }
    if (mac_ppc_invoke(rt, "GetIndString", 0xa9a0, "l:lw", args, 2, &handle)) return 0;
    dst[0] = 0;
    uint32_t p = handle ? mac_cpu_read(rt->cpu, handle, 4) : 0, size = 0;
    for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle == handle && handle) size = rt->handles[i].size;
    const uint8_t *list = p ? ppc_mem_span(c, p, size) : NULL;
    if (!list || size < 2 || index < 1 || index > ((list[0] << 8) | list[1])) return 0;
    uint32_t at = 2;
    for (int i = 1; i < index && at < size; i++) at += 1u + list[at];
    if (at < size && at + 1u + list[at] <= size) memcpy(dst, list + at, 1u + list[at]);
    return 0;
}

/* GetIndPattern(VAR thePattern, patListID, index): library glue over GetResource('PAT#'): a count word, then that many 8-byte patterns. An absent list
 * or an index outside it leaves the pattern as it was. */
static int get_ind_pattern(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    uint32_t args[2] = {UINT32_C(0x50415423), c->gpr[4] & 0xffffu}, handle = 0;
    int index = (int16_t)c->gpr[5];
    uint8_t *dst = ppc_mem_span(c, c->gpr[3], 8);
    if (!dst) { ppc_cpu_halt(c, "GetIndPattern was given a pattern outside guest RAM at %08x.", c->gpr[3]); return 0; }
    if (mac_ppc_invoke(rt, "GetResource", 0xa9a0, "l:lw", args, 2, &handle)) return 0;
    uint32_t p = handle ? mac_cpu_read(rt->cpu, handle, 4) : 0, size = 0;
    for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle == handle && handle) size = rt->handles[i].size;
    const uint8_t *list = p ? ppc_mem_span(c, p, size) : NULL;
    if (!list || size < 2 || index < 1 || index > ((list[0] << 8) | list[1]) || size < 2u + 8u * (unsigned)index) return 0;
    memcpy(dst, list + 2 + 8 * (index - 1), 8);
    return 0;
}

static int get_date_time(PpcCpu *c, void *user) { /* GetDateTime(&secs): the Time global */
    MacRuntime *rt = user;
    ppc_mem_write(c, c->gpr[3], 4, mac_cpu_read(rt->cpu, 0x20c, 4));
    return 0;
}

/* TESetClickLoop(clickProc, hTE) and TESetWordBreak(wordBreak, hTE): library glue that stores the routine pointer in the TERec (clikLoop at +42,
 * wordBreak at +38). The TextEdit here never calls either routine: it has no drag tracking and breaks words itself. */
static int te_set_proc(PpcCpu *c, void *user, unsigned offset, const char *name) {
    MacRuntime *rt = user;
    size_t z = mac_cpu_ram_size(rt->cpu);
    uint32_t h = c->gpr[4], rec = h >= 0x400 && h <= z - 4 ? mac_cpu_read(rt->cpu, h, 4) : 0;
    if (!rec || rec > z - 100) { ppc_cpu_halt(c, "%s was given a TextEdit handle outside guest RAM (%08x).", name, h); return 0; }
    mac_cpu_write(rt->cpu, rec + offset, 4, c->gpr[3]);
    return 0;
}
static int te_set_click_loop(PpcCpu *c, void *user) { return te_set_proc(c, user, 42, "TESetClickLoop"); }
static int te_set_word_break(PpcCpu *c, void *user) { return te_set_proc(c, user, 38, "TESetWordBreak"); }

/* EqualString(str1, str2, caseSensitive, diacSensitive): Boolean. Library glue over the string comparison; as in mac_cmpstring.c only the ASCII
 * letters fold and bytes above 127 compare literally, so the diacritical flag changes nothing here. */
static int equal_string(PpcCpu *c, void *user) {
    (void)user;
    const uint8_t *a = ppc_mem_span(c, c->gpr[3], 1), *b = ppc_mem_span(c, c->gpr[4], 1);
    if (!a || !b) { ppc_cpu_halt(c, "EqualString was given a string outside guest RAM."); return 0; }
    a = ppc_mem_span(c, c->gpr[3], 1u + a[0]); b = ppc_mem_span(c, c->gpr[4], 1u + b[0]);
    if (!a || !b) { ppc_cpu_halt(c, "EqualString was given a string outside guest RAM."); return 0; }
    int sensitive = (c->gpr[5] & 0xffu) != 0, same = a[0] == b[0];
    for (unsigned i = 1; same && i <= a[0]; i++) {
        unsigned x = a[i], y = b[i];
        if (!sensitive) { if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; }
        same = x == y;
    }
    c->gpr[3] = same;
    return 0;
}

/* IUEqualString(aStr, bStr): INTEGER, 0 when the strings are equal without regard to case and 1 when not (Operating System Utilities, international
 * utilities; from memory, and with only the ASCII letters folded, like the comparison above). */
static int iu_equal_string(PpcCpu *c, void *user) {
    c->gpr[5] = 0;
    equal_string(c, user);
    c->gpr[3] = c->gpr[3] ? 0 : 1;
    return 0;
}

#define N(name, fn) {"InterfaceLib", name, fn, NULL}
static const MacCfmHostImport imports[] = {
    LM_GET("MemError", 0x220, 2), LM_GET("LMGetMemErr", 0x220, 2),
    LM_GET("GetMBarHeight", 0xbaa, 2), LM_GET("LMGetMBarHeight", 0xbaa, 2),
    LM_GET("LMGetSysFontFam", 0xba6, 2), LM_SET("LMSetSysFontFam", 0xba6, 2),
    LM_GET("LMGetSysFontSize", 0xba8, 2), LM_SET("LMSetSysFontSize", 0xba8, 2),
    LM_GET("LMGetHiliteMode", 0x938, 1), LM_SET("LMSetHiliteMode", 0x938, 1),
    LM_GET("LMGetTEScrpHandle", 0xab4, 4), LM_SET("LMSetTEScrpHandle", 0xab4, 4),
    LM_GET("LMGetTEScrpLength", 0xab0, 2), LM_SET("LMSetTEScrpLength", 0xab0, 2),
    LM_SET("LMSetLastSPExtra", 0xb4c, 4), LM_GET("LMGetROM85", 0x28e, 2), LM_GET("LMGetTime", 0x20c, 4),
    LM_SET("LMSetCurActivate", 0xa64, 4), LM_SET("LMSetCurDeactive", 0xa68, 4), LM_SET("LMSetSEvtEnb", 0x15c, 1),
    LM_GET("GZSaveHnd", 0x328, 4), LM_GET("GetDblTime", 0x2f0, 4), LM_GET("GetCaretTime", 0x2f4, 4),
    LM_GET("GetGrayRgn", 0x9ee, 4), LM_GET("LMGetGrayRgn", 0x9ee, 4), LM_GET("GetSysFont", 0xba6, 2),
    LM_GET("GetDefFontSize", 0xba8, 2), LM_GET("LMGetCurrentA5", 0x904, 4), LM_GET("LMGetTicks", 0x16a, 4),
    LM_PTR("LMGetEventQueue", 0x14a), LM_PTR("LMGetCurApName", 0x910),
    N("SetCurrentA5", set_current_a5), N("SetA5", set_a5), N("c2pstr", c2pstr), N("p2cstr", p2cstr),
    N("C2PStr", c2pstr), N("P2CStr", p2cstr), N("Debugger", debugger), N("DebugStr", debug_str),
    N("NGetTrapAddress", n_get_trap_address), N("GetTrapAddress", get_trap_address), N("FlushEvents", flush_events),
    N("HLockHi", hlock_hi), N("GetOSEvent", get_os_event), N("OSEventAvail", os_event_avail),
    N("GetIndString", get_ind_string), N("GetIndPattern", get_ind_pattern), N("GetDateTime", get_date_time),
    N("EqualString", equal_string), N("IUEqualString", iu_equal_string), N("TESetClickLoop", te_set_click_loop), N("TESetWordBreak", te_set_word_break),
};

const MacCfmHostImport *mac_ppc_native_imports(size_t *count) {
    *count = sizeof imports / sizeof imports[0];
    return imports;
}
