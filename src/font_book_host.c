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

/* Host fonts: the fonts this Mac already has, listed read-only and in place.
   Nothing here opens a host file for writing or copies one anywhere. */

typedef struct {
    int files;
    long long bytes;
    int limit;
} HostBudget;

static const char k_magic[] = "OS999-FONTBOOK-HOST-1";

static int settings_path(const FontBookCatalog *catalog, char *out, size_t size) {
    int n = snprintf(out, size, "%s/System Folder/Preferences/Font Book 999 Host Fonts", catalog->root);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

int font_book_host_set_roots(FontBookCatalog *catalog, const char *const *roots, int count) {
    if (!catalog || count < 0 || (count && !roots)) return -1;
    catalog->host_root_count = 0;
    for (int i = 0; i < count && catalog->host_root_count < FONT_BOOK_HOST_ROOTS; i++) {
        if (!roots[i] || !roots[i][0] || strlen(roots[i]) >= OSPI_PATH) continue;
        snprintf(catalog->host_roots[catalog->host_root_count++], OSPI_PATH, "%s", roots[i]);
    }
    return catalog->host_root_count;
}

int font_book_host_default_roots(FontBookCatalog *catalog) {
    char home[OSPI_PATH] = {0};
    const char *h = getenv("HOME");
    if (h && h[0] == '/' && snprintf(home, sizeof home, "%s/Library/Fonts", h) >= (int)sizeof home) home[0] = 0;
    const char *roots[3] = {"/System/Library/Fonts", "/Library/Fonts", home[0] ? home : NULL};
    return font_book_host_set_roots(catalog, roots, 3);
}

int font_book_host_load(FontBookCatalog *catalog) {
    char path[OSPI_PATH + 80], line[64];
    if (!catalog) return -1;
    catalog->host_enabled = 0;
    if (settings_path(catalog, path, sizeof path) != 0) return -1;
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 256) return 0;
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    int on = 0, ok = fgets(line, sizeof line, file) && !strncmp(line, k_magic, sizeof k_magic - 1) &&
             fgets(line, sizeof line, file) && (line[0] == '0' || line[0] == '1');
    if (ok) on = line[0] == '1';
    fclose(file);
    catalog->host_enabled = ok ? on : 0;
    return 0;
}

static int host_save(const FontBookCatalog *catalog, int enabled) {
    char prefs[OSPI_PATH + 48], path[OSPI_PATH + 80], temp[OSPI_PATH + 96];
    if (settings_path(catalog, path, sizeof path) != 0) return -1;
    snprintf(prefs, sizeof prefs, "%s/System Folder/Preferences", catalog->root);
    if (volume_mkdir_p(prefs) != 0) return -1;
    struct stat st;
    if (lstat(path, &st) == 0 && (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) return -1;
    int n = snprintf(temp, sizeof temp, "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof temp) return -1;
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); unlink(temp); return -1; }
    int failed = fprintf(file, "%s\n%d\n", k_magic, enabled ? 1 : 0) < 0 || fflush(file) != 0 || fsync(fd) != 0;
    if (fclose(file) != 0) failed = 1;
    if (!failed && rename(temp, path) != 0) failed = 1;
    if (failed) { unlink(temp); return -1; }
    return 0;
}

int font_book_host_set_enabled(FontBookCatalog *catalog, int enabled,
                               char *error, size_t error_size) {
    if (!catalog) return -1;
    enabled = !!enabled;
    if (catalog->host_enabled == enabled) return 0;
    if (host_save(catalog, enabled) != 0) {
        if (error && error_size) snprintf(error, error_size, "The host fonts setting could not be saved.");
        return -1;
    }
    catalog->host_enabled = enabled;
    return font_book_catalog_scan(catalog) < 0 ? -1 : 0;
}

static int host_extension(const char *name) {
    static const char *exts[] = {".ttf", ".otf", ".ttc", ".otc", ".dfont", ".pfb", NULL};
    size_t n = strlen(name);
    for (int i = 0; exts[i]; i++) {
        size_t e = strlen(exts[i]);
        if (n > e && !strcasecmp(name + n - e, exts[i])) return 1;
    }
    return 0;
}

static int compare_names(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void add_host_file(FontBookCatalog *catalog, const char *base, const char *path) {
    FontBookEntry entry;
    char error[FONT_BOOK_ERROR] = {0};
    if (font_book_validate(path, &entry, error, sizeof error) != 0 || !entry.valid) return;
    size_t base_len = strlen(base);
    const char *relative = !strncmp(path, base, base_len) && path[base_len] == '/' ? path + base_len + 1 : path;
    char key[OSPI_PATH];
    snprintf(key, sizeof key, "host:%s", relative);
    int faces = entry.format != FONT_BOOK_FORMAT_RESOURCE ? entry.face_count : 1;
    if (faces < 1) faces = 1;
    for (int face = 0; face < faces && catalog->count < catalog->scan_limit; face++) {
        FontBookEntry item = entry;
        if (face && font_book_validate_face(path, face, &item) != 0) continue;
        snprintf(item.key, sizeof item.key, "%s", key);
        item.source = FONT_BOOK_SOURCE_HOST;
        item.active = 0;   /* display only: never fed to the desktop's text system */
        catalog->fonts[catalog->count++] = item;
    }
}

static void walk(FontBookCatalog *catalog, HostBudget *budget, const char *base,
                 const char *path, int depth) {
    if (depth > FONT_BOOK_HOST_DEPTH) return;
    DIR *dir = opendir(path);
    if (!dir) return;                       /* unreadable: skip quietly */
    char *names[512];
    int count = 0;
    struct dirent *de;
    while (count < 512 && (de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!(names[count] = strdup(de->d_name))) break;
        count++;
    }
    closedir(dir);
    qsort(names, (size_t)count, sizeof names[0], compare_names);
    for (int i = 0; i < count; i++) {
        char child[OSPI_PATH];
        struct stat st;
        int n = snprintf(child, sizeof child, "%s/%s", path, names[i]);
        if (n > 0 && (size_t)n < sizeof child && catalog->count < budget->limit &&
            budget->files < FONT_BOOK_HOST_FILES && lstat(child, &st) == 0 && !S_ISLNK(st.st_mode)) {
            if (S_ISDIR(st.st_mode)) walk(catalog, budget, base, child, depth + 1);
            else if (S_ISREG(st.st_mode) && host_extension(names[i])) {
                budget->files++;
                if (st.st_size >= 16 && st.st_size <= FONT_BOOK_HOST_FILE_BYTES &&
                    budget->bytes + st.st_size <= FONT_BOOK_HOST_TOTAL_BYTES &&
                    access(child, R_OK) == 0) {
                    budget->bytes += st.st_size;
                    add_host_file(catalog, base, child);
                }
            }
        }
        free(names[i]);
    }
}

int font_book_host_scan(FontBookCatalog *catalog) {
    if (!catalog || !catalog->host_enabled) return 0;
    HostBudget budget = {0, 0, catalog->count + FONT_BOOK_HOST_LIMIT};
    if (budget.limit > FONT_BOOK_MAX_FONTS) budget.limit = FONT_BOOK_MAX_FONTS;
    catalog->scan_limit = budget.limit;
    int start = catalog->count;
    for (int i = 0; i < catalog->host_root_count; i++) {
        struct stat st;   /* a root must itself be a real directory, not a link */
        if (lstat(catalog->host_roots[i], &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        walk(catalog, &budget, catalog->host_roots[i], catalog->host_roots[i], 0);
    }
    return catalog->count - start;
}
