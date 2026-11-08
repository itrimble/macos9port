#include "finder_recent.h"
#include "native_apps.h"
#include "movie_window.h"
#include "appearance_panel.h"
#include "session_int.h"
#include "term.h"
#include "finder_files.h"
#include "native_launch.h"
#include <stdio.h>
#include <string.h>

static int join_rel(char *out, size_t n, const char *rel, const char *name) {
    if (!name || strchr(name, '/')) {
        return -1;
    }
    int k = (!rel || !rel[0]) ? snprintf(out, n, "%s", name) : snprintf(out, n, "%s/%s", rel, name);
    if (k < 0 || (size_t)k >= n) {
        return -1;
    }
    return 0;
}

void open_item(Session *s, Window *w, int index) {
    if (index < 0 || index >= w->nitems) {
        return;
    }
    if (w->search_results) {
        char target[OSPI_PATH], name[OSPI_NAME];
        snprintf(target, sizeof target, "%s", window_items(w)[index].location);
        char *leaf = strrchr(target, '/');
        snprintf(name, sizeof name, "%s", leaf ? leaf + 1 : target);
        if (leaf) *leaf = 0; else target[0] = 0;
        if (open_folder(s, target)) { dialog_info(s, "Find", "Close a window first."); return; }
        Window *parent = &s->win[s->nwin - 1];
        for (int i = 0; i < parent->nitems; i++) if (!strcmp(window_items(parent)[i].name, name)) {
            select_only(parent, i); open_item(s, parent, i); return;
        }
        dialog_info(s, "Find", "The item is no longer available."); return;
    }
    if (window_items(w)[index].kind == ITEM_ALIAS) {
        static int alias_depth;
        if (alias_depth >= 8) { dialog_info(s, "Open Alias", "This alias chain is too long or circular."); return; }
        char target[OSPI_PATH];
        if (finder_alias_target(s->root, w->rel, window_items(w)[index].name, target) != 1) {
            dialog_info(s, "Open Alias", "The original item could not be found inside OS999 HD."); return;
        }
        char *leaf = strrchr(target, '/');
        char name[OSPI_NAME]; snprintf(name, sizeof name, "%s", leaf ? leaf + 1 : target);
        if (leaf) *leaf = 0; else target[0] = 0;
        if (open_folder(s, target)) { dialog_info(s, "Open Alias", "Close a window first."); return; }
        Window *parent = &s->win[s->nwin - 1];
        for (int i = 0; i < parent->nitems; i++) if (!strcmp(window_items(parent)[i].name, name)) {
            alias_depth++; open_item(s, parent, i); alias_depth--; return;
        }
        dialog_info(s, "Open Alias", "The original item is not available in this folder."); return;
    }
    char rel[OSPI_PATH];
    if (join_rel(rel, sizeof rel, w->rel, window_items(w)[index].name) != 0) {
        return;
    }
    int native = native_catalog_id(s->root, rel);
    if (native) { (void)native_app_open(s, native); return; }
    if (session_is_sit(window_items(w)[index].name)) {
        session_open_sit(s, rel);
        return;
    }
    if (window_items(w)[index].kind == ITEM_APPLICATION) {
        char full[OSPI_PATH], err[240] = {0};
        int opened = volume_join(full, sizeof full, s->root, rel) == 0 ? native_open(s->root, full, err, sizeof err) : -1;
        if (!opened) finder_recent_note(s, rel, 1);
        if (opened != 0) dialog_info(s, "Open", opened < 0 && err[0] ? err : "This application has no supported CODE resource.");
        return;
    }
    if (window_items(w)[index].kind == ITEM_FILE) {
        if (movie_is_name(window_items(w)[index].name)) {
            if (movie_window_open(s, window_items(w)[index].name, rel)) dialog_info(s, "Open", "Close a window first.");
            else finder_recent_note(s, rel, 0);
            return;
        }
        char full[OSPI_PATH];
        char err[240];
        int opened = 1;
        err[0] = 0;
        if (volume_join(full, sizeof full, s->root, rel) == 0) {
            opened = classic_open(s->root, full, window_items(w)[index].name, err, sizeof err);
        }
        if (!opened) finder_recent_note(s, rel, 1);
        if (opened <= 0) {
            if (opened < 0) {
                dialog_info(s, "Open", err[0] ? err : "Could not open that program.");
            }
            return;
        }
        const SessionMediaHooks *mh = session_media_hooks();
        if (mh && mh->is_media_file && mh->open_file && mh->is_media_file(full)) {
            appearance_panel_close(); mh->open_file(s, full); finder_recent_note(s, rel, 0); return;
        }
        char buf[OSPI_TEXT];
        if (volume_read(s->root, rel, buf, sizeof buf) != 0) {
            dialog_info(s, "Open", "Could not read that file.");
            return;
        }
        if (session_open_text(s, window_items(w)[index].name, rel, buf) != 0) {
            dialog_info(s, "Open", "Close a window first.");
        } else finder_recent_note(s, rel, 0);
        return;
    }
    if (open_folder(s, rel) != 0) {
        dialog_info(s, "Open", "Close a window first.");
    }
}
