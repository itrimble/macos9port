#include "mac_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Session edits to resources (SetResInfo names, WriteResource data), layered over the
 * immutable fork bytes. Entries are keyed by (owner, type, id) so they survive
 * ReleaseResource and reload. Everything lives in host memory, out of guest reach.
 *
 * Application-fork (owner 0) edits can be persisted into the writable save overlay as one
 * hidden file named for the original fork, so the application directory is never modified and
 * edits never apply to a different build of the program. A malformed edit file is discarded
 * whole: nothing is partially applied. */
#define MAGIC "OS9RE1\0\0"
#define FILE_LIMIT (1u << 20)
#define EDIT_LIMIT 512u
#define APP_OWNER 0

typedef struct {
    int16_t owner, id;
    uint32_t type, size;
    uint8_t has_name, has_data;
    uint8_t name[256]; /* Pascal string */
    uint8_t *data;
} MacResEdit;
struct MacResEdits { MacResEdit **items; size_t count, cap; };

static MacResEdit *find(const MacRuntime *rt, int16_t owner, uint32_t type, int16_t id) {
    const struct MacResEdits *t = rt->res_edits;
    if (t) for (size_t i = 0; i < t->count; i++) {
        MacResEdit *e = t->items[i];
        if (e->owner == owner && e->type == type && e->id == id) return e;
    }
    return NULL;
}

static MacResEdit *get(MacRuntime *rt, int16_t owner, uint32_t type, int16_t id) {
    MacResEdit *e = find(rt, owner, type, id);
    if (e) return e;
    struct MacResEdits *t = rt->res_edits;
    if (!t && !(t = rt->res_edits = calloc(1, sizeof *t))) return NULL;
    if (t->count >= EDIT_LIMIT) return NULL;
    if (t->count == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 16;
        MacResEdit **items = realloc(t->items, cap * sizeof *items);
        if (!items) return NULL;
        t->items = items; t->cap = cap;
    }
    if (!(e = calloc(1, sizeof *e))) return NULL;
    e->owner = owner; e->type = type; e->id = id;
    t->items[t->count++] = e;
    return e;
}

int mac_resedit_set_name(MacRuntime *rt, int16_t owner, uint32_t type, int16_t id, const uint8_t *pascal) {
    MacResEdit *e = get(rt, owner, type, id);
    if (!e) return -1;
    memcpy(e->name, pascal, (size_t)pascal[0] + 1);
    e->has_name = 1;
    return 0;
}

int mac_resedit_set_data(MacRuntime *rt, int16_t owner, uint32_t type, int16_t id, const uint8_t *data, uint32_t size) {
    if (size > FILE_LIMIT / 2) return -1;
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) return -1;
    if (size) memcpy(copy, data, size);
    MacResEdit *e = get(rt, owner, type, id);
    if (!e) { free(copy); return -1; }
    free(e->data);
    e->data = copy; e->size = size; e->has_data = 1;
    return 0;
}

void mac_resedit_apply(const MacRuntime *rt, int16_t owner, MacResource *r) {
    const MacResEdit *e = find(rt, owner, r->type, r->id);
    if (!e) return;
    if (e->has_name) { r->name = e->name + 1; r->name_size = e->name[0]; }
    if (e->has_data) { r->data = e->data; r->size = e->size; }
}

void mac_resedit_destroy(MacRuntime *rt) {
    struct MacResEdits *t = rt->res_edits;
    if (!t) return;
    for (size_t i = 0; i < t->count; i++) { free(t->items[i]->data); free(t->items[i]); }
    free(t->items); free(t);
    rt->res_edits = NULL;
}

/* --- persistence ------------------------------------------------------------ */

static uint64_t fingerprint(const MacResourceFork *f) { /* FNV-1a over the original fork bytes */
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < f->size; i++) h = (h ^ f->bytes[i]) * UINT64_C(1099511628211);
    return h ^ f->size;
}

