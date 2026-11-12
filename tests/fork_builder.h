/* A resource-fork writer for tests: header, data, map with a type list, reference lists and a name list. Everything is built by hand from the public layout;
 * no bytes come from a real file. Include after defining CHECK. */
#ifndef TESTS_FORK_BUILDER_H
#define TESTS_FORK_BUILDER_H
#include <stdint.h>
#include <string.h>
enum { FB_RES = 64, FB_DATA = 2048, FB_BYTES = 262144 };
typedef struct { uint32_t type; int16_t id; uint8_t attrs; char name[64]; uint8_t data[FB_DATA]; size_t n; } FbRes;
typedef struct { FbRes r[FB_RES]; unsigned count; } FbFork;
static void fb_put16(uint8_t *p, unsigned x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void fb_put32(uint8_t *p, uint32_t x) { fb_put16(p, x >> 16); fb_put16(p + 2, x & 0xffff); }
static FbRes *fb_add(FbFork *f, uint32_t type, int id, const char *name, const void *data, size_t n) {
    FbRes *r = &f->r[f->count < FB_RES ? f->count++ : FB_RES - 1];
    memset(r, 0, sizeof *r);
    r->type = type; r->id = (int16_t)id; r->n = n <= FB_DATA ? n : FB_DATA;
    if (name) strncpy(r->name, name, sizeof r->name - 1);
    if (n) memcpy(r->data, data, r->n);
    return r;
}
/* `out` must hold FB_BYTES. Returns the fork's length. */
static size_t fb_build(const FbFork *f, uint8_t *out) {
    size_t at = 256; memset(out, 0, FB_BYTES);
    uint32_t offsets[FB_RES], types[FB_RES]; unsigned ntypes = 0;
    for (unsigned i = 0; i < f->count; i++) { offsets[i] = (uint32_t)(at - 256); fb_put32(out + at, (uint32_t)f->r[i].n); memcpy(out + at + 4, f->r[i].data, f->r[i].n); at += 4 + f->r[i].n; }
    size_t map = at;
    for (unsigned i = 0; i < f->count; i++) { unsigned k = 0; while (k < ntypes && types[k] != f->r[i].type) k++; if (k == ntypes) types[ntypes++] = f->r[i].type; }
    uint8_t *m = out + map; unsigned typelist = 28, ref_at = typelist + 2 + 8 * ntypes, names = ref_at + 12 * f->count, name_at = names;
    fb_put16(m + 24, typelist); fb_put16(m + 26, names); fb_put16(m + typelist, ntypes - 1);
    for (unsigned t = 0; t < ntypes; t++) {
        unsigned count = 0; for (unsigned i = 0; i < f->count; i++) if (f->r[i].type == types[t]) count++;
        fb_put32(m + typelist + 2 + t * 8, types[t]); fb_put16(m + typelist + 6 + t * 8, count - 1); fb_put16(m + typelist + 8 + t * 8, ref_at - typelist);
        for (unsigned i = 0; i < f->count; i++) if (f->r[i].type == types[t]) {
            fb_put16(m + ref_at, (unsigned)(uint16_t)f->r[i].id);
            if (f->r[i].name[0]) {
                size_t len = strlen(f->r[i].name);
                fb_put16(m + ref_at + 2, name_at - names); m[name_at] = (uint8_t)len; memcpy(m + name_at + 1, f->r[i].name, len); name_at += 1 + (unsigned)len;
            } else fb_put16(m + ref_at + 2, 0xffff);
            m[ref_at + 4] = f->r[i].attrs;
            m[ref_at + 5] = (uint8_t)(offsets[i] >> 16); m[ref_at + 6] = (uint8_t)(offsets[i] >> 8); m[ref_at + 7] = (uint8_t)offsets[i];
            ref_at += 12;
        }
    }
    size_t mlen = name_at;
    fb_put32(out, 256); fb_put32(out + 4, (uint32_t)map); fb_put32(out + 8, (uint32_t)(map - 256)); fb_put32(out + 12, (uint32_t)mlen);
    memcpy(m, out, 16);
    return map + mlen;
}
#endif
