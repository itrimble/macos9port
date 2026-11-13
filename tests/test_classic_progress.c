/* Headless checks for the Install Classic Apps progress display: weighting and percentages, stage
 * transitions (including failure and stop), labels, the text wrapper, the bordered bar, a painted
 * dialog frame, and the stage/byte callbacks of the real install pipeline. No SDL, no network. */
#include "ospi.h"
#include "classic_apps.h"
#include "classic_progress.h"
#include "os999_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static ClassicRunProg three(uint64_t a, uint64_t b, uint64_t c) {
    ClassicRunProg p;
    classic_prog_init(&p);
    classic_prog_add(&p, a); classic_prog_add(&p, b); classic_prog_add(&p, c);
    return p;
}

static void test_maths(void) {
    ClassicRunProg p = three(1000000, 3000000, 0);
    CHECK(p.n == 3 && p.it[2].weight == CP_DEFAULT_WEIGHT);          /* unknown size gets the default weight */
    CHECK(classic_total_permille(&p) == 0);
    classic_prog_stage(&p, 0, CS_DONE);
    classic_prog_stage(&p, 1, CS_DOWNLOADING);
    classic_prog_bytes(&p, 1, 500, 1000);                              /* half of the download share: 400 */
    CHECK(classic_item_permille(&p.it[1]) == 400);
    /* (1000*1 MB + 400*3 MB + 0*1.048576 MB) / (1 + 3 + 1.048576)  -> weights are bytes */
    unsigned long long den = 1000000ull + 3000000ull + CP_DEFAULT_WEIGHT;
    CHECK(classic_total_permille(&p) == (int)((1000ull * 1000000 + 400ull * 3000000) / den));
    /* the fixed share: verify = 800, unpack starts at 850 and fills to 1000, done = 1000 */
    classic_prog_stage(&p, 1, CS_VERIFYING);
    CHECK(classic_item_permille(&p.it[1]) == CP_DL_END);
    classic_prog_stage(&p, 1, CS_UNPACKING);
    CHECK(classic_item_permille(&p.it[1]) == CP_VERIFY_END);
    classic_prog_bytes(&p, 1, 50, 100);
    CHECK(classic_item_permille(&p.it[1]) == CP_VERIFY_END + 75);
    classic_prog_bytes(&p, 1, 900, 100);                               /* never beyond the item */
    CHECK(classic_item_permille(&p.it[1]) == CP_FULL);
    classic_prog_bytes(&p, 1, 10, 0);                                  /* unknown total: no movement */
    CHECK(classic_item_permille(&p.it[1]) == CP_VERIFY_END);
    /* "Installing 2 of 3, 47%": three equal items, one done, one a bit past half of its download */
    ClassicRunProg q = three(100, 100, 100);
    char label[80];
    classic_prog_stage(&q, 0, CS_DONE);
    classic_prog_stage(&q, 1, CS_DOWNLOADING);
    classic_prog_bytes(&q, 1, 525, 1000);
    classic_total_label(&q, label, sizeof label);
    CHECK(!strcmp(label, "Installing 2 of 3, 47%"));
    /* a 1 GB item drowns a 1 MB one, as weighting by bytes should */
    ClassicRunProg w = three(1000000000ull, 1000000, 1000000);
    classic_prog_stage(&w, 1, CS_DONE);
    classic_prog_stage(&w, 2, CS_DONE);
    CHECK(classic_total_permille(&w) == 1);
}

