#include "app_classify_int.h"

#include <string.h>

/* Executable-format readers. Every read is bounds-checked against the buffer; a damaged structure is reported, never
 * followed. Layouts: PEF container header, section header and loader header as published for the classic Mac OS
 * runtime architecture; Mach-O fat header and mach_header cputype only. */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static int fits(size_t off, size_t len, size_t total) { return off <= total && len <= total - off; }

/* Import libraries shipped with the classic Mac OS that a PowerPC program can link against. A name that is not here is
 * reported as unlisted: it may still be classic (an application's own shared library), so it is not proof of anything. */
static const char *const classic_libs[] = {
    "InterfaceLib", "MathLib", "StdCLib", "ThreadsLib", "DragLib", "QuickTimeLib", "AppearanceLib", "NavigationLib",
    "ControlsLib", "WindowsLib", "MenusLib", "DialogsLib", "ObjectSupportLib", "DiskInitLib", "LanguageSystemLib",
    "SpeechLib", "InternetConfigLib", "PrintingLib", "OpenTptInternetLib", "OpenTransportLib", "TextCommonLib",
    "UnicodeConverterLib", "UnicodeConverter", "TextCommon", "ATSUnicodeLib", "ContextualMenuLib", "FontManagerLib", "HelpMgrLib", "TextServicesLib"
};

static int is_classic_lib(const char *name) {
    for (size_t i = 0; i < sizeof classic_libs / sizeof *classic_libs; i++)
        if (!strcmp(name, classic_libs[i])) return 1;
    return 0;
}

static int pef_loader(const uint8_t *p, size_t n, size_t off, size_t len, AppClassification *c) {
    if (len < 56 || !fits(off, len, n)) return -1;
    const uint8_t *l = p + off;
    int32_t main_section = (int32_t)be32(l);
    c->pef_has_main = main_section >= 0;
    uint32_t libs = be32(l + 24), strings = be32(l + 40);
    if (libs > (len - 56) / 24 || strings > len) return -1;
    c->pef_lib_total = libs;
    for (uint32_t i = 0; i < libs; i++) {
        const uint8_t *e = l + 56 + (size_t)i * 24;
        uint32_t name_off = be32(e);
        if (name_off >= len - strings) return -1;
        const char *name = (const char *)l + strings + name_off;
        size_t max = len - strings - name_off, k = 0;
        while (k < max && name[k]) k++;
        if (k == max) return -1;  /* unterminated library name */
        char shown[APP_LIB_NAME];
        size_t take = k < sizeof shown - 1 ? k : sizeof shown - 1;
        memcpy(shown, name, take);
        shown[take] = 0;
        for (size_t j = 0; j < take; j++) if ((unsigned char)shown[j] < 32 || (unsigned char)shown[j] > 126) shown[j] = '?';
        if (i < APP_MAX_LIBS) memcpy(c->pef_libs[i], shown, take + 1);
        /* CarbonLib is the classic-side Carbon library; "Apple;Carbon;..." are the OS X framework bundle names a Carbon
         * program also lists. Either way the program needs the Carbon API set, not just InterfaceLib. */
        if (!strcmp(shown, "CarbonLib") || !strncmp(shown, "Apple;Carbon;", 13)) { c->pef_needs_carbon = 1; c->pef_lib_known++; }
        else if (is_classic_lib(shown)) c->pef_lib_known++;
        else c->pef_lib_unlisted++;
    }
    return 0;
}

AppExecResult app_exec_pef(const uint8_t *p, size_t n, AppClassification *c) {
    if (!p || n < 12 || memcmp(p, "Joy!peff", 8)) return APP_EXEC_ABSENT;
    uint32_t arch = be32(p + 8);
    if (n < 40 || (arch != 0x70777063u && arch != 0x6d36386bu) || be32(p + 12) != 1) return APP_EXEC_DAMAGED;
    unsigned sections = be16(p + 32);
    if (!sections || sections > (n - 40) / 28) return APP_EXEC_DAMAGED;
    c->pef_arch = arch;
    c->pef_sections = sections;
    int loader = 0;
    for (unsigned i = 0; i < sections; i++) {
        const uint8_t *s = p + 40 + (size_t)i * 28;
        uint32_t packed = be32(s + 16), where = be32(s + 20);
        if (!fits(where, packed, n)) return APP_EXEC_DAMAGED;
        if (s[24] == 4) {  /* loader section */
            if (loader++) return APP_EXEC_DAMAGED;
            if (pef_loader(p, n, where, packed, c)) return APP_EXEC_DAMAGED;
        }
    }
    if (!loader) return APP_EXEC_DAMAGED;
    c->pef_valid = 1;
    return APP_EXEC_OK;
}

