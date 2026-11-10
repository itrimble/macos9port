#include "native_apps.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "desktop_gesture.h"
#include "browser_window_int.h"
#include "browser_overlay.h"
#include "appearance_panel.h"
#include "desktop_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_scale100 = 200;  /* Chromium device scale for one OS999 pixel */

BrowserTab *bw_tab(int w) {
    BrowserWin *b = &BS.win[w];
    return b->open && b->ntabs > 0 ? &b->tabs[b->active] : NULL;
}

int bw_find_tab(int id, int *t) {
    for (int w = 0; w < BW_MAX_WIN; w++) {
        if (!BS.win[w].open) continue;
        for (int i = 0; i < BS.win[w].ntabs; i++)
            if (BS.win[w].tabs[i].id == id) { *t = i; return w; }
    }
    return -1;
}

int bw_top(void) { return BS.norder > 0 ? BS.order[BS.norder - 1] : -1; }

static void unorder(int w) {
    int n = 0;
    for (int i = 0; i < BS.norder; i++) if (BS.order[i] != w) BS.order[n++] = BS.order[i];
    BS.norder = n;
}

void bw_raise(int w) {
    unorder(w);
    BS.order[BS.norder++] = w;
    BS.focus_win = w;
}

static int free_id(void) {
    for (int tries = 0; tries < OS999B_MAX_ID; tries++) {
        int id = BS.next_id;
        BS.next_id = BS.next_id + 1 >= OS999B_MAX_ID ? 1 : BS.next_id + 1;
        int t;
        if (bw_find_tab(id, &t) < 0) return id;
    }
    return -1;
}

static int content_scale(void) {
#ifdef __APPLE__
    return g_scale100;
#else
    return 100;  /* CPU frames are drawn 1:1 into the OS999 framebuffer */
#endif
}

static void send_size(int w) {
    BrowserTab *tab = bw_tab(w);
    if (!tab) return;
    Rect c = bw_content(&BS.win[w]);
    int s = content_scale();
    if (c.w == tab->sent_w && c.h == tab->sent_h && s == tab->sent_scale) return;
    tab->sent_w = c.w; tab->sent_h = c.h; tab->sent_scale = s;
    bh_send(OS999B_RESIZE, tab->id, s, 0, c.w, c.h, NULL);
}

void bw_activate_tab(int w, int t) {
    BrowserWin *b = &BS.win[w];
    if (t < 0 || t >= b->ntabs) return;
    if (b->active >= 0 && b->active < b->ntabs) bh_send(OS999B_FOCUS, b->tabs[b->active].id, 0, 0, 0, 0, NULL);
    b->active = t;
    b->editing = 0;
    for (int i = 0; i < b->ntabs; i++) bh_send(OS999B_SHOW, b->tabs[i].id, 0, 0, i == t, 0, NULL);
    overlay_bind(w, b->tabs[t].id);
    send_size(w);
    if (BS.focus_win == w) bh_send(OS999B_FOCUS, b->tabs[t].id, 0, 0, BS.focus, 0, NULL);
}

int bw_new_tab(int w, const char *url, int activate) {
    BrowserWin *b = &BS.win[w];
    if (b->ntabs >= BW_MAX_TABS) {
        bh_send(OS999B_NAVIGATE, b->tabs[b->active].id, 0, 0, 0, 0, url);
        return b->active;
    }
    int id = free_id();
    if (id < 0) return -1;
    BrowserTab *tab = &b->tabs[b->ntabs++];
    memset(tab, 0, sizeof *tab);
    tab->id = id;
    snprintf(tab->title, sizeof tab->title, "New Tab");
    snprintf(tab->url, sizeof tab->url, "%s", url);
    Rect c = bw_content(b);
    int s = content_scale();
    tab->sent_w = c.w; tab->sent_h = c.h; tab->sent_scale = s;
    bh_send(OS999B_CREATE, id, s, 0, c.w, c.h, url);
    if (b->surface[0]) bh_send(OS999B_TARGET, id, 0, 0, 0, 0, b->surface);
    if (activate) bw_activate_tab(w, b->ntabs - 1);
    else bh_send(OS999B_SHOW, id, 0, 0, 0, 0, NULL);
    return b->ntabs - 1;
}

