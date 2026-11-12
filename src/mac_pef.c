#include "mac_pef.h"

#include <stdio.h>
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int fail(char *err, size_t n, const char *message) {
    if (err && n) snprintf(err, n, "%s", message);
    return -1;
}

const char *mac_pef_kind_name(uint8_t kind) {
    static const char *const names[] = {"code", "data", "pattern-data", "constant", "loader",
                                        "debug", "executable-data", "exception", "traceback"};
    return kind < sizeof names / sizeof names[0] ? names[kind] : "unknown";
}

int mac_pef_instantiated(uint8_t kind) {
    return kind == MAC_PEF_CODE || kind == MAC_PEF_DATA || kind == MAC_PEF_PATTERN_DATA ||
           kind == MAC_PEF_CONSTANT || kind == MAC_PEF_EXEC_DATA;
}

/* NUL-terminated string in the loader string table, or NULL when it runs off the section. */
static const char *loader_string(const MacPef *pef, uint32_t offset) {
    if (pef->strings_at > pef->loader_size || offset >= pef->loader_size - pef->strings_at) return NULL;
    const uint8_t *start = pef->loader + pef->strings_at + offset;
    size_t room = pef->loader_size - pef->strings_at - offset;
    return memchr(start, 0, room) ? (const char *)start : NULL;
}

/* 1 when `count` records of `record` bytes starting at `at` fit inside `limit` bytes. */
static int fits(uint64_t at, uint64_t count, uint64_t record, uint64_t limit) {
    return at <= limit && count * record <= limit - at;
}

static int parse_loader(MacPef *pef, char *err, size_t n) {
    const MacPefSection *s = &pef->sections[pef->loader_index];
    const uint8_t *l = pef->bytes + s->container_offset;
    uint32_t size = s->packed_size;
    if (size < 56) return fail(err, n, "PEF loader section is shorter than its header.");
    pef->loader = l;
    pef->loader_size = size;
    pef->main_section = (int32_t)be32(l);
    pef->main_offset = be32(l + 4);
    pef->init_section = (int32_t)be32(l + 8);
    pef->init_offset = be32(l + 12);
    pef->term_section = (int32_t)be32(l + 16);
    pef->term_offset = be32(l + 20);
    pef->library_count = be32(l + 24);
    pef->import_count = be32(l + 28);
    pef->reloc_section_count = be32(l + 32);
    pef->reloc_words_at = be32(l + 36);
    pef->strings_at = be32(l + 40);
    pef->export_hash_at = be32(l + 44);
    pef->export_hash_power = be32(l + 48);
    pef->export_count = be32(l + 52);
    const int32_t entries[3] = {pef->main_section, pef->init_section, pef->term_section};
    for (int i = 0; i < 3; i++)
        if (entries[i] != -1 && (entries[i] < 0 || entries[i] >= pef->section_count))
            return fail(err, n, "PEF entry point names a section that does not exist.");
    if (pef->library_count > 4096 || pef->import_count > (1u << 20) || pef->reloc_section_count > pef->section_count ||
        pef->export_count > (1u << 20))
        return fail(err, n, "PEF loader table counts are implausible.");
    pef->libraries_at = 56;
    pef->imports_at = pef->libraries_at + 24 * pef->library_count;
    pef->reloc_headers_at = pef->imports_at + 4 * pef->import_count;
    if (!fits(pef->reloc_headers_at, pef->reloc_section_count, 12, size))
        return fail(err, n, "PEF loader tables run past the loader section.");
    if (pef->reloc_words_at > size || pef->strings_at > size)
        return fail(err, n, "PEF loader offsets run past the loader section.");
    for (uint32_t i = 0; i < pef->library_count; i++) {
        const uint8_t *p = l + pef->libraries_at + 24 * i;
        uint32_t count = be32(p + 12), first = be32(p + 16);
        if (!loader_string(pef, be32(p))) return fail(err, n, "PEF imported library name is not a valid string.");
        if (first > pef->import_count || count > pef->import_count - first)
            return fail(err, n, "PEF imported library claims symbols outside the import table.");
    }
    for (uint32_t i = 0; i < pef->import_count; i++)
        if (!loader_string(pef, be32(l + pef->imports_at + 4 * i) & UINT32_C(0xffffff)))
            return fail(err, n, "PEF imported symbol name is not a valid string.");
    for (uint32_t i = 0; i < pef->reloc_section_count; i++) {
        const uint8_t *p = l + pef->reloc_headers_at + 12 * i;
        if (be16(p) >= pef->section_count) return fail(err, n, "PEF relocations name a section that does not exist.");
        if (!fits(be32(p + 8), be32(p + 4), 2, size - pef->reloc_words_at))
            return fail(err, n, "PEF relocation instructions run past the loader section.");
    }
    if (pef->export_count) {
        if (pef->export_hash_power > 20) return fail(err, n, "PEF export hash table is implausibly large.");
        uint64_t keys = (uint64_t)pef->export_hash_at + ((uint64_t)4 << pef->export_hash_power);
        uint64_t symbols = keys + (uint64_t)4 * pef->export_count;
        if (!fits(symbols, pef->export_count, 10, size)) return fail(err, n, "PEF export tables run past the loader section.");
        pef->export_keys_at = (uint32_t)keys;
        pef->export_symbols_at = (uint32_t)symbols;
        for (uint32_t i = 0; i < pef->export_count; i++) {
            MacPefExport e;
            if (mac_pef_export(pef, i, &e)) return fail(err, n, "PEF exported symbol is malformed.");
        }
    }
    return 0;
}