/* cfrg: 32-byte header (member count at +30) then variable-size members; each member starts with the architecture,
 * has its usage byte at +22 and carries its own size at +40 (the name starts at +42). */
AppExecResult app_exec_cfrg(const uint8_t *p, size_t n, AppClassification *c) {
    if (!p || n < 32) return APP_EXEC_DAMAGED;
    unsigned count = be16(p + 30);
    size_t off = 32;
    for (unsigned i = 0; i < count; i++) {
        if (!fits(off, 42, n)) return APP_EXEC_DAMAGED;
        const uint8_t *m = p + off;
        size_t size = be16(m + 40);
        /* Real resources end inside the last member's padding, so only a member that is followed by another one must fit. */
        if (size < 42 || (i + 1 < count && !fits(off, size, n))) return APP_EXEC_DAMAGED;
        uint32_t arch = be32(m);
        c->cfrg_members++;
        if (arch == 0x70777063u) c->cfrg_arch |= APP_ARCH_PEF_PPC;
        else if (arch == 0x6d36386bu) c->cfrg_arch |= APP_ARCH_PEF_M68K;
        if (m[22] == 1) c->cfrg_app_members++;  /* kApplicationCFrag */
        off += size;
    }
    return APP_EXEC_OK;
}

AppExecResult app_exec_macho(const uint8_t *p, size_t n, AppClassification *c) {
    if (!p || n < 8) return APP_EXEC_ABSENT;
    uint32_t magic = be32(p), cpu = 0;
    if (magic == 0xfeedfaceu || magic == 0xfeedfacfu) cpu = be32(p + 4);
    else if (magic == 0xcefaedfeu || magic == 0xcffaedfeu) cpu = le32(p + 4);
    else if (magic == 0xcafebabeu) {  /* fat header, shared with Java class files: demand a plausible slice table */
        uint32_t slices = be32(p + 4);
        if (!slices || slices > 16 || !fits(8, (size_t)slices * 20, n)) return APP_EXEC_ABSENT;
        for (uint32_t i = 0; i < slices; i++) {
            uint32_t t = be32(p + 8 + (size_t)i * 20), offset = be32(p + 8 + (size_t)i * 20 + 8);
            unsigned lo = t & 0xffffu, hi = t >> 16;
            if (lo == 0 || lo > 0x40 || (hi != 0 && hi != 0x0100) || offset >= n) return APP_EXEC_ABSENT;
            if (i == 0) cpu = t;
            c->arch |= (t & 0xffffu) == 18 ? APP_ARCH_MACHO_PPC : (t & 0xffffu) == 7 ? APP_ARCH_MACHO_X86 : APP_ARCH_MACHO_OTHER;
        }
        c->macho_cputype = cpu;
        return APP_EXEC_OK;
    } else return APP_EXEC_ABSENT;
    c->macho_cputype = cpu;
    unsigned base = cpu & 0xffffu;
    c->arch |= base == 18 ? APP_ARCH_MACHO_PPC : base == 7 ? APP_ARCH_MACHO_X86 : APP_ARCH_MACHO_OTHER;
    return APP_EXEC_OK;
}

/* Order-independent hash of one resource: the same CODE set gives the same identity however the map is ordered. */
uint64_t app_hash_resource(uint32_t type, int16_t id, const uint8_t *data, size_t size) {
    uint64_t h = 1469598103934665603ull;
    uint8_t head[8] = {(uint8_t)(type >> 24), (uint8_t)(type >> 16), (uint8_t)(type >> 8), (uint8_t)type,
                       (uint8_t)((uint16_t)id >> 8), (uint8_t)id, 0, 0};
    for (int i = 0; i < 6; i++) { h ^= head[i]; h *= 1099511628211ull; }
    for (size_t i = 0; i < size; i++) { h ^= data[i]; h *= 1099511628211ull; }
    h ^= size; h *= 1099511628211ull;
    h ^= h >> 29; h *= 0xbf58476d1ce4e5b9ull; h ^= h >> 32;  /* mix so that XOR-combining stays well spread */
    return h;
}