void bw_close_tab(int w, int t) {
    BrowserWin *b = &BS.win[w];
    if (t < 0 || t >= b->ntabs) return;
    int active = b->active;
    bh_send(OS999B_DESTROY, b->tabs[t].id, 0, 0, 0, 0, NULL);
    memmove(&b->tabs[t], &b->tabs[t + 1], (size_t)(b->ntabs - t - 1) * sizeof b->tabs[0]);
    b->ntabs--;
    if (b->ntabs == 0) { bw_close_window(w); return; }
    if (t != active) {
        /* The current page, focus and unfinished address edit stay intact. */
        b->active = active - (t < active);
        return;
    }
    b->active = -1;  /* the old focused tab was destroyed, not the shifted tab */
    bw_activate_tab(w, t < b->ntabs ? t : b->ntabs - 1);
}

void bw_close_window(int w) {
    BrowserWin *b = &BS.win[w];
    for (int i = 0; i < b->ntabs; i++) bh_send(OS999B_DESTROY, b->tabs[i].id, 0, 0, 0, 0, NULL);
    b->ntabs = 0;
    b->open = 0;
    if (b->fullscreen) SDL_ShowCursor(SDL_ENABLE);
    b->fullscreen = 0;
    overlay_window_close(w);
    unorder(w);
    BS.focus_win = bw_top();
    if (BS.norder == 0) SDL_SetCursor(SDL_GetDefaultCursor());
}

/* Bring a window wholly inside the current desktop: a new window on a small
 * mode, and every window after the display mode changes. */
static void fit_desktop(BrowserWin *b) {
    if (b->w > OSPI_W) b->w = OSPI_W;
    if (b->h > OSPI_H - TITLE_H) b->h = OSPI_H - TITLE_H;
    if (b->x + b->w > OSPI_W) b->x = OSPI_W - b->w;
    if (b->y + b->h > OSPI_H) b->y = OSPI_H - b->h;
    if (b->x < 0) b->x = 0;
    if (b->y < TITLE_H) b->y = TITLE_H;
}

int bw_open_window(const char *url) {
    int w = -1;
    for (int i = 0; i < BW_MAX_WIN; i++) if (!BS.win[i].open) { w = i; break; }
    if (w < 0) return -1;
    bh_ensure(BS.root);
    BrowserWin *b = &BS.win[w];
    memset(b, 0, sizeof *b);
    b->open = 1;
    b->x = 48 + 24 * w;
    b->y = 36 + 20 * w;
    b->w = 928 - 24 * w;
    b->h = 704 - 20 * w;
    fit_desktop(b);
    overlay_window_open(w, b->surface, sizeof b->surface);
    bw_raise(w);
    BS.focus = 1;
    SDL_SetHint(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT, "1");  /* long IME compositions */
    if (BS.fd >= 0) bw_new_tab(w, url, 1);
    return w;
}

void bw_set_fullscreen(int w, int on) {
    BrowserWin *b = &BS.win[w];
    if (on == b->fullscreen) return;
    b->fullscreen = on;
    if (on) bw_raise(w);
    else if (bw_tab(w)) bh_send(OS999B_EXIT_FULLSCREEN, bw_tab(w)->id, 0, 0, 0, 0, NULL);
}

