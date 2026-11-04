#include "ospi.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void bring_front(Session *s, int idx) {
    if (idx < 0 || idx >= s->nwin || idx == s->nwin - 1) {
        return;
    }
    Window tmp = s->win[idx];
    memmove(&s->win[idx], &s->win[idx + 1], (size_t)(s->nwin - idx - 1) * sizeof(Window));
    s->win[s->nwin - 1] = tmp;
}

void close_index(Session *s, int idx) {
    if (idx < 0 || idx >= s->nwin) {
        return;
    }
    memmove(&s->win[idx], &s->win[idx + 1], (size_t)(s->nwin - idx - 1) * sizeof(Window));
    s->nwin--;
}

static void reload_window(Session *s, Window *w) {
    if (!w || w->kind != WIN_FOLDER) {
        return;
    }
    char keep[OSPI_NAME];
    keep[0] = 0;
    for (int i = 0; i < w->nitems; i++) {
        if (w->items[i].selected) {
            snprintf(keep, sizeof keep, "%s", w->items[i].name);
        }
    }
    int n = volume_list(s->root, w->rel, w->items, OSPI_MAX_ITEMS);
    w->nitems = n < 0 ? 0 : n;
    for (int i = 0; i < w->nitems; i++) {
        w->items[i].selected = keep[0] && strcmp(w->items[i].name, keep) == 0;
    }
}

void reload_folders(Session *s) {
    for (int i = 0; i < s->nwin; i++) {
        reload_window(s, &s->win[i]);
    }
}

static const char *base_name(const char *rel) {
    if (!rel || !rel[0]) {
        return "OS π HD";
    }
    const char *sl = strrchr(rel, '/');
    return sl ? sl + 1 : rel;
}

static int find_folder(const Session *s, const char *rel) {
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_FOLDER && strcmp(s->win[i].rel, rel ? rel : "") == 0) {
            return i;
        }
    }
    return -1;
}

static void place_new(Session *s, Window *w, int text) {
    int n = s->nwin;
    w->x = 36 + (n % 5) * 22;
    w->y = 42 + (n % 5) * 22;
    w->w = text ? 500 : 524;
    w->h = text ? 320 : 214;
    w->shade_h = w->h;
    if (w->y + 80 > s->sh) {
        w->y = 36;
    }
}

int open_folder(Session *s, const char *rel) {
    if (!rel) {
        rel = "";
    }
    int existing = find_folder(s, rel);
    if (existing >= 0) {
        bring_front(s, existing);
        reload_window(s, &s->win[s->nwin - 1]);
        return 0;
    }
    if (s->nwin >= OSPI_MAX_WIN) {
        return -1;
    }
    Window *w = &s->win[s->nwin];
    memset(w, 0, sizeof *w);
    w->kind = WIN_FOLDER;
    snprintf(w->rel, sizeof w->rel, "%s", rel);
    snprintf(w->title, sizeof w->title, "%s", base_name(rel));
    place_new(s, w, 0);
    s->nwin++;
    reload_window(s, w);
    return 0;
}

static int open_text(Session *s, const char *title, const char *rel, const char *body) {
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_TEXT && strcmp(s->win[i].title, title) == 0) {
            snprintf(s->win[i].text, sizeof s->win[i].text, "%s", body ? body : "");
            bring_front(s, i);
            return 0;
        }
    }
    if (s->nwin >= OSPI_MAX_WIN) {
        return -1;
    }
    Window *w = &s->win[s->nwin];
    memset(w, 0, sizeof *w);
    w->kind = WIN_TEXT;
    snprintf(w->title, sizeof w->title, "%s", title);
    snprintf(w->rel, sizeof w->rel, "%s", rel ? rel : "");
    snprintf(w->text, sizeof w->text, "%s", body ? body : "");
    place_new(s, w, 1);
    s->nwin++;
    return 0;
}

void dialog_info(Session *s, const char *title, const char *body) {
    s->dialog = 1;
    s->dialog_cmd = 0;
    s->dialog_down = 0;
    snprintf(s->dialog_title, sizeof s->dialog_title, "%s", title);
    snprintf(s->dialog_body, sizeof s->dialog_body, "%s", body);
}

static void dialog_confirm(Session *s, const char *title, const char *body, int cmd) {
    dialog_info(s, title, body);
    s->dialog = 2;
    s->dialog_cmd = cmd;
}

