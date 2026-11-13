/* Install Classic Apps: fetch or copy third-party classic software into the user's volume.
 * Rules: verify before touching the volume, refuse when the disk cannot hold it, all-or-nothing
 * expansion through the storage stack, receipts in .os999/installs, and nothing is ever executed. */
#include "classic_apps.h"
#include "install_int.h"
#include "ospi.h"
#include "volume_capacity.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FAIL(code, ...) do { if (err && err_n) snprintf(err, err_n, __VA_ARGS__); return (code); } while (0)

static int within(const char *root, const char *path) {
    size_t n = strlen(root);
    return !strncmp(root, path, n) && (path[n] == 0 || path[n] == '/');
}

int classic_dest_rel(const char *root, const char *dest, char *rel, size_t n) {
    if (!root || !dest || !rel || !n) return -1;
    if (!strcmp(dest, "Applications"))
        return volume_special_folder(root, "applications_folder", "Applications", rel, n);
    size_t k = 0;
    for (const char *p = dest; *p; p++) {
        if (*p == '/' || *p == '\\' || k + 1 >= n) return -1;
        rel[k++] = *p == ':' ? '/' : *p;
    }
    rel[k] = 0;
    char full[OSPI_PATH];
    return volume_join(full, sizeof full, root, rel);   /* rejects "..", "." and empty parts */
}

static int receipt_path(const char *root, const char *id, char *out, size_t n) {
    char dir[OSPI_PATH + 32];
    os999_private_set_root(root);
    if (os999_private_dir("installs", dir, sizeof dir)) return -1;
    int k = snprintf(out, n, "%s/%s.receipt", dir, id);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

static int receipt_dest(const char *root, const char *id, char *dest, size_t n) {
    char path[OSPI_PATH + 64], line[OSPI_PATH + 16];
    if (receipt_path(root, id, path, sizeof path)) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int found = -1;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "dest_rel=", 9)) {
            line[strcspn(line, "\n")] = 0;
            snprintf(dest, n, "%s", line + 9);
            found = 0;
        }
    fclose(f);
    return found;
}

int classic_installed(const char *root, const ClassicApp *app) {
    char rel[OSPI_PATH], full[OSPI_PATH];
    struct stat st;
    return root && app && !receipt_dest(root, app->id, rel, sizeof rel) && !volume_join(full, sizeof full, root, rel) &&
           !lstat(full, &st) && S_ISDIR(st.st_mode);
}