void bw_navigate(int w, const char *text) {
    BrowserTab *tab = bw_tab(w);
    if (!tab) return;
    char url[OS999B_TEXT];
    if (strstr(text, "://") || strncmp(text, "about:", 6) == 0 || strncmp(text, "chrome:", 7) == 0)
        snprintf(url, sizeof url, "%s", text);
    else if (strchr(text, '.') && !strchr(text, ' '))
        snprintf(url, sizeof url, "https://%s", text);
    else {
        char q[OS999B_TEXT / 3];
        size_t j = 0;
        for (size_t i = 0; text[i] && j + 4 < sizeof q; i++) {
            unsigned char c = (unsigned char)text[i];
            if (c == ' ') q[j++] = '+';
            else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) q[j++] = (char)c;
            else j += (size_t)snprintf(q + j, sizeof q - j, "%%%02X", c);
        }
        q[j] = 0;
        snprintf(url, sizeof url, "https://www.google.com/search?q=%s", q);
    }
    snprintf(tab->url, sizeof tab->url, "%s", url);
    bh_send(OS999B_NAVIGATE, tab->id, 0, 0, 0, 0, url);
}

void browser_window_close(void) {
    for (int w = 0; w < BW_MAX_WIN; w++) if (BS.win[w].open) bw_close_window(w);
    bh_stop();
}

void bw_publish_menu_state(Session *s) {
    s->browser_windows = BS.norder; s->browser_edit_flags = 0;
    int w = BS.focus_win >= 0 ? BS.focus_win : bw_top();
    if (w < 0 || !BS.win[w].open) return;
    BrowserWin *b = &BS.win[w];
    if (b->editing) {
        s->browser_edit_flags = BROWSER_CAN_PASTE;
        if (b->edit[0]) s->browser_edit_flags |= BROWSER_CAN_SELECT;
        if (b->edit[0] && b->edit_fresh) s->browser_edit_flags |= BROWSER_CAN_COPY;
        if (b->edit_undo_valid) s->browser_edit_flags |= BROWSER_CAN_UNDO;
    } else if (BS.fd >= 0 && bw_tab(w)) {
        /* Chromium owns page selection and undo availability. */
        s->browser_edit_flags = BROWSER_CAN_COPY | BROWSER_CAN_PASTE |
                                BROWSER_CAN_SELECT | BROWSER_CAN_UNDO;
    }
}

/* Session owns application identity; browser windows can all be closed while
 * Browser remains in the application menu, until explicit Quit. */
void bw_application_sync(Session *s) {
    int action = s->browser_action;
    s->browser_action = 0;
    if (action >= BROWSER_COPY && action <= BROWSER_UNDO) {
        bw_menu_edit(action);
    } else if (action == BROWSER_QUIT) {
        int was_focused = s->browser_focused;
        browser_window_close();
        s->browser_active = s->browser_focused = s->browser_hidden = 0;
        if (was_focused) finder_apps_return(s, 4);
    } else if (action == BROWSER_CLOSE_ALL) {
        while (BS.norder) bw_close_window(bw_top());
    } else if (action == BROWSER_CLOSE && BS.norder) {
        bw_close_window(bw_top());
    } else if (action == BROWSER_NEW_WINDOW) {
        bw_open_window("about:blank");
    } else if (action == BROWSER_ACTIVATE) {
        if (!BS.norder) bw_open_window("about:blank");
        BS.focus_win = bw_top(); BS.focus = 1;
        if (BS.focus_win >= 0 && bw_tab(BS.focus_win))
            bh_send(OS999B_FOCUS, bw_tab(BS.focus_win)->id, 0, 0, 1, 0, NULL);
    }
    bw_publish_menu_state(s);
    if (BS.norder) s->browser_active = 1;
    if (!s->browser_focused || s->browser_hidden) {
        if (BS.focus && BS.focus_win >= 0 && bw_tab(BS.focus_win))
            bh_send(OS999B_FOCUS, bw_tab(BS.focus_win)->id, 0, 0, 0, 0, NULL);
        bw_cancel_interaction();
        BS.focus = BS.overlay_focus = 0;
    }
}

