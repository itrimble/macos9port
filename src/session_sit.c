#include "session_int.h"
#include "volume_capacity.h"
#include "classic_apps.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Opening a local archive (double-click or File > Open) asks first, then
 * expands it into a new folder beside the archive through install_archive():
 * content detection, forks kept in .rsrc sidecars, capacity check, all or
 * nothing. Nothing that is extracted is ever run. */
static char pending_rel[OSPI_PATH];

int session_is_sit(const char *name) {
    return install_archive_supported(name);
}

static void leaf_base(const char *rel, char *out, size_t n) {
    const char *slash = strrchr(rel, '/');
    const char *leaf = slash ? slash + 1 : rel;
    const char *dot = strrchr(leaf, '.');
    size_t len = dot && dot > leaf ? (size_t)(dot - leaf) : strlen(leaf);
    if (len >= n) len = n - 1;
    memcpy(out, leaf, len);
    out[len] = 0;
    if (!out[0]) snprintf(out, n, "Expanded");
}

void session_open_sit(Session *s, const char *rel) {
    char body[384], base[OSPI_NAME], parent[OSPI_PATH] = "";
    const char *slash = strrchr(rel, '/');
    if (strlen(rel) >= sizeof pending_rel) {
        dialog_info(s, "Archive", "The archive path is too long.");
        return;
    }
    if (slash && (size_t)(slash - rel) < sizeof parent) {
        memcpy(parent, rel, (size_t)(slash - rel));
        parent[slash - rel] = 0;
    }
    leaf_base(rel, base, sizeof base);
    snprintf(pending_rel, sizeof pending_rel, "%s", rel);
    snprintf(body, sizeof body, "Expand \"%.80s\"?\nA new folder \"%.60s\" will be created in %.100s. "
             "The archive is kept and nothing in it is run.",
             slash ? slash + 1 : rel, base, parent[0] ? parent : VOLUME_DISK_NAME);
    dialog_info(s, "Expand Archive", body);
    s->dialog = 2;
    s->dialog_cmd = CMD_EXPAND_ARCHIVE_DO;
}

static void select_single_app(Session *s) {
    Window *w = &s->win[s->nwin - 1];
    int found = -1, apps = 0;
    for (int i = 0; i < w->nitems; i++) {
        if (window_items(w)[i].kind == ITEM_APPLICATION) { apps++; found = i; }
    }
    if (apps == 1) select_only(w, found);
}

int session_sit_command(Session *s, int cmd) {
    if (cmd == CMD_CLASSIC_APPS) { classic_apps_open(s); return 1; }
    if (cmd != CMD_EXPAND_ARCHIVE_DO) return 0;
    char expanded[OSPI_PATH] = {0};
    char err[240] = {0};
    char rel[OSPI_PATH];
    snprintf(rel, sizeof rel, "%s", pending_rel);
    pending_rel[0] = 0;
    if (!rel[0]) return 1;
    if (install_archive(s->root, rel, expanded, sizeof expanded, err, sizeof err) != 0) {
        dialog_info(s, "Archive", err[0] ? err : "Could not expand this archive.");
        return 1;
    }
    if (open_folder(s, expanded) != 0)
        dialog_info(s, "Archive", "The archive was expanded; close a window to open its folder.");
    else
        select_single_app(s);
    return 1;
}
