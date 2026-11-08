#include "ospi.h"
#include "install_int.h"
#include "archive_fs.h"
#include "modern_network.h"
#include "os999_unpack.h"
#include "os999_volume.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg);
}

int garden_page_ok(const char *url) {
    const char *path = NULL;
    if (!url) return 0;
    if (strncmp(url, "https://macintoshgarden.org/", 28) == 0) path = url + 28;
    else if (strncmp(url, "https://www.macintoshgarden.org/", 32) == 0) path = url + 32;
    else return 0;
    if (!path[0]) return 0;
    const char *p = path;
    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if (c <= 32 || c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\') {
            return 0;
        }
        if (c == '.' && p[0] == '.' && (p[1] == '/' || p[1] == 0)) {
            return 0;
        }
    }
    return 1;
}

static int host_is(const char *host, size_t n, const char *expect) {
    size_t e = strlen(expect);
    return n == e && strncasecmp(host, expect, n) == 0;
}

static int download_host_ok(const char *url) {
    const char *p = strstr(url, "://");
    if (!p) {
        return 0;
    }
    p += 3;
    const char *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }
    const char *at = p;
    while (at < end && *at != '@') {
        at++;
    }
    if (at < end) {
        p = at + 1;
    }
    const char *colon = p;
    while (colon < end && *colon != ':') {
        colon++;
    }
    size_t n = (size_t)((colon < end ? colon : end) - p);
    return host_is(p, n, "download.macintoshgarden.org") || host_is(p, n, "old.mac.gdn") ||
           host_is(p, n, "gardenmirror.oldapplestuff.com");
}

int garden_alt_url(const char *url, int which, char *out, size_t n) {
    static const char *hosts[] = {"old.mac.gdn", "gardenmirror.oldapplestuff.com"};
    const char *scheme;
    const char *path;
    const char *q;
    size_t pn;
    if (!url || !out || n < 16 || which < 0 || which > 1 || !download_host_ok(url)) {
        return -1;
    }
    scheme = strstr(url, "://");
    path = scheme ? strchr(scheme + 3, '/') : NULL;
    if (!path) {
        return -1;
    }
    q = strchr(path, '?');
    pn = q ? (size_t)(q - path) : strlen(path);
    if (snprintf(out, n, "https://%s%.*s", hosts[which], (int)pn, path) >= (int)n) {
        return -1;
    }
    return download_host_ok(out) ? 0 : -1;
}

