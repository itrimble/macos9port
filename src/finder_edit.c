/* Finder name editing and keyboard navigation (see finder_edit.h). The edit
 * is remembered by folder and original name, not window index, so windows
 * coming forward or folders reloading underneath it never lose the box. */
#include "finder_edit.h"
#include "desktop_assets.h"
#include "session_int.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static uint32_t highlight = 0xFFCCCCFFu;   /* the default Platinum highlight */

/* ---- Where an icon's label sits ---------------------------------------- */
static int label_at(const Window *w, int index, int *cx, int *ly) {
    if (!w || w->kind != WIN_FOLDER || w->collapsed || index < 0 || index >= w->nitems) return 0;
    Layout lay;
    window_layout(w, &lay);
    int cols = lay.content.w / CELL_W;
    if (cols < 1) cols = 1;
    int x = lay.content.x + (index % cols) * CELL_W + (CELL_W - 32) / 2;
    int y = lay.content.y + (index / cols) * CELL_H + 4 - w->scroll;
    *cx = x + 16;
    *ly = y + 34;
    return 1;
}

static Rect name_rect(int cx, int ly, const char *name) {
    int tw = fb_text_width(name);
    if (tw > CELL_W - 8) tw = CELL_W - 8;
    return (Rect){cx - tw / 2 - 2, ly - 1, tw + 4, typeface_line_height(0) + 2};
}

static Rect box_rect(int cx, int ly, const char *text) {
    int bw = fb_text_width(text) + 6;
    if (bw < 20) bw = 20;
    return (Rect){cx - bw / 2, ly - 2, bw, typeface_line_height(0) + 4};
}

static int renameable(const Window *w, int index) {
    return w && w->kind == WIN_FOLDER && index >= 0 && index < w->nitems &&
           strcmp(w->rel, "Trash") != 0 && window_items(w)[index].kind != ITEM_SYSTEM;
}

/* The window and icon being edited, found again by folder and name. */
static Window *edit_target(const Session *s, int *index) {
    if (!s->edit_on) return NULL;
    for (int i = s->nwin - 1; i >= 0; i--) {
        const Window *w = &s->win[i];
        if (w->kind != WIN_FOLDER || strcmp(w->rel, s->edit_rel) != 0) continue;
        for (int k = 0; k < w->nitems; k++)
            if (strcmp(window_items(w)[k].name, s->edit_old) == 0) {
                *index = k;
                return (Window *)w;
            }
    }
    return NULL;
}

/* ---- Begin, commit, cancel --------------------------------------------- */
static void begin(Session *s, Window *w, int index) {
    s->edit_on = 1;
    s->edit_armed = s->edit_cand = 0;
    snprintf(s->edit_rel, sizeof s->edit_rel, "%s", w->rel);
    snprintf(s->edit_old, sizeof s->edit_old, "%s", window_items(w)[index].name);
    snprintf(s->edit_buf, sizeof s->edit_buf, "%s", window_items(w)[index].name);
    s->edit_len = (int)strlen(s->edit_buf);
    s->edit_anchor = 0;               /* the whole name starts selected */
    s->edit_caret = s->edit_len;
    s->edit_since = s->input_now_ms;
    select_only(w, index);
    s->desk_sel = -1;
}

static void retitle_open(Session *s, const char *old_rel, const char *new_rel) {
    size_t n = strlen(old_rel);
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        if (w->kind != WIN_FOLDER || strncmp(w->rel, old_rel, n) != 0 ||
            (w->rel[n] != 0 && w->rel[n] != '/')) continue;
        int exact = w->rel[n] == 0;
        char rel[OSPI_PATH];
        if (snprintf(rel, sizeof rel, "%s%s", new_rel, w->rel + n) >= (int)sizeof rel) continue;
        snprintf(w->rel, sizeof w->rel, "%s", rel);
        if (exact) {   /* the renamed folder's own window takes the new name */
            const char *slash = strrchr(w->rel, '/');
            snprintf(w->title, sizeof w->title, "%s", slash ? slash + 1 : w->rel);
        }
    }
}

