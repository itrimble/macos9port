#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "font_book_int.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int registry_path(const FontBookCatalog *catalog, char *out, size_t size) {
    int n = snprintf(out, size, "%s/System Folder/Preferences/Font Book 999 Registry", catalog->root);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

static int preferences_path(const FontBookCatalog *catalog, char *out, size_t size) {
    int n = snprintf(out, size, "%s/System Folder/Preferences", catalog->root);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

static int safe_key(const char *key) {
    size_t n = key ? strlen(key) : 0;
    if (!n || n >= OSPI_PATH || key[0] == '/' || strstr(key, "../") || strstr(key, "/..")) return 0;
    for (size_t i = 0; i < n; i++) if ((unsigned char)key[i] < 32 || key[i] == '\t') return 0;
    return 1;
}

int font_book_registry_disabled(const FontBookCatalog *catalog, const char *key) {
    if (!catalog || !key) return 0;
    for (int i = 0; i < catalog->disabled_count; i++)
        if (!strcmp(catalog->disabled[i], key)) return 1;
    return 0;
}

int font_book_registry_set(FontBookCatalog *catalog, const char *key, int disabled) {
    if (!catalog || !safe_key(key)) return -1;
    int found = -1;
    for (int i = 0; i < catalog->disabled_count; i++)
        if (!strcmp(catalog->disabled[i], key)) { found = i; break; }
    if (!disabled) {
        if (found >= 0) {
            catalog->disabled_count--;
            if (found < catalog->disabled_count)
                memmove(catalog->disabled[found], catalog->disabled[found + 1],
                        (size_t)(catalog->disabled_count - found) * sizeof catalog->disabled[0]);
        }
        return 0;
    }
    if (found >= 0) return 0;
    if (catalog->disabled_count >= FONT_BOOK_MAX_DISABLED) return -1;
    snprintf(catalog->disabled[catalog->disabled_count++], OSPI_PATH, "%s", key);
    return 0;
}

int font_book_registry_load(FontBookCatalog *catalog) {
    char path[OSPI_PATH + 80];
    catalog->disabled_count = 0;
    if (registry_path(catalog, path, sizeof path) != 0) return -1;
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || st.st_size > 256 * 1024) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char line[OSPI_PATH + 8];
    int ok = 0;
    while (fgets(line, sizeof line, file)) {
        char *end = strchr(line, '\n');
        if (!end) { ok = -1; break; }
        *end = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!safe_key(line) || font_book_registry_set(catalog, line, 1) != 0) { ok = -1; break; }
    }
    if (ferror(file)) ok = -1;
    if (fclose(file) != 0) ok = -1;
    return ok;
}

int font_book_registry_save(const FontBookCatalog *catalog,
                            char *error, size_t error_size) {
    char prefs[OSPI_PATH + 48], path[OSPI_PATH + 80], temp[OSPI_PATH + 96];
    if (!catalog || preferences_path(catalog, prefs, sizeof prefs) != 0 ||
        registry_path(catalog, path, sizeof path) != 0) return -1;
    if (volume_mkdir_p(prefs) != 0) goto fail;
    struct stat st;
    if (lstat(path, &st) == 0 && (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) goto fail;
    int n = snprintf(temp, sizeof temp, "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof temp) goto fail;
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) goto fail;
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); unlink(temp); goto fail; }
    int ok = fprintf(file, "# OS999 Font Book 999 disabled font registry v1\n") >= 0;
    for (int i = 0; ok && i < catalog->disabled_count; i++)
        ok = safe_key(catalog->disabled[i]) && fprintf(file, "%s\n", catalog->disabled[i]) >= 0;
    int failed = !ok || fflush(file) != 0;
    if (!failed && fsync(fd) != 0) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (!failed && rename(temp, path) != 0) failed = 1;
    if (failed) {
        unlink(temp); goto fail;
    }
    if (error && error_size) error[0] = 0;
    return 0;
fail:
    if (error && error_size) snprintf(error, error_size, "The activation registry could not be saved safely.");
    return -1;
}
