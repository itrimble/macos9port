#include "mac_ppc_probe.h"

#include <stdlib.h>
#include <string.h>

#define PROBE_RAM (UINT32_C(16) << 20) /* same size as the 68K runtime's guest RAM */
#define PROBE_HEAP_BASE UINT32_C(0x100000)

typedef struct { uint32_t next, limit; } Bump;

static uint32_t bump_alloc(void *ctx, uint32_t size) {
    Bump *b = ctx;
    uint32_t aligned = (size + 3u) & ~UINT32_C(3);
    if (aligned < size || aligned > b->limit - b->next) return 0;
    uint32_t at = b->next;
    b->next += aligned ? aligned : 4;
    return at; /* scratch RAM starts zeroed and is never reused */
}

static const MacCfmHostImport *g_imports;
static size_t g_import_count;

void mac_ppc_probe_imports(const MacCfmHostImport *table, size_t count) {
    g_imports = table;
    g_import_count = count;
}

int mac_ppc_fork_member(const MacResourceFork *fork, MacCfrgMember *out) {
    MacResource r;
    MacCfrgMember m;
    int found = 0;
    if (!fork || !out || mac_resource_find(fork, MAC_CFRG_TYPE, 0, &r)) return 0;
    for (unsigned i = 0; mac_cfrg_member(r.data, r.size, i, &m) == 0; i++) {
        if (m.architecture != MAC_PEF_ARCH_PPC) continue;
        if (!found || m.usage == MAC_CFRG_APPLICATION) *out = m;
        found = 1;
        if (m.usage == MAC_CFRG_APPLICATION) break;
    }
    return found;
}

int mac_ppc_data_fork_path(const char *fork_path, char *out, size_t out_size) {
    static const char *const suffixes[] = {"/..namedfork/rsrc", ".rsrc"};
    size_t length = fork_path ? strlen(fork_path) : 0;
    for (size_t i = 0; i < 2; i++) {
        size_t n = strlen(suffixes[i]);
        if (length > n && !strcmp(fork_path + length - n, suffixes[i]) && length - n < out_size) {
            memcpy(out, fork_path, length - n);
            out[length - n] = 0;
            return 0;
        }
    }
    return -1;
}

uint8_t *mac_ppc_read_fragment(const char *fork_path, const MacCfrgMember *member, size_t *size, char *err, size_t n) {
    char path[4096];
    if (member->where != MAC_CFRG_IN_DATA_FORK) {
        snprintf(err, n, "the fragment is not stored in the data fork (locator %u)", member->where);
        return NULL;
    }
    if (mac_ppc_data_fork_path(fork_path, path, sizeof path)) {
        snprintf(err, n, "cannot derive the data fork path from the resource fork path");
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    long end = -1;
    if (f && !fseek(f, 0, SEEK_END)) end = ftell(f);
    uint64_t length = member->length ? member->length : (end > (long)member->offset ? (uint64_t)end - member->offset : 0);
    if (!f || end < 0 || member->offset > (uint64_t)end || !length || length > (uint64_t)end - member->offset ||
        length > (UINT64_C(64) << 20) || fseek(f, (long)member->offset, SEEK_SET)) {
        if (f) fclose(f);
        snprintf(err, n, "the data fork is missing or does not contain the fragment the cfrg resource describes");
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, f) != (size_t)length) {
        free(bytes);
        fclose(f);
        snprintf(err, n, "cannot read the fragment from the data fork");
        return NULL;
    }
    fclose(f);
    *size = (size_t)length;
    return bytes;
}

