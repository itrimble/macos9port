#include "app_classify_int.h"
#include "mac_resource.h"
#include "mac_codefmt.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define T(a, b, c, d) MAC_RESOURCE_TYPE(a, b, c, d)

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void note(char *dst, const char *fmt, ...) {
    size_t len = strlen(dst);
    if (len >= APP_TEXT - 1) return;
    if (len) { snprintf(dst + len, APP_TEXT - len, "; "); len = strlen(dst); }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst + len, APP_TEXT - len, fmt, ap);
    va_end(ap);
}

/* Same structural limits the 68K runtime enforces on CODE 0 (mac_runtime_init). Kept here so that a program the
 * classifier accepts as loadable is not one the loader then refuses for layout. */
static int code0_ok(const MacResource *z) {
    return mac_code0_parse(z->data, z->size, NULL) == 0;
}

static void read_resources(const void *bytes, size_t size, AppClassification *c, uint64_t *code_hash, int *damaged) {
    MacResourceFork fork;
    char err[160];
    if (!bytes || !size) return;
    if (mac_resource_open_container(&fork, bytes, size, err, sizeof err)) {
        *damaged = 1;
        note(c->uncertainty, "resource fork unreadable (%s)", err);
        return;
    }
    c->resource_fork_ok = 1;
    for (size_t i = 0; i < mac_resource_count(&fork); i++) {
        MacResource r;
        if (mac_resource_at(&fork, i, &r)) continue;
        if (r.type == T('C', 'O', 'D', 'E')) {
            c->code_resources++;
            *code_hash ^= app_hash_resource(r.type, r.id, r.data, r.size);
            if (r.id == 0) {
                MacCode0 z0;
                c->has_code0 = 1; c->code0_layout_ok = code0_ok(&r);
                if (c->code0_layout_ok && mac_code0_parse(r.data, r.size, &z0) == 0 && z0.truncated)
                    note(c->uncertainty, "CODE 0 stores %u of the %u jump-table bytes it declares (a self-loading installer fills in the rest)", z0.present, z0.length);
            }
        } else if (r.type == T('c', 'f', 'r', 'g')) {
            *code_hash ^= app_hash_resource(r.type, r.id, r.data, r.size);
            if (!c->has_cfrg) {
                c->has_cfrg = 1;
                if (app_exec_cfrg(r.data, r.size, c) == APP_EXEC_DAMAGED) note(c->uncertainty, "cfrg resource is damaged");
            }
        } else if (r.type == T('S', 'I', 'Z', 'E') && r.id == -1 && r.size == 10) {
            c->has_size = 1;
            c->size_flags = (unsigned)r.data[0] << 8 | r.data[1];
            c->size_declares_32bit = (c->size_flags & 0x0080u) != 0;  /* is32BitCompatible */
            c->size_preferred = be32(r.data + 2);
            c->size_minimum = be32(r.data + 6);
        } else if (r.type == T('c', 'a', 'r', 'b')) c->has_carb = 1;
    }
}

static void finish(AppClassification *c, AppProfile profile, AppConfidence conf, const AppClassifyCaps *caps) {
    c->profile = profile;
    c->confidence = conf;
    c->runnable = profile == APP_PROFILE_C_CLASSIC_PPC ? caps->ppc_available
                  : (profile == APP_PROFILE_A_LEGACY_68K || profile == APP_PROFILE_B_CLEAN_68K);
}

static void classify_68k(AppClassification *c, const AppClassifyCaps *caps) {
    if (c->has_size && c->size_declares_32bit) {
        finish(c, APP_PROFILE_B_CLEAN_68K, APP_CONF_MEDIUM, caps);
        snprintf(c->reason, APP_TEXT, "68K CODE application whose SIZE resource sets is32BitCompatible");
        note(c->uncertainty, "the flag is the author's declaration, not a test; programs that set it and still keep "
             "flags in pointer high bytes exist, and a failure would show only under 32-bit addressing");
    } else if (c->has_size) {
        finish(c, APP_PROFILE_A_LEGACY_68K, APP_CONF_HIGH, caps);
        snprintf(c->reason, APP_TEXT, "68K CODE application whose SIZE resource leaves is32BitCompatible clear");
        note(c->uncertainty, "the author states it is not 32-bit clean; whether it needs the 24-bit mask in practice is unproven");
    } else {
        finish(c, APP_PROFILE_A_LEGACY_68K, APP_CONF_LOW, caps);
        snprintf(c->reason, APP_TEXT, "68K CODE application with no SIZE resource");
        note(c->uncertainty, "no SIZE resource means a System 6-era program or one that omits it; absence of the flag "
             "is not proof of 32-bit cleanliness, so the legacy profile is the conservative choice");
    }
    if (c->has_size && c->size_preferred > 16u * 1024 * 1024)
        note(c->uncertainty, "SIZE asks for %u MB, more than the 24-bit space holds", (unsigned)(c->size_preferred >> 20));
}

