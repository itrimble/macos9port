/* Checks against real-world files kept outside Git. Set
 * OS999_STORAGE_FIXTURES to a folder holding any of:
 *   aaru/StuffIt/{1.5,3.0,5.0}/...  (Aaru.Compression.Tests corpus)
 *   AstralForge_2_2_5M.dsk          (1.44 MB HFS floppy)
 *   StatBar-1.4.0.dmg               (UDIF, GPT, HFS+)
 * Missing files are reported as SKIP, never as passes. */
#include "st_test.h"

#include <unistd.h>

static char base[1024];
static int skipped;

static int load(const char *rel, StArchive *a, StUnpackInfo *info, int *rc_out) {
    char p[2048];
    snprintf(p, sizeof p, "%s/%s", base, rel);
    if (access(p, R_OK)) { printf("  SKIP %s (absent)\n", rel); skipped++; return 0; }
    StErr err = {0};
    memset(a, 0, sizeof *a);
    *rc_out = st_unpack_file(p, 0, a, info, NULL, &err);
    if (*rc_out) printf("  %s: %s\n", rel, err.msg);
    return 1;
}

static const StEntry *find(const StArchive *a, const char *path) {
    for (size_t i = 0; i < a->n; i++) if (!strcmp(a->v[i].path, path)) return &a->v[i];
    return NULL;
}

static int same(const StArchive *x, const StArchive *y) {
    if (x->n != y->n) return 0;
    for (size_t i = 0; i < x->n; i++) {
        const StEntry *o = find(y, x->v[i].path);
        if (!o || o->is_dir != x->v[i].is_dir) return 0;
        if (!o->is_dir && !st_same_file(&x->v[i].file, &o->file)) return 0;
    }
    return 1;
}

static void stuffit(void) {
    const char *sets[] = {"StuffIt/1.5/1FILE", "StuffIt/1.5/CORPUS"};
    for (int s = 0; s < 2; s++) {
        StArchive st, df;
        StUnpackInfo i;
        int rc1, rc2;
        char a[256], b[256];
        snprintf(a, sizeof a, "aaru/%s/STORE.SIT", sets[s]);
        snprintf(b, sizeof b, "aaru/%s/DEFAULTS.SIT", sets[s]);
        if (!load(a, &st, &i, &rc1)) continue;
        if (!load(b, &df, &i, &rc2)) { st_archive_free(&st); continue; }
        CHECK(rc1 == ST_OK && rc2 == ST_OK);
        CHECK(same(&st, &df)); /* LZW + Huffman output equals the stored copy */
        printf("  %s: %zu entries, compressed == stored\n", sets[s], st.n);
        if (s == 1) {
            StArchive v5;
            int rc3;
            if (load("aaru/StuffIt/5.0/CORPUS/NONE.SIT", &v5, &i, &rc3)) {
                CHECK(rc3 == ST_OK && v5.n == st.n);
                for (size_t k = 0; k < st.n && rc3 == ST_OK; k++)
                    if (!st.v[k].is_dir) {
                        const StEntry *o = find(&v5, st.v[k].path);
                        CHECK(o && o->file.data.n == st.v[k].file.data.n &&
                              !memcmp(o->file.data.p, st.v[k].file.data.p, o->file.data.n));
                    }
                printf("  StuffIt 5 stored corpus matches StuffIt 1.5\n");
                st_archive_free(&v5);
            }
        }
        st_archive_free(&st);
        st_archive_free(&df);
    }
    StArchive x;
    StUnpackInfo i;
    int rc;
    if (load("aaru/StuffIt/1.5/FSSETTER.SIT", &x, &i, &rc)) {
        CHECK(rc == ST_OK && find(&x, "DEPTH/00000000/00000001/00000002/00000003/00000004/00000005/00000006/00000007/00000008/00000009"));
        size_t rsrc = 0;
        for (size_t k = 0; k < x.n; k++) rsrc += x.v[k].file.rsrc.n ? 1 : 0;
        CHECK(rsrc > 0);
        printf("  1.5 FSSETTER: %zu entries, %zu with resource forks\n", x.n, rsrc);
        st_archive_free(&x);
    }
    const char *unsup[] = {"aaru/StuffIt/3.0/1FILE/DEFAULTS.SIT", "aaru/StuffIt/5.0/1FILE/MAXIMUM.SIT"};
    for (int k = 0; k < 2; k++)
        if (load(unsup[k], &x, &i, &rc)) CHECK(rc == ST_EUNSUPPORTED); /* named, never mis-decoded */
}