int mac_pef_open(MacPef *pef, const void *bytes, size_t size, char *err, size_t n) {
    if (!pef) return fail(err, n, "Invalid arguments.");
    memset(pef, 0, sizeof *pef);
    pef->loader_index = -1;
    pef->main_section = pef->init_section = pef->term_section = -1;
    const uint8_t *b = bytes;
    if (!b || size < 40) return fail(err, n, "PEF container header is truncated.");
    if (be32(b) != UINT32_C(0x4a6f7921) || be32(b + 4) != UINT32_C(0x70656666))
        return fail(err, n, "Not a PEF container (missing Joy!peff tags).");
    pef->architecture = be32(b + 8);
    if (pef->architecture != MAC_PEF_ARCH_PPC && pef->architecture != MAC_PEF_ARCH_68K)
        return fail(err, n, "PEF container names an unknown architecture.");
    if (be32(b + 12) != 1) return fail(err, n, "Unsupported PEF format version.");
    pef->timestamp = be32(b + 16);
    pef->old_def_version = be32(b + 20);
    pef->old_imp_version = be32(b + 24);
    pef->current_version = be32(b + 28);
    pef->section_count = be16(b + 32);
    pef->inst_section_count = be16(b + 34);
    if (!pef->section_count || pef->section_count > MAC_PEF_MAX_SECTIONS || pef->inst_section_count > pef->section_count)
        return fail(err, n, "PEF section counts are invalid.");
    if (!fits(40, pef->section_count, 28, size)) return fail(err, n, "PEF section headers are truncated.");
    pef->bytes = b;
    pef->size = size;
    for (unsigned i = 0; i < pef->section_count; i++) {
        const uint8_t *h = b + 40 + 28 * i;
        MacPefSection *s = &pef->sections[i];
        s->name_offset = (int32_t)be32(h);
        s->default_address = be32(h + 4);
        s->total_size = be32(h + 8);
        s->unpacked_size = be32(h + 12);
        s->packed_size = be32(h + 16);
        s->container_offset = be32(h + 20);
        s->kind = h[24];
        s->share = h[25];
        s->alignment = h[26];
        if (s->kind > MAC_PEF_TRACEBACK) return fail(err, n, "PEF section has an unknown kind.");
        if (s->container_offset > size || s->packed_size > size - s->container_offset)
            return fail(err, n, "PEF section contents run past the container.");
        if (mac_pef_instantiated(s->kind)) {
            if (s->total_size > MAC_PEF_SECTION_LIMIT || s->unpacked_size > s->total_size)
                return fail(err, n, "PEF section sizes are inconsistent or too large.");
            if (s->kind != MAC_PEF_PATTERN_DATA && s->packed_size > s->unpacked_size)
                return fail(err, n, "PEF section stores more bytes than it unpacks.");
            if (s->alignment > 16) return fail(err, n, "PEF section alignment is unsupported.");
        }
        if (s->kind == MAC_PEF_LOADER) {
            if (pef->loader_index >= 0) return fail(err, n, "PEF container has more than one loader section.");
            pef->loader_index = (int)i;
        }
    }
    return pef->loader_index >= 0 ? parse_loader(pef, err, n) : 0;
}