static void classify_ppc(AppClassification *c, const AppClassifyCaps *caps) {
    finish(c, APP_PROFILE_C_CLASSIC_PPC, c->pef_lib_unlisted ? APP_CONF_MEDIUM : APP_CONF_HIGH, caps);
    snprintf(c->reason, APP_TEXT, "PEF container for PowerPC with a main symbol%s",
             c->pef_needs_carbon ? ", importing CarbonLib" : "");
    if (c->pef_lib_unlisted)
        note(c->uncertainty, "%u imported librar%s not on the classic list (an application's own library, or something "
             "this classifier does not know); classic status unverified", c->pef_lib_unlisted, c->pef_lib_unlisted == 1 ? "y is" : "ies are");
    if (c->pef_needs_carbon) {
        note(c->uncertainty, "CarbonLib programs run on classic Mac OS 8.6 and later only with CarbonLib installed");
        note(c->missing, "CarbonLib services");
    }
    if (!caps->ppc_available) note(c->missing, "PowerPC interpreter and PEF/CFM loader (not in this build)");
}

int app_classify(const void *data_fork, size_t data_size, const void *rsrc_fork, size_t rsrc_size,
                 uint32_t finder_type, const AppClassifyCaps *caps_in, AppClassification *c) {
    if (!c) return -1;
    memset(c, 0, sizeof *c);
    static const AppClassifyCaps none;
    const AppClassifyCaps *caps = caps_in ? caps_in : &none;
    const uint8_t *data = data_fork;
    if (!data) data_size = 0;
    uint64_t code_hash = 0;
    int rsrc_damaged = 0;
    read_resources(rsrc_fork, rsrc_size, c, &code_hash, &rsrc_damaged);

    AppExecResult macho = app_exec_macho(data, data_size, c);
    AppExecResult pef = macho == APP_EXEC_OK ? APP_EXEC_ABSENT : app_exec_pef(data, data_size, c);
    int pef_looks = pef != APP_EXEC_ABSENT;
    int has68k = c->code_resources > 0;
    int pef_ppc = pef == APP_EXEC_OK && c->pef_arch == 0x70777063u;
    int pef_68k = pef == APP_EXEC_OK && c->pef_arch == 0x6d36386bu;
    if (has68k) c->arch |= APP_ARCH_M68K;
    if (pef_ppc) c->arch |= APP_ARCH_PEF_PPC;
    if (pef_68k) c->arch |= APP_ARCH_PEF_M68K;
    c->arch |= c->cfrg_arch & APP_ARCH_PEF_M68K;
    if (pef == APP_EXEC_OK) code_hash ^= app_hash_resource(T('P', 'E', 'F', ' '), 0, data, data_size);
    c->identity = code_hash;
    c->is_application = 1;

    if (finder_type && finder_type != T('A', 'P', 'P', 'L')) {
        c->is_application = 0;
        finish(c, APP_PROFILE_D_UNSUPPORTED, APP_CONF_HIGH, caps);
        snprintf(c->reason, APP_TEXT, "Finder file type '%c%c%c%c' is not APPL: not a launchable application",
                 (char)(finder_type >> 24), (char)(finder_type >> 16), (char)(finder_type >> 8), (char)finder_type);
        c->format = has68k ? APP_FMT_CODE68K : pef_ppc ? APP_FMT_PEF : APP_FMT_NONE;
        return 0;
    }
    if (macho == APP_EXEC_OK) {
        c->format = APP_FMT_MACHO;
        finish(c, APP_PROFILE_D_UNSUPPORTED, APP_CONF_HIGH, caps);
        snprintf(c->reason, APP_TEXT, "Mac OS X Mach-O executable (cpu type %#x): needs the OS X runtime, which is out of scope", c->macho_cputype);
        note(c->missing, "Mac OS X environment (not provided; no x86, Rosetta or Cocoa support)");
        return 0;
    }

    /* A damaged PEF is fatal only when nothing else in the program can run. */
    if (pef == APP_EXEC_DAMAGED) note(c->uncertainty, "data fork starts like a PEF container but its structure is inconsistent");
    if (c->has_cfrg && (c->cfrg_arch & APP_ARCH_PEF_PPC) && !pef_ppc && pef_looks == 0)
        note(c->uncertainty, "cfrg lists a PowerPC fragment but the data fork holds no PEF container (it may be in a resource)");

    if (has68k && !c->has_code0) {
        c->format = APP_FMT_MALFORMED;
        finish(c, APP_PROFILE_D_UNSUPPORTED, APP_CONF_HIGH, caps);
        snprintf(c->reason, APP_TEXT, "%u CODE resources but no CODE 0 jump table: a loader cannot start it", c->code_resources);
        return 0;
    }
    int ok68k = has68k && c->code0_layout_ok;
    if (has68k && !ok68k && !(pef_ppc && c->pef_has_main && caps->ppc_available)) {
        c->format = APP_FMT_MALFORMED;
        finish(c, APP_PROFILE_D_UNSUPPORTED, APP_CONF_HIGH, caps);
        snprintf(c->reason, APP_TEXT, "CODE 0 jump table has an unsupported or inconsistent layout");
        return 0;
    }
    int ppc_app = pef_ppc && c->pef_has_main;

    if (ok68k && ppc_app) {  /* fat: choose a path the host can run, not the newest one */
        c->format = APP_FMT_FAT;
        if (caps->ppc_available && !c->pef_lib_unlisted && !c->pef_needs_carbon) {
            classify_ppc(c, caps);
            c->alternate = c->has_size && c->size_declares_32bit ? APP_PROFILE_B_CLEAN_68K : APP_PROFILE_A_LEGACY_68K;
            note(c->uncertainty, "mixed 68K/PowerPC program: PowerPC path chosen because the host runs it and every import is a "
                 "known classic library");
        } else {
            classify_68k(c, caps);
            c->alternate = APP_PROFILE_C_CLASSIC_PPC;
            note(c->uncertainty, "mixed 68K/PowerPC program: the 68K path is chosen because %s", caps->ppc_available
                 ? "the PowerPC part has unverified or Carbon imports" : "this host cannot run PowerPC");
        }
        return 0;
    }
    if (ok68k) {
        c->format = APP_FMT_CODE68K;
        classify_68k(c, caps);
        if (pef == APP_EXEC_DAMAGED) c->confidence = c->confidence > APP_CONF_MEDIUM ? APP_CONF_MEDIUM : c->confidence;
        return 0;
    }
    if (ppc_app) {
        c->format = APP_FMT_PEF;
        classify_ppc(c, caps);
        return 0;
    }

    /* Nothing runnable. Say what was seen and why it cannot run. */
    finish(c, APP_PROFILE_D_UNSUPPORTED, APP_CONF_HIGH, caps);
    if (pef == APP_EXEC_DAMAGED || rsrc_damaged) {
        c->format = APP_FMT_MALFORMED;
        snprintf(c->reason, APP_TEXT, "executable structure is damaged and no other code path is available");
    } else if (pef_68k || (c->arch & APP_ARCH_PEF_M68K)) {
        c->format = APP_FMT_PEF;
        snprintf(c->reason, APP_TEXT, "CFM-68K fragment: needs the 68K Code Fragment Manager, which is not implemented");
        note(c->missing, "CFM-68K runtime");
    } else if (pef_ppc) {
        c->format = APP_FMT_PEF;
        snprintf(c->reason, APP_TEXT, "PowerPC PEF container with no main symbol: a shared library, not an application");
        c->is_application = 0;
    } else {
        c->format = APP_FMT_NONE;
        snprintf(c->reason, APP_TEXT, "no CODE resources, no PEF container and no Mach-O header: no executable code found");
        if (c->resource_fork_ok && c->has_carb) note(c->uncertainty, "a 'carb' resource is present, so the program may be Carbon code outside the forks given");
    }
    return 0;
}

const char *app_format_name(AppFormat f) {
    switch (f) {
    case APP_FMT_NONE: return "none";
    case APP_FMT_MALFORMED: return "malformed";
    case APP_FMT_CODE68K: return "68k-code";
    case APP_FMT_PEF: return "pef";
    case APP_FMT_FAT: return "fat-68k-pef";
    case APP_FMT_MACHO: return "macho";
    }
    return "unknown";
}
const char *app_profile_name(AppProfile p) {
    switch (p) {
    case APP_PROFILE_A_LEGACY_68K: return "A-legacy-68k";
    case APP_PROFILE_B_CLEAN_68K: return "B-clean-68k";
    case APP_PROFILE_C_CLASSIC_PPC: return "C-classic-ppc";
    case APP_PROFILE_D_UNSUPPORTED: return "D-unsupported";
    }
    return "none";
}
const char *app_confidence_name(AppConfidence c) { return c == APP_CONF_HIGH ? "high" : c == APP_CONF_MEDIUM ? "medium" : "low"; }
