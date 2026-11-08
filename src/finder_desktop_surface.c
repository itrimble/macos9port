#include "finder_desktop_surface.h"
#include "session_int.h"
#include "volume_directory.h"
#include "desktop_theme.h"
#include "draw.h"
#include <stdio.h>
#include <sys/stat.h>

void finder_desktop_layout(Session *s) {
    Window *w = &s->desktop;
    w->x = 8; w->y = 28; w->w = s->sw > 120 ? s->sw - 112 : 88;
    w->h = s->sh > 60 ? s->sh - 60 : 140;
}
int finder_desktop_init(Session *s) {
    int dir = volume_directory_open(s->root, "");
    if (dir < 0) return -1;
    int result = mkdirat(dir, "Desktop Folder", 0700);
    int error = errno; close(dir);
    if (result && error != EEXIST) return -1;
    Window *w = &s->desktop;
    w->kind = WIN_FOLDER; w->desktop_surface = 1;
    w->selection_anchor = w->selection_cursor = -1;
    snprintf(w->rel, sizeof w->rel, "Desktop Folder");
    snprintf(w->title, sizeof w->title, "Desktop");
    finder_desktop_layout(s);
    return reload_folder_window(s, w);
}

int finder_desktop_overflow(const Session *s) {
    int count = 0;
    for (int i = 0; i < s->desktop.nitems; i++) count += !desktop_item_visible(&s->desktop, i);
    return count;
}
static Rect overflow_button(const Session *s) {
    int width = s->sw - 112; if (width > 520) width = 520;
    return (Rect){8, s->sh - 28, width, 22};
}
void finder_desktop_overflow_paint(Fb *fb, const Session *s) {
    int count = finder_desktop_overflow(s); if (!count) return;
    Rect r = overflow_button(s); char text[100];
    snprintf(text, sizeof text, "%d more desktop item%s - Open Desktop Folder", count, count == 1 ? "" : "s");
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FOREGROUND));
    fb_clip(fb, r.x + 3, r.y + 2, r.w - 6, r.h - 4);
    fb_text(fb, r.x + 6, r.y + 4, desktop_theme_color(THEME_FOREGROUND), text); fb_unclip(fb);
}
int finder_desktop_overflow_down(Session *s, int x, int y) {
    if (!finder_desktop_overflow(s) || !rect_has(overflow_button(s), x, y)) return 0;
    s->down = s->drag = 0; session_command(s, CMD_DESKTOP_ITEMS); return 1;
}
void finder_desktop_reveal_selected(Session *s) {
    Window *w = finder_current(s);
    if (w != &s->desktop || w->selection_cursor < 0 || w->selection_cursor >= w->nitems ||
        desktop_item_visible(w, w->selection_cursor)) return;
    char name[OSPI_NAME]; snprintf(name, sizeof name, "%s", window_items(w)[w->selection_cursor].name);
    if (open_folder(s, w->rel)) { dialog_info(s, "Desktop Items", "Close a window to show the selected desktop item."); return; }
    Window *folder = &s->win[s->nwin - 1];
    select_only(folder, -1);
    for (int i = 0; i < folder->nitems; i++) if (!strcmp(window_items(folder)[i].name, name)) {
        select_only(folder, i); folder->view_mode = VIEW_ICONS; Layout lay; window_layout(folder, &lay);
        int columns = lay.content.w / CELL_W; if (columns < 1) columns = 1;
        folder->scroll = i / columns * CELL_H;
        session_clamp(s); break;
    }
}