static void test_transitions(void) {
    ClassicRunProg p = three(100, 100, 100);
    char s[96];
    classic_prog_stage(&p, 0, CS_DOWNLOADING);
    classic_prog_bytes(&p, 0, 5, 10);
    classic_prog_stage(&p, 0, CS_WAITING);                              /* never goes backwards */
    CHECK(p.it[0].stage == CS_DOWNLOADING && p.it[0].got == 5);
    classic_prog_stage(&p, 0, CS_VERIFYING);
    CHECK(p.it[0].got == 0 && p.it[0].total == 0);                      /* a new stage restarts the counts */
    classic_prog_fail(&p, 0, "The download does not match the expected size or checksum, so it was not installed.");
    CHECK(p.it[0].stage == CS_FAILED);
    classic_stage_label(&p.it[0], s, sizeof s);
    CHECK(!strncmp(s, "Failed: The download does not match", 35) && strlen(p.it[0].reason) < 64);
    classic_prog_stage(&p, 0, CS_DONE);                                 /* final stays final */
    CHECK(p.it[0].stage == CS_FAILED);
    classic_prog_stage(&p, 1, CS_UNPACKING);
    classic_prog_stage(&p, 1, CS_STOPPED);                              /* Stop */
    classic_prog_stop_rest(&p);
    CHECK(p.it[1].stage == CS_STOPPED && p.it[2].stage == CS_STOPPED && classic_prog_all_final(&p));
    classic_stage_label(&p.it[2], s, sizeof s);
    CHECK(!strcmp(s, "Stopped"));
    classic_summary(&p, s, sizeof s);
    CHECK(strstr(s, "Stopped") && strstr(s, "0 of 3"));
    classic_total_label(&p, s, sizeof s);
    CHECK(!strcmp(s, "Stopped at 33%"));                                /* the failed item counts full, stopped ones do not */
    ClassicRunProg d = three(100, 100, 100);
    classic_prog_stage(&d, 0, CS_DONE); classic_prog_stage(&d, 1, CS_DONE); classic_prog_fail(&d, 2, "No space.");
    classic_summary(&d, s, sizeof s);
    CHECK(!strcmp(s, "2 of 3 installed, 1 failed."));
    CHECK(classic_total_permille(&d) == CP_FULL);
    classic_total_label(&d, s, sizeof s);
    CHECK(!strcmp(s, "Finished, 100%"));
    classic_prog_fail(&d, 0, "late");                                   /* no effect on a finished item */
    CHECK(d.it[0].stage == CS_DONE);
}

static void test_labels(void) {
    ClassicItemProg it = {.stage = CS_DOWNLOADING, .got = 3250000, .total = 8700000, .weight = 1};
    char s[96];
    classic_stage_label(&it, s, sizeof s);
    CHECK(!strcmp(s, "Downloading 3.1 of 8.3 MB"));
    it.total = 0;
    classic_stage_label(&it, s, sizeof s);
    CHECK(!strcmp(s, "Downloading 3.1 MB"));
    it.got = 20480; it.total = 40960;
    classic_stage_label(&it, s, sizeof s);
    CHECK(!strcmp(s, "Downloading 20 of 40 KB"));
    it.stage = CS_VERIFYING; classic_stage_label(&it, s, sizeof s); CHECK(!strcmp(s, "Verifying"));
    it.stage = CS_UNPACKING; classic_stage_label(&it, s, sizeof s); CHECK(!strcmp(s, "Unpacking"));
    it.stage = CS_DONE; classic_stage_label(&it, s, sizeof s); CHECK(!strcmp(s, "Installed"));
    it.stage = CS_WAITING; classic_stage_label(&it, s, sizeof s); CHECK(!strcmp(s, "Waiting"));
    classic_short_reason("Could not open the Macintosh Garden page. Try again later.", s, sizeof s);
    CHECK(!strcmp(s, "Could not open the Macintosh Garden page"));
    char tiny[16];
    classic_short_reason("A very long reason without any full stop at all", tiny, sizeof tiny);
    CHECK(strlen(tiny) == 15 && !strcmp(tiny + 12, "..."));
}

static int fixed8(const char *s, int n) { (void)s; return n * 8; }

static void test_wrap(void) {
    ClassicLine ln[4];
    int n = classic_wrap("It is only unpacked here, never run.", 20 * 8, fixed8, ln, 4);
    CHECK(n == 2 && !strcmp(ln[0].t, "It is only unpacked") && !strcmp(ln[1].t, "here, never run."));
    n = classic_wrap("short", 200, fixed8, ln, 2);
    CHECK(n == 1 && !strcmp(ln[0].t, "short"));
    n = classic_wrap("one two three four five six seven eight nine ten", 15 * 8, fixed8, ln, 2);   /* overflow: ellipsis */
    CHECK(n == 2 && strlen(ln[1].t) <= 15 && !strcmp(ln[1].t + strlen(ln[1].t) - 3, "..."));
    n = classic_wrap("abcdefghijklmnopqrstuvwxyz", 10 * 8, fixed8, ln, 4);                          /* one long word is hard-broken */
    CHECK(n == 3 && !strcmp(ln[0].t, "abcdefghij") && !strcmp(ln[2].t, "uvwxyz"));
    CHECK(classic_wrap("", 80, fixed8, ln, 2) == 0 && classic_wrap("x", 0, fixed8, ln, 2) == 0);
    /* the real catalogue notes at the real list width, with the real font: nothing cut mid-word */
    ClassicCatalog cat;
    CHECK(classic_catalog_default(&cat) > 0);
    for (int i = 0; i < cat.n; i++) {
        ClassicLine l2[2];
        int k = classic_wrap(cat.app[i].note, 500, fb_text_width_n, l2, 2), joined = 0;
        char all[300] = "";
        for (int j = 0; j < k; j++) {
            CHECK(fb_text_width_n(l2[j].t, (int)strlen(l2[j].t)) <= 500);
            if (j) strcat(all, " ");
            strcat(all, l2[j].t);
        }
        joined = !strcmp(all, cat.app[i].note);
        CHECK(k >= 1 && (joined || strstr(all, "...")));
        CHECK(joined);                                                  /* every shipped note fits in two lines */
    }
}

