#include "mac_files_int.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* FSSpec-based File Manager calls ("HighLevelFSDispatch", trap $AA52) for the virtual data volume.
 * Independent implementation from the public interface only: Inside Macintosh: Files, "File System
 * Specification Routines", and the MPW Files.p inline glue (INLINE $303C,<selector>,$AA52). Pascal calling
 * convention: arguments on the stack in declaration order, the OSErr result word below them; the selector
 * is in D0. A Boolean/SignedByte argument is a word holding the byte at the lower (even) address.
 *
 * Volume model, identical to the PB calls in mac_files.c: one flat volume "OS Pi Data" (vRefNum -1, 0 = default)
 * whose root directory has ID 2 and parent ID 1. It has no subdirectories, so every path below the root fails with
 * dirNFErr. Application data files are read-only; guest writes go to the save overlay through the same cores
 * the PB calls use (quotas, name rules, one writer, staged commit). Without --saves every mutation fails with the
 * documented File Manager error. Selectors that cannot be implemented honestly halt, naming the selector.
 * Gestalt 'fs  ' reports exactly gestaltHasFSSpecCalls (bit 1), see mac_system.c. */
enum { SPEC_BYTES = 70, SPEC_NAME_MAX = 63, ROOT_PARENT_ID = 1, ROOT_DIR_ID = 2 };
#define VOLUME_NAME "OS Pi Data"

