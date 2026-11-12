#include "ospi.h"
#include "install_int.h"
#include "os999_unpack.h"
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
    return os999_private_temp(path, n, tag);
}

int install_archive_supported(const char *name) {
    const char *ext = name ? strrchr(name, '.') : NULL;
    static const char *supported[] = {".sit", ".sitx", ".sea", ".bin", ".hqx", ".zip", ".cpt",
                                      ".img", ".dmg", ".iso", ".dsk", ".toast", ".cdr"};
    for (size_t i = 0; ext && i < sizeof supported / sizeof *supported; i++)
        if (!strcasecmp(ext, supported[i])) return 1;
    return 0;
}

int install_archive_sniff(const char *full_path) {
    uint8_t head[512];
    FILE *f = full_path ? fopen(full_path, "rb") : NULL;
    if (!f) return 0;
    size_t got = fread(head, 1, sizeof head, f);
    fclose(f);
    StFormat fmt = got >= 14 ? st_detect(head, got, NULL, 0) : ST_FMT_UNKNOWN;
    return fmt == ST_FMT_SIT || fmt == ST_FMT_SIT5 || fmt == ST_FMT_SITX;
}

static void rmdir_rel(const char *root, const char *parent, const char *leaf) {
    char rel[OSPI_PATH], full[OSPI_PATH];
    if ((parent[0] ? snprintf(rel, sizeof rel, "%s/%s", parent, leaf) : snprintf(rel, sizeof rel, "%s", leaf)) <
            (int)sizeof rel && volume_join(full, sizeof full, root, rel) == 0)
        rmdir(full);
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
        rmdir_rel(root, parent, folder);
        set_err(err, err_n, "The extracted folder path is too long.");
        return -1;
    }
    /* Extract into a hidden temporary sibling, then rename it onto the empty
     * folder reserved above: a failed or partial expansion leaves nothing. */
    char tmp_name[OSPI_NAME], tmp_rel[OSPI_PATH], tmp_full[OSPI_PATH];
    if (volume_mkdir_unique(root, parent, ".Expanding", tmp_name, sizeof tmp_name) != 0 ||
        (parent[0] ? snprintf(tmp_rel, sizeof tmp_rel, "%s/%s", parent, tmp_name) :
                     snprintf(tmp_rel, sizeof tmp_rel, "%s", tmp_name)) >= (int)sizeof tmp_rel ||
        volume_join(tmp_full, sizeof tmp_full, root, tmp_rel) != 0) {
        rmdir(destination);
        set_err(err, err_n, "Could not create a working folder beside the archive.");
        return -1;
    }
    int expanded = install_expand_archive(root, archive_real, tmp_rel, err, err_n);
    if (expanded != 0) {
        rmdir(tmp_full);   /* st_vol_install undid its own files; only an empty folder remains */
        rmdir(destination);
        return -1;
    }
    if (rename(tmp_full, destination) != 0) {
        set_err(err, err_n, "Expanded the archive but could not put the new folder in place.");
        rmdir(destination);
        return -1;   /* the hidden working folder is kept so nothing the user could need is lost */
    }
    if (created_rel && created_n) snprintf(created_rel, created_n, "%s", destination_rel);
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
