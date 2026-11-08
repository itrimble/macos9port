#include "volume_documents.h"
#include "ospi.h"
#include "native_apps.h"
#include "mac_icon.h"
#include "volume_directory.h"

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
    {"Applications/Utilities/Tailscale", "OS999-BUILTIN:tailscale:1\n"},
    {"Applications/Utilities/Terminal", "OS999-BUILTIN:terminal:1\n"},
    {"Documents/Getting Started",
     "Getting Started with OS999\n\nOS999 is a classic-Mac-style desktop running on your host computer. Double-click folders and documents, or select an item and choose File > Open. Use the File menu to create folders or a New Note.\n\nNew notes are plain .txt files in Documents and save as you type. Click to position the caret, use left and right arrows, Return for a new paragraph, and Command-A to replace all text. Existing notes are never overwritten by seeding.\n\nDrag title bars to move windows. Drag the lower-right corner to resize; long paragraphs wrap to the new width. Scroll with the wheel. Command-W closes the front window.\n\nThe Granny Smith menu opens Control Panels, Terminal, browser and media features. Some features depend on optional helpers in the build. See Help and Limitations before relying on compatibility claims.\n"},
    {"Documents/Applications Guide",
     "Applications Guide\n\nFinder manages your volume files. The built-in text viewer opens readable documents; File > New Note creates an editable plain-text note. Terminal runs host commands, so use it with the same care as your normal terminal.\n\nChromium Browser requires the separately built browser helper. OS999 Amp handles supported media when the media components are included. A packaged helper or successful launch alone does not prove video acceleration, playback quality or device support.\n\nSupported classic CODE-resource applications use the independent OS999 runtime. Other architectures and Toolbox features may be unsupported. Quake III requires the appropriate engine and separately supplied demo or retail assets; commercial data is not included.\n\nControl Panels adjust desktop preferences. Controls labeled unavailable do not change host networking or hardware. Bluetooth radio pairing and real-device interoperability remain separate acceptance tests.\n"},
    {"Documents/Storage and Installers",
     "Storage and Installers\n\nYour OS999 volume is a real directory on the host. Copy, move and rename operations affect those files. Empty Trash permanently removes its contents. Back up important documents and saves.\n\nSupported archives retain classic resource forks and Finder metadata using the storage subsystem. Keep .rsrc sidecars with their matching data files. Some StuffIt methods need an external extractor; unsupported formats are rejected rather than guessed.\n\nDisk-image support depends on the format and available parser. Disposable virtual volumes do not mean every .img, .dmg or .iso is supported. Do not assume that an extracted installer can run: its architecture and resources must also be supported.\n\nThe Memory panel scratch disk is a quota-limited host directory, not physical RAM. It refuses to adopt an existing unmanaged RAM Disk folder. Clear on quit removes data only from a verified managed scratch disk.\n\nReinstalling or reseeding OS999 does not intentionally replace existing starter documents. Classic-app saves are kept separately from application folders.\n"},
    {"Documents/Help and Limitations",
     "Help and Limitations\n\nIf double-clicking does not open an item, select it and choose File > Open. Check the resulting message: an unsupported application or malformed resource fork is different from a mouse event problem.\n\nIf a note cannot save, the file may have changed outside OS999 or become unwritable. The failed edit does not replace your saved file. Reopen it to read the current contents. New notes are in Documents.\n\nThis is a userspace desktop and clean-room compatibility runtime, not a complete Mac OS 9 emulator or operating system. Not all classic applications, installers, extensions, printers or network services work.\n\nHeadless tests and successful builds do not establish visual, gameplay, audio, performance or physical-device compatibility. Report the build, file name, action and visible error when something fails.\n\nUse Command-W to close a window and Command-Q to quit. Avoid deleting the host volume or save directories to troubleshoot an application.\n"},
    {"Documents/Notes",
     "Notes live here as plain text.\n\n"
     "Double-click a document to read it.\n"},
    {"Read Me", OS999_CURRENT_README},
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
     "OS999 Stylized ships ChicagoFLF, which Robin\n"
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

int volume_builtin_terminal(const char *root, const char *rel) {
    return native_catalog_id(root, rel) == NATIVE_TERMINAL;
}
int volume_builtin_tailnet(const char *root, const char *rel) {
    return native_catalog_id(root, rel) == NATIVE_TAILSCALE;
}