/* A special folder keeps its icon under a new name: mark it before renaming. */
static void keep_icon(Session *s) {
    int index;
    Window *w = edit_target(s, &index);
    if (!w || window_items(w)[index].kind != ITEM_FOLDER || window_items(w)[index].special[0]) return;
    const char *icon = desktop_asset_name(&window_items(w)[index], s->edit_rel);
    if (!icon || strcmp(icon, "folder") == 0) return;
    char rel[OSPI_PATH];
    if (snprintf(rel, sizeof rel, "%s%s%s", s->edit_rel, s->edit_rel[0] ? "/" : "", s->edit_old) <
        (int)sizeof rel)
        volume_set_folder_icon(s->root, rel, icon);
}

void finder_edit_commit(Session *s) {
    if (!s->edit_on) return;
    if (s->edit_buf[0] && strcmp(s->edit_buf, s->edit_old) != 0) keep_icon(s);
    s->edit_on = 0;
    char name[OSPI_NAME];
    snprintf(name, sizeof name, "%s", s->edit_buf);
    if (!name[0] || strcmp(name, s->edit_old) == 0) return;   /* nothing to do: keep the old name */
    int r = volume_rename(s->root, s->edit_rel, s->edit_old, name);
    if (r == -2) {
        char body[OSPI_NAME + 96];
        snprintf(body, sizeof body, "The name \"%s\" is already taken. Please use a different name.", name);
        dialog_info(s, "Rename", body);
        return;
    }
    if (r != 0) {
        dialog_info(s, "Rename", "That name can't be used. Names can't start with a period or contain a colon.");
        return;
    }
    char old_rel[OSPI_PATH], new_rel[OSPI_PATH];
    const char *sep = s->edit_rel[0] ? "/" : "";
    if (snprintf(old_rel, sizeof old_rel, "%s%s%s", s->edit_rel, sep, s->edit_old) < (int)sizeof old_rel &&
        snprintf(new_rel, sizeof new_rel, "%s%s%s", s->edit_rel, sep, name) < (int)sizeof new_rel)
        retitle_open(s, old_rel, new_rel);
    reload_folders(s);
    for (int i = s->nwin - 1; i >= 0; i--) {
        Window *w = &s->win[i];
        if (w->kind != WIN_FOLDER || strcmp(w->rel, s->edit_rel) != 0) continue;
        for (int k = 0; k < w->nitems; k++)
            if (strcmp(window_items(w)[k].name, name) == 0) select_only(w, k);
    }
}

int finder_edit_return(Session *s) {
    if (s->desktop_focus || s->desk_sel >= 0 || s->nwin <= 0) return 0;
    Window *w = &s->win[s->nwin - 1];
    int found = -1, count = 0;
    for (int i = 0; w->kind == WIN_FOLDER && i < w->nitems; i++)
        if (window_items(w)[i].selected) { found = i; count++; }
    if (count != 1 || !renameable(w, found) || w->collapsed) return 0;
    begin(s, w, found);
    return 1;
}

int session_rename_at(Session *s, int x, int y) {
    if (session_overlay_active(s)) return 0;
    finder_edit_commit(s);
    for (int i = s->nwin - 1; i >= 0; i--) {
        Layout lay;
        window_layout(&s->win[i], &lay);
        if (!rect_has(lay.outer, x, y)) continue;
        Window *w = &s->win[i];
        int index = item_index_at(w, x, y), cx, ly;
        if (!renameable(w, index) || !label_at(w, index, &cx, &ly) ||
            !rect_has(name_rect(cx, ly, window_items(w)[index].name), x, y)) return 0;
        bring_front(s, i);
        begin(s, &s->win[s->nwin - 1], index);
        return 1;
    }
    return 0;
}

/* ---- Clicks ------------------------------------------------------------ */
static int caret_for_x(const Session *s, int text_x, int x) {
    int best = 0, best_d = 1 << 30;
    for (int b = 0; b <= s->edit_len; b++) {
        if (b < s->edit_len && ((unsigned char)s->edit_buf[b] & 0xC0) == 0x80) continue;
        int d = text_x + fb_text_width_n(s->edit_buf, b) - x;
        if (d < 0) d = -d;
        if (d < best_d) { best_d = d; best = b; }
    }
    return best;
}