static Rect menu_hole(int menu) {
    Rect first, last;
    menu_item_rect(menu, 0, &first);
    menu_item_rect(menu, menu_nitems(menu) - 1, &last);
    Rect h = {first.x - 2, first.y - 2, first.w + 6, last.y + last.h - first.y + 6};
    if (h.x < 0) { h.w += h.x; h.x = 0; }
    if (h.y < 0) { h.h += h.y; h.y = 0; }
    if (h.x + h.w > OSPI_W) h.w = OSPI_W - h.x;
    if (h.y + h.h > OSPI_H) h.h = OSPI_H - h.y;
    return h;
}

static int overlay_regions(Session *s, Rect out[BW_MAX_OCCLUDERS]) {
    int n = 0;
    if (s->menu >= 0 && menu_nitems(s->menu) > 0) {
        out[n++] = menu_hole(s->menu);
        if (s->menu_child > 0 && menu_nitems(s->menu_child) > 0)
            out[n++] = menu_hole(s->menu_child);
    }
    if (finder_apps_above(s, 1, 4) && !s->finder_hidden) for (int i = 0; i < s->nwin; i++) {
        Layout lay; window_layout(&s->win[i], &lay);
        Rect r = lay.outer; r.w += 3; r.h += 3; out[n++] = r;
    }
    if (finder_apps_above(s, 1, 4)) n += appearance_panel_regions(s, out + n);
    if (s->guest_active && finder_apps_above(s, 2, 4) && !s->guest_hidden &&
        s->guest_bounds.w > 0 && s->guest_bounds.h > 0) {
        Rect guest = s->guest_bounds; guest.w += 3; guest.h += 3; out[n++] = guest;
    }
    Rect r;
    if (app_switcher_bounds(s, &r)) out[n++] = r;
    n += session_gesture_regions(s, out + n);
    if (context_bounds(s, &r)) out[n++] = r;
    const SessionMediaHooks *media = session_media_hooks();
    if (media && (finder_apps_above(s, 3, 4) || !media->running) && media->bounds && media->bounds(s, &r)) out[n++] = r;
    if (s->dialog) {
        Rect cancel, ok;
        dialog_layout(s, &r, &cancel, &ok);
        r.w += 3; r.h += 3; out[n++] = r;
    }
    /* A modal or menu must also leave the menu bar visible over fullscreen. */
    if (n) out[n++] = (Rect){0, 0, OSPI_W, TITLE_H};
    for (int i = 0; i < n; i++) {
        Rect *p = &out[i];
        if (p->x < 0) { p->w += p->x; p->x = 0; }
        if (p->y < 0) { p->h += p->y; p->y = 0; }
        if (p->x + p->w > OSPI_W) p->w = OSPI_W - p->x;
        if (p->y + p->h > OSPI_H) p->h = OSPI_H - p->y;
    }
    return n;
}

