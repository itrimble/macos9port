#include "mac_cfm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(MacCfmFragment *f, const char *message) {
    snprintf(f->error, sizeof f->error, "%s", message);
    return -1;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* The CPU's `sc` handler: only the `sc` word of an import's gate record is a valid call site. */
static int gate(PpcCpu *cpu, uint32_t pc, void *user) {
    MacCfmFragment *f = user;
    uint32_t offset = pc - f->gate_base;
    if (!f->import_count || pc < f->gate_base || offset / MAC_CFM_GATE_BYTES >= f->import_count ||
        offset % MAC_CFM_GATE_BYTES != 8)
        return -1;
    MacCfmImport *import = &f->imports[offset / MAC_CFM_GATE_BYTES];
    if (!import->fn) {
        ppc_cpu_halt(cpu, "Unsupported PowerPC import %s::%s (called from %08x).", import->library, import->name,
                     cpu->lr - 4);
        return -1;
    }
    import->calls++;
    f->calling = import;
    if (f->on_call) f->on_call(import, cpu, f->user);
    if (import->fn(cpu, f->user) != 0)
        ppc_cpu_halt(cpu, "PowerPC import %s::%s failed.", import->library, import->name);
    return 0; /* a halt above already stops execution; the handler itself was valid */
}

static int place_sections(MacCfmFragment *f, MacCfmAlloc alloc, void *alloc_ctx) {
    for (unsigned i = 0; i < f->pef.section_count; i++) {
        const MacPefSection *s = &f->pef.sections[i];
        if (!mac_pef_instantiated(s->kind)) continue;
        uint32_t align = UINT32_C(1) << s->alignment;
        if (align < 4) align = 4;
        uint32_t block = alloc(alloc_ctx, s->total_size + align);
        if (!block || !ppc_mem_span(f->cpu, block, s->total_size + align))
            return fail(f, "Out of guest memory for a PowerPC fragment section.");
        uint32_t address = (block + align - 1) & ~(align - 1);
        if (mac_pef_unpack(&f->pef, i, ppc_mem_span(f->cpu, address, s->total_size), s->total_size, f->error,
                           sizeof f->error))
            return -1;
        f->section_address[i] = address;
    }
    return 0;
}

static int bind_imports(MacCfmFragment *f, const MacCfmHostImport *table, size_t table_count, MacCfmAlloc alloc,
                        void *alloc_ctx) {
    f->import_count = f->pef.import_count;
    if (!f->import_count) return 0;
    f->imports = calloc(f->import_count, sizeof *f->imports);
    f->gate_base = alloc(alloc_ctx, f->import_count * MAC_CFM_GATE_BYTES);
    uint8_t *gates = f->gate_base ? ppc_mem_span(f->cpu, f->gate_base, f->import_count * MAC_CFM_GATE_BYTES) : NULL;
    if (!f->imports || !gates) return fail(f, "Out of memory for the PowerPC import table.");
    for (uint32_t i = 0; i < f->import_count; i++) {
        MacCfmImport *import = &f->imports[i];
        MacPefImport symbol;
        MacPefLibrary library;
        if (mac_pef_import(&f->pef, i, &symbol) || mac_pef_library(&f->pef, symbol.library, &library))
            return fail(f, "PowerPC fragment import does not belong to an imported library.");
        snprintf(import->library, sizeof import->library, "%s", library.name);
        snprintf(import->name, sizeof import->name, "%s", symbol.name);
        import->symbol_class = symbol.symbol_class;
        import->weak = symbol.weak || (library.options & 0x40) != 0;
        int callable = symbol.symbol_class == MAC_PEF_SYM_TVECTOR || symbol.symbol_class == MAC_PEF_SYM_CODE ||
                       symbol.symbol_class == MAC_PEF_SYM_GLUE;
        for (size_t k = 0; callable && k < table_count && !import->fn; k++)
            if (!strcmp(table[k].symbol, symbol.name) && (!table[k].library || !strcmp(table[k].library, library.name))) {
                import->fn = table[k].fn;
                import->data = table[k].data;
            }
        import->resolved = import->fn != NULL;
        uint32_t record = f->gate_base + i * MAC_CFM_GATE_BYTES;
        uint8_t *g = gates + i * MAC_CFM_GATE_BYTES;
        put32(g, record + 8);
        put32(g + 4, 0);
        put32(g + 8, UINT32_C(0x44000002));  /* sc */
        put32(g + 12, UINT32_C(0x4e800020)); /* blr */
        /* A transition-vector import resolves to the record; a code or glue import to the code inside it. Data
         * imports have no host storage yet: park them where any access faults instead of reading a gate. */
        import->address = !callable ? PPC_UNMAPPED_BASE + i * MAC_CFM_GATE_BYTES
                        : symbol.symbol_class == MAC_PEF_SYM_TVECTOR ? record : record + 8;
        if (!import->resolved && import->weak) { /* not installed: null, and not an error */
            import->absent = 1;
            import->address = 0;
            f->absent++;
            continue;
        }
        if (!import->resolved && !f->missing++)
            snprintf(f->first_missing, sizeof f->first_missing, "%s::%s", import->library, import->name);
    }
    return 0;
}

