#include "desktop_theme.h"
#include "desktop_context.h"
#include "session_int.h"
#include <stdio.h>
#include <string.h>

enum { ROW_H = 22, SEPARATOR_H = 7, MAX_ROWS = 7,
       CONTEXT_DESKTOP = 1, CONTEXT_DISK, CONTEXT_TRASH, CONTEXT_FOLDER, CONTEXT_ITEM, CONTEXT_DESKTOP_ITEM,
       CONTEXT_APPEARANCE = 410 };
typedef struct { const char *label; int command, disabled; } ContextRow;
static struct {
    Session *owner;
    Rect box;
    ContextRow rows[MAX_ROWS];
    int count, hot, target;
    char folder[OSPI_PATH], item[OSPI_NAME];
} popup;

void context_close(void) { memset(&popup, 0, sizeof popup); }
int context_active(void) { return popup.owner != NULL; }
int context_bounds(const Session *s, Rect *out) {
    if (!s || popup.owner != s) return 0;
    *out = popup.box;
    return 1;
}

static void row(const char *label, int command, int disabled) {
    if (popup.count < MAX_ROWS) popup.rows[popup.count++] = (ContextRow){label, command, disabled};
}
static Rect row_rect(int index) {
    int y = popup.box.y + 2;
    for (int i = 0; i < index; i++) y += popup.rows[i].label ? ROW_H : SEPARATOR_H;
    return (Rect){popup.box.x + 2, y, popup.box.w - 4,
                  popup.rows[index].label ? ROW_H : SEPARATOR_H};
}
static int row_at(int x, int y) {
    for (int i = 0; i < popup.count; i++)
        if (rect_has(row_rect(i), x, y)) return i;
    return -1;
}
static int enabled(int i) {
    return i >= 0 && i < popup.count && popup.rows[i].label && !popup.rows[i].disabled;
}

int context_open(Session *s, int x, int y) {
    context_close();
    if (!s || s->dialog || x < 0 || x >= s->sw || y < 20 || y >= s->sh ||
        s->sw < 80 || s->sh < 100) return 0;
    int hit = -1, item = -1;
    for (int i = s->nwin - 1; i >= 0; i--) {
        Layout lay;
        window_layout(&s->win[i], &lay);
        if (!rect_has(lay.outer, x, y)) continue;
        if (s->win[i].kind != WIN_FOLDER || s->win[i].collapsed ||
            !rect_has(lay.content, x, y)) return 0;
        hit = i;
        item = item_index_at(&s->win[i], x, y);
        break;
    }
    popup.owner = s; popup.hot = -1;
    s->menu = s->menu_hot = -1;
    s->down = s->drag = s->click_valid = 0;
    s->press_win = s->press_item = -1; s->press_zone = Z_NONE;
    /* Never leave a selected item in another folder to steal a desktop Open. */
    for (int i = 0; i < s->nwin; i++) select_only(&s->win[i], -1);
    if (hit >= 0) {
        bring_front(s, hit);
        Window *w = &s->win[s->nwin - 1];
        select_only(w, item);
        s->desk_sel = -1;
        popup.target = item >= 0 ? CONTEXT_ITEM : CONTEXT_FOLDER;
        snprintf(popup.folder, sizeof popup.folder, "%s", w->rel);
        if (item >= 0) snprintf(popup.item, sizeof popup.item, "%s", window_items(w)[item].name);
        row("Open", CMD_OPEN, item < 0);
        row("Get Info", CMD_GET_INFO, 0);
        row("Copy Name", CMD_COPY, item < 0);
        row(NULL, 0, 1);
        row("New Folder", CMD_NEW_FOLDER, 0);
        row("Clean Up", CMD_CLEANUP, 0);
    } else {
        int which = session_desktop_which(s, x, y);
        s->desk_sel = which;
        s->desktop_focus = 1;
        for (int i = 0; i < s->nwin; i++) select_only(&s->win[i], -1);
        popup.target = which == 0 ? CONTEXT_DISK : which == 1 ? CONTEXT_TRASH : CONTEXT_DESKTOP;
        int desktop_item = which < 0 ? item_index_at(&s->desktop, x, y) : -1;
        select_only(&s->desktop, desktop_item);
        if (desktop_item >= 0) {
            popup.target = CONTEXT_DESKTOP_ITEM;
            snprintf(popup.item, sizeof popup.item, "%s", window_items(&s->desktop)[desktop_item].name);
            row("Open", CMD_OPEN, 0); row("Get Info", CMD_GET_INFO, 0);
            row("Rename", CMD_RENAME, 0); row("Duplicate", CMD_DUPLICATE, 0);
            row("Make Alias", CMD_MAKE_ALIAS, 0); row("Move to Trash", CMD_MOVE_TRASH, 0);
        } else if (which >= 0) {
            row("Open", CMD_OPEN, 0);
            row("Get Info", CMD_GET_INFO, 0);
        }
        else {
            row("New Folder", CMD_NEW_FOLDER, 0);
            row("Clean Up", CMD_CLEANUP, 0);
            row(NULL, 0, 1);
            row("Change Desktop Picture...", CONTEXT_APPEARANCE, 0);
            row(NULL, 0, 1); row("Open Desktop Folder", CMD_DESKTOP_ITEMS, 0);
        }
    }
    int height = 4;
    for (int i = 0; i < popup.count; i++) height += popup.rows[i].label ? ROW_H : SEPARATOR_H;
    int width = 244;
    if (width > s->sw - 4) width = s->sw - 4;
    if (height > s->sh - 24) { context_close(); return 0; }
    if (x > s->sw - width - 2) x = s->sw - width - 2;
    if (y > s->sh - height - 2) y = s->sh - height - 2;
    if (x < 2) x = 2;
    if (y < 20) y = 20;
    popup.box = (Rect){x, y, width, height};
    return 1;
}