int finder_edit_down(Session *s, int x, int y, int clicks) {
    s->edit_armed = s->edit_cand = 0;
    int index, cx, ly;
    Window *w = edit_target(s, &index);
    if (w && label_at(w, index, &cx, &ly) && w == &s->win[s->nwin - 1]) {
        Rect box = box_rect(cx, ly, s->edit_buf);
        if (rect_has(box, x, y)) {
            if (clicks >= 2) { s->edit_anchor = 0; s->edit_caret = s->edit_len; }
            else s->edit_anchor = s->edit_caret = caret_for_x(s, box.x + 3, x);
            s->edit_since = s->input_now_ms;
            return 1;
        }
    }
    finder_edit_commit(s);
    return 0;
}

void finder_edit_press(Session *s, Window *w, int index, int x, int y, int was_selected) {
    int cx, ly;
    s->edit_cand = was_selected && renameable(w, index) && label_at(w, index, &cx, &ly) &&
                   rect_has(name_rect(cx, ly, window_items(w)[index].name), x, y);
}

void finder_edit_release(Session *s, int clean_click) {
    s->edit_armed = s->edit_cand && clean_click;
    s->edit_cand = 0;
    s->edit_arm_ms = s->input_now_ms;
}

void finder_edit_tick(Session *s) {
    if (!s->edit_armed || s->down) return;
    if (s->input_now_ms - s->edit_arm_ms < FINDER_EDIT_DELAY_MS) return;
    s->edit_armed = 0;
    if (!session_overlay_active(s)) finder_edit_return(s);
}

/* ---- Typing ------------------------------------------------------------ */
static int chars(const char *t, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) c += ((unsigned char)t[i] & 0xC0) != 0x80;
    return c;
}

static void replace_sel(Session *s, const char *t, int n) {
    int a = s->edit_anchor < s->edit_caret ? s->edit_anchor : s->edit_caret;
    int b = s->edit_anchor < s->edit_caret ? s->edit_caret : s->edit_anchor;
    if (s->edit_len - (b - a) + n >= (int)sizeof s->edit_buf ||
        chars(s->edit_buf, s->edit_len) - chars(s->edit_buf + a, b - a) + chars(t, n) > FINDER_NAME_CHARS)
        return;   /* the Finder simply stops taking letters at 31 */
    memmove(s->edit_buf + a + n, s->edit_buf + b, (size_t)(s->edit_len - b + 1));
    memcpy(s->edit_buf + a, t, (size_t)n);
    s->edit_len += n - (b - a);
    s->edit_anchor = s->edit_caret = a + n;
    s->edit_since = s->input_now_ms;
}

void session_text(Session *s, const char *utf8) {
    if (!s || !s->edit_on || !utf8) return;
    char clean[OSPI_NAME];
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p && n < (int)sizeof clean - 1; p++)
        if (*p >= 0x20 && *p != 0x7F && *p != ':' && *p != '/') clean[n++] = (char)*p;
    int lead = n - 1;   /* never keep half a character */
    while (lead > 0 && ((unsigned char)clean[lead] & 0xC0) == 0x80) lead--;
    if (lead >= 0 && (unsigned char)clean[lead] >= 0x80) {
        unsigned char c = (unsigned char)clean[lead];
        int want = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
        if (n - lead < want || want == 0) n = lead;
    }
    if (n) replace_sel(s, clean, n);
}

static int step(const Session *s, int at, int dir) {
    do at += dir;
    while (at > 0 && at < s->edit_len && ((unsigned char)s->edit_buf[at] & 0xC0) == 0x80);
    return at < 0 ? 0 : (at > s->edit_len ? s->edit_len : at);
}