static int entry_vector(MacCfmFragment *f, int32_t section, uint32_t offset, uint32_t *vector) {
    *vector = 0;
    if (section == -1) return 0;
    const MacPefSection *s = &f->pef.sections[section];
    if (!f->section_address[section] || offset > s->total_size || s->total_size - offset < 8)
        return fail(f, "PowerPC fragment entry point is outside its section.");
    *vector = f->section_address[section] + offset;
    return 0;
}

int mac_cfm_load(MacCfmFragment *f, PpcCpu *cpu, const void *pef, size_t size, const MacCfmHostImport *table,
                 size_t table_count, MacCfmAlloc alloc, void *alloc_ctx, void *user) {
    if (!f) return -1;
    memset(f, 0, sizeof *f);
    if (!cpu || !cpu->ram || !alloc || (!table && table_count)) return fail(f, "Invalid arguments.");
    f->cpu = cpu;
    f->user = user;
    if (mac_pef_open(&f->pef, pef, size, f->error, sizeof f->error)) return -1;
    if (f->pef.architecture != MAC_PEF_ARCH_PPC) return fail(f, "PEF fragment is not PowerPC code.");
    if (f->pef.loader_index < 0) return fail(f, "PEF fragment has no loader section.");
    if (place_sections(f, alloc, alloc_ctx) || bind_imports(f, table, table_count, alloc, alloc_ctx)) {
        mac_cfm_unload(f);
        return -1;
    }
    for (uint32_t i = 0; i < f->pef.reloc_section_count; i++) {
        MacPefRelocations relocations;
        if (mac_pef_relocations(&f->pef, i, &relocations) || mac_cfm_relocate(f, &relocations)) {
            if (!f->error[0]) fail(f, "PowerPC fragment relocations are malformed.");
            mac_cfm_unload(f);
            return -1;
        }
    }
    if (entry_vector(f, f->pef.main_section, f->pef.main_offset, &f->main_vector) ||
        entry_vector(f, f->pef.init_section, f->pef.init_offset, &f->init_vector) ||
        entry_vector(f, f->pef.term_section, f->pef.term_offset, &f->term_vector)) {
        mac_cfm_unload(f);
        return -1;
    }
    cpu->syscall = gate;
    cpu->syscall_user = f;
    return 0;
}

void mac_cfm_unload(MacCfmFragment *f) {
    if (!f) return;
    if (f->cpu && f->cpu->syscall_user == f) {
        f->cpu->syscall = NULL;
        f->cpu->syscall_user = NULL;
    }
    free(f->imports); /* guest blocks come from a bump allocator and are not returned */
    f->imports = NULL;
    f->import_count = 0;
}

int mac_cfm_stack(MacCfmFragment *f, uint32_t base, uint32_t size) {
    if (!f || !f->cpu || size < 256 || !ppc_mem_span(f->cpu, base, size)) return -1;
    uint32_t sp = (base + size - 64) & ~UINT32_C(15);
    memset(ppc_mem_span(f->cpu, sp, 64), 0, 64); /* zero back chain ends the frame list */
    f->cpu->gpr[1] = sp;
    return 0;
}

int mac_cfm_call(MacCfmFragment *f, uint32_t vector, size_t budget) {
    if (!f || !f->cpu || f->cpu->halted) return -1;
    const uint8_t *tv = vector ? ppc_mem_span(f->cpu, vector, 8) : NULL;
    if (!tv || !f->cpu->gpr[1]) {
        ppc_cpu_halt(f->cpu, "PowerPC call through an invalid transition vector %08x or without a stack.", vector);
        return -1;
    }
    uint32_t code = ((uint32_t)tv[0] << 24) | ((uint32_t)tv[1] << 16) | ((uint32_t)tv[2] << 8) | tv[3];
    uint32_t toc = ((uint32_t)tv[4] << 24) | ((uint32_t)tv[5] << 16) | ((uint32_t)tv[6] << 8) | tv[7];
    return ppc_cpu_call(f->cpu, code, toc, budget);
}

int mac_cfm_export(const MacCfmFragment *f, const char *name, uint32_t *address) {
    MacPefExport e;
    if (!f || !address) return -1;
    int found = mac_pef_export_find(&f->pef, name, &e);
    if (found) return found;
    if (e.section == MAC_PEF_EXPORT_ABSOLUTE) { *address = e.value; return 0; }
    if (e.section < 0 || !f->section_address[e.section] || e.value > f->pef.sections[e.section].total_size) return -1;
    *address = f->section_address[e.section] + e.value;
    return 0;
}
