#include "mac_files_int.h"
#include "mac_resource_chain.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* FSSpec-based File Manager calls ("HighLevelFSDispatch", trap $AA52) for the virtual data volume. Independent implementation from the public interface
 * only: Inside Macintosh: Files, "File System Specification Routines", and the MPW Files.p inline glue (INLINE $303C,<selector>,$AA52). Pascal calling
 * convention: arguments on the stack in declaration order, the OSErr result word below them; the selector is in D0. A Boolean/SignedByte argument is a
 * word holding the byte at the lower (even) address.
 *
 * Volume model: one volume "OS999 HD" (vRefNum -1, 0 = default) whose root directory has ID 2 and parent ID 1. Below the root are the real host
 * directories of the application data (read-only) merged with the save overlay's (mac_dirs.h); a spec is (parent directory ID, name), the parent being a
 * table ID. Guest writes go to the save overlay through the same cores the PB calls use (quotas, name rules, one writer, staged commit). Without --saves
 * every mutation fails with the documented File Manager error. Selectors that cannot be implemented honestly halt, naming the selector.
 * Gestalt 'fs  ' reports exactly gestaltHasFSSpecCalls (bit 1), see mac_system.c. */
enum { SPEC_BYTES = 70, SPEC_NAME_MAX = 63, ROOT_PARENT_ID = 1, ROOT_DIR_ID = 2 };
#define VOLUME_NAME "OS999 HD"

typedef struct {
    int root;                       /* the spec names the volume's root directory itself */
    char name[MR_HOST_MAX];         /* entry name when !root, as host UTF-8 (the spec itself carries Mac Roman bytes) */
    uint32_t dir;                   /* the directory it is in */
    char host[DIRS_PATH_MAX];       /* host-relative path of the entry ("" for the root) */
} Spec;

static const struct { const char *name; unsigned args; } SELECTORS[16] = {
    {0, 0}, {"FSMakeFSSpec", 14}, {"FSpOpenDF", 10}, {"FSpOpenRF", 10}, {"FSpCreate", 14},
    {"FSpDirCreate", 10}, {"FSpDelete", 4}, {"FSpGetFInfo", 8}, {"FSpSetFInfo", 8}, {"FSpSetFLock", 4},
    {"FSpRstFLock", 4}, {"FSpRename", 8}, {"FSpCatMove", 8}, {"FSpOpenResFile", 6}, {"FSpCreateResFile", 14},
    {"FSpExchangeFiles", 8},
};

static int ram_ok(MacCpu *cpu, uint32_t p, uint32_t n, int even) {
    return p && mac_files_valid(cpu, p, n) && !(even && (p & 1));
}

