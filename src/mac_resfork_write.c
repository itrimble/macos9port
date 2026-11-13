#include "mac_resfork_write.h"

#include <stdlib.h>
#include <string.h>

/* Written from the public description of the resource-file layout, in our own words:
 * offsets in the header are from the start of the file, offsets inside the map are from
 * the start of the map, reference-list offsets are from the start of the type list, name
 * offsets are from the start of the name list, and a resource's data offset is 24 bits from
 * the start of the data area. A count is stored as one less than the number it counts. */
enum { DATA_START = 256, MAP_HEAD = 28, REF_BYTES = 12, TYPE_BYTES = 8 };

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xffff); }

void mac_wfork_init(MacWFork *w) { memset(w, 0, sizeof *w); }

void mac_wfork_free(MacWFork *w) {
    for (size_t i = 0; i < w->count; i++) { free(w->res[i].name); free(w->res[i].data); }
    free(w->res);
    memset(w, 0, sizeof *w);
}

static uint8_t *dup(const uint8_t *p, size_t n) {
    uint8_t *q = malloc(n ? n : 1);
    if (q && n) memcpy(q, p, n);
    return q;
}

static int grow(MacWFork *w) {
    if (w->count < w->cap) return 0;
    size_t cap = w->cap ? w->cap * 2 : 16;
    MacWRes *r = realloc(w->res, cap * sizeof *r);
    if (!r) return -108;
    w->res = r; w->cap = cap;
    return 0;
}

int mac_wfork_load(MacWFork *w, const MacResourceFork *f) {
    mac_wfork_init(w);
    if (!f || !f->bytes) return -199;
    size_t n = mac_resource_count(f);
    if (n > MAC_WFORK_RESOURCES) return -199;
    w->map_attrs = (uint16_t)((f->bytes[f->map_offset + 22] << 8) | f->bytes[f->map_offset + 23]);
    size_t keep = f->data_offset > 16 ? f->data_offset - 16 : 0;
    if (keep > sizeof w->reserved) keep = sizeof w->reserved;
    if (keep > f->size - 16) keep = f->size - 16;
    memcpy(w->reserved, f->bytes + 16, keep);
    for (size_t i = 0; i < n; i++) {
        MacResource r;
        if (mac_resource_at(f, i, &r) != 0) { mac_wfork_free(w); return -199; }
        uint8_t pascal[256];
        pascal[0] = (uint8_t)r.name_size;
        if (r.name) memcpy(pascal + 1, r.name, r.name_size);
        if (mac_wfork_add(w, r.type, r.id, r.attributes, r.name ? pascal : NULL, r.data, (uint32_t)r.size)) {
            mac_wfork_free(w); return -108;
        }
    }
    return 0;
}

long mac_wfork_find(const MacWFork *w, uint32_t type, int16_t id) {
    for (size_t i = 0; i < w->count; i++)
        if (w->res[i].type == type && w->res[i].id == id) return (long)i;
    return -1;
}

int mac_wfork_add(MacWFork *w, uint32_t type, int16_t id, uint8_t attrs, const uint8_t *name, const uint8_t *data, uint32_t size) {
    if (mac_wfork_find(w, type, id) >= 0 || w->count >= MAC_WFORK_RESOURCES) return -194;
    if (grow(w)) return -108;
    MacWRes r = {.type = type, .id = id, .attrs = attrs, .size = size};
    r.data = dup(data, size);
    r.name = name ? dup(name, (size_t)name[0] + 1) : NULL;
    if (!r.data || (name && !r.name)) { free(r.data); free(r.name); return -108; }
    w->res[w->count++] = r;
    return 0;
}

int mac_wfork_set_data(MacWFork *w, size_t index, const uint8_t *data, uint32_t size) {
    uint8_t *copy = dup(data, size);
    if (!copy) return -108;
    free(w->res[index].data);
    w->res[index].data = copy; w->res[index].size = size;
    return 0;
}

