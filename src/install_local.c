#include "ospi.h"
#include "install_int.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg);
}
int install_tempfile(char *path, size_t n, const char *tag) {
    const char *tmp = getenv("TMPDIR");
    int k = snprintf(path, n, "%s/ospi-%s-XXXXXX", tmp && *tmp ? tmp : "/tmp", tag);
    return k < 0 || (size_t)k >= n ? -1 : mkstemp(path);
}

int install_archive_supported(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    static const char *supported[] = {".sit", ".sitx", ".bin", ".hqx", ".zip", ".cpt",
                                      ".img", ".dmg", ".iso", ".dsk", ".toast", ".cdr"};
    for (size_t i = 0; ext && i < sizeof supported / sizeof *supported; i++)
        if (!strcasecmp(ext, supported[i])) return 1;
    return 0;
}

static int path_within(const char *root, const char *path) {
    size_t n = strlen(root);
    return strncmp(root, path, n) == 0 && (path[n] == 0 || path[n] == '/');
}

int install_archive(const char *root, const char *archive_rel, char *created_rel, size_t created_n,
                char *err, size_t err_n) {
    if (created_rel && created_n) created_rel[0] = 0;
    if (err && err_n) err[0] = 0;
    if (!root || !archive_rel || !install_archive_supported(archive_rel)) {
        set_err(err, err_n, "This is not a supported archive or disk image.");
        return -1;
    }
    char full[OSPI_PATH], root_real[PATH_MAX], archive_real[PATH_MAX];
    struct stat st;
    if (volume_join(full, sizeof full, root, archive_rel) != 0 || lstat(full, &st) != 0 ||
        !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 200000000 ||
        !realpath(root, root_real) || !realpath(full, archive_real) || !path_within(root_real, archive_real)) {
        set_err(err, err_n, "This is not a regular archive inside the OS999 volume.");
        return -1;
    }
    char parent[OSPI_PATH] = "", base[OSPI_NAME], folder[OSPI_NAME], destination_rel[OSPI_PATH];
    const char *slash = strrchr(archive_rel, '/');
    const char *leaf = slash ? slash + 1 : archive_rel;
    if (slash) {
        size_t n = (size_t)(slash - archive_rel);
        if (!n || n >= sizeof parent) {
            set_err(err, err_n, "The archive folder path is invalid.");
            return -1;
        }
        memcpy(parent, archive_rel, n);
        parent[n] = 0;
    }
    size_t base_n = (size_t)(strrchr(leaf, '.') - leaf);
    if (base_n >= sizeof base) base_n = sizeof base - 1;
    memcpy(base, leaf, base_n);
    base[base_n] = 0;
    while (base_n && (base[base_n - 1] == '.' || base[base_n - 1] == ' ')) base[--base_n] = 0;
    if (!base[0] || !strcmp(base, ".") || !strcmp(base, "..")) snprintf(base, sizeof base, "Expanded");
    if (volume_mkdir_unique(root, parent, base, folder, sizeof folder) != 0) {
        set_err(err, err_n, "Could not create a new folder beside the archive.");
        return -1;
    }
    int k = parent[0] ? snprintf(destination_rel, sizeof destination_rel, "%s/%s", parent, folder) :
                        snprintf(destination_rel, sizeof destination_rel, "%s", folder);
    char destination[OSPI_PATH];
    if (k < 0 || (size_t)k >= sizeof destination_rel ||
        volume_join(destination, sizeof destination, root, destination_rel) != 0) {
        set_err(err, err_n, "The extracted folder path is too long.");
        return -1;
    }
    if (created_rel && created_n) snprintf(created_rel, created_n, "%s", destination_rel);
    int expanded = install_expand_archive(root, archive_real, destination_rel, err, err_n);
    if (expanded != 0) {
        if (expanded != -2) {
            rmdir(destination);
            if (created_rel && created_n) created_rel[0] = 0;
        }
        return -1;
    }
    return 0;
}

int install_sit(const char *root, const char *rel, char *created, size_t n, char *err, size_t en) {
    const char *ext = rel ? strrchr(rel, '.') : NULL;
    if (!ext || strcasecmp(ext, ".sit")) {
        if (created && n) created[0] = 0;
        set_err(err, en, "This is not a supported .sit archive.");
        return -1;
    }
    return install_archive(root, rel, created, n, err, en);
}