static int volume_name_eq(const char *s, size_t n) {
    if (n != strlen(VOLUME_NAME)) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned a = (unsigned char)s[i], b = (unsigned char)VOLUME_NAME[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return 1;
}

static void write_spec(MacCpu *cpu, uint32_t p, int root, const char *name, uint32_t dir) {
    const char *shown = root ? VOLUME_NAME : name;
    memset(mac_cpu_ram(cpu) + p, 0, SPEC_BYTES); /* zero padding so specs compare equal bytewise */
    mac_cpu_write(cpu, p, 2, 0xffff);
    mac_cpu_write(cpu, p + 2, 4, root ? ROOT_PARENT_ID : dir);
    mr_put_pstring(mac_cpu_ram(cpu) + p + 6, MR_NAME_MAX, shown);   /* host UTF-8 back to Mac Roman */
}

/* The volumes a spec may name: the default volume (0), the startup volume (-1), the System volume (-2) when the runtime has one, and an open working directory. */
static int vol_ok(const MacRuntime *rt, int vref) { return vref == 0 || vref == -1 || (vref == -2 && rt->system_volume) || mac_files_wd_folder(rt, vref) != 0; }

/* Decode and validate a guest FSSpec. Never reads outside guest RAM. */
static int read_spec(MacRuntime *rt, uint32_t p, Spec *out) {
    MacCpu *cpu = rt->cpu;
    memset(out, 0, sizeof *out);
    FileState *s = rt->file_state;
    if (!s) return -35;
    if (!ram_ok(cpu, p, SPEC_BYTES, 1)) return -50;
    int vref = (int16_t)mac_cpu_read(cpu, p, 2);
    uint32_t parent = mac_cpu_read(cpu, p + 2, 4);
    size_t n = mac_cpu_read(cpu, p + 6, 1);
    if (!vol_ok(rt, vref)) return -35; /* nsvErr */
    if (n > SPEC_NAME_MAX) return -37;
    const char *name = (const char *)mac_cpu_ram(cpu) + p + 7;
    out->root = parent == ROOT_PARENT_ID;
    if (out->root) return volume_name_eq(name, n) ? 0 : -35;
    char dirpath[DIRS_PATH_MAX];
    if (mac_dirs_path(&s->dirs, parent, dirpath)) return -120; /* dirNFErr */
    /* The spec's name is Mac Roman: at most 31 bytes, no control byte, ':' '/' or '\\', no leading dot (bdNamErr); it maps to the host's own spelling of an
     * existing entry (any case, either normalisation) or to precomposed UTF-8. */
    if (mac_dirs_leaf_to_host(s, dirpath, (const uint8_t *)name, n, out->name)) return -37;
    if (strlen(dirpath) + strlen(out->name) + 2 >= DIRS_PATH_MAX) return -37;
    snprintf(out->host, sizeof out->host, "%s%s%s", dirpath, dirpath[0] ? "/" : "", out->name);
    out->dir = parent;
    return 0;
}

/* Existence as the guest sees it: 0 a regular file, 1 a directory, -43 absent (or a link or device, which are never files). */
static int kind_of(const FileState *s, const Spec *spec) {
    if (spec->root) return 1;
    int kind = mac_files_classify(s, spec->host);
    if (kind == FILE_SAVE || kind == FILE_DATA) return 0;
    return mac_dirs_is_dir(s, spec->host) ? 1 : -43;
}
static int exists(const FileState *s, const Spec *spec) { return kind_of(s, spec) == 0 ? 0 : -43; }

static int unsupported(MacRuntime *rt, unsigned selector, const char *why) {
    char message[160];
    snprintf(message, sizeof message, "HighLevelFSDispatch selector %u (%s) is not implemented: %s.",
             selector, SELECTORS[selector].name, why);
    mac_cpu_halt(rt->cpu, message);
    return -1;
}

/* FSMakeFSSpec(vRefNum, dirID, fileName, VAR spec). Path interpretation is mac_dirs_resolve's: "Vol:a:b" is absolute and ignores vRefNum / dirID, ":a:b"
 * and "a:b" are partial paths from dirID, "a" is an entry of dirID, and an empty or volume-only name denotes the directory itself. A missing last component
 * is fnfErr with a usable spec; a missing earlier component is dirNFErr. */
static int make_fsspec(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    uint32_t out = mac_cpu_read(cpu, sp, 4), name = mac_cpu_read(cpu, sp + 4, 4);
    uint32_t dir = mac_cpu_read(cpu, sp + 8, 4);
    int vref = (int16_t)mac_cpu_read(cpu, sp + 12, 2);
    if (!ram_ok(cpu, out, SPEC_BYTES, 1)) return -50;
    if (!s) return -35;
    char path[DIRS_PATH_MAX];
    int error = mac_files_ptr_path_raw(rt, name, vref, dir, path);   /* a NULL fileName means the empty name */
    if (error) { memset(mac_cpu_ram(cpu) + out, 0, SPEC_BYTES); return error; } /* contents are undefined on failure; leave nothing stale */
    if (!path[0]) { write_spec(cpu, out, 1, "", ROOT_DIR_ID); return 0; }
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(path, parent, &leaf);
    write_spec(cpu, out, 0, leaf, mac_dirs_id(&s->dirs, parent));
    int kind = mac_files_classify(s, path);
    return kind == FILE_SAVE || kind == FILE_DATA || mac_dirs_is_dir(s, path) ? 0 : -43;
}

static int open_fork(MacRuntime *rt, uint32_t sp, int resource) {
    MacCpu *cpu = rt->cpu;
    uint32_t ref_ptr = mac_cpu_read(cpu, sp, 4);
    unsigned permission = mac_cpu_read(cpu, sp + 4, 1);
    Spec spec;
    if (!ram_ok(cpu, ref_ptr, 2, 1)) return -50;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 6, 4), &spec);
    if (error) return error;
    if (spec.root) return -43; /* a directory is not a file */
    if (permission > 4) return -50;
    if ((error = exists(rt->file_state, &spec))) return error; /* links and host directories are not files */
    int ref;
    error = resource ? mac_files_open_rf_path(rt, spec.host, permission, &ref) : mac_files_open_name(rt, spec.host, permission, &ref);
    if (error) return error;
    mac_cpu_write(cpu, ref_ptr, 2, (uint16_t)ref);
    return 0;
}

