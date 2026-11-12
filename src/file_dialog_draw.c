#include "file_dialog.h"
#include "draw.h"

#include <stdio.h>
#include <string.h>

#define ROW_H 18
#define BTN_H 20

typedef struct {
    Rect up, roots[FD_ROOTS], path, list, bar, name, msg, filter, cancel, ok, box, box_cancel, box_ok;
} Lay;

static Rect R(int x, int y, int w, int h) { return (Rect){x, y, w, h}; }

static void layout(FileDialog *fd, Rect a, Lay *l) {
    int x = a.x + 8, w = a.w - 16, bottom = a.y + a.h;
    l->up = R(x, a.y + 6, 36, BTN_H);
    int bx = x + 42;
    for (int i = 0; i < fd->nroots; i++) {
        int bw = fb_text_width(fd->root_label[i]) + 14;
        l->roots[i] = R(bx, a.y + 6, bw, BTN_H);
        bx += bw + 4;
    }
    l->path = R(x, a.y + 30, w, 14);
    l->ok = R(a.x + a.w - 8 - 76, bottom - 28, 76, 22);
    l->cancel = R(l->ok.x - 6 - 70, bottom - 28, 70, 22);
    l->filter = R(x, bottom - 28, 110, 22);
    l->msg = R(x, bottom - 44, w, 14);
    int list_bottom = bottom - 50;
    if (fd->mode == FD_SAVE) { l->name = R(x + 44, bottom - 70, w - 44, 20); list_bottom = bottom - 76; }
    else l->name = R(0, 0, 0, 0);
    l->list = R(x, a.y + 48, w - 12, list_bottom - (a.y + 48));
    if (l->list.h < ROW_H) l->list.h = ROW_H;
    l->bar = R(l->list.x + l->list.w + 2, l->list.y, 10, l->list.h);
    fd->rows = l->list.h / ROW_H;
    if (fd->rows < 1) fd->rows = 1;
    l->box = R(a.x + (a.w - 280) / 2, l->list.y + 10, 280, 74);
    l->box_ok = R(l->box.x + 280 - 8 - 76, l->box.y + 74 - 28, 76, 22);
    l->box_cancel = R(l->box_ok.x - 6 - 70, l->box_ok.y, 70, 22);
}

static void button(Fb *fb, Rect r, const char *label, int on, int dflt) {
    fb_fill(fb, r.x, r.y, r.w, r.h, on ? C_WHITE : C_GRAY);
    fb_frame(fb, r.x, r.y, r.w, r.h, C_BLACK);
    if (dflt) fb_frame(fb, r.x - 2, r.y - 2, r.w + 4, r.h + 4, C_BLACK);
    int tw = fb_text_width(label);
    fb_text(fb, r.x + (r.w - tw) / 2, r.y + (r.h - 8) / 2, on ? C_BLACK : C_DISABLED, label);
}

static void clip_to(Fb *fb, Rect r, Fb *save) {
    *save = *fb;
    fb->clip = 1; fb->cx = r.x; fb->cy = r.y; fb->cw = r.w; fb->ch = r.h;
}

static const char *tail_fit(const char *s, int width) {
    while (*s && fb_text_width(s) > width) s++;
    return s;
}

