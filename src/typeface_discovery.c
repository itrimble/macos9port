#include "typeface_discovery.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static int font_file(const char *name) {
    static const char *exts[] = {".ttf", ".otf", ".ttc", ".pfa", ".pfb", ".dfont", NULL};
    size_t n = strlen(name);
    for (int i = 0; exts[i]; i++) {
        size_t e = strlen(exts[i]);
        if (n > e && strcasecmp(name + n - e, exts[i]) == 0) return 1;
    }
    return 0;
}

static int name_has(const char *file, const char *token) {
    size_t n = strlen(file), m = strlen(token);
    if (!m || m > n) return 0;
    for (size_t i = 0; i + m <= n; i++) {
        size_t j = 0;
        while (j < m && tolower((unsigned char)file[i + j]) ==
                        tolower((unsigned char)token[j])) j++;
        if (j == m) return 1;
    }
    return 0;
}

static int scan_dir(const char *directory, const char *token, char *out, size_t size,
                    TypefacePathFilter filter, void *context) {
    if (!directory || !directory[0]) return 0;
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !font_file(entry->d_name) ||
            (token && !name_has(entry->d_name, token)) || strchr(entry->d_name, '/')) continue;
        if (snprintf(out, size, "%s/%s", directory, entry->d_name) >= (int)size) continue;
        if (filter && !filter(out, context)) continue;
        closedir(dir);
        return 1;
    }
    closedir(dir);
    return 0;
}

int typeface_find_named(const char *token, char *out, size_t size,
                        const char **directories, int directory_count,
                        TypefacePathFilter filter, void *context) {
    for (int i = 0; i < directory_count; i++)
        if (scan_dir(directories[i], token, out, size, filter, context)) return 1;
    return 0;
}

void typeface_executable_fonts(char *out, size_t size) {
    if (!out || !size) return;
    out[0] = 0;
    char executable[PATH_MAX];
#if defined(__APPLE__)
    uint32_t n = sizeof executable;
    if (_NSGetExecutablePath(executable, &n) != 0) return;
#else
    ssize_t n = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (n < 0) return;
    executable[n] = 0;
#endif
    char real[PATH_MAX];
    if (!realpath(executable, real)) return;
    char *slash = strrchr(real, '/');
    if (!slash) return;
    *slash = 0;
    char relative[PATH_MAX];
    if (snprintf(relative, sizeof relative, "%s/../Resources/Fonts", real) >= (int)sizeof relative) return;
    if (!realpath(relative, out)) out[0] = 0;
}
