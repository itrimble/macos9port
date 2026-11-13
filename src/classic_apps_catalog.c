/* The built-in Install Classic Apps catalogue. Metadata only: names, public Macintosh Garden page
 * URLs, archive file names and the MD5 values those pages print (read 12 Nov 2026, page text only;
 * no Apple software was downloaded to write this). Nothing here is, or contains, Apple software. */
#include "classic_apps.h"
#include "ospi.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *classic_catalog_default_text(void) {
    return
"# Install Classic Apps catalogue. Third-party software is fetched into your own volume, never bundled.\n"
"[quicktime-403]\n"
"name = QuickTime 4.0.3 (68K and PowerPC)\n"
"page = https://macintoshgarden.org/apps/quicktime-4\n"
"file = QT_403_68k_Mac.sit\n"
"md5 = 0ad325554dfcf2950f08a7093690d417\n"
"sha256 = 18ae45e2fbfa20f04a28c0d3fd648c9d40e7bd7ff7769971184580cd843d3b37\n"
"size = 8688785\n"
"size_mb = 8.3\n"
"dest = Applications\n"
"wrap = yes\n"
"default = yes\n"
"note = Apple's QuickTime installer for System 7 to Mac OS 9. It is only unpacked here, never run.\n"
"\n"
"[quicktime-412-ppc]\n"
"name = QuickTime 4.1.2 (PowerPC)\n"
"page = https://macintoshgarden.org/apps/quicktime-4\n"
"file = QT_412_PPC_Mac.sit\n"
"md5 = 80ad3ec00afdceceac21586ab81d0de2\n"
"size_mb = 7.6\n"
"dest = Applications\n"
"wrap = yes\n"
"default = no\n"
"note = Apple's PowerPC-only QuickTime installer. It is only unpacked here, never run.\n"
"\n"
"[teachtext-72]\n"
"name = TeachText 7.2\n"
"page = https://macintoshgarden.org/apps/teachtext\n"
"file = teachtext-7.2.sit_.hqx\n"
"md5 = 0f23ad87d34a64a0d2aed2027a7af088\n"
"size_mb = 0.04\n"
"dest = Applications\n"
"wrap = yes\n"
"default = yes\n"
"note = Apple's plain text viewer, the ancestor of SimpleText.\n"
"\n"
/* TODO(Ian): SimpleText has no standalone Garden page (it ships inside system releases). Add the page
 * URL here once one is chosen and verified; until then it can be installed from a local source. */
"[simpletext]\n"
"name = SimpleText\n"
"page =\n"
"dest = Applications\n"
"wrap = yes\n"
"default = no\n"
"note = No download page chosen yet: use Install from a folder or archive to add your own copy.\n";
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int valid_id(const char *id) {
    size_t n = strlen(id);
    if (!n || n >= sizeof((ClassicApp *)0)->id) return 0;
    for (size_t i = 0; i < n; i++)
        if (!islower((unsigned char)id[i]) && !isdigit((unsigned char)id[i]) && id[i] != '-') return 0;
    return 1;
}

static int hex_ok(const char *s, size_t want) {
    if (!*s) return 1;
    if (strlen(s) != want) return 0;
    for (; *s; s++) if (!isxdigit((unsigned char)*s)) return 0;
    return 1;
}

static int dest_ok(const char *d) {
    if (!d[0]) return 0;
    const char *p = d;
    while (*p) {
        const char *q = strchr(p, ':');
        size_t n = q ? (size_t)(q - p) : strlen(p);
        if (!n || n > 31 || (n == 2 && !strncmp(p, "..", 2)) || (n == 1 && *p == '.')) return 0;
        for (size_t i = 0; i < n; i++) if (p[i] == '/' || p[i] == '\\' || (unsigned char)p[i] < 32) return 0;
        p += n + (q ? 1 : 0);
        if (q && !*p) return 0;
    }
    return 1;
}

