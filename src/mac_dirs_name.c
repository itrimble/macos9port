#include "mac_files_int.h"
#include "macroman.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Guest path interpretation for the hierarchical File Manager (Inside Macintosh: Files, "Specifying a Catalog Entry"): an object is named by a volume
 * reference, a directory ID and a name. A name with a leading colon, or one that contains a colon and does not start with this volume's name, is a
 * partial path from that directory ("::" goes up one level); "OS999 HD:a:b" is a full path; a name with no colon is an entry of the directory. The
 * directory is ioDirID when it is not 0, else the directory an open working-directory reference stands for, else the default directory.
 * A guest name is Mac Roman bytes (macroman.h): a component is matched against the host's names without regard to case (HFS is case-insensitive;
 * ASCII plus the accented Latin letters) and in either Unicode normalisation, and takes the host's own spelling (UTF-8) when it exists; otherwise
 * it is stored as precomposed UTF-8. The result path is host UTF-8. Components that cannot be names (longer than 31 Mac Roman bytes, a control
 * byte, a leading dot, a slash or backslash) are bdNamErr. */
enum { COMPONENT_MAX = 31 };

static int ci_equal(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return !*a && !*b;
}

int mac_dirs_is_dir(const FileState *s, const char *rel) {
    struct stat st;
    if (!rel[0]) return 1;
    if (s->saves >= 0 && !mac_dirs_stat(s->saves, rel, &st)) return S_ISDIR(st.st_mode);
    return !mac_dirs_stat(s->directory, rel, &st) && S_ISDIR(st.st_mode);
}

/* The host's spelling of the guest component `mr` inside directory `dir`, when an entry matches without regard to case (the overlay's spelling wins;
 * an exact match is preferred to a case-only one). Host names the guest cannot see are never candidates. */
static int canonical(const FileState *s, const char *dir, const uint8_t *mr, size_t n, char *out) {
    char exact[MR_HOST_MAX];
    if (mr_component_to_host(mr, n, exact, sizeof exact) < 0) return 0;
    for (int pass = 0; pass < 2; pass++) {
        int root = pass == 0 ? s->saves : s->directory;
        if (root < 0) continue;
        DIR *d = mac_dirs_opendir(root, dir);
        if (!d) continue;
        struct dirent *e; unsigned scanned = 0; int found = 0;
        while ((e = readdir(d)) && ++scanned <= 4096) {
            uint8_t host[MR_NAME_MAX];
            int hn = mr_component_from_host(e->d_name, host, sizeof host);
            if (hn < 0 || !mr_equal_fold(host, (size_t)hn, mr, n)) continue;
            if (!found || !strcmp(e->d_name, exact)) snprintf(out, MR_HOST_MAX, "%s", e->d_name);
            found = 1;
            if (!strcmp(e->d_name, exact)) break;
        }
        closedir(d);
        if (found) return 1;
    }
    return 0;
}

/* The host spelling of a guest leaf name (Mac Roman bytes) in directory `dir`: an existing entry's own spelling, else the precomposed UTF-8 form.
 * 0, or -37 for a name that cannot be one. */
int mac_dirs_leaf_to_host(const FileState *s, const char *dir, const uint8_t *mr, size_t n, char out[MR_HOST_MAX]) {
    if (mr_component_to_host(mr, n, out, MR_HOST_MAX) < 0) return -37;
    canonical(s, dir, mr, n, out);
    return 0;
}

