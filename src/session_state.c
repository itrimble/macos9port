#include "ospi.h"
#include "draw.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void session_set_clip(Session *s, const char *text) {
    if (!s) {
        return;
    }
    s->clip[0] = 0;
    if (!text) {
        return;
    }
    snprintf(s->clip, sizeof s->clip, "%s", text);
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
    typeface_init(s->root);
    s->ram = host_ram();
    snprintf(s->arch, sizeof s->arch, "%s", host_arch());
    volume_free_bytes(s->root, &s->free_bytes);
    s->last_stat = 1;
    open_folder(s, "");
    return s;
}

void session_destroy(Session *s) { free(s); }

int session_take_startup_toggle(Session *s) {
    if (!s) return 0;
    int request = s->startup_toggle_request;
    s->startup_toggle_request = 0;
    return request;
}

int session_take_startup_preview(Session *s) {
    if (!s) return 0;
    int request = s->startup_preview_request;
    s->startup_preview_request = 0;
    return request;
}

int session_quit(const Session *s) { return s && s->quit; }

void session_tick(Session *s, uint32_t now_ms) {
    s->input_now_ms = now_ms;
    if (s->last_stat != 0 && now_ms - s->last_stat < 1000) {
        return;
    }
    uint64_t b = 0;
    if (volume_free_bytes(s->root, &b) == 0) {
        s->free_bytes = b;
    }
    s->last_stat = now_ms;
}