void fd_paint(Fb *fb, FileDialog *fd, Rect a) {
    Lay l; Fb saved;
    if (!fb || !fd) return;
    layout(fd, a, &l);
    fb_fill(fb, a.x, a.y, a.w, a.h, C_MENU);
    int can_up = strcmp(fd->cwd, fd->root_path[fd->root]) != 0 && !fd->confirm;
    button(fb, l.up, "Up", can_up, 0);
    for (int i = 0; i < fd->nroots; i++) button(fb, l.roots[i], fd->root_label[i], !fd->confirm && i != fd->root, 0);
    /* Path: the part below the root label, so a long path shows its tail. */
    char shown[OSPI_PATH + 32];
    const char *below = fd->cwd + strlen(fd->root_path[fd->root]);
    snprintf(shown, sizeof shown, "%s%s", fd->root_label[fd->root], below);
    clip_to(fb, l.path, &saved);
    fb_text(fb, l.path.x, l.path.y + 2, C_BLACK, tail_fit(shown, l.path.w));
    *fb = saved;
    /* List */
    fb_fill(fb, l.list.x, l.list.y, l.list.w, l.list.h, C_WHITE);
    clip_to(fb, l.list, &saved);
    for (int i = 0; i < fd->rows && fd->top + i < fd->nent; i++) {
        const FdEntry *e = &fd->ent[fd->top + i];
        int y = l.list.y + i * ROW_H, sel = fd->top + i == fd->sel;
        if (sel) fb_fill(fb, l.list.x, y, l.list.w, ROW_H, C_BLACK);
        uint32_t ink = sel ? C_WHITE : C_BLACK;
        if (e->dir) { fb_fill(fb, l.list.x + 4, y + 5, 14, 9, sel ? C_FOLDER_TAB : C_FOLDER); fb_frame(fb, l.list.x + 4, y + 5, 14, 9, C_FOLDER_EDGE); }
        fb_text(fb, l.list.x + 24, y + 5, ink, e->name);
        if (e->dir) fb_text(fb, l.list.x + l.list.w - 12, y + 5, ink, ">");
    }
    if (fd->nent == 0) fb_text(fb, l.list.x + 8, l.list.y + 8, C_DISABLED, fd->filter && fd->filter_on ? "No folders or matching files here." : "This folder is empty.");
    *fb = saved;
    fb_frame(fb, l.list.x, l.list.y, l.list.w, l.list.h, C_BLACK);
    /* Scroll bar */
    fb_fill(fb, l.bar.x, l.bar.y, l.bar.w, l.bar.h, C_TRACK);
    fb_frame(fb, l.bar.x, l.bar.y, l.bar.w, l.bar.h, C_BLACK);
    if (fd->nent > fd->rows) {
        int th = l.bar.h * fd->rows / fd->nent; if (th < 10) th = 10;
        int ty = l.bar.y + (l.bar.h - th) * fd->top / (fd->nent - fd->rows);
        fb_fill(fb, l.bar.x + 1, ty, l.bar.w - 2, th, C_GRAY);
    }
    if (fd->mode == FD_SAVE) {
        fb_text(fb, a.x + 8, l.name.y + 6, C_BLACK, "Name:");
        fb_fill(fb, l.name.x, l.name.y, l.name.w, l.name.h, C_WHITE);
        fb_frame(fb, l.name.x, l.name.y, l.name.w, l.name.h, C_BLACK);
        clip_to(fb, l.name, &saved);
        const char *t = tail_fit(fd->name, l.name.w - 14);
        fb_text(fb, l.name.x + 4, l.name.y + 6, C_BLACK, t);
        fb_fill(fb, l.name.x + 4 + fb_text_width(t), l.name.y + 4, 1, 12, C_BLACK);
        *fb = saved;
    }
    if (fd->msg[0] && !fd->confirm) { clip_to(fb, l.msg, &saved); fb_text(fb, l.msg.x, l.msg.y + 2, C_BLACK, fd->msg); *fb = saved; }
    if (fd->filter) {
        char f[FD_LABEL + 16]; snprintf(f, sizeof f, fd->filter_on ? "%s only" : "All files", fd->filter_label);
        button(fb, l.filter, f, !fd->confirm, 0);
    }
    button(fb, l.cancel, "Cancel", !fd->confirm, 0);
    button(fb, l.ok, fd->mode == FD_SAVE ? "Save" : "Open", !fd->confirm, !fd->confirm);
    if (fd->confirm) {
        fb_fill(fb, l.box.x, l.box.y, l.box.w, l.box.h, C_MENU);
        fb_frame(fb, l.box.x, l.box.y, l.box.w, l.box.h, C_BLACK);
        fb_frame(fb, l.box.x + 2, l.box.y + 2, l.box.w - 4, l.box.h - 4, C_BLACK);
        clip_to(fb, R(l.box.x + 6, l.box.y + 6, l.box.w - 12, 20), &saved);
        fb_text(fb, l.box.x + 10, l.box.y + 12, C_BLACK, tail_fit(fd->msg, l.box.w - 24));
        *fb = saved;
        button(fb, l.box_cancel, "Cancel", 1, 0);
        button(fb, l.box_ok, "Replace", 1, 1);
    }
}

void fd_wheel(FileDialog *fd, int delta) {
    if (!fd || fd->nent <= fd->rows) return;
    fd->top += delta;
    if (fd->top > fd->nent - fd->rows) fd->top = fd->nent - fd->rows;
    if (fd->top < 0) fd->top = 0;
}

int fd_click(FileDialog *fd, Rect a, int x, int y) {
    Lay l;
    if (!fd || fd->state != FD_RUNNING || !rect_has(a, x, y)) return 0;
    layout(fd, a, &l);
    if (fd->confirm) {
        if (rect_has(l.box_ok, x, y)) fd_activate(fd);
        else if (rect_has(l.box_cancel, x, y)) fd_cancel(fd);
        return 1;
    }
    if (rect_has(l.up, x, y)) { (void)fd_up(fd); return 1; }
    for (int i = 0; i < fd->nroots; i++)
        if (rect_has(l.roots[i], x, y)) { (void)fd_select_root(fd, i); return 1; }
    if (rect_has(l.cancel, x, y)) { fd_cancel(fd); return 1; }
    if (rect_has(l.ok, x, y)) { fd_activate(fd); return 1; }
    if (fd->filter && rect_has(l.filter, x, y)) { fd_toggle_filter(fd); return 1; }
    if (rect_has(l.bar, x, y)) { fd_wheel(fd, y < l.bar.y + l.bar.h / 2 ? -fd->rows : fd->rows); return 1; }
    if (rect_has(l.list, x, y)) {
        int idx = fd->top + (y - l.list.y) / ROW_H;
        if (idx < fd->nent) {
            if (idx == fd->sel) fd_activate(fd);   /* a second click on the picked row opens it */
            else { fd_select(fd, idx); fd->msg[0] = 0; }
        }
        return 1;
    }
    return 1;
}