static int write_receipt(const char *root, const ClassicApp *app, const char *source, const char *leaf,
                         uint64_t size, const char *md5, const char *sha, int verified, const char *dest_rel) {
    char path[OSPI_PATH + 64], tmp[OSPI_PATH + 72];
    if (receipt_path(root, app->id, path, sizeof path)) return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "id=%s\nname=%s\nsource=%s\narchive=%s\nsize=%llu\nmd5=%s\nsha256=%s\nverified=%s\n"
               "dest_rel=%s\ninstalled=%lld\nexecuted=no\n",
            app->id, app->name, source, leaf, (unsigned long long)size, md5, sha, verified ? "yes" : "no",
            dest_rel, (long long)time(NULL));
    if (fclose(f) != 0 || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ---- Macintosh Garden transport ---- */

int classic_pick_named(const char *html, const char *leaf, char *out, size_t n) {
    static const char *hosts[] = {"download.macintoshgarden.org/apps/", "old.mac.gdn/apps/", "gardenmirror.oldapplestuff.com/apps/"};
    for (int h = 0; h < 3; h++) {
        char needle[160];
        snprintf(needle, sizeof needle, "%s%s", hosts[h], leaf);
        for (const char *p = html; (p = strstr(p, needle)) != NULL; p += strlen(needle)) {
            char c = p[strlen(needle)];
            if (c != '"' && c != '\'' && c != '?' && c != '&') continue;   /* whole file name only */
            const char *end = p + strlen(needle);
            while (*end && *end != '"' && *end != '\'' && *end != '<' && *end != ' ' && *end != '\n') end++;
            int k = snprintf(out, n, "https://%.*s", (int)(end - p), p);
            if (k <= 0 || (size_t)k >= n) return -1;
            for (char *a; (a = strstr(out, "&amp;")) != NULL;) memmove(a + 1, a + 5, strlen(a + 5) + 1);
            return install_download_url_ok(out) ? 0 : -1;
        }
    }
    return -1;
}

static int fetch_garden(const ClassicApp *app, const char *dest, const atomic_bool *cancel, void *ctx, char *err, size_t en) {
    (void)ctx;
    char page_tmp[OSPI_PATH], url[2048];
    int fd = install_tempfile(page_tmp, sizeof page_tmp, "classicpage");
    if (fd < 0) { snprintf(err, en, "Could not start the download."); return -1; }
    close(fd);
    install_fetch_set_cancel(cancel);
    int bad = install_fetch_page(app->page, page_tmp, 60000);
    char *html = NULL;
    if (!bad) {
        FILE *f = fopen(page_tmp, "rb");
        if (f) {
            html = calloc(1, 2000001);
            if (html) fread(html, 1, 2000000, f);
            fclose(f);
        }
    }
    unlink(page_tmp);
    if (bad || !html) { free(html); install_fetch_set_cancel(NULL); snprintf(err, en, "Could not open the Macintosh Garden page."); return -1; }
    int found = app->file[0] ? classic_pick_named(html, app->file, url, sizeof url) : garden_pick_url(html, url, sizeof url);
    free(html);
    if (found) { install_fetch_set_cancel(NULL); snprintf(err, en, "The page no longer lists that download."); return -1; }
    bad = install_fetch_archive(app->page, url, dest);
    install_fetch_set_cancel(NULL);
    if (bad) snprintf(err, en, "Could not download the archive.");
    return bad ? -1 : 0;
}

/* ---- local sources ---- */

enum { COPY_MAX_ENTRIES = 20000, COPY_MAX_DEPTH = 12 };
typedef struct { const ClassicHooks *h; uint64_t done, total; } CopyProg;
static int cancelled(const ClassicHooks *h) { return h && h->cancel && atomic_load(h->cancel); }

static int copy_plain_file(const char *src, const char *dst, CopyProg *cp) {
    FILE *in = fopen(src, "rb"), *out = in ? fopen(dst, "wbx") : NULL;
    char buf[65536];
    size_t got;
    int ok = in && out;
    while (ok && (got = fread(buf, 1, sizeof buf, in)) > 0) {
        ok = fwrite(buf, 1, got, out) == got;
        if (cp) {
            cp->done += got;
            if (cp->h && cp->h->bytes) cp->h->bytes(cp->h->ctx, cp->done, cp->total);
            if (cancelled(cp->h)) ok = 0;
        }
    }
    if (in) { ok = ok && !ferror(in); fclose(in); }
    if (out) ok = fclose(out) == 0 && ok;
    if (ok) chmod(dst, 0644);   /* never carry an execute bit across */
    return ok ? 0 : -1;
}

/* dst == NULL: measure only. Symlinks and special files are skipped, never followed. */
static int copy_tree(const char *src, const char *dst, int depth, int *count, uint64_t *bytes, CopyProg *cp) {
    DIR *d = depth > COPY_MAX_DEPTH ? NULL : opendir(src);
    if (!d) return -1;
    if (dst && mkdir(dst, 0755) != 0 && errno != EEXIST) { closedir(d); return -1; }
    struct dirent *e;
    int rc = 0;
    while (!rc && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char s[OSPI_PATH], t[OSPI_PATH];
        struct stat st;
        if (snprintf(s, sizeof s, "%s/%s", src, e->d_name) >= (int)sizeof s ||
            (dst && snprintf(t, sizeof t, "%s/%s", dst, e->d_name) >= (int)sizeof t) || lstat(s, &st)) { rc = -1; break; }
        if (++*count > COPY_MAX_ENTRIES) { rc = -1; break; }
        if (S_ISDIR(st.st_mode)) rc = copy_tree(s, dst ? t : NULL, depth + 1, count, bytes, cp);
        else if (S_ISREG(st.st_mode)) { *bytes += (uint64_t)st.st_size; if (dst) rc = copy_plain_file(s, t, cp); }
    }
    closedir(d);
    return rc;
}

/* ---- the install ---- */

static void progress(const ClassicHooks *h, const char *stage) { if (h && h->progress) h->progress(h->ctx, stage); }
static void stage(const ClassicHooks *h, ClassicStage st) { if (h && h->stage) h->stage(h->ctx, st); }

/* Network byte counts arrive from install.c; pass them on, falling back to the catalogue size for the total. */
typedef struct { const ClassicHooks *h; uint64_t expect; } NetTap;
static void net_tap(void *ctx, uint64_t got, uint64_t total) {
    NetTap *t = ctx;
    if (t->h && t->h->bytes) t->h->bytes(t->h->ctx, got, total ? total : t->expect);
}

static void safe_leaf(const char *name, char *out, size_t n) {
    size_t w = 0;
    for (const char *p = name; *p && w + 1 < n && w < 48; p++)
        if ((unsigned char)*p >= 32 && *p != '/' && *p != ':' && *p != '\\') out[w++] = *p;
    while (w && (out[w - 1] == ' ' || out[w - 1] == '.')) w--;
    out[w] = 0;
    if (!out[0]) snprintf(out, n, "Classic App");
}

/* Make the destination folder for a new install. Returns 0 and its volume-relative path in `target`. */
static int make_target(const char *root, const ClassicApp *app, char *target, size_t tn, char *created_wrap, char *err, size_t err_n) {
    char rel[OSPI_PATH], full[OSPI_PATH], real_root[PATH_MAX], real[PATH_MAX];
    created_wrap[0] = 0;
    if (classic_dest_rel(root, app->dest, rel, sizeof rel) || volume_join(full, sizeof full, root, rel) ||
        volume_mkdir_p(full) || !realpath(root, real_root) || !realpath(full, real) || !within(real_root, real))
        FAIL(-1, "Could not prepare the %s folder.", app->dest);
    if (!app->wrap) { snprintf(target, tn, "%s", rel); return 0; }
    char leaf[64], made[OSPI_NAME];
    safe_leaf(app->name, leaf, sizeof leaf);
    if (volume_mkdir_unique(root, rel, leaf, made, sizeof made) || snprintf(target, tn, "%s/%s", rel, made) >= (int)tn)
        FAIL(-1, "Could not create a folder in %s.", app->dest);
    snprintf(created_wrap, OSPI_NAME, "%s", made);
    return 0;
}

static void rm_tree(const char *path, int depth) {
    DIR *d = depth > COPY_MAX_DEPTH + 1 ? NULL : opendir(path);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        char c[OSPI_PATH];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || snprintf(c, sizeof c, "%s/%s", path, e->d_name) >= (int)sizeof c ||
            lstat(c, &st)) continue;
        if (S_ISDIR(st.st_mode)) rm_tree(c, depth + 1); else unlink(c);
    }
    if (d) closedir(d);
    rmdir(path);
}