int finder_edit_key(Session *s, int key, int mods) {
    if (!s->edit_on) return 0;
    int lo = s->edit_anchor < s->edit_caret ? s->edit_anchor : s->edit_caret;
    int hi = s->edit_anchor < s->edit_caret ? s->edit_caret : s->edit_anchor;
    if (mods & MOD_CMD) {
        if (key == 'a') { s->edit_anchor = 0; s->edit_caret = s->edit_len; return 1; }
        if ((key == 'c' || key == 'x') && hi > lo) {
            snprintf(s->clip, sizeof s->clip, "%.*s", hi - lo, s->edit_buf + lo);
            s->clip_out = 1;
            if (key == 'x') replace_sel(s, "", 0);
            return 1;
        }
        if (key == 'v') {
            char line[OSPI_NAME];
            size_t n = strcspn(s->clip, "\r\n");   /* a name is one line */
            if (n >= sizeof line) n = sizeof line - 1;
            memcpy(line, s->clip, n);
            line[n] = 0;
            session_text(s, line);
            return 1;
        }
        finder_edit_commit(s);   /* any other command finishes the name first */
        return 0;
    }
    s->edit_since = s->input_now_ms;
    switch (key) {
    case '\r': case 3: finder_edit_commit(s); return 1;
    case 27: s->edit_on = 0; return 1;   /* keep the old name */
    case '\t': {
        finder_edit_commit(s);
        finder_nav_key(s, FINDER_KEY_RIGHT, 0);
        return 1;
    }
    case 8:
        if (hi == lo) s->edit_anchor = step(s, s->edit_caret, -1);
        replace_sel(s, "", 0);
        return 1;
    case 127:
        if (hi == lo) s->edit_anchor = step(s, s->edit_caret, +1);
        replace_sel(s, "", 0);
        return 1;
    case FINDER_KEY_LEFT: s->edit_caret = s->edit_anchor = hi > lo ? lo : step(s, s->edit_caret, -1); return 1;
    case FINDER_KEY_RIGHT: s->edit_caret = s->edit_anchor = hi > lo ? hi : step(s, s->edit_caret, +1); return 1;
    case FINDER_KEY_UP: case FINDER_KEY_HOME: s->edit_caret = s->edit_anchor = 0; return 1;
    case FINDER_KEY_DOWN: case FINDER_KEY_END: s->edit_caret = s->edit_anchor = s->edit_len; return 1;
    default: return 1;   /* letters arrive through session_text */
    }
}

/* ---- Navigation -------------------------------------------------------- */
static void reveal(Session *s, Window *w, int index) {
    Layout lay;
    window_layout(w, &lay);
    int cols = lay.content.w / CELL_W;
    if (cols < 1) cols = 1;
    int top = (index / cols) * CELL_H;
    if (top < w->scroll) w->scroll = top;
    else if (top + CELL_H > w->scroll + lay.content.h) w->scroll = top + CELL_H - lay.content.h;
    session_clamp(s);
}

static int pick(Session *s, Window *w, int index) {
    if (index < 0) index = 0;
    if (index >= w->nitems) index = w->nitems - 1;
    select_only(w, index);
    s->desk_sel = -1;
    reveal(s, w, index);
    return 1;
}