int mac_pef_library(const MacPef *pef, uint32_t index, MacPefLibrary *out) {
    if (!pef || !out || pef->loader_index < 0 || index >= pef->library_count) return -1;
    const uint8_t *p = pef->loader + pef->libraries_at + 24 * index;
    out->name = loader_string(pef, be32(p));
    out->old_version = be32(p + 4);
    out->current_version = be32(p + 8);
    out->symbol_count = be32(p + 12);
    out->first_symbol = be32(p + 16);
    out->options = p[20];
    return out->name ? 0 : -1;
}

int mac_pef_import(const MacPef *pef, uint32_t index, MacPefImport *out) {
    if (!pef || !out || pef->loader_index < 0 || index >= pef->import_count) return -1;
    uint32_t v = be32(pef->loader + pef->imports_at + 4 * index);
    out->name = loader_string(pef, v & UINT32_C(0xffffff));
    out->symbol_class = (uint8_t)((v >> 24) & 0x0fu);
    out->weak = (uint8_t)((v >> 31) & 1u);
    out->library = UINT32_MAX;
    for (uint32_t i = 0; i < pef->library_count; i++) {
        const uint8_t *p = pef->loader + pef->libraries_at + 24 * i;
        uint32_t count = be32(p + 12), first = be32(p + 16);
        if (index >= first && index - first < count) { out->library = i; break; }
    }
    return out->name ? 0 : -1;
}

int mac_pef_relocations(const MacPef *pef, uint32_t index, MacPefRelocations *out) {
    if (!pef || !out || pef->loader_index < 0 || index >= pef->reloc_section_count) return -1;
    const uint8_t *p = pef->loader + pef->reloc_headers_at + 12 * index;
    out->section = be16(p);
    out->count = be32(p + 4);
    out->words = pef->loader + pef->reloc_words_at + be32(p + 8);
    return 0;
}

int mac_pef_export(const MacPef *pef, uint32_t index, MacPefExport *out) {
    if (!pef || !out || pef->loader_index < 0 || index >= pef->export_count) return -1;
    const uint8_t *p = pef->loader + pef->export_symbols_at + 10 * index;
    uint32_t class_name = be32(p), offset = class_name & UINT32_C(0xffffff);
    out->name_length = be32(pef->loader + pef->export_keys_at + 4 * index) >> 16;
    out->symbol_class = (uint8_t)((class_name >> 24) & 0x0fu);
    out->value = be32(p + 4);
    out->section = (int16_t)be16(p + 8);
    if (pef->strings_at > pef->loader_size || offset > pef->loader_size - pef->strings_at ||
        out->name_length > pef->loader_size - pef->strings_at - offset)
        return -1;
    if (out->section != MAC_PEF_EXPORT_ABSOLUTE && out->section != MAC_PEF_EXPORT_REEXPORT &&
        (out->section < 0 || out->section >= pef->section_count))
        return -1;
    out->name = (const char *)pef->loader + pef->strings_at + offset;
    return 0;
}

int mac_pef_export_find(const MacPef *pef, const char *name, MacPefExport *out) {
    if (!pef || !name || !out || pef->loader_index < 0) return -1;
    size_t length = strlen(name);
    for (uint32_t i = 0; i < pef->export_count; i++)
        if (!mac_pef_export(pef, i, out) && out->name_length == length && !memcmp(out->name, name, length)) return 0;
    memset(out, 0, sizeof *out);
    return 1;
}

int mac_cfrg_member(const uint8_t *r, size_t size, unsigned index, MacCfrgMember *out) {
    if (!r || !out || size < 32) return -1;
    unsigned count = be16(r + 30);
    size_t at = 32;
    for (unsigned i = 0; i < count; i++) {
        if (at > size || size - at < 43) return -1;
        const uint8_t *m = r + at;
        size_t member = be16(m + 40), name_length = m[42];
        if (member < 43 + name_length) return -1;
        /* Real linkers wrote member sizes that run past the resource (DesignWorkshop Lite 1.8.5
         * declares 68 bytes where 56 exist); accept that when the fixed part and name fit. */
        if (member > size - at) { if (43 + name_length > size - at) return -1; member = size - at; }
        if (i == index) {
            memset(out, 0, sizeof *out);
            out->architecture = be32(m);
            out->usage = m[22];
            out->where = m[23];
            out->offset = be32(m + 24);
            out->length = be32(m + 28);
            if (name_length >= sizeof out->name) name_length = sizeof out->name - 1;
            memcpy(out->name, m + 43, name_length);
            return 0;
        }
        at += member;
    }
    return 1;
}
