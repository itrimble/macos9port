#include "telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "term.h"

#define TEL_MAX_BYTES (16L * 1024 * 1024)

typedef struct {
    int valid, nwin, menu, dialog, desktop_focus, guest, browser;
    char front[OSPI_NAME], dialog_title[64], guest_title[OSPI_NAME];
} Snap;

static FILE *g_fp;
static char g_path[1024], g_override[1024];
static int g_off = -1, g_tried;
static long g_seq;
static Snap g_last;

/* Consent: a one-byte file beside the logs. Absent means off, so a new user
 * records nothing until they choose Help, Record Local Usage Log. */
static int g_on, g_loaded;
static int consent_path(char *out, size_t cap) {
    const char *home = getenv("HOME");
    return home && snprintf(out, cap, "%s/Library/Application Support/OS999/Telemetry/consent", home) < (int)cap;
}
void telemetry_start(void) {
    if (g_loaded) return;
    g_loaded = 1;
    char p[1024];
    g_on = consent_path(p, sizeof p) && access(p, F_OK) == 0;
    menu_set_usage_log(g_on);
}
int telemetry_consent(void) { telemetry_start(); return g_on; }
int telemetry_set_consent(int on) {
    char p[1024];
    if (!consent_path(p, sizeof p)) return -1;
    if (on) {
        char dir[1024]; snprintf(dir, sizeof dir, "%s", p);
        char *slash = strrchr(dir, '/'); *slash = 0;
        for (char *c = dir + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(dir, 0700); *c = '/'; }
        mkdir(dir, 0700);
        FILE *f = fopen(p, "w");
        if (!f) return -1;
        fputs("1\n", f); fclose(f);
    } else {
        if (unlink(p) != 0 && access(p, F_OK) == 0) return -1;
        telemetry_close();
    }
    g_loaded = 1; g_on = on != 0; g_last.valid = 0;
    menu_set_usage_log(g_on);
    return 0;
}
int telemetry_enabled(void) {
    if (g_off < 0) { const char *e = getenv("OS999_TELEMETRY"); g_off = e && !strcmp(e, "0"); }
    return g_on && !g_off;
}

void telemetry_set_path(const char *path) {
    telemetry_close();
    g_override[0] = 0;
    if (path) { snprintf(g_override, sizeof g_override, "%s", path); g_on = 1; }
    g_tried = 0; g_seq = 0; g_last.valid = 0;
}

void telemetry_close(void) { if (g_fp) fclose(g_fp); g_fp = NULL; g_tried = 0; }

static void mkdirs(char *p) {
    for (char *c = p + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(p, 0700); *c = '/'; }
}

static FILE *open_log(void) {
    if (g_fp) return g_fp;
    if (g_tried || !telemetry_enabled()) return NULL;
    g_tried = 1;
    if (g_override[0]) snprintf(g_path, sizeof g_path, "%s", g_override);
    else {
        const char *home = getenv("HOME");
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        if (!home || snprintf(g_path, sizeof g_path,
                "%s/Library/Application Support/OS999/Telemetry/events-%04d%02d%02d.jsonl",
                home, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) >= (int)sizeof g_path) return NULL;
    }
    char dir[1024]; snprintf(dir, sizeof dir, "%s", g_path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdirs(dir); mkdir(dir, 0700); }
    struct stat st;
    if (!stat(g_path, &st) && st.st_size > TEL_MAX_BYTES) {   /* keep one rotated copy */
        char old[1100]; snprintf(old, sizeof old, "%s.1", g_path); rename(g_path, old);
    }
    g_fp = fopen(g_path, "a");
    if (g_fp) {
        setvbuf(g_fp, NULL, _IOLBF, 0);
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(g_fp, "{\"seq\":0,\"t\":%lld.%03ld,\"ev\":\"session\",\"pid\":%ld}\n",
                (long long)ts.tv_sec, ts.tv_nsec / 1000000, (long)getpid());
    }
    return g_fp;
}

static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 32) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void head(FILE *f, const char *ev) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    fprintf(f, "{\"seq\":%ld,\"t\":%lld.%03ld,\"ev\":\"%s\"", ++g_seq,
            (long long)ts.tv_sec, ts.tv_nsec / 1000000, ev);
}