static int seed_directory(const char *root, const char *relative) {
    int dir = volume_directory_open(root, "");
    char path[OSPI_PATH];
    if (dir < 0 || snprintf(path, sizeof path, "%s", relative) >= (int)sizeof path) {
        if (dir >= 0) close(dir);
        return -1;
    }
    char *part = path;
    while (*part) {
        char *slash = strchr(part, '/');
        if (slash) *slash = 0;
        if (mkdirat(dir, part, 0755) != 0 && errno != EEXIST) { close(dir); return -1; }
        int child = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(dir);
        if (child < 0) return -1;
        dir = child;
        if (!slash) break;
        part = slash + 1;
    }
    close(dir); return 0;
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
            if (seed_directory(root, parent_rel) != 0) return -1;
        }
        char full[OSPI_PATH];
        if (volume_join(full, sizeof full, root, k_seed[i].rel) != 0) {
            return -1;
        }
        if (exists_file(full)) {
            continue;
        }
        const char *leaf = strrchr(k_seed[i].rel, '/');
        int dir = volume_directory_open(root, leaf ? parent_rel : "");
        if (dir < 0) return -1;
        int fd = openat(dir, leaf ? leaf + 1 : k_seed[i].rel,
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        close(dir);
        if (fd < 0) { if (errno == EEXIST) continue; return -1; }
        FILE *f = fdopen(fd, "w");
        if (!f) { close(fd); return -1; }
        int ok = !k_seed[i].body || fputs(k_seed[i].body, f) >= 0;
        if (fclose(f) != 0 || !ok) return -1;
    }
    return native_catalog_seed(root);
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
    size_t n = strlen(name);
    if (name[0] == '.' || (n > 5 && strcasecmp(name + n - 5, ".rsrc") == 0)) {
        return 1;
    }
    if ((!root_rel || !root_rel[0]) && (strcmp(name, "Trash") == 0 || strcmp(name, "Desktop Folder") == 0)) {
        return 1;
    }
    return 0;
}

static int cmp_item(const void *a, const void *b) {
    return strcasecmp(((const Item *)a)->name, ((const Item *)b)->name);
}

static int list_items(const char *root, const char *rel, Item **buffer, int max, int grow, int *limited) {
    Item *items = *buffer;
    if (!items || max < 1) { errno = EINVAL; return -1; }
    int fd = volume_directory_open(root, rel ? rel : "");
    if (fd < 0) return -1;
    DIR *d = fdopendir(fd);
    if (!d) { int error = errno; close(fd); errno = error; return -1; }
    int n = 0;
    struct dirent *de;
    for (;;) {
        errno = 0; de = readdir(d);
        if (!de) {
            int error = errno;
            if (error) { closedir(d); errno = error; return -1; }
            break;
        }
        if (hidden(rel, de->d_name)) {
            continue;
        }
        if (strlen(de->d_name) >= OSPI_NAME) {
            if (limited) *limited = 1;
            continue;
        }
        if (n >= max) {
            if (!grow || max == OSPI_FOLDER_LIMIT) {
                if (limited) *limited = 1;
                break;
            }
            int next = max * 2;
            if (next > OSPI_FOLDER_LIMIT) next = OSPI_FOLDER_LIMIT;
            Item *larger = realloc(items, (size_t)next * sizeof *items);
            if (!larger) { closedir(d); return -1; }
            items = *buffer = larger; max = next;
        }
        struct stat st;
        if (fstatat(fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            continue;
        }
        memset(&items[n], 0, sizeof items[n]);
        items[n].data_size = S_ISREG(st.st_mode) && st.st_size > 0 ? (uint64_t)st.st_size : 0;
        items[n].modified = (int64_t)st.st_mtime;
#ifdef __APPLE__
        items[n].created = (int64_t)st.st_birthtime;
#endif
        memcpy(items[n].name, de->d_name, strlen(de->d_name) + 1);
        if (S_ISDIR(st.st_mode)) {
            items[n].kind = strcmp(de->d_name, "System Folder") == 0 ? ITEM_SYSTEM : ITEM_FOLDER;
            char child_rel[OSPI_PATH];
            int cn = rel && rel[0] ? snprintf(child_rel, sizeof child_rel, "%s/%s", rel, de->d_name)
                                   : snprintf(child_rel, sizeof child_rel, "%s", de->d_name);
            if (cn > 0 && (size_t)cn < sizeof child_rel)
                volume_folder_icon(root, child_rel, items[n].special, sizeof items[n].special);
        } else {
            char file_rel[OSPI_PATH];
            int fn = rel && rel[0] ? snprintf(file_rel, sizeof file_rel, "%s/%s", rel, de->d_name)
                                   : snprintf(file_rel, sizeof file_rel, "%s", de->d_name);
            if (fn > 0 && (size_t)fn < sizeof file_rel && native_catalog_placeholder(root, file_rel)) continue;
            int builtin = fn > 0 && (size_t)fn < sizeof file_rel && S_ISREG(st.st_mode) &&
                          native_catalog_id(root, file_rel) != NATIVE_NONE;
            items[n].kind = builtin || (S_ISREG(st.st_mode) && mac_icon_application_at(fd, de->d_name, &items[n])) ?
                            ITEM_APPLICATION : ITEM_FILE;
        }
        n++;
    }
    closedir(d);
    qsort(items, (size_t)n, sizeof items[0], cmp_item);
    return n;
}

int volume_list(const char *root, const char *rel, Item *items, int max) {
    return list_items(root, rel, &items, max, 0, NULL);
}
int volume_list_dynamic(const char *root, const char *rel, Item **items, int *limited) {
    if (!items || !limited) { errno = EINVAL; return -1; }
    *items = calloc(OSPI_MAX_ITEMS, sizeof **items); *limited = 0;
    if (!*items) return -1;
    int n = list_items(root, rel, items, OSPI_MAX_ITEMS, 1, limited);
    if (n < 0) { free(*items); *items = NULL; }
    return n;
}

int volume_read(const char *root, const char *rel, char *buf, size_t n) {
    char full[OSPI_PATH];
    if (!buf || n < 2 || volume_join(full, sizeof full, root, rel) != 0) {
        return -1;
    }
    if (volume_disk_cache_lookup(full, n, buf) == 0) return 0;
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
                     "OS999 Stylized cannot show this file as text.\n\n"
                     "Double-click a StuffIt, MacBinary,\n"
                     "or disk image to open it in\n"
                     "Systemless. Other files stay put.\n");
            volume_disk_cache_store(full, n, buf);
            return 0;
        }
    }
    volume_disk_cache_store(full, n, buf);
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

