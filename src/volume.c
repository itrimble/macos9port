#include "ospi.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>

static const struct {
    const char *rel;
    const char *body;
} k_seed[] = {
    {"Applications/Nothing Installed Yet",
     "No classic applications are installed.\n\n"
     "File, Install from Garden, copies a\n"
     "Macintosh Garden page here. Double-click\n"
     "a StuffIt archive to open it in\n"
     "Systemless. Many programs still fail.\n"},
    {"Documents/Notes",
     "Notes live here as plain text.\n\n"
     "Double-click a document to read it.\n"},
    {"Read Me",
     "OS Pi 0.1\n\n"
     "This desktop is a clean-room program.\n"
     "It is not Apple's Mac OS 9. There is\n"
     "no Apple ROM and no Apple Toolbox.\n\n"
     "These folders are real folders. New\n"
     "Folder makes a real folder. Empty Trash\n"
     "deletes real files.\n\n"
     "Cmd-N  New Folder\n"
     "Cmd-O  Open\n"
     "Cmd-W  Close\n"
     "Cmd-Q  Quit\n\n"
     "Double-click the disk if you close\n"
     "this window. Shut Down is under the\n"
     "pi mark at the left of the menu bar.\n"},
    {"System Folder/About This Folder",
     "On a classic Mac this folder held the\n"
     "system. Here it is an ordinary folder,\n"
     "so the desktop has the same shape as\n"
     "that screenshot. Nothing in it is an\n"
     "Apple system file.\n"},
    {"Trash/.keep", ""},
    {"System Folder/Fonts/About Fonts",
     "Drop TrueType, OpenType, or PostScript\n"
     "Type 1 fonts in this folder.\n"
     "Extensions: .ttf .otf .ttc .pfa .pfb\n\n"
     "OS Pi ships ChicagoFLF, which Robin\n"
     "Casady placed in the public domain.\n"
     "It is not Apple's Chicago. Geneva,\n"
     "Charcoal, and Monaco are used only\n"
     "when this computer already has them.\n"},
};

int volume_join(char *out, size_t n, const char *root, const char *rel) {
    if (!out || !root || n == 0) {
        return -1;
    }
    if (rel && rel[0]) {
        const char *p = rel;
        if (rel[0] == '/') {
            return -1;
        }
        while (*p) {
            const char *seg = p;
            while (*p && *p != '/') {
                p++;
            }
            size_t len = (size_t)(p - seg);
            if (len == 0 || (len == 1 && seg[0] == '.') ||
                (len == 2 && seg[0] == '.' && seg[1] == '.')) {
                return -1;
            }
            if (*p == '/') {
                p++;
            }
        }
    }
    int k;
    if (!rel || !rel[0]) {
        k = snprintf(out, n, "%s", root);
    } else {
        k = snprintf(out, n, "%s/%s", root, rel);
    }
    if (k < 0 || (size_t)k >= n) {
        return -1;
    }
    return 0;
}