void dialog_accept(Session *s) {
    int cmd = s->dialog_cmd;
    s->dialog = 0;
    s->dialog_cmd = 0;
    s->dialog_down = 0;
    if (cmd) {
        session_command(s, cmd);
    }
}

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
    char rel[OSPI_PATH];
    if (join_rel(rel, sizeof rel, w->rel, w->items[index].name) != 0) {
        return;
    }
    if (w->items[index].kind == ITEM_FILE) {
        char buf[OSPI_TEXT];
        if (volume_read(s->root, rel, buf, sizeof buf) != 0) {
            dialog_info(s, "Open", "Could not read that file.");
            return;
        }
        if (open_text(s, w->items[index].name, rel, buf) != 0) {
            dialog_info(s, "Open", "Close a window first.");
        }
        return;
    }
    if (open_folder(s, rel) != 0) {
        dialog_info(s, "Open", "Close a window first.");
    }
}

void select_only(Window *w, int index) {
    for (int i = 0; i < w->nitems; i++) {
        w->items[i].selected = i == index;
    }
}

void toggle_shade(Window *w) {
    if (!w->collapsed) {
        w->shade_h = w->h;
        w->h = TITLE_H;
        w->collapsed = 1;
    } else {
        w->h = w->shade_h > TITLE_H ? w->shade_h : 214;
        w->collapsed = 0;
    }
}

void toggle_zoom(Session *s, Window *w) {
    if (w->collapsed) {
        toggle_shade(w);
    }
    if (!w->zoomed) {
        w->zoom_x = w->x;
        w->zoom_y = w->y;
        w->zoom_w = w->w;
        w->zoom_h = w->h;
        w->x = 8;
        w->y = 26;
        w->w = s->sw - 16;
        w->h = s->sh - 34;
        w->zoomed = 1;
    } else {
        w->x = w->zoom_x;
        w->y = w->zoom_y;
        w->w = w->zoom_w;
        w->h = w->zoom_h;
        w->zoomed = 0;
    }
}

static void format_ram(char *out, size_t n, uint64_t b) {
    uint64_t g = 1024ull * 1024ull * 1024ull;
    if (g != 0 && b >= g && b % g == 0) {
        snprintf(out, n, "%llu GB", (unsigned long long)(b / g));
    } else {
        snprintf(out, n, "%.1f GB", (double)b / (double)g);
    }
}

