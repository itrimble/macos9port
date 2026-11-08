/* HFS writer: lays out a fresh Mac OS Standard volume holding an
 * archive. Catalog keys are ordered by parent ID, then by name compared
 * case-insensitively for ASCII (non-ASCII names keep byte order). Forks
 * are stored contiguously, so no extents-overflow records are needed. */
#include "st_hfs_int.h"

#include <stdio.h>
#include <stdlib.h>

#define NODE 512

typedef struct {
    uint32_t parent;
    uint8_t name[32]; /* Pascal string */
    uint8_t data[102];
    int dlen;
} Rec;

typedef struct { char path[4096]; uint32_t id, parent; uint16_t valence; size_t entry; } WDir;

static int fold(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int rec_cmp(const void *a, const void *b) {
    const Rec *x = a, *y = b;
    if (x->parent != y->parent) return x->parent < y->parent ? -1 : 1;
    int n = x->name[0] < y->name[0] ? x->name[0] : y->name[0];
    for (int i = 1; i <= n; i++) {
        int c = fold(x->name[i]) - fold(y->name[i]);
        if (c) return c;
    }
    return (int)x->name[0] - (int)y->name[0];
}

static int key_bytes(const Rec *r, uint8_t *k, int padded) {
    int nl = r->name[0];
    memset(k, 0, 38);
    k[0] = (uint8_t)(padded ? 37 : 6 + nl);
    st_put32(k + 2, r->parent);
    memcpy(k + 6, r->name, (size_t)nl + 1);
    int len = padded ? 38 : 7 + nl;
    return (len + 1) & ~1;
}

static void node_init(uint8_t *n, int8_t kind, uint8_t height) {
    memset(n, 0, NODE);
    n[8] = (uint8_t)kind;
    n[9] = height;
    st_put16(n + NODE - 2, 14);
}

/* Append a record; returns 0, or -1 when the node is full. */
static int node_add(uint8_t *n, const uint8_t *rec, int len) {
    int count = st_be16(n + 10), free_off = st_be16(n + NODE - 2 * (count + 1));
    if (free_off + len > NODE - 2 * (count + 2)) return -1;
    memcpy(n + free_off, rec, (size_t)len);
    st_put16(n + 10, (uint16_t)(count + 1));
    st_put16(n + NODE - 2 * (count + 2), (uint16_t)(free_off + len));
    return 0;
}

static void header_node(uint8_t *n, uint16_t depth, uint32_t root, uint32_t leafrecs, uint32_t first,
                        uint32_t last, uint16_t maxkey, uint32_t total, uint32_t used) {
    node_init(n, 1, 0);
    uint8_t h[106] = {0}, user[128] = {0}, map[NODE - 14 - 106 - 128 - 8];
    st_put16(h, depth);
    st_put32(h + 2, root);
    st_put32(h + 6, leafrecs);
    st_put32(h + 10, first);
    st_put32(h + 14, last);
    st_put16(h + 18, NODE);
    st_put16(h + 20, maxkey);
    st_put32(h + 22, total);
    st_put32(h + 26, total - used);
    memset(map, 0, sizeof map);
    for (uint32_t i = 0; i < used; i++) map[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    node_add(n, h, sizeof h);
    node_add(n, user, sizeof user);
    node_add(n, map, sizeof map);
}

static int add_name(Rec *r, const char *utf8) {
    int nl = st_utf8_to_macroman(utf8, r->name + 1, 31);
    if (nl <= 0) return -1;
    for (int i = 1; i <= nl; i++) if (r->name[i] == ':') r->name[i] = '/';
    r->name[0] = (uint8_t)nl;
    return 0;
}

static WDir *find_dir(WDir *d, size_t nd, const char *path) {
    for (size_t i = 0; i < nd; i++) if (!strcmp(d[i].path, path)) return &d[i];
    return NULL;
}

int st_hfs_write(const StArchive *a, const char *vol_name, size_t bytes, StBytes *out, StErr *err) {
    if (bytes < 409600 || bytes > (2ull << 30) || bytes % 512) return st_fail(err, ST_EINVAL, "HFS volume size must be 400K..2G in 512-byte blocks");
    uint32_t sectors = (uint32_t)(bytes / 512);
    uint32_t alsz = 512 * ((sectors + 65534) / 65535);
    uint32_t nbl = (sectors - 5) * 512 / alsz, bm = (nbl + 4095) / 4096;
    uint32_t alst = 3 + bm;
    nbl = (sectors - alst - 2) * 512 / alsz;
    /* Folders: root (CNID 2) plus every folder named or implied. */
    size_t cap = a->n * 2 + 2, nd = 0, nr = 0;
    WDir *dirs = calloc(cap, sizeof *dirs);
    Rec *recs = calloc(cap * 2 + 4, sizeof *recs);
    uint8_t *img = NULL;
    int rc = ST_OK;
    if (!dirs || !recs) { rc = st_fail(err, ST_ENOMEM, "out of memory"); goto done; }
    dirs[nd++] = (WDir){"", 2, 1, 0, (size_t)-1};
    uint32_t next_id = 16, nfiles = 0;
    for (size_t i = 0; i < a->n && rc == ST_OK; i++) {
        char p[4096];
        snprintf(p, sizeof p, "%s", a->v[i].path);
        for (char *s = p;; s++) {
            if (*s != '/' && *s) continue;
            char c = *s;
            *s = 0;
            int is_leaf = !c;
            if ((!is_leaf || a->v[i].is_dir) && !find_dir(dirs, nd, p)) {
                char *slash = strrchr(p, '/');
                char parent[4096];
                snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - p) : 0, p);
                WDir *pd = find_dir(dirs, nd, parent);
                dirs[nd] = (WDir){"", next_id++, pd ? pd->id : 2, 0, is_leaf ? i : (size_t)-1};
                snprintf(dirs[nd].path, sizeof dirs[nd].path, "%s", p);
                if (pd) pd->valence++;
                nd++;
            }
            *s = c;
            if (!c) break;
        }
    }
    uint64_t need_blocks = 0;
    for (size_t i = 0; i < a->n; i++)
        if (!a->v[i].is_dir)
            need_blocks += (a->v[i].file.data.n + alsz - 1) / alsz + (a->v[i].file.rsrc.n + alsz - 1) / alsz;
    /* Catalog records. */
    for (size_t i = 0; i < nd; i++) {
        WDir *d = &dirs[i];
        Rec *r = &recs[nr++], *t = &recs[nr++];
        const char *nm = d->id == 2 ? (vol_name && *vol_name ? vol_name : "Untitled") : strrchr(d->path, '/') ? strrchr(d->path, '/') + 1 : d->path;
        r->parent = d->parent;
        if (add_name(r, nm)) { rc = st_fail(err, ST_EINVAL, "folder name \"%s\" is not a valid HFS name", nm); goto done; }
        r->data[0] = 1;
        st_put16(r->data + 4, d->valence);
        st_put32(r->data + 6, d->id);
        if (d->entry != (size_t)-1) {
            const StMacFile *m = &a->v[d->entry].file;
            st_put32(r->data + 10, m->ctime);
            st_put32(r->data + 14, m->mtime);
            memcpy(r->data + 22, m->finfo, 16);
            memcpy(r->data + 38, m->fxinfo, 16);
        }
        r->dlen = 70;
        t->parent = d->id; /* thread: key (id, ""), points back to parent+name */
        t->data[0] = 3;
        st_put32(t->data + 10, d->parent);
        memcpy(t->data + 14, r->name, (size_t)r->name[0] + 1);
        t->dlen = 46;
    }
    uint32_t xt_nodes = 4, cat_leaf = 0;
    uint32_t xt_blocks = (xt_nodes * NODE + alsz - 1) / alsz;
    /* File records are filled after allocation; reserve them now. */
    size_t file_rec0 = nr;
    for (size_t i = 0; i < a->n; i++) {
        if (a->v[i].is_dir) continue;
        char parent[4096];
        const char *slash = strrchr(a->v[i].path, '/');
        snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - a->v[i].path) : 0, a->v[i].path);
        WDir *pd = find_dir(dirs, nd, parent);
        Rec *r = &recs[nr++];
        r->parent = pd ? pd->id : 2;
        if (pd) pd->valence++;
        if (add_name(r, a->v[i].file.name[0] ? a->v[i].file.name : slash ? slash + 1 : a->v[i].path)) {
            rc = st_fail(err, ST_EINVAL, "file name in %s is not a valid HFS name", a->v[i].path);
            goto done;
        }
        r->dlen = 102;
        nfiles++;
    }
    for (size_t i = 0; i < nd; i++) /* valence may have grown from files */
        for (size_t k = 0; k < file_rec0; k++)
            if (recs[k].data[0] == 1 && st_be32(recs[k].data + 6) == dirs[i].id) st_put16(recs[k].data + 4, dirs[i].valence);
    /* Pack leaves to learn the catalog size. */
    qsort(recs, nr, sizeof *recs, rec_cmp);
    uint32_t leaves = 1, used = 14 + 2;
    for (size_t i = 0; i < nr; i++) {
        uint8_t k[40];
        int len = key_bytes(&recs[i], k, 0) + recs[i].dlen + 2;
        if (used + len > NODE) { leaves++; used = 14 + 2; }
        used += (uint32_t)len;
    }
    uint32_t per_index = (NODE - 14 - 2) / 44, index_nodes = 0;
    for (uint32_t level = leaves; level > 1; level = (level + per_index - 1) / per_index)
        index_nodes += (level + per_index - 1) / per_index;
    uint32_t cat_nodes = 1 + leaves + index_nodes;
    if (cat_nodes > (NODE - 14 - 106 - 128 - 8) * 8) { rc = st_fail(err, ST_ELIMIT, "too many files for one HFS catalog"); goto done; }
    uint32_t cat_blocks = (cat_nodes * NODE + alsz - 1) / alsz;
    if (xt_blocks + cat_blocks + need_blocks > nbl) { rc = st_fail(err, ST_ELIMIT, "files do not fit in a %zu-byte HFS volume", bytes); goto done; }
    img = calloc(1, bytes);
    if (!img) { rc = st_fail(err, ST_ENOMEM, "out of memory"); goto done; }
    uint8_t *vol = img + (size_t)alst * 512;
    uint32_t next_block = xt_blocks + cat_blocks;
    /* Place forks and finish file records (match by sorted record identity). */
    for (size_t i = 0; i < a->n; i++) {
        if (a->v[i].is_dir) continue;
        const StMacFile *m = &a->v[i].file;
        const char *slash = strrchr(a->v[i].path, '/');
        char parent[4096];
        snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - a->v[i].path) : 0, a->v[i].path);
        WDir *pd = find_dir(dirs, nd, parent);
        Rec probe = {0};
        probe.parent = pd ? pd->id : 2;
        add_name(&probe, m->name[0] ? m->name : slash ? slash + 1 : a->v[i].path);
        Rec *r = bsearch(&probe, recs, nr, sizeof *recs, rec_cmp);
        if (!r || r->data[0]) { rc = st_fail(err, ST_EEXIST, "duplicate name %s in HFS folder", a->v[i].path); goto done; }
        uint8_t *d = r->data;
        d[0] = 2;
        memcpy(d + 4, m->finfo, 16);
        st_put32(d + 20, next_id++);
        const StBytes *fork[2] = {&m->data, &m->rsrc};
        for (int f = 0; f < 2; f++) {
            uint32_t blocks = (uint32_t)((fork[f]->n + alsz - 1) / alsz);
            st_put32(d + (f ? 36 : 26), (uint32_t)fork[f]->n);
            st_put32(d + (f ? 40 : 30), blocks * alsz);
            if (blocks) {
                st_put16(d + (f ? 86 : 74), (uint16_t)next_block);
                st_put16(d + (f ? 88 : 76), (uint16_t)blocks);
                st_put16(d + (f ? 34 : 24), (uint16_t)next_block);
                memcpy(vol + (size_t)next_block * alsz, fork[f]->p, fork[f]->n);
                next_block += blocks;
            }
        }
        st_put32(d + 44, m->ctime);
        st_put32(d + 48, m->mtime);
        memcpy(d + 56, m->fxinfo, 16);
    }
    /* Catalog B-tree: header, leaves 1..L, then index levels. */
    uint8_t *cat = vol + (size_t)xt_blocks * alsz;
    uint32_t node = 1;
    node_init(cat + NODE, -1, 1);
    for (size_t i = 0; i < nr; i++) {
        uint8_t buf[160];
        int kl = key_bytes(&recs[i], buf, 0);
        memcpy(buf + kl, recs[i].data, (size_t)recs[i].dlen);
        if (node_add(cat + (size_t)node * NODE, buf, kl + recs[i].dlen)) {
            st_put32(cat + (size_t)node * NODE, node + 1);
            node++;
            node_init(cat + (size_t)node * NODE, -1, 1);
            st_put32(cat + (size_t)node * NODE + 4, node - 1);
            node_add(cat + (size_t)node * NODE, buf, kl + recs[i].dlen);
        }
    }
    cat_leaf = node;
    uint32_t lo = 1, hi = cat_leaf, height = 1;
    while (hi > lo) { /* build one index level over nodes [lo, hi] */
        height++;
        uint32_t first = node + 1;
        node++;
        node_init(cat + (size_t)node * NODE, 0, (uint8_t)height);
        for (uint32_t c = lo; c <= hi; c++) {
            const uint8_t *cn = cat + (size_t)c * NODE;
            const uint8_t *k0 = cn + st_be16(cn + NODE - 2);
            Rec probe = {0};
            probe.parent = st_be32(k0 + 2);
            memcpy(probe.name, k0 + 6, (size_t)k0[6] + 1);
            uint8_t buf[44];
            key_bytes(&probe, buf, 1);
            st_put32(buf + 38, c);
            if (node_add(cat + (size_t)node * NODE, buf, 42)) {
                st_put32(cat + (size_t)node * NODE, node + 1);
                node++;
                node_init(cat + (size_t)node * NODE, 0, (uint8_t)height);
                st_put32(cat + (size_t)node * NODE + 4, node - 1);
                node_add(cat + (size_t)node * NODE, buf, 42);
            }
        }
        lo = first;
        hi = node;
    }
    uint32_t total = cat_blocks * alsz / NODE;
    header_node(cat, (uint16_t)height, hi, (uint32_t)nr, 1, cat_leaf, 37, total, node + 1);
    header_node(vol, 0, 0, 0, 0, 0, 7, xt_blocks * alsz / NODE, 1);
    /* Volume bitmap and master directory block (plus its copy). */
    for (uint32_t b = 0; b < next_block; b++) img[3 * 512 + b / 8] |= (uint8_t)(0x80 >> (b % 8));
    uint8_t *m = img + 1024;
    uint32_t now = 0xB0000000u;
    st_put16(m, 0x4244);
    st_put32(m + 2, now);
    st_put32(m + 6, now);
    st_put16(m + 10, 0x0100);
    uint16_t root_files = 0, root_dirs = 0;
    for (size_t i = 0; i < nr; i++) {
        if (recs[i].parent != 2) continue;
        if (recs[i].data[0] == 2) root_files++;
        if (recs[i].data[0] == 1) root_dirs++;
    }
    st_put16(m + 12, root_files);
    st_put16(m + 14, 3);
    st_put16(m + 18, (uint16_t)nbl);
    st_put32(m + 20, alsz);
    st_put32(m + 24, alsz * 4);
    st_put16(m + 28, (uint16_t)alst);
    st_put32(m + 30, next_id);
    st_put16(m + 34, (uint16_t)(nbl - next_block));
    Rec vn = {0};
    add_name(&vn, vol_name && *vol_name ? vol_name : "Untitled");
    memcpy(m + 36, vn.name, (size_t)(vn.name[0] > 27 ? 27 : vn.name[0]) + 1);
    if (m[36] > 27) m[36] = 27;
    st_put32(m + 74, xt_blocks * alsz);
    st_put32(m + 78, cat_blocks * alsz);
    st_put16(m + 82, root_dirs);
    st_put32(m + 84, nfiles);
    st_put32(m + 88, (uint32_t)nd - 1);
    st_put32(m + 130, xt_blocks * alsz);
    st_put16(m + 136, (uint16_t)xt_blocks);
    st_put32(m + 146, cat_blocks * alsz);
    st_put16(m + 150, (uint16_t)xt_blocks);
    st_put16(m + 152, (uint16_t)cat_blocks);
    memcpy(img + bytes - 1024, m, 512);
    out->n = 0;
    if (st_bytes_append(out, img, bytes)) rc = st_fail(err, ST_ENOMEM, "out of memory");
done:
    free(img);
    free(dirs);
    free(recs);
    return rc;
}