/* Rename one item in a folder, carrying its .rsrc resource-fork sidecar.
 * Returns -2 when another item already has the name (a case-only change of
 * the same item is fine), -1 for a name the Finder would refuse. */
int volume_rename(const char *root, const char *rel, const char *from, const char *to) {
    if (!from || !to || !from[0] || !to[0] || strchr(to, '/') || strchr(to, ':') ||
        strchr(from, '/') || hidden(rel, to) || hidden(rel, from)) {
        return -1;
    }
    char dir[OSPI_PATH], src[OSPI_PATH], dst[OSPI_PATH];
    if (volume_join(dir, sizeof dir, root, rel) != 0 ||
        snprintf(src, sizeof src, "%s/%s", dir, from) >= (int)sizeof src ||
        snprintf(dst, sizeof dst, "%s/%s", dir, to) >= (int)sizeof dst) {
        return -1;
    }
    struct stat a, b;
    if (lstat(src, &a) != 0) {
        return -1;
    }
    if (strcmp(from, to) == 0) {
        return 0;
    }
    if (lstat(dst, &b) == 0 && (a.st_dev != b.st_dev || a.st_ino != b.st_ino)) {
        return -2;
    }
    if (rename(src, dst) != 0) {
        return -1;
    }
    char rsrc_src[OSPI_PATH + 8], rsrc_dst[OSPI_PATH + 8];
    snprintf(rsrc_src, sizeof rsrc_src, "%s.rsrc", src);
    snprintf(rsrc_dst, sizeof rsrc_dst, "%s.rsrc", dst);
    if (lstat(rsrc_src, &a) == 0 && lstat(rsrc_dst, &b) != 0) {
        rename(rsrc_src, rsrc_dst);
    }
    return 0;
}

int volume_count(const char *root, const char *rel) {
    int fd = volume_directory_open(root, rel ? rel : "");
    if (fd < 0) return -1;
    DIR *d = fdopendir(fd);
    if (!d) { close(fd); return -1; }
    int count = 0;
    for (;;) {
        errno = 0; struct dirent *entry = readdir(d);
        if (!entry) { if (errno) count = -1; break; }
        if (hidden(rel, entry->d_name)) continue;
        if (count == INT32_MAX) { count = -1; break; }
        count++;
    }
    closedir(d); return count;
}
