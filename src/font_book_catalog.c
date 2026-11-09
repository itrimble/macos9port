#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "font_book_int.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int known_extension(const char *name) {
    static const char *exts[] = {".ttf", ".otf", ".ttc", ".dfont", ".pfa", ".pfb", ".rsrc", ".suit", ".suitcase", NULL};
    size_t n = strlen(name);
    for (int i = 0; exts[i]; i++) {
        size_t e = strlen(exts[i]);
        if (n > e && !strcasecmp(name + n - e, exts[i])) return 1;
    }
    return 0;
}

static int compare_entries(const void *a, const void *b) {
    const FontBookEntry *fa = a, *fb = b;
    int result = strcasecmp(fa->family, fb->family);
    return result ? result : strcasecmp(fa->style, fb->style);
}

static int add_file(FontBookCatalog *catalog, const char *base, const char *path,
                    FontBookSource source, int include_unknown) {
    if (catalog->count >= FONT_BOOK_MAX_FONTS) return -1;
    const char *leaf = strrchr(path, '/');
    if (!known_extension(leaf ? leaf + 1 : path) && !include_unknown) return 0;
    FontBookEntry entry;
    char error[FONT_BOOK_ERROR] = {0};
    int valid = font_book_validate(path, &entry, error, sizeof error) == 0;
    if (!valid && !known_extension(leaf ? leaf + 1 : path)) return 0;
    entry.source = source;
    const char *relative = path;
    size_t base_len = strlen(base);
    if (!strncmp(path, base, base_len) && path[base_len] == '/') relative = path + base_len + 1;
    snprintf(entry.key, sizeof entry.key, "%s:%s",
             source == FONT_BOOK_SOURCE_SYSTEM ? "system" : "bundled",
             source == FONT_BOOK_SOURCE_SYSTEM ? relative : (leaf ? leaf + 1 : path));
    entry.active = valid && !font_book_registry_disabled(catalog, entry.key);
    if (!valid) snprintf(entry.error, sizeof entry.error, "%s", error[0] ? error : "Invalid font file.");
    int added = 0;
    int faces = entry.valid && entry.format != FONT_BOOK_FORMAT_RESOURCE ? entry.face_count : 1;
    if (faces < 1) faces = 1;
    for (int face = 0; face < faces && catalog->count < FONT_BOOK_MAX_FONTS; face++) {
        FontBookEntry item = entry;
        if (face && font_book_validate_face(path, face, &item) != 0) continue;
        item.source = source;
        snprintf(item.key, sizeof item.key, "%s", entry.key);
        item.active = entry.active;
        catalog->fonts[catalog->count++] = item;
        added++;
    }
    return added;
}

static int scan_directory(FontBookCatalog *catalog, const char *base, const char *path,
                          FontBookSource source, int depth) {
    if (!path || !path[0] || depth > 4) return 0;
    DIR *dir = opendir(path);
    if (!dir) return errno == ENOENT ? 0 : -1;
    int scanned = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char child[OSPI_PATH];
        int n = snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof child) continue;
        struct stat st;
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (scan_directory(catalog, base, child, source, depth + 1) < 0) scanned = -1;
        } else if (S_ISREG(st.st_mode)) {
            int result = add_file(catalog, base, child, source, depth == 0);
            if (result > 0) scanned += result;
        }
    }
    if (closedir(dir) != 0) return -1;
    return scanned;
}