static void test_bar(void) {
    uint32_t px[120 * 20];
    Fb fb;
    for (int i = 0; i < 120 * 20; i++) px[i] = 0xFF808080u;
    fb_init(&fb, px, 120, 20);
    CHECK(classic_bar_fill_width(102, 500) == 50 && classic_bar_fill_width(102, 0) == 0 && classic_bar_fill_width(102, 1000) == 100);
    CHECK(classic_bar_fill_width(102, 5000) == 100 && classic_bar_fill_width(102, -4) == 0);
    classic_bar_paint(&fb, 4, 4, 102, 12, 500);
    CHECK(px[10 * 120 + 4] == C_BLACK && px[10 * 120 + 105] == C_BLACK);          /* frame */
    CHECK(px[4 * 120 + 50] == C_BLACK && px[15 * 120 + 50] == C_BLACK);
    CHECK(px[10 * 120 + 5] == C_BLACK && px[10 * 120 + 54] == C_BLACK);           /* filled to x = 4+1+49 */
    CHECK(px[10 * 120 + 55] == C_WHITE && px[10 * 120 + 104] == C_WHITE);         /* empty remainder */
    classic_bar_paint(&fb, 4, 4, 102, 12, 0);
    CHECK(px[10 * 120 + 5] == C_WHITE);
    classic_bar_paint(&fb, 4, 4, 102, 12, 1000);
    CHECK(px[10 * 120 + 104] == C_BLACK);
}

static void test_panel_frame(void) {
    ClassicRunProg p;
    classic_prog_init(&p);
    classic_prog_add(&p, 1000000); classic_prog_add(&p, 1000000);
    classic_prog_stage(&p, 0, CS_DOWNLOADING);
    classic_prog_bytes(&p, 0, 400, 1000);                                          /* item 0 at 40% of 800 = 320 */
    int idx[2] = {0, 1};
    classic_apps_panel_test_seed(&p, idx, 2, 1);
    Window w = {0};
    w.kind = WIN_TEXT; w.w = 540; w.h = 440; w.shade_h = 440; w.x = 10; w.y = 10;
    snprintf(w.rel, sizeof w.rel, ":classicapps");
    Layout lay;
    window_layout(&w, &lay);
    static uint32_t px[1024 * 768];
    Fb fb;
    fb_init(&fb, px, 1024, 768);
    CHECK(classic_apps_panel_paint(&fb, &w, &lay) == 1);
    Rect b = classic_apps_panel_row_bar(&lay, 0), t = classic_apps_panel_total_bar(&lay);
    int fill = classic_bar_fill_width(b.w, 320);
    int y = b.y + b.h / 2;
    CHECK(fill > 50 && px[y * 1024 + b.x] == C_BLACK);
    CHECK(px[y * 1024 + b.x + fill] == C_BLACK && px[y * 1024 + b.x + fill + 1] == C_WHITE);   /* row 0 fill edge */
    Rect b1 = classic_apps_panel_row_bar(&lay, 1);
    CHECK(px[(b1.y + b1.h / 2) * 1024 + b1.x + 1] == C_WHITE);                                  /* row 1 is waiting: empty */
    int total = classic_total_permille(&p), tfill = classic_bar_fill_width(t.w, total);
    int ty = t.y + t.h / 2;
    CHECK(total == 160 && tfill > 0);
    CHECK(px[ty * 1024 + t.x + tfill] == C_BLACK && px[ty * 1024 + t.x + tfill + 1] == C_WHITE);   /* total fill edge */
    /* finished: both done, the total bar is full */
    classic_prog_stage(&p, 0, CS_DONE); classic_prog_stage(&p, 1, CS_DONE);
    classic_apps_panel_test_seed(&p, idx, 2, 0);
    CHECK(classic_apps_panel_paint(&fb, &w, &lay) == 1);
    CHECK(px[ty * 1024 + t.x + t.w - 2] == C_BLACK);
    classic_apps_panel_test_seed(&(ClassicRunProg){0}, idx, 0, 0);
}

