#ifndef MAC_PEF_H
#define MAC_PEF_H

#include <stddef.h>
#include <stdint.h>

/* Read-only view of a PEF (Preferred Executable Format) container, written
 * from the publicly documented layout. All fields are big-endian. A MacPef
 * borrows the caller's bytes; keep them alive while the view is used. Every
 * accessor is bounds-checked and returns an error instead of reading outside
 * the container. */

#define MAC_PEF_ARCH_PPC UINT32_C(0x70777063)  /* 'pwpc' */
#define MAC_PEF_ARCH_68K UINT32_C(0x6d36386b)  /* 'm68k' */
#define MAC_PEF_MAX_SECTIONS 32
#define MAC_PEF_SECTION_LIMIT (UINT32_C(64) << 20) /* largest section this runtime will instantiate */

enum {
    MAC_PEF_CODE = 0, MAC_PEF_DATA = 1, MAC_PEF_PATTERN_DATA = 2, MAC_PEF_CONSTANT = 3, MAC_PEF_LOADER = 4,
    MAC_PEF_DEBUG = 5, MAC_PEF_EXEC_DATA = 6, MAC_PEF_EXCEPTION = 7, MAC_PEF_TRACEBACK = 8
};

/* Symbol classes (low four bits of the class byte); 0x80 in the same byte marks a weak import. */
enum { MAC_PEF_SYM_CODE = 0, MAC_PEF_SYM_DATA = 1, MAC_PEF_SYM_TVECTOR = 2, MAC_PEF_SYM_TOC = 3, MAC_PEF_SYM_GLUE = 4 };

#define MAC_PEF_EXPORT_ABSOLUTE (-2)
#define MAC_PEF_EXPORT_REEXPORT (-3)

typedef struct {
    int32_t name_offset; /* -1 when unnamed */
    uint32_t default_address, total_size, unpacked_size, packed_size, container_offset;
    uint8_t kind, share, alignment; /* alignment is a power-of-two exponent */
} MacPefSection;

typedef struct {
    const char *name;
    uint32_t old_version, current_version, symbol_count, first_symbol;
    uint8_t options;
} MacPefLibrary;

typedef struct {
    const char *name;
    uint32_t library; /* index into the imported library table */
    uint8_t symbol_class, weak;
} MacPefImport;

typedef struct {
    uint16_t section;
    uint32_t count;          /* 16-bit relocation words */
    const uint8_t *words;    /* count * 2 bytes, big-endian */
} MacPefRelocations;

typedef struct {
    const char *name;        /* not NUL-terminated */
    uint32_t name_length, value;
    int16_t section;         /* section index, MAC_PEF_EXPORT_ABSOLUTE or MAC_PEF_EXPORT_REEXPORT */
    uint8_t symbol_class;
} MacPefExport;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t architecture, timestamp, old_def_version, old_imp_version, current_version;
    uint16_t section_count, inst_section_count;
    MacPefSection sections[MAC_PEF_MAX_SECTIONS];
    int loader_index; /* -1: the container has no loader section */
    /* Loader section; valid when loader_index >= 0. Offsets are relative to `loader`. */
    const uint8_t *loader;
    uint32_t loader_size;
    int32_t main_section, init_section, term_section; /* -1: absent */
    uint32_t main_offset, init_offset, term_offset;
    uint32_t library_count, import_count, reloc_section_count, export_count;
    uint32_t libraries_at, imports_at, reloc_headers_at, reloc_words_at, strings_at;
    uint32_t export_hash_at, export_hash_power, export_keys_at, export_symbols_at;
} MacPef;

/* 0: a structurally valid container; -1: rejected, with a reason in `err`. */
int mac_pef_open(MacPef *pef, const void *bytes, size_t size, char *err, size_t err_size);
const char *mac_pef_kind_name(uint8_t kind);
/* 1 when the section kind is placed in memory by the loader. */
int mac_pef_instantiated(uint8_t kind);
/* Writes the section's initial contents (total_size bytes, zero-filled past the unpacked part) to `out`.
 * Pattern-initialized data is expanded. 0 on success, -1 with a reason on any malformed or oversized input. */
int mac_pef_unpack(const MacPef *pef, unsigned section, uint8_t *out, size_t out_size, char *err, size_t err_size);
/* Expands pattern-initialized data. Returns the number of bytes produced, or -1 when the stream is malformed
 * or would produce more than `out_size` bytes. */
long mac_pef_pattern_expand(const uint8_t *packed, size_t packed_size, uint8_t *out, size_t out_size);
/* The accessors below return 0 on success and -1 for an index out of range or a missing loader section. */
int mac_pef_library(const MacPef *pef, uint32_t index, MacPefLibrary *out);
int mac_pef_import(const MacPef *pef, uint32_t index, MacPefImport *out);
int mac_pef_relocations(const MacPef *pef, uint32_t index, MacPefRelocations *out);
int mac_pef_export(const MacPef *pef, uint32_t index, MacPefExport *out);
/* Linear search of the exported symbol table by exact name. 0: found; 1: not found; -1: no loader section. */
int mac_pef_export_find(const MacPef *pef, const char *name, MacPefExport *out);

/* Code fragment resource ('cfrg' ID 0) member. */
#define MAC_CFRG_TYPE UINT32_C(0x63667267)
enum { MAC_CFRG_LIBRARY = 0, MAC_CFRG_APPLICATION = 1, MAC_CFRG_DROPIN = 2 };
enum { MAC_CFRG_IN_MEMORY = 0, MAC_CFRG_IN_DATA_FORK = 1, MAC_CFRG_IN_RESOURCE = 2 };
typedef struct {
    uint32_t architecture, offset, length; /* length 0: the fragment runs to the end of the fork */
    uint8_t usage, where;
    char name[64];
} MacCfrgMember;
/* Finds member `index` (0-based). 0: found; 1: no such member; -1: malformed resource. */
int mac_cfrg_member(const uint8_t *resource, size_t size, unsigned index, MacCfrgMember *out);

#endif