static int decode_copy(char *out, size_t n, const char *start, const char *end) {
    size_t o = 0;
    for (const char *p = start; p < end && o + 1 < n;) {
        if ((size_t)(end - p) >= 5 && strncmp(p, "&amp;", 5) == 0) {
            out[o++] = '&';
            p += 5;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    if (strncmp(out, "//", 2) == 0) {
        char tmp[2048];
        snprintf(tmp, sizeof tmp, "https:%s", out);
        if (strlen(tmp) >= n) {
            return -1;
        }
        memcpy(out, tmp, strlen(tmp) + 1);
    }
    return out[0] ? 0 : -1;
}

int garden_pick_url(const char *html, char *out, size_t n) {
    static const char *hosts[] = {"download.macintoshgarden.org", "old.mac.gdn",
                                  "gardenmirror.oldapplestuff.com"};
    if (!html || !out || n < 16) {
        return -1;
    }
    for (int h = 0; h < 3; h++) {
        const char *p = html;
        while ((p = strstr(p, hosts[h])) != NULL) {
            const char *start = p;
            while (start > html && start[-1] != '"' && start[-1] != '\'') {
                start--;
                if ((size_t)(p - start) > 32) {
                    start = p;
                    break;
                }
            }
            const char *end = p;
            while (*end && *end != '"' && *end != '\'' && *end != '<' && *end != ' ' && *end != '\n') {
                end++;
            }
            if (decode_copy(out, n, start, end) == 0 && download_host_ok(out) &&
                strncmp(out, "https://", 8) == 0) {
                return 0;
            }
            p += strlen(hosts[h]);
        }
    }
    return -1;
}

static void safe_name(const char *url, char *out, size_t n) {
    const char *q = strchr(url, '?');
    const char *end = q ? q : url + strlen(url);
    const char *base = url;
    for (const char *p = url; p < end; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    size_t len = (size_t)(end - base);
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, base, len);
    out[len] = 0;
    static const char *exts[] = {".sitx", ".sit", ".hqx", ".bin", ".zip", ".sea",
                                 ".dmg",  ".img", ".toast", ".iso", ".cdr", NULL};
    for (;;) {
        size_t L = strlen(out);
        int cut = 0;
        for (int i = 0; exts[i]; i++) {
            size_t e = strlen(exts[i]);
            if (L > e && strcasecmp(out + L - e, exts[i]) == 0) {
                out[L - e] = 0;
                cut = 1;
                break;
            }
        }
        if (!cut) {
            size_t L2 = strlen(out);
            if (L2 && (out[L2 - 1] == '_' || out[L2 - 1] == '.')) {
                out[L2 - 1] = 0;
                continue;
            }
            break;
        }
    }
    size_t w = 0;
    for (size_t i = 0; out[i] && w + 1 < n; i++) {
        unsigned char c = (unsigned char)out[i];
        if (c < 32 || c == '/' || c == ':') {
            continue;
        }
        out[w++] = (char)c;
    }
    out[w] = 0;
    while (w && out[w - 1] == '.') {
        out[--w] = 0;
    }
    if (!out[0] || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) {
        snprintf(out, n, "Download");
    }
}

static void archive_leaf(const char *url, char *out, size_t n) {
    const char *q = strchr(url, '?');
    const char *end = q ? q : url + strlen(url);
    const char *base = url;
    for (const char *p = url; p < end; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    size_t w = 0;
    for (const char *p = base; p < end && w + 1 < n; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c == '/' || c == '\\' || c == ':') {
            continue;
        }
        out[w++] = (char)c;
    }
    out[w] = 0;
    if (!out[0] || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) {
        snprintf(out, n, "download.sit");
    }
}

static int read_file_cap(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 2000000) {
        fclose(f);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    *out = buf;
    return 0;
}

static int garden_network_allowed(const char *url, void *context) {
    (void)context;
    return net_https_url(url) && (garden_page_ok(url) || download_host_ok(url));
}

static int secure_fetch(const char *url, const char *dest, size_t limit, long timeout, const char *referer) {
    NetPolicy policy = {.max_bytes = limit, .timeout_ms = timeout,
                        .allow_url = garden_network_allowed, .referer = referer};
    if (net_init() != 0) return -1;
    return net_download(url, dest, &policy).status == NET_OK ? 0 : -1;
}

static int curl_archive(const char *page, const char *url, const char *dest) {
    return secure_fetch(url, dest, 200000000, 180000, page);
}

static int fetch_archive(const char *page, const char *file_url, const char *dest) {
    char alt[2048];
    if (curl_archive(page, file_url, dest) == 0) return 0;
    Os999NetConfig cfg;
    net_get_config(&cfg);
    if (!cfg.allow_redirects) return -1;
    for (int i = 0; i < 2; i++) {
        unlink(dest);
        if (garden_alt_url(file_url, i, alt, sizeof alt) != 0) continue;
        if (curl_archive(page, alt, dest) == 0) return 0;
    }
    return -1;
}

/* Expand an archive into dest_rel (an existing, empty volume folder) through
 * the storage stack: content-based detection, forks and Finder info kept in
 * "<name>.rsrc" sidecars, all-or-nothing install, unar only as the fallback
 * for StuffIt methods we do not decode. */
int install_expand_archive(const char *root, const char *archive, const char *dest_rel, char *err, size_t err_n) {
    StArchive a = {0};
    StUnpackInfo info;
    StErr e = {0};
    int rc = st_unpack_file(archive, ST_EXT_ARCHIVE, &a, &info, NULL, &e);
    if (rc == ST_OK) {
        StVolume *v = NULL;
        rc = st_vol_open(root, NULL, &v, &e);
        if (rc == ST_OK) {
            rc = st_vol_install(v, dest_rel, &a, &e);
            st_vol_close(v);
        }
    }
    st_archive_free(&a);
    if (rc != ST_OK) {
        char msg[300];
        snprintf(msg, sizeof msg, "Could not expand this archive: %s.", e.msg[0] ? e.msg : st_status_text(rc));
        set_err(err, err_n, msg);
        return -1;
    }
    set_err(err, err_n, "");
    return 0;
}

int install_garden(const char *root, const char *page_url, char *created_rel, size_t created_n, char *err,
                   size_t err_n) {
    if (created_rel && created_n) {
        created_rel[0] = 0;
    }
    if (!garden_page_ok(page_url)) {
        set_err(err, err_n, "That link is not a macintoshgarden.org page.");
        return -1;
    }
    Os999NetConfig cfg;
    net_get_config(&cfg);
    if (cfg.connect_via == 2) {
        set_err(err, err_n, "Network is set to Offline in TCP/IP & Network.");
        return -1;
    }
    char page_tmp[OSPI_PATH];
    int pfd = install_tempfile(page_tmp, sizeof page_tmp, "page");
    if (pfd < 0) {
        set_err(err, err_n, "Could not start the download.");
        return -1;
    }
    close(pfd);
    long page_timeout = cfg.timeout_sec > 0 ? (long)cfg.timeout_sec * 1000L : 60000L;
    if (secure_fetch(page_url, page_tmp, 2000000, page_timeout, NULL) != 0) {
        unlink(page_tmp);
        set_err(err, err_n, "Could not open that Macintosh Garden page.");
        return -1;
    }
    char *html = NULL;
    if (read_file_cap(page_tmp, &html) != 0) {
        unlink(page_tmp);
        set_err(err, err_n, "Could not read that page.");
        return -1;
    }
    unlink(page_tmp);
    char file_url[2048];
    if (garden_pick_url(html, file_url, sizeof file_url) != 0) {
        free(html);
        set_err(err, err_n, "That page has no download link.");
        return -1;
    }
    free(html);
    char name[96];
    safe_name(file_url, name, sizeof name);
    char created[OSPI_NAME], apps[OSPI_PATH];   /* Applications, even if renamed */
    if (volume_special_folder(root, "applications_folder", "Applications", apps, sizeof apps) != 0 ||
        volume_mkdir_unique(root, apps, name, created, sizeof created) != 0) {
        set_err(err, err_n, "Could not create a folder in Applications.");
        return -1;
    }
    char rel[OSPI_PATH];
    if (snprintf(rel, sizeof rel, "%s/%s", apps, created) >= (int)sizeof rel) {
        set_err(err, err_n, "Could not create a folder in Applications.");
        return -1;
    }
    char dest[OSPI_PATH];
    if (volume_join(dest, sizeof dest, root, rel) != 0) {
        set_err(err, err_n, "Could not create a folder in Applications.");
        return -1;
    }
    char arch_tmp[OSPI_PATH];
    int afd = install_tempfile(arch_tmp, sizeof arch_tmp, "archive");
    if (afd < 0) {
        set_err(err, err_n, "Could not start the download.");
        return -1;
    }
    close(afd);
    if (fetch_archive(page_url, file_url, arch_tmp) != 0) {
        unlink(arch_tmp);
        rmdir(dest);
        set_err(err, err_n, "Could not download the archive. It may be over 200 MB.");
        return -1;
    }
    char leaf[96];
    char kept[OSPI_PATH];
    archive_leaf(file_url, leaf, sizeof leaf);
    if (snprintf(kept, sizeof kept, "%s/%s", dest, leaf) >= (int)sizeof kept ||
        archive_copy_file(arch_tmp, kept) != 0) {
        unlink(arch_tmp);
        rmdir(dest);
        set_err(err, err_n, "Downloaded the archive, but could not copy it in.");
        return -1;
    }
    unlink(arch_tmp);
    if (created_rel && created_n) {
        snprintf(created_rel, created_n, "%s", rel);
    }
    int expanded = install_expand_archive(root, kept, rel, err, err_n);
    if (expanded != 0) {
        char why[300];
        snprintf(why, sizeof why, "Archive saved in Applications, but it could not be expanded. %s", err && err_n ? err : "");
        set_err(err, err_n, why);
        return -1;
    }
    set_err(err, err_n, "");
    return 0;
}