/* Remove a folder this install created (never an existing shared folder such as Extensions). */
static void undo_target(const char *root, const char *target, const char *wrapped) {
    char full[OSPI_PATH];
    if (wrapped[0] && !volume_join(full, sizeof full, root, target)) rm_tree(full, 0);
}

static ClassicResult install_core(const char *root, const ClassicApp *app, const ClassicHooks *hooks, const char *local,
                                  char *rel_out, size_t rel_n, char *err, size_t err_n) {
    if (rel_out && rel_n) rel_out[0] = 0;
    if (err && err_n) err[0] = 0;
    if (!root || !app || !app->id[0]) FAIL(CA_FAILED, "Nothing to install.");
    if (classic_installed(root, app)) FAIL(CA_ALREADY, "%s is already installed.", app->name);
    if (cancelled(hooks)) FAIL(CA_CANCELLED, "Cancelled.");
    struct stat ls = {0};
    int local_dir = 0;
    if (local) {
        if (lstat(local, &ls) || (!S_ISREG(ls.st_mode) && !S_ISDIR(ls.st_mode))) FAIL(CA_NO_SOURCE, "That is not a file or folder OS999 can read.");
        local_dir = S_ISDIR(ls.st_mode);
    } else if (!app->page[0] && !(hooks && hooks->fetch)) {
        FAIL(CA_NO_SOURCE, "%s has no download page yet. Use Install from a folder or archive.", app->name);
    }
    char md5[33] = "", sha[65] = "", leaf[OSPI_NAME] = "", source[320], tmp[OSPI_PATH] = "";
    uint64_t size = 0;
    int verified = 0;
    snprintf(source, sizeof source, "%s", local ? "local" : app->page[0] ? app->page : "supplied");
    int count = 0;
    if (local_dir) {
        progress(hooks, "Measuring the folder");
        if (copy_tree(local, NULL, 0, &count, &size, NULL)) FAIL(CA_FAILED, "That folder could not be read (too deep, too many files or unreadable).");
        snprintf(leaf, sizeof leaf, "%s", strrchr(local, '/') ? strrchr(local, '/') + 1 : local);
    } else {
        int fd = install_tempfile(tmp, sizeof tmp, "classic");
        if (fd < 0) FAIL(CA_FAILED, "Could not start the download.");
        close(fd);
        progress(hooks, local ? "Reading the archive" : "Downloading");
        stage(hooks, local ? CS_COPYING : CS_DOWNLOADING);
        char why[200] = "";
        int bad;
        if (local) {
            CopyProg cp = {hooks, 0, (uint64_t)ls.st_size};
            unlink(tmp);   /* the temp file above only reserved a name; copy_plain_file creates exclusively */
            bad = copy_plain_file(local, tmp, &cp);
            if (bad) snprintf(why, sizeof why, "Could not read that file.");
        } else {
            NetTap tap = {hooks, app->size};
            if (!(hooks && hooks->fetch)) install_fetch_set_progress(net_tap, &tap);
            ClassicFetch fetch = hooks && hooks->fetch ? hooks->fetch : fetch_garden;
            bad = fetch(app, tmp, hooks ? hooks->cancel : NULL, hooks ? hooks->ctx : NULL, why, sizeof why);
            install_fetch_set_progress(NULL, NULL);
        }
        if (cancelled(hooks)) { unlink(tmp); FAIL(CA_CANCELLED, "Cancelled."); }
        if (bad) { unlink(tmp); FAIL(CA_FAILED, "%s", why[0] ? why : "The download failed."); }
        progress(hooks, "Checking the download");
        stage(hooks, CS_VERIFYING);
        if (classic_hash_file(tmp, md5, sha, &size)) { unlink(tmp); FAIL(CA_FAILED, "Could not read the downloaded file."); }
        if ((app->size && app->size != size) || (app->md5[0] && strcasecmp(app->md5, md5)) ||
            (app->sha256[0] && strcasecmp(app->sha256, sha))) {
            unlink(tmp);
            FAIL(CA_HASH, "The download does not match the expected size or checksum, so it was not installed.");
        }
        verified = app->md5[0] || app->sha256[0] || app->size;
        snprintf(leaf, sizeof leaf, "%s", local ? (strrchr(local, '/') ? strrchr(local, '/') + 1 : local) : app->file);
    }
    char full_msg[220];
    if (volume_space_reserve(root, size, full_msg, sizeof full_msg)) {
        if (tmp[0]) unlink(tmp);
        FAIL(CA_FULL, "%s", full_msg);
    }
    if (cancelled(hooks)) { if (tmp[0]) unlink(tmp); FAIL(CA_CANCELLED, "Cancelled."); }
    char target[OSPI_PATH], wrapped[OSPI_NAME];
    char why[200] = "";
    if (make_target(root, app, target, sizeof target, wrapped, why, sizeof why)) {
        if (tmp[0]) unlink(tmp);
        FAIL(CA_FAILED, "%s", why);
    }
    progress(hooks, "Installing into your volume");
    stage(hooks, CS_UNPACKING);
    int bad;
    if (local_dir) {
        char dest_full[OSPI_PATH];
        int n2 = 0;
        uint64_t b2 = 0;
        CopyProg cp = {hooks, 0, size};
        bad = volume_join(dest_full, sizeof dest_full, root, target) || copy_tree(local, dest_full, 0, &n2, &b2, &cp);
        if (bad) snprintf(why, sizeof why, "Could not copy that folder in.");
    } else {
        bad = install_expand_archive(root, tmp, target, why, sizeof why);
    }
    if (tmp[0]) unlink(tmp);
    if (bad) {
        undo_target(root, target, wrapped);   /* an expansion that fails leaves nothing behind */
        if (cancelled(hooks)) FAIL(CA_CANCELLED, "Cancelled.");
        FAIL(CA_FAILED, "%s", why[0] ? why : "Could not install this item.");
    }
    if (write_receipt(root, app, source, leaf, size, md5, sha, verified, target))
        FAIL(CA_FAILED, "Installed, but could not write the install receipt.");
    volume_space_invalidate();
    if (rel_out && rel_n) snprintf(rel_out, rel_n, "%s", target);
    return CA_OK;
}

