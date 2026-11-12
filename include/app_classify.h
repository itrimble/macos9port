#ifndef OS999_APP_CLASSIFY_H
#define OS999_APP_CLASSIFY_H

#include <stddef.h>
#include <stdint.h>

/* Classic Macintosh application classification.
 *
 * Reads real headers and resources from the two forks (never names, paths or extensions) and reports what the
 * program is, which compatibility profile it needs, how sure that is, and what is missing. It is a pure function of
 * the bytes: no files, no runtime, no CPU or addressing state. Recognition is not demonstrated compatibility: this
 * module never says an application works, only what it appears to need. Intel-era Mac OS X programs (Mach-O) are
 * recognised only so they can be refused with a reason; there is no x86 or OS X backend.
 *
 * Profiles (docs/CLASSIC-COMPAT-PROFILES.md):
 *   A  legacy 68K        24-bit addressing semantics, small machine
 *   B  32-bit-clean 68K  declares 32-bit compatibility (a declaration, not proof)
 *   C  classic PowerPC   PEF/CFM program for the classic Mac OS
 *   D  unsupported       not an application, damaged, OS X, CFM-68K, or otherwise outside scope
 */

typedef enum {
    APP_FMT_NONE,       /* no executable code found in either fork */
    APP_FMT_MALFORMED,  /* an executable format was recognised but its structure is damaged */
    APP_FMT_CODE68K,    /* classic 68K: CODE resources in the resource fork */
    APP_FMT_PEF,        /* PEF container in the data fork (classic PowerPC, or CFM-68K) */
    APP_FMT_FAT,        /* both CODE resources and a PowerPC PEF */
    APP_FMT_MACHO       /* Mac OS X Mach-O (any architecture): out of scope */
} AppFormat;

#define APP_ARCH_M68K 0x01u        /* CODE resources */
#define APP_ARCH_PEF_PPC 0x02u     /* PEF container, architecture 'pwpc' */
#define APP_ARCH_PEF_M68K 0x04u    /* PEF container or cfrg member, architecture 'm68k' (CFM-68K) */
#define APP_ARCH_MACHO_PPC 0x08u
#define APP_ARCH_MACHO_X86 0x10u   /* i386 or x86_64 */
#define APP_ARCH_MACHO_OTHER 0x20u /* arm or an unknown cpu type */

typedef enum {
    APP_PROFILE_A_LEGACY_68K = 1,
    APP_PROFILE_B_CLEAN_68K,
    APP_PROFILE_C_CLASSIC_PPC,
    APP_PROFILE_D_UNSUPPORTED
} AppProfile;

typedef enum { APP_CONF_LOW, APP_CONF_MEDIUM, APP_CONF_HIGH } AppConfidence;

/* What the host can actually run. Recognition does not depend on it; the chosen path of a mixed program and
 * `runnable` do. Zero value: 68K only, which is what this build implements. */
typedef struct {
    int ppc_available;  /* a PowerPC interpreter and PEF loader are present */
    int addr32_available;
} AppClassifyCaps;

#define APP_MAX_LIBS 12
#define APP_LIB_NAME 32
#define APP_TEXT 256

typedef struct {
    AppFormat format;
    unsigned arch;                 /* APP_ARCH_* bits found */
    AppProfile profile;            /* recommended profile among the paths the host can run, else the one needed */
    AppProfile alternate;          /* second path of a mixed program, 0 if none */
    AppConfidence confidence;      /* in `profile`; HIGH means the binary itself states it */
    int runnable;                  /* the host has the runtime for `profile` (still not "works") */
    int is_application;            /* structurally an application rather than a library or a data file */

    /* Evidence. Values are zero when the item was not found. */
    int resource_fork_ok;          /* a resource fork was supplied and parsed */
    unsigned code_resources;
    int has_code0, code0_layout_ok;
    int has_size;                  /* SIZE -1 resource with the documented 10-byte layout */
    unsigned size_flags, size_declares_32bit;
    uint32_t size_preferred, size_minimum;
    int has_cfrg;
    unsigned cfrg_members, cfrg_app_members;
    unsigned cfrg_arch;            /* APP_ARCH_* of the cfrg members */
    int has_carb;                  /* 'carb' resource: the program says it is Carbon-aware */
    int pef_valid;
    uint32_t pef_arch;             /* 'pwpc' or 'm68k' */
    int pef_has_main;
    unsigned pef_sections;
    unsigned pef_lib_total;
    unsigned pef_lib_known, pef_lib_unlisted;
    int pef_needs_carbon;
    char pef_libs[APP_MAX_LIBS][APP_LIB_NAME];
    uint32_t macho_cputype;        /* first slice, 0 if none */

    /* Executable identity: order-independent FNV-1a/64 over CODE and cfrg resources plus the data fork when it
     * holds a PEF. Independent of name, path and timestamps. Change detection only, not authentication. */
    uint64_t identity;

    char reason[APP_TEXT];         /* why this profile */
    char uncertainty[APP_TEXT];    /* what the evidence does not establish */
    char missing[APP_TEXT];        /* requirements the host lacks, empty if none */
} AppClassification;

/* data_fork and rsrc_fork may be NULL/0. rsrc_fork may be a raw fork or an AppleSingle/AppleDouble container.
 * finder_type is the Finder file type (0 = unknown). Returns 0 and fills `out` for any input, damaged or not;
 * -1 only for a NULL `out`. */
int app_classify(const void *data_fork, size_t data_size, const void *rsrc_fork, size_t rsrc_size,
                 uint32_t finder_type, const AppClassifyCaps *caps, AppClassification *out);

const char *app_format_name(AppFormat f);
const char *app_profile_name(AppProfile p);
const char *app_confidence_name(AppConfidence c);

/* One JSON object, UTF-8, strings escaped. Returns the length it needs (excluding the NUL); output is truncated
 * and terminated when `n` is too small. */
size_t app_classification_json(const AppClassification *c, char *out, size_t n);

#endif