static void dispatch(Session *s, int command) {
    int target = popup.target;
    char folder[OSPI_PATH], item[OSPI_NAME];
    snprintf(folder, sizeof folder, "%s", popup.folder);
    snprintf(item, sizeof item, "%s", popup.item);
    context_close();
    if (command == CONTEXT_APPEARANCE) { session_command(s, command); return; }
    if (target == CONTEXT_DISK || target == CONTEXT_TRASH) {
        if (command == CMD_GET_INFO) { s->desk_sel = target == CONTEXT_DISK ? 0 : 1; session_command(s, command); return; }
        if (command == CMD_OPEN && open_folder(s, target == CONTEXT_DISK ? "" : "Trash"))
            dialog_info(s, "Open", "Close a window first.");
        return;
    }
    if (target == CONTEXT_DESKTOP || target == CONTEXT_DESKTOP_ITEM) {
        s->desktop_focus = 1; s->desk_sel = -1;
        if (target == CONTEXT_DESKTOP_ITEM) {
            int found = -1;
            for (int i = 0; i < s->desktop.nitems; i++)
                if (!strcmp(window_items(&s->desktop)[i].name, item)) { found = i; break; }
            if (found < 0) { dialog_info(s, "Context Menu", "The desktop item is no longer available."); return; }
            select_only(&s->desktop, found);
        }
    } else {
        int found = -1;
        for (int i = s->nwin - 1; i >= 0; i--)
            if (s->win[i].kind == WIN_FOLDER && !strcmp(s->win[i].rel, folder)) { found = i; break; }
        if (found < 0) { dialog_info(s, "Context Menu", "The target folder is no longer open."); return; }
        bring_front(s, found);
        Window *w = &s->win[s->nwin - 1];
        select_only(w, -1);
        if (command == CMD_OPEN || (target == CONTEXT_ITEM &&
            (command == CMD_GET_INFO || command == CMD_COPY))) {
            for (int i = 0; i < w->nitems; i++) {
                if (!strcmp(window_items(w)[i].name, item)) {
                    select_only(w, i);
                    if (command == CMD_OPEN) open_item(s, w, i);
                    else session_command(s, command);
                    return;
                }
            }
            dialog_info(s, "Open", "The selected item is no longer available.");
            return;
        }
    }
    session_command(s, command);
}

int context_down(Session *s, int x, int y) {
    if (!popup.owner) return 0;
    if (s) { s->down = s->drag = s->click_valid = 0; s->press_zone = Z_NONE; }
    if (popup.owner != s || s->dialog) { context_close(); return 1; }
    int i = row_at(x, y);
    if (enabled(i)) dispatch(s, popup.rows[i].command);
    else if (!rect_has(popup.box, x, y)) context_close();
    return 1;
}
void context_move(Session *s, int x, int y) {
    if (popup.owner == s) {
        int i = row_at(x, y);
        popup.hot = enabled(i) ? i : -1;
    }
}
int context_key(Session *s, int key, int mods) {
    if (!popup.owner) return 0;
    if (popup.owner != s || s->dialog) { context_close(); return 1; }
    if (key == 27) { context_close(); return 1; }
    if (mods & MOD_CMD) {
        int command = key == 'n' ? CMD_NEW_FOLDER : key == 'o' ? CMD_OPEN :
                      key == 'i' ? CMD_GET_INFO : key == 'c' ? CMD_COPY : CMD_NONE;
        for (int i = 0; command && i < popup.count; i++) {
            if (popup.rows[i].command == command && enabled(i)) {
                dispatch(s, command); return 1;
            }
        }
        context_close(); return 0;
    }
    if (key == 13 && enabled(popup.hot)) { dispatch(s, popup.rows[popup.hot].command); return 1; }
    if (key == CONTEXT_KEY_UP || key == CONTEXT_KEY_DOWN) {
        int step = key == CONTEXT_KEY_UP ? -1 : 1;
        int i = popup.hot < 0 ? (step > 0 ? -1 : 0) : popup.hot;
        for (int n = 0; n < popup.count; n++) {
            i = (i + step + popup.count) % popup.count;
            if (enabled(i)) { popup.hot = i; break; }
        }
    }
    return 1;
}
void context_draw(Session *s, Fb *fb) {
    if (!fb || popup.owner != s || !popup.owner) return;
    if (s->dialog || s->sw < popup.box.w + 4 || s->sh < popup.box.h + 22) {
        context_close(); return;
    }
    if (popup.box.x > s->sw - popup.box.w - 2) popup.box.x = s->sw - popup.box.w - 2;
    if (popup.box.y > s->sh - popup.box.h - 2) popup.box.y = s->sh - popup.box.h - 2;
    Fb saved = *fb;
    fb_clip(fb, popup.box.x, popup.box.y, popup.box.w, popup.box.h);
    fb_fill(fb, popup.box.x, popup.box.y, popup.box.w, popup.box.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, popup.box.x, popup.box.y, popup.box.w, popup.box.h, desktop_theme_color(THEME_FOREGROUND));
    for (int i = 0; i < popup.count; i++) {
        Rect r = row_rect(i);
        ContextRow *it = &popup.rows[i];
        if (!it->label) { fb_hline(fb, r.x + 5, r.y + 3, r.w - 10, C_GRAY); continue; }
        int hot = i == popup.hot && enabled(i);
        if (hot) fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FOREGROUND));
        fb_text(fb, r.x + 7, r.y + (r.h - typeface_line_height(0)) / 2,
                it->disabled ? desktop_theme_color(THEME_DISABLED) : hot ? desktop_theme_color(THEME_PANEL) : desktop_theme_color(THEME_FOREGROUND), it->label);
    }
    *fb = saved;
}
