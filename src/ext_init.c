#include "ext_init.h"
#include "mac_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EXT_FORK_MAX = 16 * 1024 * 1024 };

static void set_error(ExtEntry *e, const char *message) {
    e->status = EXT_FAILED;
    snprintf(e->error, sizeof e->error, "%s", message);
}

/* 0 ok (report filled), -1 unusable fork, -2 runtime halted. */
static int run_entry(MacRuntime *rt, ExtEntry *e, MacInitReport *rep) {
    memset(rep, 0, sizeof *rep);
    FILE *f = fopen(e->fork_path, "rb");
    if (!f) { snprintf(rep->error, sizeof rep->error, "cannot read extension file"); return -1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = size > 0 && size <= EXT_FORK_MAX ? malloc((size_t)size) : NULL;
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        snprintf(rep->error, sizeof rep->error, "extension file is empty or too large");
        return -1;
    }
    fclose(f);
    MacResourceFork fork;
    char err[96];
    int rc = -1;
    if (mac_resource_open_container(&fork, bytes, (size_t)size, err, sizeof err) != 0)
        snprintf(rep->error, sizeof rep->error, "no readable resource fork");
    else {
        rc = mac_init_run(rt, &fork, MAC_INIT_BUDGET, rep);
        if (rc == 0 && rep->count == 0) {
            snprintf(rep->error, sizeof rep->error, "no INIT resource");
            rc = -1;
        }
    }
    free(bytes); /* INIT code was copied into guest RAM */
    return rc;
}

int ext_init_load_all(ExtList *list) {
    if (!list) return -1;
    MacRuntime *rt = calloc(1, sizeof *rt);
    char err[160];
    if (!rt || mac_init_host_create(rt, err, sizeof err) != 0) {
        for (int k = 0; k < list->load_count; k++) set_error(&list->items[list->load[k]], "extension host unavailable");
        free(rt);
        return 0;
    }
    int loaded[EXT_MAX], loaded_count = 0;
    for (int k = 0; k < list->load_count; k++) {
        ExtEntry *e = &list->items[list->load[k]];
        MacInitReport rep;
        int rc = run_entry(rt, e, &rep);
        if (rc == 0) {
            e->status = EXT_LOADED;
            loaded[loaded_count++] = list->load[k];
            continue;
        }
        set_error(e, rep.error[0] ? rep.error : "failed");
        if (rc == -2) { /* host is dead: rebuild and replay what loaded */
            mac_runtime_destroy(rt);
            if (mac_init_host_create(rt, err, sizeof err) != 0) { free(rt); return loaded_count; }
            for (int i = 0; i < loaded_count; i++) run_entry(rt, &list->items[loaded[i]], &rep);
        }
    }
    mac_runtime_destroy(rt);
    free(rt);
    return loaded_count;
}

int ext_init_apply(ExtList *list, MacRuntime *rt, char *err, size_t n) {
    int applied = 0;
    for (int k = 0; list && rt && k < list->load_count; k++) {
        ExtEntry *e = &list->items[list->load[k]];
        if (e->status != EXT_LOADED) continue;
        MacInitReport rep;
        if (run_entry(rt, e, &rep) != 0) {
            if (err && n) snprintf(err, n, "%s: %s", e->name, rep.error);
            return -1;
        }
        applied++;
    }
    return applied;
}