static void compactpro(void) {
    const char *sets[] = {"Compact Pro/1FILE", "Compact Pro/CORPUS"};
    for (int s = 0; s < 2; s++) {
        StArchive st, df;
        StUnpackInfo i;
        int rc1, rc2;
        char a[256], b[256];
        snprintf(a, sizeof a, "aaru/%s/STORE.CPT", sets[s]);
        snprintf(b, sizeof b, "aaru/%s/DEFAULTS.CPT", sets[s]);
        if (!load(a, &st, &i, &rc1)) continue;
        if (!load(b, &df, &i, &rc2)) { st_archive_free(&st); continue; }
        CHECK(rc1 == ST_OK && rc2 == ST_OK && same(&st, &df)); /* LZH+RLE == RLE only */
        printf("  %s: %zu entries, LZH == stored, CRC-32 verified\n", sets[s], st.n);
        st_archive_free(&st);
        st_archive_free(&df);
    }
    StArchive c, t;
    StUnpackInfo i;
    int rc, rc2;
    if (load("aaru/Compact Pro/FSSETTER.CPT", &c, &i, &rc)) {
        CHECK(rc == ST_OK);
        size_t rsrc = 0, matched = 0;
        for (size_t k = 0; k < c.n; k++) rsrc += c.v[k].file.rsrc.n ? 1 : 0;
        if (load("aaru/StuffIt/1.5/FSSETTER.SIT", &t, &i, &rc2) && rc2 == ST_OK) {
            for (size_t k = 0; k < t.n; k++) {
                const StEntry *o = find(&c, t.v[k].path);
                if (o && (t.v[k].is_dir || st_same_file(&t.v[k].file, &o->file))) matched++;
            }
            /* The same folder tree archived by two programs: all but one
             * volatile Finder file agree, resource forks included. */
            CHECK(matched + 1 >= t.n);
            printf("  Compact Pro FSSETTER: %zu entries, %zu resource forks, %zu/%zu match StuffIt 1.5\n", c.n, rsrc, matched, t.n);
            st_archive_free(&t);
        }
        st_archive_free(&c);
    }
}

/* Methods 13 and 15 through the unar fallback must agree with our own
 * decode of the same folder tree archived by StuffIt 1.5. */
static void external(void) {
    if (system("command -v unar >/dev/null 2>&1") != 0 && !getenv("OS999_UNAR")) {
        printf("  SKIP unar fallback (unar not installed)\n");
        skipped++;
        return;
    }
    StArchive ref;
    StUnpackInfo i;
    int rc;
    if (!load("aaru/StuffIt/1.5/FSSETTER.SIT", &ref, &i, &rc) || rc) return;
    const char *sets[] = {"aaru/StuffIt/3.0/FSSETTER.SIT", "aaru/StuffIt/5.0/FSSETTER.SIT"};
    for (int k = 0; k < 2; k++) {
        char p[2048];
        snprintf(p, sizeof p, "%s/%s", base, sets[k]);
        if (access(p, R_OK)) { printf("  SKIP %s\n", sets[k]); skipped++; continue; }
        StArchive x = {0};
        StErr err;
        rc = st_unpack_file(p, 1, &x, &i, NULL, &err);
        CHECK(rc == ST_OK && i.external);
        size_t matched = 0, files = 0, absent = 0;
        for (size_t e = 0; rc == ST_OK && e < ref.n; e++) {
            if (ref.v[e].is_dir) continue;
            const StEntry *o = find(&x, ref.v[e].path);
            if (!o) { absent++; continue; } /* names unar spells differently; aliases StuffIt 5 omitted */
            files++;
            if (o->file.data.n == ref.v[e].file.data.n && o->file.rsrc.n == ref.v[e].file.rsrc.n &&
                (!o->file.data.n || !memcmp(o->file.data.p, ref.v[e].file.data.p, o->file.data.n)) &&
                (!o->file.rsrc.n || !memcmp(o->file.rsrc.p, ref.v[e].file.rsrc.p, o->file.rsrc.n)) &&
                !memcmp(o->file.finfo, ref.v[e].file.finfo, 8))
                matched++;
        }
        CHECK(matched == files && files > 200);
        printf("  %s via unar: %zu/%zu shared files identical (forks, type, creator); %zu not in both\n", sets[k],
               matched, files, absent);
        st_archive_free(&x);
    }
    st_archive_free(&ref);
}