static int create(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t file_type = mac_cpu_read(cpu, sp + 2, 4), creator = mac_cpu_read(cpu, sp + 6, 4);
    Spec spec;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 10, 4), &spec);
    if (error) return error;
    FileState *s = rt->file_state;
    if (s->saves < 0) return -46; /* vLckdErr: the volume is read-only without a save overlay */
    if (spec.root) return -48; /* the directory exists */
    if (!mac_files_save_name_ok(spec.host)) return -37;
    if (kind_of(s, &spec) == 1) return -48;
    if ((error = mac_files_create_name(rt, spec.host))) return error;
    if (!file_type && !creator) return 0;
    uint8_t info[16] = {0};
    for (int i = 0; i < 4; i++) {
        info[i] = (uint8_t)(file_type >> (24 - 8 * i));
        info[4 + i] = (uint8_t)(creator >> (24 - 8 * i));
    }
    int fd = mac_files_open_regular(s->saves, spec.host, O_RDWR);
    error = fd < 0 ? fd : mac_files_finder_set(fd, info);
    if (fd >= 0) close(fd);
    if (error) mac_dirs_unlink(s->saves, spec.host); /* never leave a file whose type and creator were lost */
    return error;
}

static int get_finfo(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t out = mac_cpu_read(cpu, sp, 4);
    Spec spec;
    if (!ram_ok(cpu, out, 16, 0)) return -50;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 4, 4), &spec);
    if (error) return error;
    if (spec.root || (error = exists(rt->file_state, &spec))) return -43;
    FileState *s = rt->file_state;
    int saved = mac_files_classify(s, spec.host) == FILE_SAVE;
    int fd = mac_files_open_regular(saved ? s->saves : s->directory, spec.host, O_RDONLY);
    if (fd < 0) return fd == -43 ? -43 : -36;
    uint8_t info[16] = {0};
    mac_files_finder_get(fd, info);
    close(fd);
    memcpy(mac_cpu_ram(cpu) + out, info, 16);
    return 0;
}

static int set_finfo(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t in = mac_cpu_read(cpu, sp, 4);
    Spec spec;
    if (!ram_ok(cpu, in, 16, 0)) return -50;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 4, 4), &spec);
    if (error) return error;
    FileState *s = rt->file_state;
    if (spec.root) return s->saves < 0 ? -46 : -43;
    if ((error = mac_files_locked_error(s, mac_files_classify(s, spec.host)))) return error;
    int fd = mac_files_open_regular(s->saves, spec.host, O_RDWR);
    if (fd < 0) return fd;
    error = mac_files_finder_set(fd, mac_cpu_ram(cpu) + in);
    close(fd);
    return error;
}

static int delete_file(MacRuntime *rt, uint32_t sp) {
    Spec spec;
    int error = read_spec(rt, mac_cpu_read(rt->cpu, sp, 4), &spec);
    if (error) return error;
    if (spec.root) return ((FileState *)rt->file_state)->saves < 0 ? -46 : -47; /* the volume root cannot go */
    return mac_files_delete_name(rt, spec.host);
}

static int rename_file(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t to_ptr = mac_cpu_read(cpu, sp, 4);
    Spec spec;
    if (!to_ptr || !mac_files_valid(cpu, to_ptr, 1)) return -50;
    size_t n = mac_cpu_read(cpu, to_ptr, 1);
    if (!mac_files_valid(cpu, to_ptr + 1, (uint32_t)n)) return -50;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 4, 4), &spec);
    if (error) return error;
    if (spec.root) return -46; /* the volume name is fixed */
    char to[DIRS_PATH_MAX], parent[DIRS_PATH_MAX], leaf_name[MR_HOST_MAX];
    const char *leaf;
    if (n > FILE_NAME_MAX || mr_component_to_host(mac_cpu_ram(cpu) + to_ptr + 1, n, leaf_name, sizeof leaf_name) < 0) return -37;   /* Mac Roman bytes, exact spelling */
    mac_dirs_parent(spec.host, parent, &leaf);
    if (snprintf(to, sizeof to, "%s%s%s", parent, parent[0] ? "/" : "", leaf_name) >= (int)sizeof to || !mac_files_save_name_ok(to)) return -37;
    return mac_files_rename_names(rt, spec.host, to);
}

/* FSpDirCreate(spec, scriptTag, VAR createdDirID): the directory is made in the save overlay (parents that exist only in the application data are made
 * there first); -48 when the name is taken, -46 without an overlay, -34 when the volume is full. */
static int dir_create(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    uint32_t out = mac_cpu_read(cpu, sp, 4);
    Spec spec;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 6, 4), &spec);
    if (error) return error;
    if (s->saves < 0) return -46;
    if (spec.root || kind_of(s, &spec) != -43) return -48;
    if (!ram_ok(cpu, out, 4, 1)) return -50;
    if (!mac_files_save_name_ok(spec.host)) return -37;
    unsigned files; uint64_t bytes;
    if ((error = mac_files_usage(s, &files, &bytes))) return error == -50 ? -34 : error;
    if (volume_budget_spend(&s->disk, 512)) return -34;
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(spec.host, parent, &leaf);
    if (parent[0] && (error = mac_dirs_ensure_overlay(s, parent))) return error;
    if (mac_dirs_mkdir(s->saves, spec.host)) return -36;
    fsync(s->saves);
    mac_cpu_write(cpu, out, 4, mac_dirs_id(&s->dirs, spec.host));
    return 0;
}