static void snap_of(const Session *s, Snap *o) {
    memset(o, 0, sizeof *o);
    o->valid = 1; o->nwin = s->nwin; o->menu = s->menu; o->dialog = s->dialog;
    o->desktop_focus = s->desktop_focus; o->guest = s->guest_active; o->browser = s->browser_active;
    if (s->nwin > 0 && s->nwin <= OSPI_MAX_WIN) snprintf(o->front, sizeof o->front, "%s", s->win[s->nwin - 1].title);
    if (s->dialog) snprintf(o->dialog_title, sizeof o->dialog_title, "%s", s->dialog_title);
    if (s->guest_active) snprintf(o->guest_title, sizeof o->guest_title, "%s", s->guest_title);
}

void telemetry_click(Session *s, int x, int y, int button, int clicks) {
    FILE *f = open_log(); if (!f || !s) return;
    head(f, "click");
    fprintf(f, ",\"x\":%d,\"y\":%d,\"button\":%d,\"clicks\":%d", x, y, button, clicks);
    if (y >= 0 && y < TITLE_H) {
        int m = menu_title_at(x, y);
        if (m >= 0) { fputs(",\"menu_title\":", f); jstr(f, menu_title(m)); }
        else fputs(",\"target\":\"menubar\"", f);
    } else if (s->dialog) {
        fputs(",\"target\":\"dialog\",\"dialog\":", f); jstr(f, s->dialog_title);
    } else if (s->menu >= 0) {
        fputs(",\"target\":\"open_menu\"", f);
    } else {
        Window *w = s->nwin > 0 ? &s->win[s->nwin - 1] : NULL;
        Layout lay = {0};
        if (w) window_layout(w, &lay);
        if (w && x >= lay.outer.x && x < lay.outer.x + lay.outer.w && y >= lay.outer.y && y < lay.outer.y + lay.outer.h) {
            fputs(",\"target\":\"window\",\"window\":", f); jstr(f, w->title);
            int i = item_index_at(w, x, y);
            if (i >= 0 && i < w->nitems) { fputs(",\"item\":", f); jstr(f, w->items[i].name); }
        } else {
            int i = item_index_at(&s->desktop, x, y);
            fputs(",\"target\":\"desktop\"", f);
            if (i >= 0 && i < s->desktop.nitems) { fputs(",\"item\":", f); jstr(f, s->desktop.items[i].name); }
        }
    }
    fputs("}\n", f);
}

void telemetry_key(Session *s, int key, int mods) {
    FILE *f = open_log(); if (!f || !s) return;
    int plain = key >= 32 && key < 127 && !(mods & (MOD_CMD | MOD_OTHER | TERM_MOD_CTRL | TERM_MOD_ALT));
    head(f, "key");
    if (plain) fputs(",\"kind\":\"char\"", f);   /* never record what was typed */
    else fprintf(f, ",\"key\":%d,\"mods\":%d", key, mods);
    fputs("}\n", f);
}

void telemetry_command(Session *s, int cmd) {
    FILE *f = open_log(); if (!f || !s) return;
    head(f, "command");
    fprintf(f, ",\"cmd\":%d", cmd);
    if (s->menu >= 0) { fputs(",\"menu\":", f); jstr(f, menu_title(s->menu)); }
    fputs("}\n", f);
}

void telemetry_frame(Session *s) {
    if (!s || !telemetry_enabled()) return;
    Snap n; snap_of(s, &n);
    if (g_last.valid && !memcmp(&n, &g_last, sizeof n)) return;
    FILE *f = open_log(); if (!f) return;
    if (g_last.valid) {
        head(f, "state");
        fprintf(f, ",\"windows\":%d,\"front\":", n.nwin); jstr(f, n.front);
        fprintf(f, ",\"menu\":%d,\"dialog\":", n.menu);
        if (n.dialog) jstr(f, n.dialog_title); else fputs("null", f);
        fprintf(f, ",\"guest_app\":"); if (n.guest) jstr(f, n.guest_title); else fputs("null", f);
        fprintf(f, ",\"browser\":%d,\"desktop_focus\":%d}\n", n.browser, n.desktop_focus);
    }
    g_last = n;
}
