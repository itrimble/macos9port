#ifndef PEF_FIXTURE_H
#define PEF_FIXTURE_H

/* Builds synthetic PEF containers for tests: section 0 is code, section 1 is
 * data (raw or pattern-initialized) and section 2 is the loader section. The
 * bytes are assembled field by field from the public container layout; no
 * real fragment is involved. */

#include <stdint.h>
#include <string.h>

typedef struct { const char *name; uint8_t symbol_class; } FixImport; /* class byte, 0x80 marks a weak import */
typedef struct { const char *name; uint32_t first, count; } FixLibrary;
typedef struct { const char *name; uint32_t value; int16_t section; uint8_t symbol_class; } FixExport;

typedef struct {
    const uint8_t *code;
    uint32_t code_size;
    const uint8_t *data;
    uint32_t data_packed, data_unpacked, data_total, data_default_address;
    uint8_t data_kind; /* 1 raw, 2 pattern-initialized */
    const FixLibrary *libraries;
    uint32_t library_count;
    const FixImport *imports;
    uint32_t import_count;
    const uint16_t *reloc;
    uint32_t reloc_count;
    uint16_t reloc_section;
    int32_t main_section;
    uint32_t main_offset;
    const FixExport *exports;
    uint32_t export_count;
} PefFixture;

#define FIX_UNUSED __attribute__((unused))
#define FIX_SECTION_HEADER(i) (40u + 28u * (i))
#define FIX_CODE_AT 128u

static FIX_UNUSED void fix16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static FIX_UNUSED void fix32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static FIX_UNUSED uint32_t fix_get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static FIX_UNUSED void fix_section(uint8_t *h, uint32_t total, uint32_t unpacked, uint32_t packed, uint32_t offset,
                                   uint8_t kind, uint32_t default_address) {
    fix32(h, UINT32_C(0xffffffff)); /* unnamed */
    fix32(h + 4, default_address);
    fix32(h + 8, total);
    fix32(h + 12, unpacked);
    fix32(h + 16, packed);
    fix32(h + 20, offset);
    h[24] = kind;
    h[25] = 1; /* process share */
    h[26] = 2; /* 4-byte alignment */
}

/* Returns the container size, or 0 when it does not fit in `cap` bytes. `*loader_at` receives the loader offset. */
static FIX_UNUSED size_t pef_fixture_build(const PefFixture *f, uint8_t *out, size_t cap, uint32_t *loader_at) {
    uint8_t strings[2048];
    uint32_t string_size = 0, library_names[16], import_names[64], export_names[16];
    if (f->library_count > 16 || f->import_count > 64 || f->export_count > 16) return 0;
#define FIX_ADD_STRING(dst, text) do { \
        size_t n_ = strlen(text) + 1; \
        if (string_size + n_ > sizeof strings) return 0; \
        (dst) = string_size; memcpy(strings + string_size, (text), n_); string_size += (uint32_t)n_; \
    } while (0)
    for (uint32_t i = 0; i < f->library_count; i++) FIX_ADD_STRING(library_names[i], f->libraries[i].name);
    for (uint32_t i = 0; i < f->import_count; i++) FIX_ADD_STRING(import_names[i], f->imports[i].name);
    for (uint32_t i = 0; i < f->export_count; i++) FIX_ADD_STRING(export_names[i], f->exports[i].name);
#undef FIX_ADD_STRING
    uint32_t reloc_headers = f->reloc_count ? 1u : 0u;
    uint32_t reloc_at = 56 + 24 * f->library_count + 4 * f->import_count + 12 * reloc_headers;
    uint32_t strings_at = reloc_at + 2 * f->reloc_count;
    uint32_t hash_at = (strings_at + string_size + 3u) & ~3u;
    uint32_t loader_size = hash_at + (f->export_count ? 4 + 14 * f->export_count : 0);
    uint32_t data_at = FIX_CODE_AT + ((f->code_size + 3u) & ~3u);
    uint32_t loader = data_at + ((f->data_packed + 3u) & ~3u);
    size_t size = (size_t)loader + loader_size;
    if (size > cap) return 0;
    memset(out, 0, size);
    memcpy(out, "Joy!peffpwpc", 12);
    fix32(out + 12, 1);
    fix32(out + 28, 0x01008000); /* current version */
    fix16(out + 32, 3);
    fix16(out + 34, 2);
    fix_section(out + FIX_SECTION_HEADER(0), f->code_size, f->code_size, f->code_size, FIX_CODE_AT, 0, 0);
    fix_section(out + FIX_SECTION_HEADER(1), f->data_total, f->data_unpacked, f->data_packed, data_at, f->data_kind,
                f->data_default_address);
    fix_section(out + FIX_SECTION_HEADER(2), 0, 0, loader_size, loader, 4, 0);
    if (f->code_size) memcpy(out + FIX_CODE_AT, f->code, f->code_size);
    if (f->data_packed) memcpy(out + data_at, f->data, f->data_packed);
    uint8_t *l = out + loader;
    fix32(l, (uint32_t)f->main_section);
    fix32(l + 4, f->main_offset);
    fix32(l + 8, UINT32_C(0xffffffff));
    fix32(l + 16, UINT32_C(0xffffffff));
    fix32(l + 24, f->library_count);
    fix32(l + 28, f->import_count);
    fix32(l + 32, reloc_headers);
    fix32(l + 36, reloc_at);
    fix32(l + 40, strings_at);
    fix32(l + 44, hash_at);
    fix32(l + 48, 0);
    fix32(l + 52, f->export_count);
    uint8_t *p = l + 56;
    for (uint32_t i = 0; i < f->library_count; i++, p += 24) {
        fix32(p, library_names[i]);
        fix32(p + 12, f->libraries[i].count);
        fix32(p + 16, f->libraries[i].first);
    }
    for (uint32_t i = 0; i < f->import_count; i++, p += 4)
        fix32(p, ((uint32_t)f->imports[i].symbol_class << 24) | import_names[i]);
    if (reloc_headers) {
        fix16(p, f->reloc_section);
        fix32(p + 4, f->reloc_count);
        fix32(p + 8, 0);
    }
    for (uint32_t i = 0; i < f->reloc_count; i++) fix16(l + reloc_at + 2 * i, f->reloc[i]);
    memcpy(l + strings_at, strings, string_size);
    if (f->export_count) {
        fix32(l + hash_at, f->export_count << 18); /* one hash slot holding the whole chain */
        for (uint32_t i = 0; i < f->export_count; i++) {
            uint8_t *symbol = l + hash_at + 4 + 4 * f->export_count + 10 * i;
            fix32(l + hash_at + 4 + 4 * i, (uint32_t)strlen(f->exports[i].name) << 16); /* length; hash word unused */
            fix32(symbol, ((uint32_t)f->exports[i].symbol_class << 24) | export_names[i]);
            fix32(symbol + 4, f->exports[i].value);
            fix16(symbol + 8, (uint16_t)f->exports[i].section);
        }
    }
    if (loader_at) *loader_at = loader;
    return size;
}

#endif