typedef struct { int seq[16], n; uint64_t last_got, last_total; atomic_bool cancel; int cancel_on_bytes; } Rec;
static void rec_stage(void *c, ClassicStage st) { Rec *r = c; if (r->n < 16) r->seq[r->n++] = (int)st; }
static void rec_bytes(void *c, uint64_t got, uint64_t total) {
    Rec *r = c;
    r->last_got = got; r->last_total = total;
    if (r->cancel_on_bytes) atomic_store(&r->cancel, 1);
}
static int bad_fetch(const ClassicApp *app, const char *dest, const atomic_bool *cancel, void *ctx, char *err, size_t en) {
    (void)app; (void)cancel; (void)ctx; (void)err; (void)en;
    FILE *f = fopen(dest, "wb");
    if (f) { fputs("not what the catalogue expects", f); fclose(f); }
    return 0;
}

static void test_pipeline(const char *root, const char *scratch) {
    char src[OSPI_PATH * 2], file[OSPI_PATH * 2], rel[OSPI_PATH], err[300];
    snprintf(src, sizeof src, "%s/Folder One", scratch);
    CHECK(mkdir(src, 0755) == 0);
    snprintf(file, sizeof file, "%s/data", src);
    FILE *f = fopen(file, "wb");
    for (int i = 0; f && i < 300000; i++) fputc('a' + i % 26, f);
    if (f) fclose(f);
    Rec rec = {0};
    ClassicHooks h = {.stage = rec_stage, .bytes = rec_bytes, .ctx = &rec, .cancel = &rec.cancel};
    CHECK(classic_install_local(root, src, &h, rel, sizeof rel, err, sizeof err) == CA_OK);
    CHECK(rec.n == 1 && rec.seq[0] == CS_UNPACKING);                           /* a folder is not verified */
    CHECK(rec.last_got == 300000 && rec.last_total == 300000);                 /* byte progress reached the end */
    /* Stop part-way through a folder copy: cancelled, and the half-copied folder is removed */
    snprintf(src, sizeof src, "%s/Folder Two", scratch);
    CHECK(mkdir(src, 0755) == 0);
    snprintf(file, sizeof file, "%s/data", src);
    f = fopen(file, "wb");
    for (int i = 0; f && i < 300000; i++) fputc('b', f);
    if (f) fclose(f);
    Rec stop = {.cancel_on_bytes = 1};
    ClassicHooks hs = {.stage = rec_stage, .bytes = rec_bytes, .ctx = &stop, .cancel = &stop.cancel};
    char dest[OSPI_PATH * 2];
    snprintf(dest, sizeof dest, "%s/Applications/Folder Two", root);
    CHECK(classic_install_local(root, src, &hs, rel, sizeof rel, err, sizeof err) == CA_CANCELLED);
    CHECK(access(dest, F_OK) != 0);
    /* a download that fails verification goes Downloading -> Verifying and stops there */
    ClassicApp app = {0};
    snprintf(app.id, sizeof app.id, "prog-test");
    snprintf(app.name, sizeof app.name, "Prog Test");
    snprintf(app.page, sizeof app.page, "https://macintoshgarden.org/apps/none");
    snprintf(app.dest, sizeof app.dest, "Applications");
    snprintf(app.md5, sizeof app.md5, "00000000000000000000000000000000");
    app.wrap = 1;
    Rec bad = {0};
    ClassicHooks hb = {.stage = rec_stage, .bytes = rec_bytes, .ctx = &bad, .fetch = bad_fetch};
    CHECK(classic_install(root, &app, &hb, rel, sizeof rel, err, sizeof err) == CA_HASH);
    CHECK(bad.n == 2 && bad.seq[0] == CS_DOWNLOADING && bad.seq[1] == CS_VERIFYING);
}

int main(void) {
    char tmpl[] = "/tmp/os999-cprogXXXXXX", scratch[OSPI_PATH * 2], root[OSPI_PATH];
    char *base = mkdtemp(tmpl);
    CHECK(base != NULL);
    if (!base) return 1;
    snprintf(root, sizeof root, "%s/vol", base);
    snprintf(scratch, sizeof scratch, "%s/scratch", base);
    CHECK(mkdir(scratch, 0755) == 0);
    CHECK(volume_seed(root) == 0);
    os999_private_set_root(root);
    test_maths();
    test_transitions();
    test_labels();
    test_wrap();
    test_bar();
    test_panel_frame();
    test_pipeline(root, scratch);
    char cmd[OSPI_PATH * 2];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", base);
    if (fails) { fprintf(stderr, "%d classic progress checks failed\n", fails); return 1; }
    printf("classic progress checks passed\n");
    return 0;
}