typedef struct {
    int root;                       /* the spec names the volume's root directory itself */
    char name[SPEC_NAME_MAX + 1];   /* file name when !root */
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

/* HFS-style names for this volume: printable ASCII, no separators, no leading dot (hidden names stay hidden). */
static int name_ok(const char *s, size_t n) {
    if (!n || n > SPEC_NAME_MAX || s[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c >= 127 || c == '/' || c == ':' || c == '\\') return 0;
    }
    return 1;
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

static void write_spec(MacCpu *cpu, uint32_t p, int root, const char *name) {
    const char *shown = root ? VOLUME_NAME : name;
    memset(mac_cpu_ram(cpu) + p, 0, SPEC_BYTES); /* zero padding so specs compare equal bytewise */
    mac_cpu_write(cpu, p, 2, 0xffff);
    mac_cpu_write(cpu, p + 2, 4, root ? ROOT_PARENT_ID : ROOT_DIR_ID);
    mac_cpu_ram(cpu)[p + 6] = (uint8_t)strlen(shown);
    memcpy(mac_cpu_ram(cpu) + p + 7, shown, strlen(shown));
}

/* Decode and validate a guest FSSpec. Never reads outside guest RAM. */
static int read_spec(MacRuntime *rt, uint32_t p, Spec *out) {
    MacCpu *cpu = rt->cpu;
    if (!rt->file_state) return -35;
    if (!ram_ok(cpu, p, SPEC_BYTES, 1)) return -50;
    int vref = (int16_t)mac_cpu_read(cpu, p, 2);
    uint32_t parent = mac_cpu_read(cpu, p + 2, 4);
    size_t n = mac_cpu_read(cpu, p + 6, 1);
    if (vref != 0 && vref != -1) return -35; /* nsvErr: no working directories or other volumes */
    if (n > SPEC_NAME_MAX) return -37;
    const char *s = (const char *)mac_cpu_ram(cpu) + p + 7;
    memcpy(out->name, s, n);
    out->name[n] = 0;
    out->root = parent == ROOT_PARENT_ID;
    if (out->root) return volume_name_eq(s, n) ? 0 : -35;
    if (parent != ROOT_DIR_ID) return -120; /* dirNFErr */
    return name_ok(s, n) ? 0 : -37; /* bdNamErr */
}

/* FSMakeFSSpec path interpretation (Files, FSMakeFSSpec): "Vol:name" is absolute and ignores vRefNum/dirID, ":name" is
 * relative, "name" is in dirID. The empty or volume-only name denotes the directory itself. Intermediate components
 * would be subdirectories, which this volume does not have. */
static int make_spec(MacRuntime *rt, int vref, uint32_t dir, const char *path, size_t n, Spec *out) {
    const char *colon = memchr(path, ':', n), *rest = path;
    if (!rt->file_state) return -35;
    if (n && path[0] != ':' && colon) { /* absolute */
        if (!volume_name_eq(path, (size_t)(colon - path))) return -35;
        rest = colon + 1;
    } else {
        if (vref != 0 && vref != -1) return -35;
        if (dir != 0 && dir != ROOT_DIR_ID) return -120;
        if (n && path[0] == ':') rest = path + 1;
    }
    n -= (size_t)(rest - path);
    out->root = 0;
    if (!n) { out->root = 1; return 0; }
    const char *next = memchr(rest, ':', n);
    size_t length = next ? (size_t)(next - rest) : n;
    if (length && !name_ok(rest, length)) return -37;
    if (next) return -120; /* first component would be a subdirectory (or the parent "::") */
    memcpy(out->name, rest, length);
    out->name[length] = 0;
    return 0;
}

/* Existence of the named object as the guest sees it: 0 file, -43 absent or not a regular file. */
static int exists(const FileState *s, const Spec *spec) {
    int kind = mac_files_classify(s, spec->name);
    return kind == FILE_SAVE || kind == FILE_DATA ? 0 : -43;
}

static int unsupported(MacRuntime *rt, unsigned selector, const char *why) {
    char message[160];
    snprintf(message, sizeof message, "HighLevelFSDispatch selector %u (%s) is not implemented: %s.",
             selector, SELECTORS[selector].name, why);
    mac_cpu_halt(rt->cpu, message);
    return -1;
}

static int make_fsspec(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t out = mac_cpu_read(cpu, sp, 4), name = mac_cpu_read(cpu, sp + 4, 4);
    uint32_t dir = mac_cpu_read(cpu, sp + 8, 4);
    int vref = (int16_t)mac_cpu_read(cpu, sp + 12, 2);
    char path[256] = {0};
    size_t n = 0;
    if (!ram_ok(cpu, out, SPEC_BYTES, 1)) return -50;
    if (name) { /* a NULL fileName means the empty name */
        if (!mac_files_valid(cpu, name, 1)) return -50;
        n = mac_cpu_read(cpu, name, 1);
        if (!mac_files_valid(cpu, name + 1, (uint32_t)n)) return -50;
        memcpy(path, mac_cpu_ram(cpu) + name + 1, n);
    }
    Spec spec;
    int error = make_spec(rt, vref, dir, path, n, &spec);
    if (!error && !spec.root) error = exists(rt->file_state, &spec);
    if (error == 0 || error == -43) write_spec(cpu, out, spec.root, spec.name); /* fnfErr still yields a usable spec */
    else memset(mac_cpu_ram(cpu) + out, 0, SPEC_BYTES); /* contents are undefined on failure; leave nothing stale */
    return error;
}

static int open_df(MacRuntime *rt, uint32_t sp) {
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
    if ((error = mac_files_open_name(rt, spec.name, permission, &ref))) return error;
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
    if (!mac_files_save_name_ok(spec.name)) return -37; /* HFS names are at most 31 characters */
    if ((error = mac_files_create_name(rt, spec.name))) return error;
    if (!file_type && !creator) return 0;
    uint8_t info[16] = {0};
    for (int i = 0; i < 4; i++) {
        info[i] = (uint8_t)(file_type >> (24 - 8 * i));
        info[4 + i] = (uint8_t)(creator >> (24 - 8 * i));
    }
    int fd = mac_files_open_regular(s->saves, spec.name, O_RDWR);
    error = fd < 0 ? fd : mac_files_finder_set(fd, info);
    if (fd >= 0) close(fd);
    if (error) unlinkat(s->saves, spec.name, 0); /* never leave a file whose type and creator were lost */
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
    int saved = mac_files_classify(s, spec.name) == FILE_SAVE;
    int fd = mac_files_open_regular(saved ? s->saves : s->directory, spec.name, O_RDONLY);
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
    if ((error = mac_files_locked_error(s, mac_files_classify(s, spec.name)))) return error;
    int fd = mac_files_open_regular(s->saves, spec.name, O_RDWR);
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
    return mac_files_delete_name(rt, spec.name);
}

static int rename_file(MacRuntime *rt, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t to_ptr = mac_cpu_read(cpu, sp, 4);
    Spec spec;
    if (!to_ptr || !mac_files_valid(cpu, to_ptr, 1)) return -50;
    size_t n = mac_cpu_read(cpu, to_ptr, 1);
    if (!mac_files_valid(cpu, to_ptr + 1, (uint32_t)n)) return -50;
    char to[SPEC_NAME_MAX + 1];
    int error = read_spec(rt, mac_cpu_read(cpu, sp + 4, 4), &spec);
    if (error) return error;
    if (spec.root) return -46; /* the volume name is fixed */
    if (!name_ok((const char *)mac_cpu_ram(cpu) + to_ptr + 1, n)) return -37;
    memcpy(to, mac_cpu_ram(cpu) + to_ptr + 1, n);
    to[n] = 0;
    if (!mac_files_save_name_ok(to)) return -37;
    return mac_files_rename_names(rt, spec.name, to);
}

/* Calls whose semantics cannot be implemented on this flat, forkless host-backed volume. A read-only volume
 * still answers with the documented errors; with a save overlay they halt rather than pretend. */
static int deferred(MacRuntime *rt, unsigned selector, uint32_t sp) {
    MacCpu *cpu = rt->cpu;
    uint32_t first = mac_cpu_read(cpu, sp + (selector == 5 ? 6 : selector == 12 || selector == 15 ? 4 : 0), 4);
    Spec spec, other;
    int error = read_spec(rt, first, &spec);
    if (error) return error;
    FileState *s = rt->file_state;
    if (selector == 12 || selector == 15) { /* the second spec is the first argument on the stack */
        if ((error = read_spec(rt, mac_cpu_read(cpu, sp, 4), &other))) return error;
        if (s->saves < 0) return exists(s, &spec) ? -43 : -46;
    } else if (s->saves < 0) {
        return selector == 5 || spec.root ? -46 : exists(s, &spec) ? -43 : -46;
    }
    return unsupported(rt, selector,
        selector == 5 ? "the volume has no subdirectories" : "lock bits, forks and moves are not modelled");
}

int mac_fsspec_service(MacRuntime *rt, uint16_t opcode) {
    (void)opcode;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
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
    case 2: error = open_df(rt, sp); break;
    case 4: error = create(rt, sp); break;
    case 6: error = delete_file(rt, sp); break;
    case 7: error = get_finfo(rt, sp); break;
    case 8: error = set_finfo(rt, sp); break;
    case 11: error = rename_file(rt, sp); break;
    case 5: case 9: case 10: case 12: case 15: error = deferred(rt, selector, sp); break;
    default: return unsupported(rt, selector, "resource forks and resource files are outside this volume model");
    }
    if (error == -1 && mac_cpu_halted(cpu)) return -1;
    if (rt->trace) fprintf(stderr, "fsspec selector=%u (%s) error=%d\n", selector, SELECTORS[selector].name, error);
    mac_cpu_write(cpu, sp + args, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_A7, sp + args);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)error);
    return 1;
}