int volume_mkdir_p(const char *path) {
    if (!path || !path[0]) {
        return -1;
    }
    char buf[OSPI_PATH];
    size_t n = strlen(path);
    if (n >= sizeof buf) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] != '/') {
            continue;
        }
        buf[i] = 0;
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        buf[i] = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int exists_file(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

int volume_seed(const char *root) {
    if (!root || volume_mkdir_p(root) != 0) {
        return -1;
    }
    for (size_t i = 0; i < sizeof k_seed / sizeof k_seed[0]; i++) {
        char parent_rel[OSPI_PATH];
        const char *sl = strrchr(k_seed[i].rel, '/');
        if (sl) {
            size_t len = (size_t)(sl - k_seed[i].rel);
            if (len >= sizeof parent_rel) {
                return -1;
            }
            memcpy(parent_rel, k_seed[i].rel, len);
            parent_rel[len] = 0;
            char parent[OSPI_PATH];
            if (volume_join(parent, sizeof parent, root, parent_rel) != 0) {
                return -1;
            }
            if (mkdir(parent, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
        }
        char full[OSPI_PATH];
        if (volume_join(full, sizeof full, root, k_seed[i].rel) != 0) {
            return -1;
        }
        if (exists_file(full)) {
            continue;
        }
        FILE *f = fopen(full, "w");
        if (!f) {
            return -1;
        }
        if (k_seed[i].body && fputs(k_seed[i].body, f) < 0) {
            fclose(f);
            return -1;
        }
        fclose(f);
    }
    return 0;
}

int volume_free_bytes(const char *root, uint64_t *out) {
    struct statvfs st;
    if (!out || statvfs(root, &st) != 0) {
        return -1;
    }
    unsigned long bs = st.f_frsize ? st.f_frsize : st.f_bsize;
    *out = (uint64_t)st.f_bavail * (uint64_t)bs;
    return 0;
}

static int hidden(const char *root_rel, const char *name) {
    if (name[0] == '.') {
        return 1;
    }
    if ((!root_rel || !root_rel[0]) && strcmp(name, "Trash") == 0) {
        return 1;
    }
    return 0;
}

static int cmp_item(const void *a, const void *b) {
    return strcasecmp(((const Item *)a)->name, ((const Item *)b)->name);
}

int volume_list(const char *root, const char *rel, Item *items, int max) {
    char full[OSPI_PATH];
    if (volume_join(full, sizeof full, root, rel) != 0) {
        return -1;
    }
    DIR *d = opendir(full);
    if (!d) {
        return -1;
    }
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (hidden(rel, de->d_name)) {
            continue;
        }
        if (strlen(de->d_name) >= OSPI_NAME) {
            continue;
        }
        if (n >= max) {
            break;
        }
        char child_rel[OSPI_PATH];
        char path[OSPI_PATH];
        if (!rel || !rel[0]) {
            snprintf(child_rel, sizeof child_rel, "%s", de->d_name);
        } else {
            snprintf(child_rel, sizeof child_rel, "%s/%s", rel, de->d_name);
        }
        if (volume_join(path, sizeof path, root, child_rel) != 0) {
            continue;
        }
        struct stat st;
        if (lstat(path, &st) != 0) {
            continue;
        }
        memset(&items[n], 0, sizeof items[n]);
        memcpy(items[n].name, de->d_name, strlen(de->d_name) + 1);
        if (S_ISDIR(st.st_mode)) {
            items[n].kind = strcmp(de->d_name, "System Folder") == 0 ? ITEM_SYSTEM : ITEM_FOLDER;
        } else {
            items[n].kind = ITEM_FILE;
        }
        n++;
    }
    closedir(d);
    qsort(items, (size_t)n, sizeof items[0], cmp_item);
    return n;
}

int volume_read(const char *root, const char *rel, char *buf, size_t n) {
    char full[OSPI_PATH];
    if (!buf || n < 2 || volume_join(full, sizeof full, root, rel) != 0) {
        return -1;
    }
    FILE *f = fopen(full, "rb");
    if (!f) {
        return -1;
    }
    size_t got = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[got] = 0;
    for (size_t i = 0; i < got; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c != '\n' && c != '\r' && c != '\t' && c < 32) {
            snprintf(buf, n,
                     "OS Pi cannot show this file as text.\n\n"
                     "Double-click a StuffIt, MacBinary,\n"
                     "or disk image to open it in\n"
                     "Systemless. Other files stay put.\n");
            return 0;
        }
    }
    return 0;
}

int volume_mkdir_unique(const char *root, const char *rel, const char *base, char *created,
                        size_t created_n) {
    char dir[OSPI_PATH];
    if (volume_join(dir, sizeof dir, root, rel) != 0) {
        return -1;
    }
    for (int i = 0; i < 100; i++) {
        char name[OSPI_NAME];
        if (i == 0) {
            snprintf(name, sizeof name, "%s", base);
        } else {
            snprintf(name, sizeof name, "%s %d", base, i + 1);
        }
        char path[OSPI_PATH];
        int k = snprintf(path, sizeof path, "%s/%s", dir, name);
        if (k < 0 || (size_t)k >= sizeof path) {
            return -1;
        }
        if (mkdir(path, 0755) == 0) {
            if (created && created_n) {
                snprintf(created, created_n, "%s", name);
            }
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    return -1;
}

static int copy_name(char *out, size_t n, const char *dir, const char *name) {
    int k = snprintf(out, n, "%s/%s", dir, name);
    if (k < 0 || (size_t)k >= n) {
        return -1;
    }
    struct stat st;
    if (lstat(out, &st) != 0) {
        return 0;
    }
    return 1;
}

int volume_trash(const char *root, const char *rel, const char *name) {
    if (!name || strchr(name, '/') || strcmp(name, "Trash") == 0) {
        return -1;
    }
    char src_rel[OSPI_PATH];
    if (!rel || !rel[0]) {
        snprintf(src_rel, sizeof src_rel, "%s", name);
    } else {
        int k = snprintf(src_rel, sizeof src_rel, "%s/%s", rel, name);
        if (k < 0 || (size_t)k >= sizeof src_rel) {
            return -1;
        }
    }
    char src[OSPI_PATH];
    char trash[OSPI_PATH];
    if (volume_join(src, sizeof src, root, src_rel) != 0) {
        return -1;
    }
    if (volume_join(trash, sizeof trash, root, "Trash") != 0) {
        return -1;
    }
    if (mkdir(trash, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    char dest[OSPI_PATH];
    for (int i = 0; i < 100; i++) {
        char nm[OSPI_NAME];
        if (i == 0) {
            snprintf(nm, sizeof nm, "%s", name);
        } else {
            snprintf(nm, sizeof nm, "%s %d", name, i + 1);
        }
        int state = copy_name(dest, sizeof dest, trash, nm);
        if (state < 0) {
            return -1;
        }
        if (state == 1) {
            continue;
        }
        if (rename(src, dest) == 0) {
            return 0;
        }
        return -1;
    }
    return -1;
}

int volume_count(const char *root, const char *rel) {
    Item items[OSPI_MAX_ITEMS];
    return volume_list(root, rel, items, OSPI_MAX_ITEMS);
}

static int wipe_at(const char *path, int depth) {
    struct stat st;
    if (depth > 16 || lstat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char child[OSPI_PATH];
        int k = snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (k < 0 || (size_t)k >= sizeof child || wipe_at(child, depth + 1) != 0) {
            rc = -1;
        }
    }
    closedir(d);
    if (rc != 0 || rmdir(path) != 0) {
        return -1;
    }
    return 0;
}

int volume_empty_trash(const char *root) {
    char trash[OSPI_PATH];
    if (volume_join(trash, sizeof trash, root, "Trash") != 0) {
        return -1;
    }
    DIR *d = opendir(trash);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char child[OSPI_PATH];
        int k = snprintf(child, sizeof child, "%s/%s", trash, de->d_name);
        if (k < 0 || (size_t)k >= sizeof child || wipe_at(child, 0) != 0) {
            rc = -1;
        }
    }
    closedir(d);
    return rc;
}