static int set_field(ClassicApp *a, const char *key, const char *val, char *why, size_t wn) {
#define STR(k, f) if (!strcmp(key, k)) { if (strlen(val) >= sizeof a->f) { snprintf(why, wn, "%s is too long", k); return -1; } \
                                         snprintf(a->f, sizeof a->f, "%s", val); return 0; }
    STR("name", name) STR("page", page) STR("file", file) STR("dest", dest) STR("size_mb", size_mb) STR("note", note)
    if (!strcmp(key, "md5") || !strcmp(key, "sha256")) {
        int is_md5 = key[0] == 'm';
        if (!hex_ok(val, is_md5 ? 32 : 64)) { snprintf(why, wn, "%s is not a hex digest", key); return -1; }
        char *dst = is_md5 ? a->md5 : a->sha256;
        for (size_t i = 0; i <= strlen(val); i++) dst[i] = (char)tolower((unsigned char)val[i]);
        return 0;
    }
    if (!strcmp(key, "size")) {
        char *end;
        unsigned long long v = strtoull(val, &end, 10);
        if (*val && (*end || v > 4000000000ull)) { snprintf(why, wn, "size must be a byte count"); return -1; }
        a->size = v;
        return 0;
    }
    if (!strcmp(key, "wrap") || !strcmp(key, "default")) {
        int yes = !strcasecmp(val, "yes");
        if (!yes && strcasecmp(val, "no")) { snprintf(why, wn, "%s must be yes or no", key); return -1; }
        if (key[0] == 'w') a->wrap = yes; else a->default_on = yes;
        return 0;
    }
    snprintf(why, wn, "unknown key \"%s\"", key);
    return -1;
#undef STR
}

static int finish_entry(const ClassicApp *a, const ClassicCatalog *c, int line, char *err, size_t en) {
    const char *why = NULL;
    if (!a->name[0]) why = "missing name";
    else if (!dest_ok(a->dest)) why = "missing or unsafe dest";
    else if (a->page[0] && !garden_page_ok(a->page)) why = "page must be a macintoshgarden.org URL";
    else if (strchr(a->file, '/') || strchr(a->file, '\\') || strstr(a->file, "..")) why = "file must be a plain file name";
    else if (classic_catalog_find(c, a->id)) why = "duplicate id";
    if (why && err) snprintf(err, en, "catalogue line %d (%s): %s", line, a->id, why);
    return why ? -1 : 0;
}

int classic_catalog_parse(const char *text, ClassicCatalog *out, char *err, size_t err_n) {
    if (err && err_n) err[0] = 0;
    if (!text || !out) return -1;
    memset(out, 0, sizeof *out);
    char line[600], why[96];
    ClassicApp cur;
    int have = 0, lineno = 0;
    const char *p = text;
    while (*p || have) {
        size_t n = strcspn(p, "\n");
        int end = !*p;
        if (n >= sizeof line) { if (err) snprintf(err, err_n, "catalogue line %d is too long", lineno + 1); return -1; }
        memcpy(line, p, n);
        line[n] = 0;
        p += n + (p[n] ? 1 : 0);
        lineno++;
        char *s = trim(line);
        if (end) s = (char *)"[";   /* flush the last block */
        if (!*s || *s == '#') { if (end) break; continue; }
        if (*s == '[') {
            if (have) {
                if (out->n >= CLASSIC_MAX_APPS) { if (err) snprintf(err, err_n, "too many catalogue items"); return -1; }
                if (finish_entry(&cur, out, lineno, err, err_n)) return -1;
                out->app[out->n++] = cur;
            }
            have = 0;
            if (end) break;
            char *close = strchr(s, ']');
            if (!close || close[1] || close - s < 2) { if (err) snprintf(err, err_n, "catalogue line %d: bad header", lineno); return -1; }
            *close = 0;
            memset(&cur, 0, sizeof cur);
            if (!valid_id(s + 1) || strlen(s + 1) >= sizeof cur.id) { if (err) snprintf(err, err_n, "catalogue line %d: bad id", lineno); return -1; }
            snprintf(cur.id, sizeof cur.id, "%s", s + 1);
            cur.wrap = 1;
            have = 1;
            continue;
        }
        char *eq = strchr(s, '=');
        if (!have || !eq) { if (err) snprintf(err, err_n, "catalogue line %d: expected key = value", lineno); return -1; }
        *eq = 0;
        if (set_field(&cur, trim(s), trim(eq + 1), why, sizeof why)) {
            if (err) snprintf(err, err_n, "catalogue line %d: %s", lineno, why);
            return -1;
        }
    }
    return out->n;
}

int classic_catalog_default(ClassicCatalog *out) {
    char err[160];
    return classic_catalog_parse(classic_catalog_default_text(), out, err, sizeof err);
}

const ClassicApp *classic_catalog_find(const ClassicCatalog *c, const char *id) {
    for (int i = 0; c && id && i < c->n; i++) if (!strcmp(c->app[i].id, id)) return &c->app[i];
    return NULL;
}