ClassicResult classic_install(const char *root, const ClassicApp *app, const ClassicHooks *hooks,
                              char *rel_out, size_t rel_n, char *err, size_t err_n) {
    return install_core(root, app, hooks, NULL, rel_out, rel_n, err, err_n);
}

ClassicResult classic_install_local(const char *root, const char *host_path, const ClassicHooks *hooks,
                                    char *rel_out, size_t rel_n, char *err, size_t err_n) {
    if (!host_path || host_path[0] != '/') FAIL(CA_NO_SOURCE, "Choose a folder or archive first.");
    const char *slash = strrchr(host_path, '/');
    const char *leaf = slash && slash[1] ? slash + 1 : host_path;
    ClassicApp app = {0};
    char base[56], ident[32] = "local-";
    safe_leaf(leaf, base, sizeof base);
    if (install_archive_supported(base)) *strrchr(base, '.') = 0;   /* "Foo.sit" installs as "Foo" */
    if (!base[0]) snprintf(base, sizeof base, "Classic App");
    snprintf(app.name, sizeof app.name, "%s", base);
    size_t w = strlen(ident);
    for (const char *p = base; *p && w + 1 < sizeof ident; p++) {
        char c = (char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) ident[w++] = c;
        else if (w > 6 && ident[w - 1] != '-') ident[w++] = '-';
    }
    ident[w] = 0;
    snprintf(app.id, sizeof app.id, "%s", ident);
    snprintf(app.dest, sizeof app.dest, "Applications");
    app.wrap = 1;
    char real[PATH_MAX];
    if (!realpath(host_path, real)) FAIL(CA_NO_SOURCE, "That file or folder is not there.");
    return install_core(root, &app, hooks, real, rel_out, rel_n, err, err_n);
}