static void floppy(void) {
    StArchive a;
    StUnpackInfo i;
    int rc;
    if (!load("AstralForge_2_2_5M.dsk", &a, &i, &rc)) return;
    CHECK(rc == ST_OK && a.n == 12 && !strcmp(i.volume, "Crystal Quest"));
    /* CRC-32 of both forks, cross-checked with the independent machfs reader. */
    static const struct { const char *name; uint32_t d, r; } want[] = {
        {"Crystal Quest", 0x00000000, 0x18e4dafd}, {"Crystal Quest Sounds", 0x00000000, 0x9908003c},
        {"Crystal Quest Read Me", 0xd9a5504f, 0x0dfec5f6}, {"Totally silly 2.3 ", 0x00000000, 0x666e81bf},
        {"CritterEditor 1.0.4M", 0x00000000, 0x2f631033},
    };
    for (size_t k = 0; k < sizeof want / sizeof *want; k++) {
        const StEntry *e = find(&a, want[k].name);
        CHECK(e && st_crc32(0, e->file.data.p, e->file.data.n) == want[k].d &&
              st_crc32(0, e->file.rsrc.p, e->file.rsrc.n) == want[k].r);
    }
    const StEntry *app = find(&a, "Crystal Quest");
    CHECK(app && !memcmp(app->file.finfo, "APPLCYQS", 8));
    printf("  Crystal Quest floppy: %zu files, forks match machfs\n", a.n);
    st_archive_free(&a);
}

static void dmg(void) {
    StArchive a;
    StUnpackInfo i;
    int rc;
    if (!load("StatBar-1.4.0.dmg", &a, &i, &rc)) return;
    CHECK(rc == ST_OK && !strcmp(i.volume, "StatBar"));
    CHECK(strstr(i.chain, "UDIF") && strstr(i.chain, "HFS+"));
    const StEntry *pl = find(&a, "StatBar.app/Contents/Info.plist");
    CHECK(pl && pl->file.data.n > 100 && strstr((const char *)pl->file.data.p, "CFBundle") != NULL);
    printf("  StatBar dmg: %s, %zu entries\n", i.chain, a.n);
    /* Wrap the real HFS+ volume in an HFS wrapper (drEmbedSigWord 'H+'),
     * the layout Mac OS 8.1+ used so older Macs could see the disk. */
    char path[2048];
    snprintf(path, sizeof path, "%s/StatBar-1.4.0.dmg", base);
    StBytes img = {0}, raw = {0}, wrap = {0};
    StErr err;
    size_t off, len;
    if (st_read_file(path, 1u << 30, &img, &err) == ST_OK && st_image_decode(img.p, img.n, NULL, 0, &raw, NULL, &err) == ST_OK &&
        st_find_hfs(raw.p, raw.n, &off, &len, &err) == ST_OK) {
        size_t lead = 6144, blocks = (len + 4095) / 4096;
        st_bytes_reserve(&wrap, lead + blocks * 4096);
        memset(wrap.p, 0, lead + blocks * 4096);
        wrap.n = lead + blocks * 4096;
        uint8_t *m = wrap.p + 1024;
        m[0] = 'B'; m[1] = 'D';
        m[20] = 0; m[21] = 0; m[22] = 0x10; m[23] = 0;   /* 4096-byte blocks */
        m[28] = 0; m[29] = 4;                             /* blocks start at sector 4 */
        m[124] = 'H'; m[125] = '+';
        m[126] = 0; m[127] = 1;                           /* embedded at block 1 */
        m[128] = (uint8_t)(blocks >> 8); m[129] = (uint8_t)blocks;
        memcpy(wrap.p + lead, raw.p + off, len);
        StArchive w = {0};
        char vn[64];
        CHECK(st_find_hfs(wrap.p, wrap.n, &off, &len, &err) == ST_OK && off == 0);
        CHECK(st_hfs_read(wrap.p, wrap.n, &w, vn, sizeof vn, NULL, &err) == ST_OK && w.n == a.n && !strcmp(vn, "StatBar"));
        printf("  HFS wrapper around the same HFS+ volume: %zu entries\n", w.n);
        st_archive_free(&w);
    }
    st_bytes_free(&img); st_bytes_free(&raw); st_bytes_free(&wrap);
    st_archive_free(&a);
}

int main(void) {
    const char *e = getenv("OS999_STORAGE_FIXTURES");
    if (!e || !*e) {
        printf("test_storage_real: SKIP (OS999_STORAGE_FIXTURES not set)\n");
        return 0;
    }
    snprintf(base, sizeof base, "%s", e);
    stuffit();
    compactpro();
    external();
    floppy();
    dmg();
    printf("  %d fixture(s) skipped\n", skipped);
    return st_test_done("test_storage_real");
}
