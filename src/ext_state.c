#include "ext_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEADER "OS999-EXT-1"

static int disabled_index(const ExtList *list, const char *name) {
    for (int i = 0; i < list->disabled_count; i++)
        if (strcmp(list->disabled[i], name) == 0) return i;
    return -1;
}

int ext_set_enabled(ExtList *list, const char *name, int enabled) {
    if (!list || !ext_valid_name(name)) return -1;
    int at = disabled_index(list, name);
    if (enabled && at >= 0) {
        memmove(list->disabled[at], list->disabled[at + 1],
                (size_t)(list->disabled_count - at - 1) * EXT_NAME);
        list->disabled_count--;
    } else if (!enabled && at < 0) {
        if (list->disabled_count >= EXT_MAX) return -1;
        snprintf(list->disabled[list->disabled_count++], EXT_NAME, "%s", name);
    }
    ExtEntry *e = ext_find(list, name);
    if (e) e->enabled = enabled ? 1 : 0;
    return 0;
}

static ExtSet *find_set(ExtList *list, const char *name) {
    for (int i = 0; i < list->set_count; i++)
        if (strcmp(list->sets[i].name, name) == 0) return &list->sets[i];
    return NULL;
}

int ext_set_save(ExtList *list, const char *set_name) {
    if (!list || !ext_valid_name(set_name)) return -1;
    ExtSet *s = find_set(list, set_name);
    if (!s) {
        if (list->set_count >= EXT_SETS) return -1;
        s = &list->sets[list->set_count++];
        snprintf(s->name, sizeof s->name, "%s", set_name);
    }
    s->count = 0;
    for (int i = 0; i < list->count; i++)
        if (list->items[i].enabled)
            snprintf(s->enabled[s->count++], EXT_NAME, "%s", list->items[i].name);
    return 0;
}

int ext_set_apply(ExtList *list, const char *set_name) {
    ExtSet *s = list && set_name ? find_set(list, set_name) : NULL;
    if (!s) return -1;
    for (int i = 0; i < list->count; i++) {
        int on = 0;
        for (int k = 0; k < s->count && !on; k++) on = strcmp(s->enabled[k], list->items[i].name) == 0;
        ext_set_enabled(list, list->items[i].name, on);
    }
    return 0;
}

static int state_path(char *out, size_t n, const char *root) {
    return snprintf(out, n, "%s/.os999-extensions", root) < (int)n ? 0 : -1;
}

int ext_state_load(ExtList *list, const char *root) {
    if (!list || !root) return -1;
    list->disabled_count = list->set_count = 0;
    list->crash_pending = 0;
    char path[EXT_PATH];
    if (state_path(path, sizeof path, root)) return -1;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[EXT_MAX * (EXT_NAME + 1) + EXT_NAME + 8];
        if (fgets(line, sizeof line, f) && strncmp(line, HEADER, strlen(HEADER)) == 0) {
            while (fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (!strcmp(line, "pending 1")) list->crash_pending = 1;
                else if (!strncmp(line, "D ", 2)) ext_set_enabled(list, line + 2, 0);
                else if (!strncmp(line, "S ", 2) && list->set_count < EXT_SETS) {
                    char *name = line + 2, *rest = strchr(name, '\t');
                    if (rest) *rest++ = 0;
                    if (!ext_valid_name(name)) continue;
                    ExtSet *s = &list->sets[list->set_count++];
                    memset(s, 0, sizeof *s);
                    snprintf(s->name, sizeof s->name, "%s", name);
                    while (rest && *rest && s->count < EXT_MAX) {
                        char *next = strchr(rest, '\t');
                        if (next) *next++ = 0;
                        if (ext_valid_name(rest)) snprintf(s->enabled[s->count++], EXT_NAME, "%s", rest);
                        rest = next;
                    }
                }
            }
        } /* unknown header: ignore and keep defaults */
        fclose(f);
    }
    for (int i = 0; i < list->count; i++)
        list->items[i].enabled = disabled_index(list, list->items[i].name) < 0;
    return 0;
}

static int write_state(const ExtList *list, const char *root, int pending) {
    char path[EXT_PATH], tmp[EXT_PATH + 16];
    if (!list || !root || state_path(path, sizeof path, root)) return -1;
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", path, (int)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, HEADER "\npending %d\n", pending);
    for (int i = 0; i < list->disabled_count; i++) fprintf(f, "D %s\n", list->disabled[i]);
    for (int i = 0; i < list->set_count; i++) {
        fprintf(f, "S %s", list->sets[i].name);
        for (int k = 0; k < list->sets[i].count; k++) fprintf(f, "\t%s", list->sets[i].enabled[k]);
        fputc('\n', f);
    }
    if (fclose(f) != 0 || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int ext_state_save(const ExtList *list, const char *root) { return write_state(list, root, 0); }
int ext_state_begin_boot(const ExtList *list, const char *root) { return write_state(list, root, 1); }
int ext_state_end_boot(const ExtList *list, const char *root) { return write_state(list, root, 0); }

ExtBootMode ext_boot_decide(int space_held, int shift_held, int env_manager, int crash_pending) {
    if (space_held || env_manager || crash_pending) return EXT_BOOT_MANAGER;
    if (shift_held) return EXT_BOOT_NONE;
    return EXT_BOOT_NORMAL;
}

void ext_boot_apply(ExtList *list, ExtBootMode mode) {
    if (!list) return;
    ext_order(list);
    if (mode != EXT_BOOT_NONE) return;
    for (int i = 0; i < list->count; i++) list->items[i].status = EXT_DISABLED;
    list->load_count = 0;
}
