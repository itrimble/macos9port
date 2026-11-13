/* Install Classic Apps: progress maths, labels, wrapping and the progress bar. See classic_progress.h. */
#include "classic_progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t classic_item_weight(const ClassicApp *app) {
    if (!app) return CP_DEFAULT_WEIGHT;
    if (app->size) return app->size;
    double mb = app->size_mb[0] ? atof(app->size_mb) : 0;
    return mb > 0 ? (uint64_t)(mb * 1048576.0) : CP_DEFAULT_WEIGHT;
}

void classic_prog_init(ClassicRunProg *p) { if (p) memset(p, 0, sizeof *p); }

int classic_prog_add(ClassicRunProg *p, uint64_t weight) {
    if (!p || p->n >= CLASSIC_MAX_APPS + 1) return -1;
    ClassicItemProg *it = &p->it[p->n];
    memset(it, 0, sizeof *it);
    it->weight = weight ? weight : CP_DEFAULT_WEIGHT;
    return p->n++;
}

static int final_stage(ClassicStage s) { return s == CS_DONE || s == CS_FAILED || s == CS_STOPPED; }
static int valid(const ClassicRunProg *p, int i) { return p && i >= 0 && i < p->n; }

void classic_prog_stage(ClassicRunProg *p, int i, ClassicStage st) {
    if (!valid(p, i)) return;
    ClassicItemProg *it = &p->it[i];
    if (final_stage(it->stage) || (st < it->stage && !final_stage(st))) return;
    if (st != it->stage) { it->got = 0; it->total = 0; }
    it->stage = st;
}

void classic_prog_bytes(ClassicRunProg *p, int i, uint64_t got, uint64_t total) {
    if (!valid(p, i) || final_stage(p->it[i].stage)) return;
    p->it[i].got = got;
    p->it[i].total = total;
}

void classic_short_reason(const char *err, char *out, size_t n) {
    if (!out || !n) return;
    out[0] = 0;
    if (!err) return;
    while (*err == ' ') err++;
    size_t len = 0;
    while (err[len] && err[len] != '.' && err[len] != '\n') len++;
    while (len && err[len - 1] == ' ') len--;
    if (len >= n) {
        len = n > 4 ? n - 4 : 0;
        snprintf(out, n, "%.*s...", (int)len, err);
        return;
    }
    snprintf(out, n, "%.*s", (int)len, err);
}

void classic_prog_fail(ClassicRunProg *p, int i, const char *err) {
    if (!valid(p, i) || final_stage(p->it[i].stage)) return;
    classic_short_reason(err && *err ? err : "the install did not finish", p->it[i].reason, sizeof p->it[i].reason);
    classic_prog_stage(p, i, CS_FAILED);
}

void classic_prog_stop_rest(ClassicRunProg *p) {
    if (!p) return;
    for (int i = 0; i < p->n; i++) classic_prog_stage(p, i, CS_STOPPED);
}

static int frac(uint64_t got, uint64_t total, int span) {
    if (!total) return 0;
    if (got > total) got = total;
    return (int)((got * (uint64_t)span) / total);
}

int classic_item_permille(const ClassicItemProg *it) {
    if (!it) return 0;
    switch (it->stage) {
    case CS_DOWNLOADING: case CS_COPYING: return frac(it->got, it->total, CP_DL_END);
    case CS_VERIFYING: return CP_DL_END;
    case CS_UNPACKING: return CP_VERIFY_END + frac(it->got, it->total, CP_FULL - CP_VERIFY_END);
    case CS_DONE: return CP_FULL;
    default: return 0;   /* WAITING, FAILED and STOPPED show an empty bar */
    }
}

/* Failed items stop mattering to the total, so it still reaches 100% when nothing was stopped. */
int classic_total_permille(const ClassicRunProg *p) {
    if (!p || !p->n) return 0;
    unsigned long long num = 0, den = 0;
    for (int i = 0; i < p->n; i++) {
        const ClassicItemProg *it = &p->it[i];
        int pm = it->stage == CS_FAILED ? CP_FULL : classic_item_permille(it);
        num += (unsigned long long)pm * it->weight;
        den += it->weight;
    }
    return den ? (int)(num / den) : 0;
}

int classic_prog_finished(const ClassicRunProg *p) {
    int k = 0;
    for (int i = 0; p && i < p->n; i++) k += final_stage(p->it[i].stage);
    return k;
}
int classic_prog_all_final(const ClassicRunProg *p) { return p && p->n > 0 && classic_prog_finished(p) == p->n; }

static void size_text(char *out, size_t n, uint64_t got, uint64_t total) {
    if (total && total < 1048576) snprintf(out, n, "%llu of %llu KB", (unsigned long long)(got >> 10), (unsigned long long)(total >> 10));
    else if (total) snprintf(out, n, "%.1f of %.1f MB", (double)got / 1048576.0, (double)total / 1048576.0);
    else snprintf(out, n, "%.1f MB", (double)got / 1048576.0);
}

