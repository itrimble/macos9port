#include "ext_registry.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

int ext_valid_name(const char *name) {
    if (!name || !name[0] || strlen(name) >= EXT_NAME || name[0] == '.') return 0;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\t' || *p == '\n' || *p == '\r' || (unsigned char)*p < 32) return 0;
    return 1;
}

static int has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n > m && strcasecmp(s + n - m, suffix) == 0;
}

static int is_regular(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int cmp_names(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

static void parse_info(ExtEntry *e, const char *root_dir) {
    char path[EXT_PATH];
    if (snprintf(path, sizeof path, "%s/%s.extinfo", root_dir, e->name) >= (int)sizeof path ||
        !is_regular(path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "priority ", 9)) {
            char *end;
            long v = strtol(line + 9, &end, 10);
            if (*end == 0 && v >= -1000 && v <= 1000) e->priority = (int)v;
        } else if (!strncmp(line, "requires ", 9) && e->dep_count < EXT_DEPS &&
                   ext_valid_name(line + 9)) {
            snprintf(e->deps[e->dep_count++], EXT_NAME, "%s", line + 9);
        }
    }
    fclose(f);
}

int ext_scan(ExtList *list, const char *root) {
    if (!list || !root) return -1;
    memset(list->items, 0, sizeof list->items);
    list->count = list->load_count = 0;
    char dir_path[EXT_PATH];
    if (snprintf(dir_path, sizeof dir_path, "%s/System Folder/Extensions", root) >= (int)sizeof dir_path)
        return -1;
    DIR *dir = opendir(dir_path);
    if (!dir) return 0; /* no folder is an empty set, not an error */
    static char names[EXT_MAX][EXT_NAME];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && n < EXT_MAX) {
        if (!ext_valid_name(de->d_name) || has_suffix(de->d_name, ".rsrc") ||
            has_suffix(de->d_name, ".extinfo")) continue;
        char full[EXT_PATH];
        if (snprintf(full, sizeof full, "%s/%s", dir_path, de->d_name) >= (int)sizeof full ||
            !is_regular(full)) continue;
        snprintf(names[n++], EXT_NAME, "%s", de->d_name);
    }
    closedir(dir);
    qsort(names, (size_t)n, EXT_NAME, cmp_names);
    for (int i = 0; i < n; i++) {
        ExtEntry *e = &list->items[list->count++];
        e->enabled = 1;
        e->priority = EXT_PRIORITY_DEFAULT;
        snprintf(e->name, sizeof e->name, "%s", names[i]);
        snprintf(e->path, sizeof e->path, "%s/%s", dir_path, names[i]);
        char side[EXT_PATH];
        snprintf(side, sizeof side, "%s/%s.rsrc", dir_path, names[i]);
        snprintf(e->fork_path, sizeof e->fork_path, "%s", is_regular(side) ? side : e->path);
        parse_info(e, dir_path);
    }
    return list->count;
}

ExtEntry *ext_find(ExtList *list, const char *name) {
    if (!list || !name) return NULL;
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i].name, name) == 0) return &list->items[i];
    return NULL;
}

const char *ext_status_label(ExtStatus status) {
    switch (status) {
    case EXT_LOADED: return "Loaded";
    case EXT_DISABLED: return "Off";
    case EXT_FAILED: return "Failed";
    case EXT_MISSING_DEP: return "Needs another extension";
    case EXT_CYCLE: return "Dependency loop";
    default: return "Pending";
    }
}

static int placed_before(const int *order, int count, int idx) {
    for (int i = 0; i < count; i++) if (order[i] == idx) return 1;
    return 0;
}

void ext_order(ExtList *list) {
    if (!list) return;
    list->load_count = 0;
    for (int i = 0; i < list->count; i++) {
        ExtEntry *e = &list->items[i];
        e->error[0] = 0;
        e->status = e->enabled ? EXT_PENDING : EXT_DISABLED;
    }
    /* Missing or disabled requirements propagate until nothing changes. */
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < list->count; i++) {
            ExtEntry *e = &list->items[i];
            if (e->status != EXT_PENDING) continue;
            for (int d = 0; d < e->dep_count; d++) {
                ExtEntry *dep = ext_find(list, e->deps[d]);
                if (!dep || dep->status == EXT_DISABLED || dep->status == EXT_MISSING_DEP) {
                    e->status = EXT_MISSING_DEP;
                    snprintf(e->error, sizeof e->error, "requires %s (%s)", e->deps[d],
                             dep ? "not available" : "not installed");
                    changed = 1;
                    break;
                }
            }
        }
    }
    /* Priority, then name order; an entry is released once its requirements are placed. */
    for (;;) {
        int best = -1;
        for (int i = 0; i < list->count; i++) {
            ExtEntry *e = &list->items[i];
            if (e->status != EXT_PENDING || placed_before(list->load, list->load_count, i)) continue;
            int ready = 1;
            for (int d = 0; d < e->dep_count && ready; d++) {
                ExtEntry *dep = ext_find(list, e->deps[d]);
                ready = dep && placed_before(list->load, list->load_count, (int)(dep - list->items));
            }
            if (!ready) continue;
            if (best < 0 || e->priority < list->items[best].priority) best = i; /* ties keep name order */
        }
        if (best < 0) break;
        list->load[list->load_count++] = best;
    }
    for (int i = 0; i < list->count; i++) {
        ExtEntry *e = &list->items[i];
        if (e->status == EXT_PENDING && !placed_before(list->load, list->load_count, i)) {
            e->status = EXT_CYCLE;
            snprintf(e->error, sizeof e->error, "circular requirement");
        }
    }
}
