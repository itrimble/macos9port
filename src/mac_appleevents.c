#include "mac_runtime.h"
#include <stdio.h>

/* Apple Event Manager (_Pack8, $A816): the HANDLER-TABLE subset only.
 *
 * Calling convention, checked against the public MPW Pascal interface text (every routine below, argument sizes
 * summed): Pascal, routine in the low word of D0, HIGH byte = number of argument WORDS, low byte = routine number.
 * Arguments are at A7 on entry (no return address), last argument first; the 2-byte OSErr result slot sits just
 * above them. The trap pops the arguments and leaves the result there.
 *   $091F AEInstallEventHandler(class, id: OSType; handler: ProcPtr; refcon: LONGINT; isSys: BOOLEAN): OSErr  18 bytes
 *   $0720 AERemoveEventHandler(class, id: OSType; handler: ProcPtr; isSys: BOOLEAN): OSErr                    14 bytes
 *   $0921 AEGetEventHandler(class, id: OSType; VAR handler: ProcPtr; VAR refcon: LONGINT; isSys): OSErr       18 bytes
 *   $021D AEGetInteractionAllowed(VAR level: AEInteractAllowed): OSErr                                         4 bytes
 *   $011E AESetInteractionAllowed(level: AEInteractAllowed): OSErr                                             2 bytes
 *
 * HONEST SCOPE: handlers are RECORDED in host memory (never guest-visible). This runtime never delivers an Apple
 * Event, so no recorded handler is ever called. Descriptors, AEProcessAppleEvent, AEGetParamDesc, lists, replies and
 * calling guest handlers are future work; every other selector halts with its number. Gestalt 'evnt' is not answered.
 *
 * Unverified from the interface text (it states none of these): the wildcard lookup order in mac_ae_find_handler, the
 * errors for Remove of a missing handler, a full table and a bad level, and the default level (kAEInteractWithLocal).
 * Remove and Get match the exact stored key (a '****' key is an ordinary key); Remove ignores its handler argument. */
enum {
    ERR_HANDLER_NOT_FOUND = -1717, /* errAEHandlerNotFound */
    ERR_MEM_FULL = -108,           /* memFullErr: handler table full */
    ERR_PARAM = -50,               /* paramErr: level out of range */
    WILD = 0x2a2a2a2a              /* typeWildCard '****' */
};
enum { LEVEL_SELF = 0, LEVEL_LOCAL = 1, LEVEL_ALL = 2 };

static int fail(MacRuntime *rt, const char *why) {
    mac_cpu_halt(rt->cpu, why);
    return -1;
}

static unsigned *count_of(MacAppleEvents *ae, int sys) { return sys ? &ae->sys_count : &ae->app_count; }
static MacAeHandler *table_of(MacAppleEvents *ae, int sys) { return sys ? ae->sys : ae->app; }

static MacAeHandler *find_exact(MacAppleEvents *ae, int sys, uint32_t cls, uint32_t id) {
    MacAeHandler *t = table_of(ae, sys);
    for (unsigned i = 0, n = *count_of(ae, sys); i < n; i++)
        if (t[i].event_class == cls && t[i].event_id == id) return &t[i];
    return NULL;
}

int mac_ae_find_handler(const MacRuntime *rt, uint32_t cls, uint32_t id, uint32_t *handler, uint32_t *refcon) {
    if (!rt) return 0;
    MacAppleEvents *ae = (MacAppleEvents *)&rt->appleevents;
    const uint32_t cs[4] = {cls, cls, WILD, WILD}, is[4] = {id, WILD, id, WILD};
    for (int sys = 0; sys < 2; sys++)
        for (int k = 0; k < 4; k++) {
            MacAeHandler *h = find_exact(ae, sys, cs[k], is[k]);
            if (!h) continue;
            if (handler) *handler = h->handler;
            if (refcon) *refcon = h->refcon;
            return 1;
        }
    return 0;
}

static int valid_var(uint32_t pointer, size_t bytes, size_t ram) {
    return pointer && (bytes == 1 || !(pointer & 1)) && pointer <= ram && ram - pointer >= bytes;
}

