#include "mac_rescomp.h"

#include <stdlib.h>
#include <string.h>

/* A copy of a resource fork with the data of some resources replaced. Written from the public fork layout (header, data area of length-prefixed resources,
 * map with a type list, reference lists and a name list); nothing is taken from a real file. Every resource keeps its type, ID, attributes and name; the
 * order of types and of resources within a type is the order the source fork lists them. */
static void put16(uint8_t *p, unsigned x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, x >> 16); put16(p + 2, x & 0xffff); }

static const MacResPatch *patch_for(const MacResPatch *patches, size_t n, const MacResource *r) {
    for (size_t i = 0; i < n; i++) if (patches[i].type == r->type && patches[i].id == r->id) return &patches[i];
    return NULL;
}

int mac_rescomp_build_fork(const MacResourceFork *old, const MacResPatch *patches, size_t patch_count, uint8_t **out, size_t *out_size) {
    size_t count = mac_resource_count(old);
    if (!count || count > 0xffff) return -1;
    MacResource *res = calloc(count, sizeof *res);
    const uint8_t **data = calloc(count, sizeof *data);
    size_t *size = calloc(count, sizeof *size), *dofs = calloc(count, sizeof *dofs);
    uint32_t *types = calloc(count, sizeof *types);
    uint8_t *buf = NULL;
    int rc = -1;
    if (!res || !data || !size || !dofs || !types) goto done;
    size_t data_bytes = 0, names_bytes = 0, ntypes = 0;
    for (size_t i = 0; i < count; i++) {
        if (mac_resource_at(old, i, &res[i]) != 0) goto done;
        const MacResPatch *p = patch_for(patches, patch_count, &res[i]);
        data[i] = p ? p->bytes : res[i].data; size[i] = p ? p->size : res[i].size;
        if (size[i] > 0x7fffffffu) goto done;
        dofs[i] = data_bytes; data_bytes += 4 + size[i];
        if (res[i].name_size) names_bytes += 1 + res[i].name_size;
        size_t k = 0; while (k < ntypes && types[k] != res[i].type) k++;
        if (k == ntypes) types[ntypes++] = res[i].type;
    }
    if (data_bytes > 0xffffff || names_bytes > 0xffff) goto done;   /* a data offset is 24 bits and a name offset 16 */
    size_t typelist = 28, refs = typelist + 2 + 8 * ntypes, names = refs + 12 * count, map_len = names + names_bytes, total = 256 + data_bytes + map_len;
    buf = calloc(1, total);
    if (!buf) goto done;
    for (size_t i = 0; i < count; i++) {
        put32(buf + 256 + dofs[i], (uint32_t)size[i]);
        if (size[i]) memcpy(buf + 256 + dofs[i] + 4, data[i], size[i]);
    }
    uint8_t *map = buf + 256 + data_bytes;
    put32(buf, 256); put32(buf + 4, (uint32_t)(256 + data_bytes)); put32(buf + 8, (uint32_t)data_bytes); put32(buf + 12, (uint32_t)map_len);
    memcpy(map, buf, 16);
    put16(map + 24, (unsigned)typelist); put16(map + 26, (unsigned)names);
    put16(map + typelist, (unsigned)(ntypes - 1));
    size_t ref_at = refs, name_at = names;
    for (size_t t = 0; t < ntypes; t++) {
        unsigned in_type = 0;
        for (size_t i = 0; i < count; i++) if (res[i].type == types[t]) in_type++;
        put32(map + typelist + 2 + t * 8, types[t]); put16(map + typelist + 6 + t * 8, in_type - 1); put16(map + typelist + 8 + t * 8, (unsigned)(ref_at - typelist));
        for (size_t i = 0; i < count; i++) {
            if (res[i].type != types[t]) continue;
            put16(map + ref_at, (uint16_t)res[i].id);
            if (res[i].name_size) {
                put16(map + ref_at + 2, (unsigned)(name_at - names));
                map[name_at] = (uint8_t)res[i].name_size; memcpy(map + name_at + 1, res[i].name, res[i].name_size); name_at += 1 + res[i].name_size;
            } else put16(map + ref_at + 2, 0xffff);
            map[ref_at + 4] = res[i].attributes;
            map[ref_at + 5] = (uint8_t)(dofs[i] >> 16); map[ref_at + 6] = (uint8_t)(dofs[i] >> 8); map[ref_at + 7] = (uint8_t)dofs[i];
            ref_at += 12;
        }
    }
    *out = buf; *out_size = total; buf = NULL; rc = 0;
done:
    free(buf); free(res); free(data); free(size); free(dofs); free(types);
    return rc;
}