/* FSpCatMove(source, dest): `dest` names the destination directory (a spec of a directory, or the root); the source keeps its name there. */
static int cat_move(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    Spec from, dest;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 4, 4), &from);
    if (!error) error = read_spec(rt, mac_cpu_read(cpu, sp, 4), &dest);
    if (error) return error;
    if (from.root) return -46;
    if (s->saves < 0) return -46;
    if (kind_of(s, &dest) != 1) return -120;
    char to[DIRS_PATH_MAX];
    if (snprintf(to, sizeof to, "%s%s%s", dest.host, dest.host[0] ? "/" : "", from.name) >= (int)sizeof to) return -37;
    if (kind_of(s, &from) == -43) return -43;
    if (!strcmp(from.host, to)) return 0;
    return mac_files_rename_names(rt, from.host, to);
}

/* Calls whose semantics cannot be implemented on this forkless host-backed volume. A read-only volume still answers with the documented errors; with a
 * save overlay they halt rather than pretend. */
static int deferred(MacRuntime *rt, unsigned selector, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    Spec spec;
    int error = read_spec(rt, mac_cpu_read(cpu, sp + (selector == 15 ? 4 : 0), 4), &spec);
    if (error) return error;
    FileState *s = rt->file_state;
    if (selector == 15) {   /* the second spec is the first argument on the stack */
        Spec other;
        if ((error = read_spec(rt, mac_cpu_read(cpu, sp, 4), &other))) return error;
        if (s->saves < 0) return exists(s, &spec) ? -43 : -46;
    } else if (s->saves < 0) {
        return spec.root ? -46 : exists(s, &spec) ? -43 : -46;
    }
    return unsupported(rt, selector, "lock bits and exchanges are not modelled");
}

int mac_fsspec_service(MacRuntime *rt, uint16_t opcode) {
    (void)opcode;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    /* A sandboxed startup INIT does not get the volume model: mac_env.c answers every FSp call with
     * fnfErr so the INIT keeps running instead of halting on a selector this model leaves out. */
    if (rt->init_guard) return 0;
    MacCpu *cpu = rt->cpu;
    unsigned selector = mac_cpu_get(cpu, MAC_D0) & 0xffff;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (selector < 1 || selector > 15) {
        char message[96];
        snprintf(message, sizeof message, "HighLevelFSDispatch selector %u is unknown.", selector);
        mac_cpu_halt(cpu, message);
        return -1;
    }
    unsigned args = SELECTORS[selector].args;
    if (!ram_ok(cpu, sp, args + 2, 1)) {
        mac_cpu_halt(cpu, "Invalid stack for HighLevelFSDispatch.");
        return -1;
    }
    int error;
    switch (selector) {
    case 1: error = make_fsspec(rt, sp); break;
    case 2: error = open_fork(rt, sp, 0); break;
    case 3: error = open_fork(rt, sp, 1); break;
    case 4: error = create(rt, sp); break;
    case 5: error = dir_create(rt, sp); break;
    case 6: error = delete_file(rt, sp); break;
    case 7: error = get_finfo(rt, sp); break;
    case 8: error = set_finfo(rt, sp); break;
    case 11: error = rename_file(rt, sp); break;
    case 12: error = cat_move(rt, sp); break;
    case 9: case 10: case 15: error = deferred(rt, selector, sp); break;
    case 13: {   /* FSpOpenResFile(spec, permission): INTEGER result is the file reference (or -1), the error goes to ResError ($A60) */
        Spec spec; int16_t ref = -1;
        uint32_t spec_ptr = mac_cpu_read(cpu, sp + 2, 4);
        error = read_spec(rt, spec_ptr, &spec);
        if (!error && spec.root) error = -37;
        if (!error) error = mac_resource_open_path(rt, spec.host, &ref);
        if (mac_cpu_halted(cpu)) return -1;
        mac_cpu_write(cpu, 0xa60, 2, (uint16_t)error);
        mac_cpu_write(cpu, sp + args, 2, (uint16_t)(error ? -1 : ref));
        mac_cpu_set(cpu, MAC_A7, sp + args);
        return 1;
    }
    default: return unsupported(rt, selector, "creating resource files is outside this volume model");
    }
    if (error == -1 && mac_cpu_halted(cpu)) return -1;
    if (rt->trace) fprintf(stderr, "fsspec selector=%u (%s) error=%d\n", selector, SELECTORS[selector].name, error);
    mac_cpu_write(cpu, sp + args, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_A7, sp + args);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)error);
    return 1;
}
