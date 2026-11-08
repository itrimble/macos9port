#include "ext_ui.h"
#include "cdev_ui.h"
#include "desktop_theme.h"

#include <stdio.h>
#include <string.h>

enum { LIST_TOP = 30, BUTTON_W = 104, BUTTON_H = 26 };
static const char *const k_button_labels[6] = {
    "Enable All", "Disable All", "Save Set", "Use Set", "Continue", "Disable All & Go"
};

int ext_manager_open(ExtManager *m, const char *root, int overlay) {
    if (!m || !root || strlen(root) >= sizeof m->root) return -1;
    memset(m, 0, sizeof *m);
    snprintf(m->root, sizeof m->root, "%s", root);
    m->overlay = overlay;
    int n = ext_scan(&m->list, root);
    if (n < 0) return -1;
    ext_state_load(&m->list, root);
    ext_order(&m->list);
    return n;
}

static Rect list_box(Rect c) { return (Rect){c.x, c.y + LIST_TOP, c.w, EXT_UI_ROWS * EXT_UI_ROW_H + 2}; }

Rect ext_ui_checkbox_rect(Rect c, int row) {
    return (Rect){c.x + 8, c.y + LIST_TOP + 1 + row * EXT_UI_ROW_H, 260, EXT_UI_ROW_H};
}

Rect ext_ui_button_rect(Rect c, int index) {
    int y = c.y + LIST_TOP + EXT_UI_ROWS * EXT_UI_ROW_H + 14;
    if (index < 4) return (Rect){c.x + index * (BUTTON_W + 8), y, BUTTON_W, BUTTON_H};
    if (index == 4) return (Rect){c.x, y + BUTTON_H + 10, BUTTON_W, BUTTON_H};
    return (Rect){c.x + BUTTON_W + 8, y + BUTTON_H + 10, BUTTON_W + 32, BUTTON_H};
}

static void commit(ExtManager *m) {
    ext_order(&m->list);
    if (ext_state_save(&m->list, m->root) != 0) snprintf(m->note, sizeof m->note, "Could not save extension settings.");
    else m->note[0] = 0;
}

static int clamp_scroll(const ExtManager *m, int scroll) {
    int max = m->list.count - EXT_UI_ROWS;
    return scroll > max ? (max < 0 ? 0 : max) : scroll < 0 ? 0 : scroll;
}

int ext_manager_wheel(ExtManager *m, int dy) {
    int before = m->scroll;
    m->scroll = clamp_scroll(m, m->scroll + dy);
    return m->scroll != before;
}

static void set_all(ExtManager *m, int enabled) {
    for (int i = 0; i < m->list.count; i++) ext_set_enabled(&m->list, m->list.items[i].name, enabled);
    commit(m);
}

int ext_manager_click(ExtManager *m, Rect c, int x, int y) {
    if (!m) return 0;
    for (int r = 0; r < EXT_UI_ROWS; r++) {
        int idx = m->scroll + r;
        if (idx >= m->list.count) break;
        if (rect_has(ext_ui_checkbox_rect(c, r), x, y)) {
            ExtEntry *e = &m->list.items[idx];
            ext_set_enabled(&m->list, e->name, !e->enabled);
            commit(m);
            return 1;
        }
    }
    int top = m->overlay ? 6 : 4;
    for (int b = 0; b < top; b++) {
        if (!rect_has(ext_ui_button_rect(c, b), x, y)) continue;
        if (b == 0 || b == 1) set_all(m, b == 0);
        else if (b == 2) {
            char name[EXT_NAME];
            snprintf(name, sizeof name, "Set %d", (m->list.set_count % EXT_SETS) + 1);
            ext_set_save(&m->list, name);
            commit(m);
            snprintf(m->note, sizeof m->note, "Saved \"%s\".", name);
        } else if (b == 3 && m->list.set_count > 0) {
            m->next_set %= m->list.set_count;
            ext_set_apply(&m->list, m->list.sets[m->next_set].name);
            snprintf(m->note, sizeof m->note, "Using \"%s\".", m->list.sets[m->next_set].name);
            m->next_set++;
            commit(m);
        } else if (b == 4 || b == 5) {
            if (b == 5) set_all(m, 0);
            m->done = 1;
            m->disable_all_and_continue = b == 5;
        }
        return 1;
    }
    return 0;
}

int ext_manager_key(ExtManager *m, int key) {
    if (m && m->overlay && (key == '\r' || key == '\n')) { m->done = 1; return 1; }
    return 0;
}

void ext_manager_render(ExtManager *m, Fb *fb, Rect c) {
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND), dim = desktop_theme_color(THEME_DISABLED);
    fb_text(fb, c.x + 4, c.y + 6, fg, m->overlay
            ? "Extensions Manager - startup paused. Choose what loads this time."
            : "Checked extensions load at the next startup (hold Space at launch to open this).");
    Rect box = list_box(c);
    fb_fill(fb, box.x, box.y, box.w, box.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, box.x, box.y, box.w, box.h, desktop_theme_color(THEME_FRAME));
    if (m->list.count == 0)
        fb_text(fb, box.x + 12, box.y + 8, dim, "No extensions found in System Folder > Extensions.");
    fb_clip(fb, box.x + 1, box.y + 1, box.w - 2, box.h - 2);
    for (int r = 0; r < EXT_UI_ROWS && m->scroll + r < m->list.count; r++) {
        ExtEntry *e = &m->list.items[m->scroll + r];
        Rect row = ext_ui_checkbox_rect(c, r);
        cdev_ui_checkbox(fb, row, e->name, e->enabled);
        char status[128];
        if (e->error[0]) snprintf(status, sizeof status, "%s: %s", ext_status_label(e->status), e->error);
        else snprintf(status, sizeof status, "%s", e->enabled ? "On" : "Off");
        fb_text(fb, c.x + 280, row.y + 3, e->status == EXT_FAILED || e->status == EXT_MISSING_DEP ||
                e->status == EXT_CYCLE ? fg : dim, status);
    }
    fb_unclip(fb);
    int top = m->overlay ? 6 : 4;
    for (int b = 0; b < top; b++)
        cdev_ui_button(fb, ext_ui_button_rect(c, b), k_button_labels[b], b == 4, b != 3 || m->list.set_count > 0);
    if (m->note[0]) fb_text(fb, c.x + 4, c.y + LIST_TOP + EXT_UI_ROWS * EXT_UI_ROW_H + 82, dim, m->note);
}