void session_command(Session *s, int cmd) {
    s->menu = -1;
    switch (cmd) {
    case CMD_QUIT:
    case CMD_SHUTDOWN:
        s->quit = 1;
        break;
    case CMD_CLOSE:
        if (s->nwin > 0) {
            close_index(s, s->nwin - 1);
        }
        break;
    case CMD_CLEANUP:
        if (s->nwin > 0 && s->win[s->nwin - 1].kind == WIN_FOLDER) {
            s->win[s->nwin - 1].scroll = 0;
            reload_window(s, &s->win[s->nwin - 1]);
        }
        break;
    case CMD_NEW_FOLDER: {
        if (s->nwin == 0 || s->win[s->nwin - 1].kind != WIN_FOLDER) {
            open_folder(s, "");
        }
        Window *w = &s->win[s->nwin - 1];
        char created[OSPI_NAME];
        if (volume_mkdir_unique(s->root, w->rel, "untitled folder", created, sizeof created) != 0) {
            dialog_info(s, "New Folder", "Could not create the folder.");
            break;
        }
        reload_window(s, w);
        select_only(w, -1);
        for (int i = 0; i < w->nitems; i++) {
            w->items[i].selected = strcmp(w->items[i].name, created) == 0;
        }
        break;
    }
    case CMD_OPEN: {
        Window *w = s->nwin ? &s->win[s->nwin - 1] : NULL;
        if (w && w->kind == WIN_FOLDER) {
            for (int i = 0; i < w->nitems; i++) {
                if (w->items[i].selected) {
                    open_item(s, w, i);
                    return;
                }
            }
        }
        if (s->desk_sel == 0) {
            open_folder(s, "");
        } else if (s->desk_sel == 1) {
            open_folder(s, "Trash");
        }
        break;
    }
    case CMD_EMPTY_ASK: {
        int n = volume_count(s->root, "Trash");
        if (n <= 0) {
            dialog_info(s, "Trash", "The Trash is empty.");
        } else {
            char body[160];
            snprintf(body, sizeof body, "Empty the Trash permanently?\n\n%d item%s will be deleted.", n,
                     n == 1 ? "" : "s");
            dialog_confirm(s, "Trash", body, CMD_EMPTY_DO);
        }
        break;
    }
    case CMD_EMPTY_DO:
        if (volume_empty_trash(s->root) != 0) {
            dialog_info(s, "Trash", "Some items could not be deleted.");
        }
        reload_folders(s);
        break;
    case CMD_ABOUT: {
        char ram[32];
        char body[OSPI_TEXT];
        format_ram(ram, sizeof ram, s->ram);
        snprintf(body, sizeof body,
                 "OS Pi 0.1\n%s  ·  %s memory\n\n"
                 "A clean-room desktop in the style\n"
                 "of the classic Mac. This is not\n"
                 "Mac OS 9. There is no Apple ROM\n"
                 "and no Apple Toolbox here.\n\n"
                 "Your files are here:\n%s\n",
                 s->arch, ram, s->root);
        if (open_text(s, "About OS Pi", ":about", body) != 0) {
            dialog_info(s, "About OS Pi", "Close a window first.");
        }
        break;
    }
    case CMD_HELP: {
        char body[OSPI_TEXT];
        snprintf(body, sizeof body,
                 "Click a menu, then a command.\n"
                 "Double-click a folder or a file.\n"
                 "Drag a file onto the Trash.\n"
                 "Special, Empty Trash, deletes it.\n\n"
                 "Cmd-N  New Folder\n"
                 "Cmd-O  Open\n"
                 "Cmd-W  Close\n"
                 "Cmd-Q  Quit\n\n"
                 "The volume on disk:\n%s\n\n"
                 "Quit, then delete that folder,\n"
                 "to get the original files back.\n",
                 s->root);
        if (open_text(s, "How to Use OS Pi", ":help", body) != 0) {
            dialog_info(s, "Help", "Close a window first.");
        }
        break;
    }
    default:
        break;
    }
}

void session_clamp(Session *s) {
    if (s->sw < 200) {
        s->sw = OSPI_W;
    }
    if (s->sh < 200) {
        s->sh = OSPI_H;
    }
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        int maxs = window_max_scroll(w);
        if (w->scroll < 0) {
            w->scroll = 0;
        }
        if (w->scroll > maxs) {
            w->scroll = maxs;
        }
        if (w->w < 260) {
            w->w = 260;
        }
        if (w->collapsed) {
            w->h = TITLE_H;
        } else if (w->h < 140) {
            w->h = 140;
        }
        if (w->x < 0) {
            w->x = 0;
        }
        if (w->y < 20) {
            w->y = 20;
        }
        if (w->x > s->sw - 48) {
            w->x = s->sw - 48;
        }
        if (w->y > s->sh - TITLE_H) {
            w->y = s->sh - TITLE_H;
        }
        if (w->x + w->w > s->sw) {
            w->w = s->sw - w->x;
        }
        if (!w->collapsed && w->y + w->h > s->sh) {
            w->h = s->sh - w->y;
        }
    }
}

Session *session_create(const char *root) {
    struct stat st;
    if (!root || strlen(root) >= OSPI_PATH || stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return NULL;
    }
    Session *s = calloc(1, sizeof *s);
    if (!s) {
        return NULL;
    }
    memcpy(s->root, root, strlen(root) + 1);
    s->menu = -1;
    s->menu_hot = -1;
    s->desk_sel = -1;
    s->press_win = -1;
    s->sw = OSPI_W;
    s->sh = OSPI_H;
    s->ram = host_ram();
    snprintf(s->arch, sizeof s->arch, "%s", host_arch());
    volume_free_bytes(s->root, &s->free_bytes);
    s->last_stat = 1;
    open_folder(s, "");
    return s;
}

void session_destroy(Session *s) { free(s); }

int session_quit(const Session *s) { return s && s->quit; }

void session_tick(Session *s, uint32_t now_ms) {
    if (s->last_stat != 0 && now_ms - s->last_stat < 1000) {
        return;
    }
    uint64_t b = 0;
    if (volume_free_bytes(s->root, &b) == 0) {
        s->free_bytes = b;
    }
    s->last_stat = now_ms;
}