int mac_appleevents_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa816) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    MacAppleEvents *ae = &rt->appleevents;
    uint32_t selector = mac_cpu_get(cpu, MAC_D0) & 0xffff, sp = mac_cpu_get(cpu, MAC_A7);
    unsigned words;
    switch (selector) {
    case 0x091F: case 0x0921: words = 9; break;
    case 0x0720: words = 7; break;
    case 0x021D: words = 2; break;
    case 0x011E: words = 1; break;
    default: {
        static char message[96];
        snprintf(message, sizeof message, "Apple Event Manager selector 0x%04x is unsupported.", (unsigned)selector);
        return fail(rt, message);
    }
    }
    size_t ram = mac_cpu_ram_size(cpu), arg_bytes = (size_t)words * 2;
    if ((sp & 1) || sp > ram || ram - sp < arg_bytes + 2) return fail(rt, "Apple Event Manager stack exceeds guest RAM.");
    int16_t result = 0;
    if (selector == 0x011E) {
        /* A one-byte argument occupies the HIGH byte of its stack word; the low byte is whatever was there before
         * (Glider PRO pushes 0x0274 for kAEInteractWithAll), so only the high byte is read. A caller that pushed a
         * full Pascal enum word (0x0002) is NOT recognised: it would read as kAEInteractWithSelf. */
        unsigned level = (unsigned)(mac_cpu_read(cpu, sp, 2) >> 8) & 0xff;
        if (level > LEVEL_ALL) result = ERR_PARAM;
        else { ae->level = (uint8_t)level; ae->level_set = 1; }
    } else if (selector == 0x021D) {
        uint32_t pointer = mac_cpu_read(cpu, sp, 4);
        if (!valid_var(pointer, 1, ram)) return fail(rt, "AEGetInteractionAllowed needs a valid level variable.");
        mac_cpu_write(cpu, pointer, 1, ae->level_set ? ae->level : LEVEL_LOCAL);   /* AEInteractAllowed is one byte */
    } else {
        /* isSys is the high byte of the word at sp; the OSType arguments follow the handler/refcon fields. */
        int sys = (mac_cpu_read(cpu, sp, 2) & 0xff00) != 0;
        uint32_t cls, id;
        if (selector == 0x091F) {          /* sp: isSys; +2 refcon; +6 handler; +10 id; +14 class */
            cls = mac_cpu_read(cpu, sp + 14, 4); id = mac_cpu_read(cpu, sp + 10, 4);
            MacAeHandler *h = find_exact(ae, sys, cls, id);
            if (!h && *count_of(ae, sys) == MAC_AE_HANDLERS) result = ERR_MEM_FULL;
            else {
                if (!h) h = &table_of(ae, sys)[(*count_of(ae, sys))++];
                *h = (MacAeHandler){cls, id, mac_cpu_read(cpu, sp + 6, 4), mac_cpu_read(cpu, sp + 2, 4)};
            }
        } else if (selector == 0x0720) {   /* sp: isSys; +2 handler (ignored); +6 id; +10 class */
            cls = mac_cpu_read(cpu, sp + 10, 4); id = mac_cpu_read(cpu, sp + 6, 4);
            MacAeHandler *h = find_exact(ae, sys, cls, id);
            if (!h) result = ERR_HANDLER_NOT_FOUND;
            else {
                MacAeHandler *t = table_of(ae, sys);
                unsigned *n = count_of(ae, sys);
                *h = t[--*n];              /* order is irrelevant: lookups are by exact key */
            }
        } else {                           /* 0x0921, sp: isSys; +2 VAR refcon; +6 VAR handler; +10 id; +14 class */
            uint32_t refcon_var = mac_cpu_read(cpu, sp + 2, 4), handler_var = mac_cpu_read(cpu, sp + 6, 4);
            if (!valid_var(refcon_var, 4, ram) || !valid_var(handler_var, 4, ram))
                return fail(rt, "AEGetEventHandler needs valid handler and refcon variables.");
            cls = mac_cpu_read(cpu, sp + 14, 4); id = mac_cpu_read(cpu, sp + 10, 4);
            MacAeHandler *h = find_exact(ae, sys, cls, id);
            if (!h) result = ERR_HANDLER_NOT_FOUND;   /* the VARs are left untouched */
            else { mac_cpu_write(cpu, handler_var, 4, h->handler); mac_cpu_write(cpu, refcon_var, 4, h->refcon); }
        }
    }
    mac_cpu_write(cpu, sp + (uint32_t)arg_bytes, 2, (uint16_t)result);
    mac_cpu_set(cpu, MAC_A7, sp + (uint32_t)arg_bytes);
    return 1;
}