int finder_nav_key(Session *s, int key, int mods) {
    if (s->nwin > 0 && s->win[s->nwin - 1].kind != WIN_FOLDER) return 0;   /* terminal, movie, text keep their keys */
    if (s->nwin <= 0) {
        if ((key == FINDER_KEY_UP || key == FINDER_KEY_DOWN) && !(mods & MOD_CMD)) {
            s->desk_sel = key == FINDER_KEY_UP ? 0 : 1;   /* the disk sits above the Trash */
            return 1;
        }
        return 0;
    }
    Window *w = &s->win[s->nwin - 1];
    if (mods & MOD_CMD) {
        if (key == FINDER_KEY_DOWN) { session_command(s, CMD_OPEN); return 1; }
        if (key == FINDER_KEY_UP) {   /* open the enclosing folder */
            char parent[OSPI_PATH];
            snprintf(parent, sizeof parent, "%s", w->rel);
            char *slash = strrchr(parent, '/');
            if (!w->rel[0]) return 1;
            if (slash) *slash = 0; else parent[0] = 0;
            open_folder(s, parent);
            return 1;
        }
        return 0;
    }
    if (w->collapsed || w->nitems == 0) return 0;
    Layout lay;
    window_layout(w, &lay);
    int cols = lay.content.w / CELL_W, cur = -1;
    if (cols < 1) cols = 1;
    for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected) cur = i;
    switch (key) {
    case FINDER_KEY_LEFT: return pick(s, w, cur < 0 ? w->nitems - 1 : cur - 1);
    case FINDER_KEY_RIGHT: case '\t': return pick(s, w, cur < 0 ? 0 : cur + 1);
    case FINDER_KEY_UP: return pick(s, w, cur < 0 ? w->nitems - 1 : (cur - cols < 0 ? cur : cur - cols));
    case FINDER_KEY_DOWN: return pick(s, w, cur < 0 ? 0 : (cur + cols >= w->nitems ? cur : cur + cols));
    default: break;
    }
    if (key <= 32 || key >= 127) return 0;
    /* Type-to-select: the first name at or after what was typed. */
    if (s->input_now_ms - s->type_ms > FINDER_TYPE_MS) s->type_buf[0] = 0;
    s->type_ms = s->input_now_ms;
    size_t n = strlen(s->type_buf);
    if (n + 1 < sizeof s->type_buf) { s->type_buf[n] = (char)key; s->type_buf[n + 1] = 0; }
    int best = w->nitems - 1;
    for (int i = 0; i < w->nitems; i++)
        if (strncasecmp(window_items(w)[i].name, s->type_buf, strlen(s->type_buf)) >= 0) { best = i; break; }
    return pick(s, w, best);
}

/* ---- Paint ------------------------------------------------------------- */
int finder_edit_paint(Fb *fb, const Session *s, const Window *w, int index, int cx, int y) {
    if (!s->edit_on || strcmp(w->rel, s->edit_rel) != 0 || strcmp(window_items(w)[index].name, s->edit_old) != 0)
        return 0;
    Rect box = box_rect(cx, y, s->edit_buf);
    int tx = box.x + 3, lh = typeface_line_height(0);
    fb_fill(fb, box.x, box.y, box.w, box.h, C_WHITE);
    fb_frame(fb, box.x, box.y, box.w, box.h, C_BLACK);
    int lo = s->edit_anchor < s->edit_caret ? s->edit_anchor : s->edit_caret;
    int hi = s->edit_anchor < s->edit_caret ? s->edit_caret : s->edit_anchor;
    if (hi > lo) {
        int a = fb_text_width_n(s->edit_buf, lo), b = fb_text_width_n(s->edit_buf, hi);
        fb_fill(fb, tx + a, y - 1, b - a, lh + 2, highlight);
    }
    fb_text(fb, tx, y, C_BLACK, s->edit_buf);
    if (hi == lo && ((s->input_now_ms - s->edit_since) / 530u) % 2u == 0)   /* the caret blinks */
        fb_fill(fb, tx + fb_text_width_n(s->edit_buf, s->edit_caret), y - 1, 1, lh + 1, C_BLACK);
    return 1;
}

int session_editing(const Session *s) { return s && s->edit_on; }

int session_edit_rect(const Session *s, Rect *r) {
    int index, cx, ly;
    Window *w = s ? edit_target(s, &index) : NULL;
    if (!w || !label_at(w, index, &cx, &ly)) return 0;
    *r = box_rect(cx, ly, s->edit_buf);
    return 1;
}

int session_take_clip_out(Session *s) {
    int out = s && s->clip_out;
    if (s) s->clip_out = 0;
    return out;
}

const char *finder_fit_label(const char *name, char *out, size_t n) {
    int max = CELL_W - 8;
    if (fb_text_width(name) <= max) return name;
    size_t len = strlen(name);
    while (len > 0) {
        do len--;
        while (len > 0 && ((unsigned char)name[len] & 0xC0) == 0x80);
        if (snprintf(out, n, "%.*s\xE2\x80\xA6", (int)len, name) < (int)n && fb_text_width(out) <= max)
            return out;
    }
    return name;
}

void finder_standard_icon(Session *s) {
    if (!s->info_rel[0]) return;
    if (volume_clear_folder_icon(s->root, s->info_rel) != 0)
        dialog_info(s, "Folder Icon", "The folder icon could not be reset.");
    s->info_rel[0] = 0;
    reload_folders(s);
}