int font_book_catalog_init(FontBookCatalog *catalog, const char *volume_root,
                           const char *bundled_fonts, const char *extra_fonts) {
    struct stat st;
    if (!catalog || !volume_root || strlen(volume_root) >= sizeof catalog->root ||
        stat(volume_root, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    memset(catalog, 0, sizeof *catalog);
    snprintf(catalog->root, sizeof catalog->root, "%s", volume_root);
    if (bundled_fonts) snprintf(catalog->bundled, sizeof catalog->bundled, "%s", bundled_fonts);
    if (extra_fonts) snprintf(catalog->extra, sizeof catalog->extra, "%s", extra_fonts);
    if (font_book_registry_load(catalog) != 0) catalog->disabled_count = 0;
    return font_book_catalog_scan(catalog);
}

int font_book_catalog_scan(FontBookCatalog *catalog) {
    if (!catalog) return -1;
    catalog->count = 0;
    char system_fonts[OSPI_PATH];
    int n = snprintf(system_fonts, sizeof system_fonts, "%s/System Folder/Fonts", catalog->root);
    if (n < 0 || (size_t)n >= sizeof system_fonts) return -1;
    int result = scan_directory(catalog, system_fonts, system_fonts, FONT_BOOK_SOURCE_SYSTEM, 0);
    if (result < 0) return -1;
    if (catalog->bundled[0] && scan_directory(catalog, catalog->bundled, catalog->bundled,
                                              FONT_BOOK_SOURCE_BUNDLED, 0) < 0) return -1;
    if (catalog->extra[0] && scan_directory(catalog, catalog->extra, catalog->extra,
                                            FONT_BOOK_SOURCE_BUNDLED, 0) < 0) return -1;
    qsort(catalog->fonts, (size_t)catalog->count, sizeof catalog->fonts[0], compare_entries);
    catalog->generation++;
    return catalog->count;
}

int font_book_path_active(const FontBookCatalog *catalog, const char *path) {
    if (!catalog || !path) return 1;
    for (int i = 0; i < catalog->count; i++)
        if (!strcmp(catalog->fonts[i].path, path)) return catalog->fonts[i].active;
    return 1;
}

int font_book_set_active(FontBookCatalog *catalog, int index, int active,
                         char *error, size_t error_size) {
    if (!catalog || index < 0 || index >= catalog->count || !catalog->fonts[index].valid) return -1;
    FontBookEntry *entry = &catalog->fonts[index];
    active = !!active;
    if (entry->active == active) return 0;
    if (!active && entry->previewable) {
        int remaining = 0;
        for (int i = 0; i < catalog->count; i++)
            remaining += strcmp(catalog->fonts[i].path, entry->path) != 0 &&
                         catalog->fonts[i].valid && catalog->fonts[i].active && catalog->fonts[i].previewable;
        if (!remaining) {
            if (error && error_size) snprintf(error, error_size, "At least one usable outline font must remain active.");
            return -1;
        }
    }
    if (font_book_registry_set(catalog, entry->key, !active) != 0) return -1;
    if (font_book_registry_save(catalog, error, error_size) != 0) {
        font_book_registry_set(catalog, entry->key, active);
        return -1;
    }
    for (int i = 0; i < catalog->count; i++)
        if (!strcmp(catalog->fonts[i].path, entry->path)) catalog->fonts[i].active = active;
    catalog->generation++;
    return 0;
}

static int safe_leaf(const char *path, const char **leaf_out) {
    const char *leaf = strrchr(path ? path : "", '/');
    leaf = leaf ? leaf + 1 : path;
    if (!leaf || !leaf[0] || !strcmp(leaf, ".") || !strcmp(leaf, "..") || strchr(leaf, ':')) return 0;
    *leaf_out = leaf;
    return 1;
}

int font_book_install(FontBookCatalog *catalog, const char *source,
                      char *installed_path, size_t installed_size,
                      char *error, size_t error_size) {
    FontBookEntry checked;
    const char *leaf;
    if (!catalog || !safe_leaf(source, &leaf) ||
        font_book_validate(source, &checked, error, error_size) != 0) return -1;
    char directory[OSPI_PATH], destination[OSPI_PATH];
    int n = snprintf(directory, sizeof directory, "%s/System Folder/Fonts", catalog->root);
    int d = snprintf(destination, sizeof destination, "%s/%s", directory, leaf);
    if (n < 0 || d < 0 || (size_t)n >= sizeof directory || (size_t)d >= sizeof destination ||
        volume_mkdir_p(directory) != 0) return -1;
    struct stat st;
    if (lstat(destination, &st) == 0) {
        if (error && error_size) snprintf(error, error_size, "A font with that file name is already installed.");
        return -1;
    }
    if (font_book_copy_validated(source, destination, error, error_size) != 0) return -1;
    if (font_book_catalog_scan(catalog) < 0) { unlink(destination); return -1; }
    if (installed_path && installed_size) snprintf(installed_path, installed_size, "%s", destination);
    return 0;
}

int font_book_copy_validated(const char *source, const char *destination,
                             char *error, size_t error_size) {
    char temp[OSPI_PATH + 48];
    int n = snprintf(temp, sizeof temp, "%s.install.%ld", destination, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof temp) return -1;
    int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int out = in >= 0 ? open(temp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644) : -1;
    if (in < 0 || out < 0) goto fail;
    char buffer[16384];
    size_t total = 0;
    for (;;) {
        ssize_t got = read(in, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) goto fail;
        if (!got) break;
        total += (size_t)got;
        if (total > 64u * 1024u * 1024u) goto fail;
        size_t used = 0;
        while (used < (size_t)got) {
            ssize_t put = write(out, buffer + used, (size_t)got - used);
            if (put < 0 && errno == EINTR) continue;
            if (put <= 0) goto fail;
            used += (size_t)put;
        }
    }
    int close_error = fsync(out) != 0;
    if (close(in) != 0) close_error = 1;
    in = -1;
    if (close(out) != 0) close_error = 1;
    out = -1;
    if (close_error) goto fail;
    if (rename(temp, destination) != 0) goto fail;
    return 0;
fail:
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    unlink(temp);
    if (error && error_size) snprintf(error, error_size, "The validated font could not be copied atomically.");
    return -1;
}