static void file_name(const MacRuntime *rt, char *out, size_t n) {
    snprintf(out, n, ".resedits-%016llx", (unsigned long long)fingerprint(&rt->fork));
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int mac_resedit_save(MacRuntime *rt) {
    const struct MacResEdits *t = rt->res_edits;
    size_t need = 12;
    uint32_t count = 0;
    if (t) for (size_t i = 0; i < t->count; i++) {
        const MacResEdit *e = t->items[i];
        if (e->owner != APP_OWNER) continue; /* other open files have no stable identity across runs */
        need += 7 + (e->has_name ? (size_t)e->name[0] + 1 : 0) + (e->has_data ? 4 + (size_t)e->size : 0);
        count++;
    }
    if (!count) return 1;
    if (need > FILE_LIMIT) return -34; /* dskFulErr */
    uint8_t *buf = malloc(need);
    if (!buf) return -108;
    memcpy(buf, MAGIC, 8);
    put32(buf + 8, count);
    size_t at = 12;
    for (size_t i = 0; i < t->count; i++) {
        const MacResEdit *e = t->items[i];
        if (e->owner != APP_OWNER) continue;
        put32(buf + at, e->type); buf[at + 4] = (uint8_t)((uint16_t)e->id >> 8); buf[at + 5] = (uint8_t)e->id;
        buf[at + 6] = (uint8_t)(e->has_name | e->has_data << 1);
        at += 7;
        if (e->has_name) { memcpy(buf + at, e->name, (size_t)e->name[0] + 1); at += (size_t)e->name[0] + 1; }
        if (e->has_data) { put32(buf + at, e->size); at += 4; memcpy(buf + at, e->data, e->size); at += e->size; }
    }
    char name[40];
    file_name(rt, name, sizeof name);
    int rc = mac_files_overlay_put_hidden(rt, name, buf, at);
    free(buf);
    return rc;
}

int mac_resedit_load(MacRuntime *rt) {
    char name[40];
    file_name(rt, name, sizeof name);
    uint8_t *buf = NULL;
    size_t size = 0;
    if (mac_files_overlay_get_hidden(rt, name, FILE_LIMIT, &buf, &size)) return 0;
    int loaded = 0;
    if (size >= 12 && !memcmp(buf, MAGIC, 8)) {
        uint32_t count = get32(buf + 8);
        size_t at = 12;
        struct { uint32_t type; int16_t id; uint8_t flags, name[256]; const uint8_t *data; uint32_t size; } *parsed = NULL;
        int ok = count && count <= EDIT_LIMIT && (parsed = calloc(count, sizeof *parsed)) != NULL;
        for (uint32_t i = 0; ok && i < count; i++) {
            ok = size - at >= 7;
            if (!ok) break;
            parsed[i].type = get32(buf + at); parsed[i].id = (int16_t)((buf[at + 4] << 8) | buf[at + 5]);
            parsed[i].flags = buf[at + 6]; at += 7;
            ok = !(parsed[i].flags & ~3u);
            if (ok && (parsed[i].flags & 1)) {
                size_t len = size - at ? buf[at] : 0;
                ok = size - at >= 1 && size - at >= len + 1;
                if (ok) { memcpy(parsed[i].name, buf + at, len + 1); at += len + 1; }
            }
            if (ok && (parsed[i].flags & 2)) {
                ok = size - at >= 4;
                if (ok) {
                    parsed[i].size = get32(buf + at); at += 4;
                    ok = parsed[i].size <= size - at;
                    if (ok) { parsed[i].data = buf + at; at += parsed[i].size; }
                }
            }
        }
        if (ok && at == size) {
            for (uint32_t i = 0; i < count; i++) {
                if ((parsed[i].flags & 1) && mac_resedit_set_name(rt, APP_OWNER, parsed[i].type, parsed[i].id, parsed[i].name)) break;
                if ((parsed[i].flags & 2) && mac_resedit_set_data(rt, APP_OWNER, parsed[i].type, parsed[i].id, parsed[i].data, parsed[i].size)) break;
                loaded++;
            }
        }
        free(parsed);
    }
    free(buf);
    return loaded;
}