static void describe(const MacCfmFragment *fragment, FILE *out) {
    const MacPef *pef = &fragment->pef;
    fprintf(out, "pef architecture=pwpc sections=%u instantiated=%u version=%08x\n", pef->section_count,
            pef->inst_section_count, pef->current_version);
    for (unsigned i = 0; i < pef->section_count; i++) {
        const MacPefSection *s = &pef->sections[i];
        fprintf(out, "pef section=%u kind=%s total=%u unpacked=%u packed=%u align=%u\n", i, mac_pef_kind_name(s->kind),
                s->total_size, s->unpacked_size, s->packed_size, 1u << (s->alignment > 16 ? 16 : s->alignment));
    }
    fprintf(out, "pef main=%d+%u init=%d+%u term=%d+%u exports=%u relocated-sections=%u\n", pef->main_section,
            pef->main_offset, pef->init_section, pef->init_offset, pef->term_section, pef->term_offset,
            pef->export_count, pef->reloc_section_count);
    for (uint32_t i = 0; i < pef->library_count; i++) {
        MacPefLibrary library;
        if (!mac_pef_library(pef, i, &library))
            fprintf(out, "pef library=%s imports=%u%s\n", library.name, library.symbol_count,
                    (library.options & 0x40) ? " weak" : "");
    }
    static const char *const classes[] = {"code", "data", "tvector", "toc", "glue"};
    for (uint32_t i = 0; i < fragment->import_count; i++) {
        const MacCfmImport *import = &fragment->imports[i];
        fprintf(out, "pef import=%s::%s class=%s%s binding=%s\n", import->library, import->name,
                import->symbol_class < 5 ? classes[import->symbol_class] : "unknown", import->weak ? " weak" : "",
                import->resolved ? "host" : import->absent ? "null" : "missing");
    }
}

int mac_ppc_probe(const uint8_t *pef, size_t size, FILE *out, char *message, size_t n) {
    uint8_t *ram = calloc(1, PROBE_RAM);
    if (!ram) {
        snprintf(message, n, "PowerPC runtime is incomplete: out of host memory for the trial load.");
        return -1;
    }
    PpcCpu cpu;
    MacCfmFragment fragment;
    Bump bump = {PROBE_HEAP_BASE, PROBE_RAM};
    size_t count = g_import_count;
    const MacCfmHostImport *table = g_imports ? g_imports : mac_ppc_host_imports(&count);
    ppc_cpu_init(&cpu, ram, PROBE_RAM);
    int rc = mac_cfm_load(&fragment, &cpu, pef, size, table, count, bump_alloc, &bump, NULL);
    if (rc) snprintf(message, n, "PowerPC fragment cannot be loaded (%s)", fragment.error);
    else {
        if (out) describe(&fragment, out);
        if (fragment.missing)
            snprintf(message, n, "PowerPC runtime is incomplete: %u of %u imports have no host implementation, first %s.",
                     fragment.missing, fragment.import_count, fragment.first_missing);
        else
            snprintf(message, n, "Every import of the PowerPC fragment is bound or weak (%u weak imports resolve to null).",
                     fragment.absent);
        mac_cfm_unload(&fragment);
    }
    free(ram);
    return rc;
}

static int report(const MacResourceFork *fork, const char *fork_path, FILE *out, char *message, size_t n) {
    static const char *const usages[] = {"library", "application", "drop-in"};
    static const char *const places[] = {"memory", "data-fork", "resource"};
    MacCfrgMember member;
    char reason[200];
    size_t size = 0;
    if (!mac_ppc_fork_member(fork, &member)) return 0;
    if (out)
        fprintf(out, "cfrg architecture=pwpc usage=%s where=%s offset=%u length=%u name=%s\n",
                member.usage < 3 ? usages[member.usage] : "unknown", member.where < 3 ? places[member.where] : "unknown",
                member.offset, member.length, member.name);
    uint8_t *bytes = mac_ppc_read_fragment(fork_path, &member, &size, reason, sizeof reason);
    if (!bytes) snprintf(message, n, "PowerPC fragment cannot be read: %s.", reason);
    else {
        mac_ppc_probe(bytes, size, out, message, n);
        free(bytes);
    }
    return 1;
}

int mac_ppc_inspect(const MacResourceFork *fork, const char *fork_path, FILE *out) {
    char message[400];
    if (!report(fork, fork_path, out, message, sizeof message)) return 0;
    fprintf(out, "powerpc %s\n", message);
    return 1;
}

int mac_ppc_launch_diagnostic(const MacResourceFork *fork, const char *fork_path, char *message, size_t n) {
    MacResource code;
    if (!fork || mac_resource_find(fork, MAC_RESOURCE_TYPE('C', 'O', 'D', 'E'), 0, &code) == 0) return 0;
    return report(fork, fork_path, NULL, message, n);
}