static int component_ok(const char *c, size_t n) {
    if (!n || n > COMPONENT_MAX || c[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) if (!mr_name_byte_ok((uint8_t)c[i])) return 0;
    return 1;
}

static int vref_ok(const MacRuntime *rt, int vref) {
    return vref == 0 || vref == -1 || (vref == -2 && rt->system_volume) || mac_dirs_wd_dir(&((FileState *)rt->file_state)->dirs, vref) != 0;
}

static int volume_named(const char *s, size_t n) {
    static const char name[] = "OS999 HD";
    if (n != sizeof name - 1) return 0;
    char tmp[sizeof name]; memcpy(tmp, s, n); tmp[n] = 0;
    return ci_equal(tmp, name);
}

int mac_dirs_resolve(MacRuntime *rt, int vref, uint32_t dir_id, const char *name, char out[DIRS_PATH_MAX]) {
    FileState *s = rt->file_state;
    out[0] = 0;
    if (!s) return -35;
    char path[DIRS_PATH_MAX] = "";
    size_t n = name ? strlen(name) : 0;
    const char *rest = name;
    int absolute = 0;
    if (n) {
        const char *colon = memchr(name, ':', n);
        if (colon && colon != name && volume_named(name, (size_t)(colon - name))) { absolute = 1; rest = colon + 1; n -= (size_t)(rest - name); }
    }
    if (!absolute && !vref_ok(rt, vref)) return -35;
    if (!absolute) {
        uint32_t base = dir_id ? dir_id : mac_dirs_wd_dir(&s->dirs, vref);
        if (base) { if (mac_dirs_path(&s->dirs, base, path)) return -120; }
        else snprintf(path, sizeof path, "%s", s->dirs.current);
        if (dir_id && !mac_dirs_is_dir(s, path)) return -120;
        if (n && rest[0] != ':') {   /* "a:b" starts with a volume name; when no such volume is mounted a folder of that name in the directory is taken instead */
            const char *colon = memchr(rest, ':', n);
            char first[MR_HOST_MAX], probe[DIRS_PATH_MAX];
            if (colon) {
                size_t length = (size_t)(colon - rest);
                if (!component_ok(rest, length)) return -37;
                if (mac_dirs_leaf_to_host(s, path, (const uint8_t *)rest, length, first)) return -37;
                snprintf(probe, sizeof probe, "%s%s%s", path, path[0] ? "/" : "", first);
                if (!mac_dirs_is_dir(s, probe)) return -35;   /* nsvErr: neither this volume nor a folder here */
            }
        }
        if (n && rest[0] == ':') {   /* ":x" is in the directory, "::x" in its parent, ":::x" in the grandparent */
            rest++; n--;
            while (n && rest[0] == ':') {
                char up[DIRS_PATH_MAX]; const char *leaf;
                if (!path[0]) return -120;
                mac_dirs_parent(path, up, &leaf);
                snprintf(path, sizeof path, "%s", up);
                rest++; n--;
            }
        }
    }
    int want_dir = 0;
    while (n && rest[n - 1] == ':') { n--; want_dir = 1; }   /* a trailing colon names a directory */
    while (n) {
        const char *colon = memchr(rest, ':', n);
        size_t length = colon ? (size_t)(colon - rest) : n;
        char comp[MR_HOST_MAX];
        if (!component_ok(rest, length)) return -37;
        if (mac_dirs_leaf_to_host(s, path, (const uint8_t *)rest, length, comp)) return -37;
        size_t have = strlen(path);
        if (have + (have ? 1 : 0) + strlen(comp) >= DIRS_PATH_MAX) return -37;
        if (have) path[have++] = '/';
        snprintf(path + have, sizeof path - have, "%s", comp);
        if (!mac_dirs_path_ok(path)) return -37;
        if (!colon) break;
        if (!mac_dirs_is_dir(s, path)) return -120;   /* an intermediate component that is missing, or is a file */
        rest = colon + 1; n -= length + 1;
    }
    if (want_dir && !mac_dirs_is_dir(s, path)) return -120;
    snprintf(out, DIRS_PATH_MAX, "%s", path);
    return 0;
}

/* The path a guest Pascal-string pointer names relative to (vref, dir_id); vref 0 and dir_id 0 are the default directory. A bad pointer is paramErr. */
int mac_files_ptr_path(MacRuntime *rt, uint32_t name_ptr, int vref, uint32_t dir_id, char *out) {
    int error = mac_files_ptr_path_raw(rt, name_ptr, vref, dir_id, out);
    return error == -37 ? -50 : error;
}
/* As above, with -37 (bdNamErr) for a name that cannot be a host name. */
int mac_files_ptr_path_raw(MacRuntime *rt, uint32_t name_ptr, int vref, uint32_t dir_id, char *out) {
    MacCpu *cpu = rt->cpu;
    char name[256] = "";
    if (!rt->file_state) return -35;
    if (name_ptr) {
        if (!mac_files_valid(cpu, name_ptr, 1)) return -50;
        unsigned length = mac_cpu_read(cpu, name_ptr, 1);
        if (!mac_files_valid(cpu, name_ptr + 1, length)) return -50;
        memcpy(name, mac_cpu_ram(cpu) + name_ptr + 1, length);
        name[length] = 0;
        if (memchr(name, 0, length)) return -50;
    }
    return mac_dirs_resolve(rt, vref, dir_id, name, out);
}

/* The path a parameter block names: ioNamePtr, ioVRefNum (+22) and, for the HFS flavours, ioDirID (+48). With allow_empty == 0 a name that denotes a
 * directory (empty, or only a volume name) is paramErr, as the file calls always need an entry name. */
int mac_files_pb_path(MacRuntime *rt, uint32_t pb, uint32_t name_ptr, char *out, int allow_empty) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    int vref = (int16_t)mac_cpu_read(rt->cpu, pb + 22, 2);
    uint32_t dir = s->hfs_call ? mac_cpu_read(rt->cpu, pb + 48, 4) : 0;
    int error = mac_files_ptr_path(rt, name_ptr, vref, dir, out);
    if (error) return error;
    if (!allow_empty && !out[0]) return -50;
    return 0;
}

/* Make the overlay's copy of an existing directory (and of its parents), so a file can be created inside it. */
int mac_dirs_ensure_overlay(FileState *s, const char *dirpath) {
    if (s->saves < 0) return -46;
    char partial[DIRS_PATH_MAX];
    size_t at = 0;
    for (const char *p = dirpath; *p;) {
        const char *slash = strchr(p, '/');
        size_t length = slash ? (size_t)(slash - p) : strlen(p);
        if (at) partial[at++] = '/';
        memcpy(partial + at, p, length); at += length; partial[at] = 0;
        struct stat st;
        if (mac_dirs_stat(s->saves, partial, &st)) {
            if (errno != ENOENT) return -36;
            if (!mac_dirs_is_dir(s, partial)) return -120;
            if (mac_dirs_mkdir(s->saves, partial) && errno != EEXIST) return -36;
        } else if (!S_ISDIR(st.st_mode)) return -120;
        p += length + (slash ? 1 : 0);
    }
    return 0;
}