int mac_wfork_set_name(MacWFork *w, size_t index, const uint8_t *name) {
    uint8_t *copy = name ? dup(name, (size_t)name[0] + 1) : NULL;
    if (name && !copy) return -108;
    free(w->res[index].name);
    w->res[index].name = copy;
    return 0;
}

void mac_wfork_remove(MacWFork *w, size_t index) {
    free(w->res[index].name); free(w->res[index].data);
    memmove(w->res + index, w->res + index + 1, (w->count - index - 1) * sizeof *w->res);
    w->count--;
}

int mac_wfork_build(const MacWFork *w, uint8_t **out, size_t *out_size) {
    /* types in order of first appearance */
    uint32_t *types = malloc((w->count + 1) * sizeof *types);
    size_t *per = calloc(w->count + 1, sizeof *per), nt = 0;
    if (!types || !per) { free(types); free(per); return -108; }
    uint64_t data_len = 0, names_len = 0;
    for (size_t i = 0; i < w->count; i++) {
        size_t t = 0;
        while (t < nt && types[t] != w->res[i].type) t++;
        if (t == nt) types[nt++] = w->res[i].type;
        per[t]++;
        data_len += 4 + (uint64_t)w->res[i].size;
        if (w->res[i].name) names_len += (uint64_t)w->res[i].name[0] + 1;
    }
    uint64_t list_len = 2 + (uint64_t)nt * TYPE_BYTES + (uint64_t)w->count * REF_BYTES;
    uint64_t map_len = MAP_HEAD + list_len + names_len, total = DATA_START + data_len + map_len;
    int error = 0;
    if (data_len > 0xffffff || MAP_HEAD + list_len > 0xffff || names_len > 0xfffe || total > MAC_WFORK_BYTES) error = -199;
    uint8_t *b = error ? NULL : calloc(1, (size_t)total);
    if (!error && !b) error = -108;
    if (error) { free(types); free(per); free(b); return error; }
    size_t map = DATA_START + (size_t)data_len, list = map + MAP_HEAD, names = list + (size_t)list_len;
    put32(b, DATA_START); put32(b + 4, (uint32_t)map); put32(b + 8, (uint32_t)data_len); put32(b + 12, (uint32_t)map_len);
    memcpy(b + 16, w->reserved, sizeof w->reserved);
    memcpy(b + map, b, 16);
    put16(b + map + 22, w->map_attrs);
    put16(b + map + 24, MAP_HEAD); put16(b + map + 26, (unsigned)(MAP_HEAD + list_len));
    put16(b + list, nt ? (unsigned)(nt - 1) : 0xffff);
    size_t refs = 2 + nt * TYPE_BYTES, data_at = 0, name_at = 0;
    for (size_t t = 0; t < nt; t++) {
        uint8_t *te = b + list + 2 + t * TYPE_BYTES;
        put32(te, types[t]); put16(te + 4, (unsigned)(per[t] - 1)); put16(te + 6, (unsigned)refs);
        uint8_t *ref = b + list + refs;
        for (size_t i = 0; i < w->count; i++) {
            const MacWRes *r = &w->res[i];
            if (r->type != types[t]) continue;
            put16(ref, (uint16_t)r->id);
            if (r->name) {
                put16(ref + 2, (unsigned)name_at);
                memcpy(b + names + name_at, r->name, (size_t)r->name[0] + 1);
                name_at += (size_t)r->name[0] + 1;
            } else put16(ref + 2, 0xffff);
            ref[4] = (uint8_t)(r->attrs & ~2u);   /* the changed bit is not kept on disk */
            ref[5] = (uint8_t)(data_at >> 16); ref[6] = (uint8_t)(data_at >> 8); ref[7] = (uint8_t)data_at;
            put32(b + DATA_START + data_at, r->size);
            if (r->size) memcpy(b + DATA_START + data_at + 4, r->data, r->size);
            data_at += 4 + (size_t)r->size;
            ref += REF_BYTES;
        }
        refs += per[t] * REF_BYTES;
    }
    free(types); free(per);
    *out = b; *out_size = (size_t)total;
    return 0;
}
