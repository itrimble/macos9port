#include "ext_registry.h"
#include "ext_fixture.h"

#include <stdlib.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static char root[64], dir[256];

static void put(const char *name, const char *extinfo) {
    char path[300];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    CHECK(fx_write(path, FX_OK, sizeof FX_OK) == 0);
    if (extinfo) {
        snprintf(path, sizeof path, "%s/%s.extinfo", dir, name);
        CHECK(fx_write_text(path, extinfo) == 0);
    }
}

static int position(const ExtList *l, const char *name) {
    for (int i = 0; i < l->load_count; i++)
        if (!strcmp(l->items[l->load[i]].name, name)) return i;
    return -1;
}

int main(void) {
    snprintf(root, sizeof root, "/tmp/os999-extscan-XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    snprintf(dir, sizeof dir, "%s/System Folder/Extensions", root);
    static ExtList list;

    /* No folder at all is an empty set, not an error. */
    CHECK(ext_scan(&list, root) == 0);
    char cmd[400];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
    CHECK(system(cmd) == 0);
    CHECK(ext_scan(&list, root) == 0);

    put("Zebra", NULL);
    put("alpha", NULL);
    put("Mango", NULL);
    put("Needs Net", "requires OpenTpt\n");
    put("OpenTpt", "priority 10\n");
    put("Early", "priority -5\n");
    put("Loop A", "requires Loop B\n");
    put("Loop B", "requires Loop A\n");
    put("Orphan", "requires Ghost\n");
    CHECK(fx_write_text(strcat(strcpy(cmd, dir), "/.hidden"), "x") == 0);
    snprintf(cmd, sizeof cmd, "%s/Sidecar.rsrc", dir);
    CHECK(fx_write(cmd, FX_OK, sizeof FX_OK) == 0);
    put("Sidecar", NULL);

    CHECK(ext_scan(&list, root) == 10); /* .hidden, .rsrc, .extinfo files are not extensions */
    CHECK(ext_find(&list, ".hidden") == NULL);
    CHECK(ext_find(&list, "Sidecar.rsrc") == NULL);
    ExtEntry *side = ext_find(&list, "Sidecar");
    CHECK(side && strstr(side->fork_path, "Sidecar.rsrc") != NULL);
    CHECK(ext_find(&list, "Mango") && !strstr(ext_find(&list, "Mango")->fork_path, ".rsrc"));

    ext_order(&list);
    /* Priority first, then name (case-insensitive); requirements always precede dependants. */
    CHECK(position(&list, "Early") == 0);
    CHECK(position(&list, "OpenTpt") == 1);
    CHECK(position(&list, "alpha") < position(&list, "Mango"));
    CHECK(position(&list, "Mango") < position(&list, "Zebra"));
    CHECK(position(&list, "OpenTpt") < position(&list, "Needs Net"));
    CHECK(position(&list, "Needs Net") >= 0);

    /* Diagnostics, never a crash. */
    CHECK(ext_find(&list, "Orphan")->status == EXT_MISSING_DEP);
    CHECK(strstr(ext_find(&list, "Orphan")->error, "Ghost") != NULL);
    CHECK(ext_find(&list, "Loop A")->status == EXT_CYCLE);
    CHECK(ext_find(&list, "Loop B")->status == EXT_CYCLE);
    CHECK(position(&list, "Loop A") == -1 && position(&list, "Orphan") == -1);
    CHECK(list.load_count == 7);

    /* Disabling a requirement takes its dependants down with it. */
    CHECK(ext_set_enabled(&list, "OpenTpt", 0) == 0);
    ext_order(&list);
    CHECK(ext_find(&list, "OpenTpt")->status == EXT_DISABLED);
    CHECK(ext_find(&list, "Needs Net")->status == EXT_MISSING_DEP);
    CHECK(position(&list, "Needs Net") == -1 && list.load_count == 5);
    CHECK(ext_set_enabled(&list, "bad/name", 0) == -1);

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
    if (failures) return 1;
    puts("test_ext_scan: ok");
    return 0;
}
