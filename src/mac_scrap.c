#include "mac_scrap.h"
#include <stdlib.h>
#include <string.h>

/* A small private clipboard held on the host. Pascal signatures from the MPW interface listing (opened this session):
 *   FUNCTION ZeroScrap: LONGINT                                         result at SP
 *   FUNCTION PutScrap(length: LONGINT; theType: ResType; source: Ptr): LONGINT     source SP, type SP+4, length SP+8, result SP+12
 *   FUNCTION GetScrap(hDest: Handle; theType: ResType; VAR offset: LONGINT): LONGINT   offset SP, type SP+4, hDest SP+8, result SP+12
 * Results of 0 are noErr; GetScrap returns the length, or noTypeErr (-102) when the scrap holds no item of that type. That error wording and
 * code are from my memory of the Scrap Manager, not from a document I opened. Items are kept per type (at most 8 types, 64 KB each); a new
 * item of a type already present replaces it (the real manager appends; no application here depends on that). GetScrap of a type that IS
 * present would have to resize the destination handle, which is not done: it halts. The scrap is not shared with the host clipboard. */
enum { MAX_ITEMS = 8, MAX_BYTES = 65536 };
typedef struct MacScrap { unsigned count; uint32_t type[MAX_ITEMS]; uint32_t size[MAX_ITEMS]; uint8_t *data[MAX_ITEMS]; } Scrap;

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static void clear(Scrap *s) { for (unsigned i = 0; i < s->count; i++) free(s->data[i]); memset(s, 0, sizeof *s); }
void mac_scrap_destroy(MacRuntime *rt) { if (rt->scrap) { clear(rt->scrap); free(rt->scrap); rt->scrap = NULL; } }

int mac_scrap_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9fa && opcode != 0xa9fb && opcode != 0xa9fc && opcode != 0xa9fd && opcode != 0xa9fe) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    unsigned args = opcode == 0xa9fc || opcode == 0xa9fa || opcode == 0xa9fb ? 0 : 12;
    if ((sp & 1) || !ok(rt, sp, args + 4)) return fail(rt, "Scrap Manager call has an invalid stack.");
    if (!rt->scrap && !(rt->scrap = calloc(1, sizeof(Scrap)))) return fail(rt, "Scrap Manager could not allocate its scrap.");
    Scrap *s = rt->scrap; int32_t result = 0;
    if (opcode == 0xa9fc) clear(s);                                                   /* ZeroScrap */
    else if (opcode == 0xa9fa || opcode == 0xa9fb) result = 0;                        /* UnloadScrap / LoadScrap: the scrap lives in host memory, so there is nothing to move to or from a scrap file; noErr (the signatures, FUNCTION ...: LONGINT with no arguments, are from memory) */
    else if (opcode == 0xa9fe) {                                                      /* PutScrap */
        uint32_t src = mac_cpu_read(cpu, sp, 4), type = mac_cpu_read(cpu, sp + 4, 4); int32_t len = (int32_t)mac_cpu_read(cpu, sp + 8, 4);
        if (len < 0 || len > MAX_BYTES) return fail(rt, "PutScrap length is negative or larger than the 64 KB this scrap keeps.");
        if (len && !ok(rt, src, (size_t)len)) return fail(rt, "PutScrap source is outside guest RAM.");
        unsigned slot = s->count;
        for (unsigned i = 0; i < s->count; i++) if (s->type[i] == type) slot = i;
        if (slot == MAX_ITEMS) return fail(rt, "PutScrap: the scrap already holds 8 types.");
        uint8_t *copy = malloc(len ? (size_t)len : 1);
        if (!copy) return fail(rt, "PutScrap could not allocate.");
        if (len) memcpy(copy, mac_cpu_ram(cpu) + src, (size_t)len);
        if (slot < s->count) free(s->data[slot]); else s->count++;
        s->type[slot] = type; s->size[slot] = (uint32_t)len; s->data[slot] = copy;
    } else {                                                                          /* GetScrap */
        uint32_t type = mac_cpu_read(cpu, sp + 4, 4), dest = mac_cpu_read(cpu, sp + 8, 4), off = mac_cpu_read(cpu, sp, 4);
        if (!ok(rt, off, 4)) return fail(rt, "GetScrap offset pointer is outside guest RAM.");
        int have = 0; for (unsigned i = 0; i < s->count; i++) if (s->type[i] == type) have = 1;
        if (have) return fail(rt, "GetScrap of a type the scrap holds needs to resize the destination handle: not implemented.");
        (void)dest; result = -102;                                                    /* noTypeErr: nothing is written to the handle or the offset */
    }
    mac_cpu_write(cpu, sp + args, 4, (uint32_t)result);
    mac_cpu_set(cpu, MAC_A7, sp + args);
    return 1;
}