void browser_window_frame(Session *s, SDL_Window *win, uint32_t *frame, int hidden) {
    static int autostart = 1;
    snprintf(BS.root, sizeof BS.root, "%s", s->root);
    const char *auto_url = autostart ? getenv("OSPI_BROWSER_URL") : NULL;  /* test hook */
    autostart = 0;
    if (s->browser_request || auto_url) {
        s->browser_request = 0;
        bw_open_window(s->browser_open_url[0] ? s->browser_open_url : auto_url ? auto_url : "https://www.youtube.com");
        s->browser_open_url[0] = 0;
        if (BS.norder) native_app_note(s, NATIVE_BROWSER);
        s->browser_active = s->browser_focused = 1; s->app_request = 4;
        appearance_panel_deactivate();
        s->browser_hidden = s->media_focused = s->guest_focused = 0;
    }
    bw_application_sync(s);
    hidden |= s->browser_hidden;
    if (BS.norder == 0) return;
    bw_script_tick(s, win);
    bh_drain();
    /* Gamepad/key ownership follows real OS focus; never activate the app. */
    int focused = (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) && !hidden &&
                  !s->dialog && s->menu < 0 && BS.focus && BS.focus_win >= 0 &&
                  !BS.win[BS.focus_win].editing && bw_tab(BS.focus_win);
    int next = focused ? bw_tab(BS.focus_win)->id : 0;
    if (BS.input_id && BS.input_id != next) bh_send(OS999B_FOCUS, BS.input_id, 0, 0, 0, 0, NULL);
    if (next && next != BS.input_id) bh_send(OS999B_FOCUS, next, 0, 0, 1, 0, NULL);
    /* Tabs requested before the helper was up. */
    for (int k = 0; k < BS.norder; k++) {
        int w = BS.order[k];
        if (BS.fd >= 0 && BS.win[w].ntabs == 0) bw_new_tab(w, "https://www.youtube.com", 1);
    }
    static int desktop_w, desktop_h;
    if (desktop_w != OSPI_W || desktop_h != OSPI_H) {
        if (desktop_w) for (int k = 0; k < BS.norder; k++) fit_desktop(&BS.win[BS.order[k]]);
        desktop_w = OSPI_W; desktop_h = OSPI_H;
    }
    g_scale100 = (int)(overlay_scale(win, OSPI_W) * 100.0f + 0.5f);
    for (int k = 0; k < BS.norder; k++) send_size(BS.order[k]);

    Rect menus[BW_MAX_OCCLUDERS];
    int nmenus = overlay_regions(s, menus);
    /* Keep each popup separately: their bounding union includes browser pixels.
     * Absolute offsets also make overlapping parent/child rectangles safe. */
    static uint32_t *saved;
    static size_t saved_pixels;
    size_t pixels = (size_t)OSPI_W * (size_t)OSPI_H;
    if (saved_pixels != pixels) {
        free(saved);
        saved = malloc(pixels * sizeof *saved);
        saved_pixels = saved ? pixels : 0;
    }
    for (int i = 0; saved && i < nmenus; i++) {
        Rect r = menus[i];
        for (int y = r.y; r.w > 0 && y < r.y + r.h; y++)
            memcpy(&saved[y * OSPI_W + r.x], &frame[y * OSPI_W + r.x],
                   (size_t)r.w * sizeof *frame);
    }
    Fb fb;
    fb_init(&fb, frame, OSPI_W, OSPI_H);
    for (int k = 0; k < BS.norder; k++) {
        int w = BS.order[k];
        BrowserWin *b = &BS.win[w];
        Rect c = bw_content(b);
        if (!hidden) {
            bw_paint(&fb, w, w == BS.focus_win);
            overlay_draw_cpu(w, frame, OSPI_W, OSPI_H, (OverlayRect){c.x, c.y, c.w, c.h});
        }
        /* Everything above this window must show through its GPU layer. */
        OverlayRect holes[BW_MAX_WIN + BW_MAX_OCCLUDERS];
        int n = 0;
        for (int j = k + 1; j < BS.norder; j++) {
            Rect o = BS.win[BS.order[j]].fullscreen ? (Rect){0, 0, OSPI_W, OSPI_H}
                                                    : bw_outer(&BS.win[BS.order[j]]);
            holes[n++] = (OverlayRect){o.x, o.y, o.w + 3, o.h + 3};
        }
        for (int i = 0; i < nmenus; i++) {
            Rect r = menus[i];
            holes[n++] = (OverlayRect){r.x, r.y, r.w, r.h};
        }
        int visible = !hidden && BS.fd >= 0 && bw_tab(w) &&
                      overlay_frames(w) > 0;
        overlay_place(win, OSPI_W, OSPI_H, w, visible, k, (OverlayRect){c.x, c.y, c.w, c.h},
                      holes, n);
    }
    for (int i = 0; saved && i < nmenus; i++) {
        Rect r = menus[i];
        for (int y = r.y; r.w > 0 && y < r.y + r.h; y++)
            memcpy(&frame[y * OSPI_W + r.x], &saved[y * OSPI_W + r.x],
                   (size_t)r.w * sizeof *frame);
    }
    bw_publish_menu_state(s);
}
