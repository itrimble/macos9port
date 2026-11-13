#include "mac_runtime.h"
#include "mac_font.h"

/* Font Manager name calls, answered from the fonts the guest application carries in its own resource fork (this runtime installs no font files):
 *   GetFNum  ($A900) PROCEDURE GetFNum(name: Str255; VAR familyID: INTEGER)   stack from SP: familyID pointer, name pointer
 *   GetFName ($A8FF) PROCEDURE GetFontName(familyID: INTEGER; VAR name: Str255) stack from SP: name pointer, familyID
 *   RealFont ($A902) FUNCTION RealFont(fontNum, size: INTEGER): BOOLEAN         stack from SP: size, fontNum, result
 * A family's name is the resource name of its FOND (the resource ID is the family number) or of the FONT whose ID is family * 128 (the size-0 name
 * holder). Names compare without regard to ASCII case. A name nobody carries gives family 0 and a family nobody carries gives the empty string, which
 * is what the calls do for a font that is not installed. RealFont is true when mac_font_find_exact has a plain strike of that family and size (what is drawn for any other face is a substitute, see mac_font.h), which for
 * families 0 and 1 (the system and application font stand-ins) is any size from 1 to 255. */
#define FOND_T MAC_RESOURCE_TYPE('F', 'O', 'N', 'D')
#define FONT_T MAC_RESOURCE_TYPE('F', 'O', 'N', 'T')

static int fold(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int same(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    if (an != bn) return 0;
    for (size_t i = 0; i < an; i++) if (fold(a[i]) != fold(b[i])) return 0;
    return 1;
}
static int family_of(const MacResource *r, int *family) {
    if (r->type == FOND_T) { *family = r->id; return 1; }
    if (r->type == FONT_T && r->id >= 0 && !(r->id & 127)) { *family = r->id >> 7; return 1; }
    return 0;
}
static int ram_ok(MacCpu *cpu, uint32_t p, uint32_t n) {
    size_t z = mac_cpu_ram_size(cpu);
    return p >= 0x400 && p <= z && z - p >= n;
}

int mac_fontnames_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa900 && opcode != 0xa8ff && opcode != 0xa902) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if ((sp & 1) || !ram_ok(cpu, sp, 8)) { mac_cpu_halt(cpu, "Font Manager name call has an invalid stack."); return -1; }
    size_t count = mac_resource_count(&rt->fork);
    MacResource r;
    int family;
    if (opcode == 0xa902) { /* RealFont */
        int size = (int16_t)mac_cpu_read(cpu, sp, 2), fam = (int16_t)mac_cpu_read(cpu, sp + 2, 2);
        MacStrike strike; const char *why = NULL;
        int real = size > 0 && size <= 255 && fam >= 0 && fam <= 32767 && mac_font_find_exact(&rt->fork, fam, size, 0, &strike, &why) == 0;
        mac_cpu_write(cpu, sp + 4, 2, real ? 0x0100 : 0);
        mac_cpu_set(cpu, MAC_A7, sp + 4);
        return 1;
    }
    if (opcode == 0xa900) { /* GetFNum */
        uint32_t out = mac_cpu_read(cpu, sp, 4), name = mac_cpu_read(cpu, sp + 4, 4);
        if (!ram_ok(cpu, out, 2) || !ram_ok(cpu, name, 1) || !ram_ok(cpu, name, 1u + mac_cpu_read(cpu, name, 1))) {
            mac_cpu_halt(cpu, "GetFNum pointer is invalid."); return -1;
        }
        const uint8_t *want = mac_cpu_ram(cpu) + name;
        int found = 0;
        for (size_t i = 0; i < count && !found; i++)
            if (!mac_resource_at(&rt->fork, i, &r) && r.name && family_of(&r, &family) && same(r.name, r.name_size, want + 1, want[0])) found = family;
        mac_cpu_write(cpu, out, 2, (uint16_t)found);
        mac_cpu_set(cpu, MAC_A7, sp + 8);
        return 1;
    }
    uint32_t out = mac_cpu_read(cpu, sp, 4); /* GetFontName */
    int want = (int16_t)mac_cpu_read(cpu, sp + 4, 2);
    if (!ram_ok(cpu, out, 256)) { mac_cpu_halt(cpu, "GetFontName pointer is invalid."); return -1; }
    uint8_t *dst = mac_cpu_ram(cpu) + out;
    dst[0] = 0;
    for (size_t i = 0; i < count; i++)
        if (!mac_resource_at(&rt->fork, i, &r) && r.name && r.name_size <= 255 && family_of(&r, &family) && family == want) {
            dst[0] = (uint8_t)r.name_size;
            for (size_t k = 0; k < r.name_size; k++) dst[1 + k] = r.name[k];
            break;
        }
    mac_cpu_set(cpu, MAC_A7, sp + 6);
    return 1;
}
