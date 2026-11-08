#include "ospi.h"
#include "volume_directory.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>

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

/* Special folders keep their identity through renames, as Mac OS 9's did.
 * A hidden ".os999-folder" file inside a folder records the special icon it
 * carries (written when such a folder is first renamed), and FindFolder-style
 * lookups follow that mark before falling back to the usual name. */

#define FOLDER_MARK ".os999-folder"

/* Icon names are lowercase asset ids ending in "_folder", nothing else. */
static int valid_icon(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n < 8 || n >= OSPI_SPECIAL || strcmp(s + n - 7, "_folder") != 0) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return 0;
    return 1;
}

static int mark_path(char *out, size_t n, const char *root, const char *rel) {
    char dir[OSPI_PATH];
    if (volume_join(dir, sizeof dir, root, rel) != 0) return -1;
    return snprintf(out, n, "%s/%s", dir, FOLDER_MARK) < (int)n ? 0 : -1;
}

int volume_folder_icon(const char *root, const char *rel, char *out, size_t n) {
    char path[OSPI_PATH + 16], buf[OSPI_SPECIAL + 8] = {0};
    if (!out || n == 0 || mark_path(path, sizeof path, root, rel) != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = 0;
    buf[strcspn(buf, " \t\r\n")] = 0;
    if (!valid_icon(buf) || strlen(buf) >= n) return 0;
    memcpy(out, buf, strlen(buf) + 1);
    return 1;
}

int volume_set_folder_icon(const char *root, const char *rel, const char *icon) {
    char path[OSPI_PATH + 16];
    if (!valid_icon(icon) || mark_path(path, sizeof path, root, rel) != 0) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%s\n", icon) > 0;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

/* The top-level folder playing a role: the one marked with `icon`, else the
 * unmarked one with the usual name, else a new folder with that name. */
int volume_special_folder(const char *root, const char *icon, const char *name, char *rel, size_t n) {
    DIR *d = opendir(root);
    char named[OSPI_NAME] = {0};
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.' || strlen(de->d_name) >= OSPI_NAME) continue;
            char path[OSPI_PATH], mark[OSPI_SPECIAL];
            struct stat st;
            if (volume_join(path, sizeof path, root, de->d_name) != 0 || stat(path, &st) != 0 ||
                !S_ISDIR(st.st_mode)) continue;
            int marked = volume_folder_icon(root, de->d_name, mark, sizeof mark);
            if (marked && strcmp(mark, icon) == 0) {
                int ok = snprintf(rel, n, "%s", de->d_name) < (int)n;   /* before closedir frees it */
                closedir(d);
                return ok ? 0 : -1;
            }
            if (!marked && strcasecmp(de->d_name, name) == 0) snprintf(named, sizeof named, "%s", de->d_name);
        }
        closedir(d);
    }
    if (named[0]) return snprintf(rel, n, "%s", named) < (int)n ? 0 : -1;
    char path[OSPI_PATH];
    if (volume_join(path, sizeof path, root, name) != 0 || (mkdir(path, 0755) != 0 && errno != EEXIST))
        return -1;
    return snprintf(rel, n, "%s", name) < (int)n ? 0 : -1;
}

int volume_clear_folder_icon(const char *root, const char *rel) {
    int fd = volume_directory_open(root, rel);
    if (fd < 0) return -1;
    int ok = unlinkat(fd, ".os999-folder", 0) == 0 || errno == ENOENT;
    close(fd);
    return ok ? 0 : -1;
}