void classic_stage_label(const ClassicItemProg *it, char *out, size_t n) {
    char sz[48];
    if (!out || !n) return;
    out[0] = 0;
    if (!it) return;
    switch (it->stage) {
    case CS_WAITING: snprintf(out, n, "Waiting"); break;
    case CS_DOWNLOADING: size_text(sz, sizeof sz, it->got, it->total); snprintf(out, n, "Downloading %s", sz); break;
    case CS_COPYING: size_text(sz, sizeof sz, it->got, it->total); snprintf(out, n, "Copying %s", sz); break;
    case CS_VERIFYING: snprintf(out, n, "Verifying"); break;
    case CS_UNPACKING: snprintf(out, n, "Unpacking"); break;
    case CS_DONE: snprintf(out, n, "Installed"); break;
    case CS_FAILED: snprintf(out, n, "Failed: %s", it->reason[0] ? it->reason : "not installed"); break;
    case CS_STOPPED: snprintf(out, n, "Stopped"); break;
    }
}

void classic_total_label(const ClassicRunProg *p, char *out, size_t n) {
    if (!out || !n) return;
    out[0] = 0;
    if (!p || !p->n) return;
    int pct = classic_total_permille(p) / 10, fin = classic_prog_finished(p);
    if (!classic_prog_all_final(p)) {
        snprintf(out, n, "Installing %d of %d, %d%%", fin + 1 > p->n ? p->n : fin + 1, p->n, pct);
        return;
    }
    int stopped = 0;
    for (int i = 0; i < p->n; i++) stopped += p->it[i].stage == CS_STOPPED;
    snprintf(out, n, stopped ? "Stopped at %d%%" : "Finished, %d%%", pct);
}

void classic_summary(const ClassicRunProg *p, char *out, size_t n) {
    int done = 0, bad = 0, stopped = 0;
    if (!out || !n) return;
    out[0] = 0;
    if (!p || !p->n) return;
    for (int i = 0; i < p->n; i++) {
        done += p->it[i].stage == CS_DONE;
        bad += p->it[i].stage == CS_FAILED;
        stopped += p->it[i].stage == CS_STOPPED;
    }
    if (stopped) snprintf(out, n, "Stopped. %d of %d installed; anything half-finished was removed.", done, p->n);
    else if (bad) snprintf(out, n, "%d of %d installed, %d failed.", done, p->n, bad);
    else snprintf(out, n, "Done. %d of %d installed. The files are in your volume and were not run.", done, p->n);
}

/* ---- wrapping ---- */

static void put_line(ClassicLine *l, const char *s, int len, int ellipsis, ClassicMeasure m, int max_w) {
    if (len > (int)sizeof l->t - 4) len = (int)sizeof l->t - 4;
    if (ellipsis) {
        while (len > 0 && m(s, len) + m("...", 3) > max_w) len--;
        while (len > 0 && s[len - 1] == ' ') len--;
        snprintf(l->t, sizeof l->t, "%.*s...", len, s);
    } else {
        snprintf(l->t, sizeof l->t, "%.*s", len, s);
    }
}

int classic_wrap(const char *s, int max_w, ClassicMeasure m, ClassicLine *out, int max_lines) {
    int lines = 0;
    if (!s || !m || !out || max_lines < 1 || max_w < 1) return 0;
    while (*s == ' ') s++;
    while (*s && lines < max_lines) {
        int len = 0, brk = 0, total = (int)strlen(s);
        /* longest prefix that fits, remembering the last word end inside it */
        while (len < total && len < (int)sizeof out->t - 4 && m(s, len + 1) <= max_w) {
            len++;
            if (s[len] == ' ' || s[len] == 0) brk = len;
        }
        if (len == 0) len = 1;                       /* one glyph wider than the line: take it anyway */
        int end = len == total ? total : brk ? brk : len;
        const char *rest = s + end;
        while (*rest == ' ') rest++;
        if (*rest && lines == max_lines - 1) { put_line(&out[lines++], s, total, 1, m, max_w); return lines; }
        put_line(&out[lines++], s, end, 0, m, max_w);
        s = rest;
    }
    return lines;
}

/* ---- the bar ---- */

int classic_bar_fill_width(int w, int permille) {
    int inside = w - 2;
    if (inside < 1) return 0;
    if (permille < 0) permille = 0;
    if (permille > CP_FULL) permille = CP_FULL;
    return inside * permille / CP_FULL;
}

void classic_bar_paint(Fb *fb, int x, int y, int w, int h, int permille) {
    if (!fb || w < 3 || h < 3) return;
    fb_fill(fb, x, y, w, h, C_WHITE);
    int fill = classic_bar_fill_width(w, permille);
    if (fill > 0) fb_fill(fb, x + 1, y + 1, fill, h - 2, C_BLACK);
    fb_frame(fb, x, y, w, h, C_BLACK);
}
