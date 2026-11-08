#include "browser_window_int.h"
#include "appearance_panel.h"
#include "finder_apps.h"
#include "app_switcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void logical(SDL_Window *win, int x, int y, int *lx, int *ly) {
    int ww = OSPI_W, wh = OSPI_H;
    SDL_GetWindowSize(win, &ww, &wh);
    *lx = x * OSPI_W / (ww > 0 ? ww : 1);
    *ly = y * OSPI_H / (wh > 0 ? wh : 1);
}

static uint32_t cef_mods(void) {
    SDL_Keymod m = SDL_GetModState();
    uint32_t f = 0;
    if (m & KMOD_SHIFT) f |= F_SHIFT;
    if (m & KMOD_CTRL) f |= F_CTRL;
    if (m & KMOD_ALT) f |= F_ALT;
    if (m & KMOD_GUI) f |= F_CMD;
    uint32_t b = SDL_GetMouseState(NULL, NULL);
    if (b & SDL_BUTTON_LMASK) f |= F_LEFT;
    if (b & SDL_BUTTON_MMASK) f |= F_MIDDLE;
    if (b & SDL_BUTTON_RMASK) f |= F_RIGHT;
    return f;
}

static void mouse(int w, uint32_t kind, int lx, int ly, int a, int b) {
    BrowserTab *tab = bw_tab(w);
    if (!tab) return;
    Rect c = bw_content(&BS.win[w]);
    Os999BrowserMsg m;
    memset(&m, 0, sizeof m);
    m.kind = kind; m.id = tab->id; m.x = lx - c.x; m.y = ly - c.y; m.a = a; m.b = b;
    m.modifiers = cef_mods();
    bh_send_full(&m);
}

/* SDL key -> macOS virtual key code, Windows VKEY and AppKit character. */
static int map_key(SDL_Keycode k, int shift, int *vk, int *ch) {
    static const unsigned char letters[26] = {0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46,
                                              45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6};
    static const unsigned char digits[10] = {29, 18, 19, 20, 21, 23, 22, 26, 28, 25};
    static const struct { int sdl, mac, vk, ch; } keys[] = {
        {SDLK_SPACE, 49, 0x20, ' '}, {SDLK_RETURN, 36, 0x0D, '\r'}, {SDLK_KP_ENTER, 76, 0x0D, '\r'},
        {SDLK_TAB, 48, 0x09, '\t'}, {SDLK_BACKSPACE, 51, 0x08, 0x7F}, {SDLK_ESCAPE, 53, 0x1B, 0x1B},
        {SDLK_DELETE, 117, 0x2E, 0xF728}, {SDLK_LEFT, 123, 0x25, 0xF702}, {SDLK_RIGHT, 124, 0x27, 0xF703},
        {SDLK_DOWN, 125, 0x28, 0xF701}, {SDLK_UP, 126, 0x26, 0xF700}, {SDLK_HOME, 115, 0x24, 0xF729},
        {SDLK_END, 119, 0x23, 0xF72B}, {SDLK_PAGEUP, 116, 0x21, 0xF72C}, {SDLK_PAGEDOWN, 121, 0x22, 0xF72D},
        {SDLK_MINUS, 27, 0xBD, '-'}, {SDLK_EQUALS, 24, 0xBB, '='}, {SDLK_LEFTBRACKET, 33, 0xDB, '['},
        {SDLK_RIGHTBRACKET, 30, 0xDD, ']'}, {SDLK_BACKSLASH, 42, 0xDC, '\\'}, {SDLK_SEMICOLON, 41, 0xBA, ';'},
        {SDLK_QUOTE, 39, 0xDE, '\''}, {SDLK_COMMA, 43, 0xBC, ','}, {SDLK_PERIOD, 47, 0xBE, '.'},
        {SDLK_SLASH, 44, 0xBF, '/'}, {SDLK_BACKQUOTE, 50, 0xC0, '`'}, {SDLK_F1, 122, 0x70, 0xF704},
        {SDLK_F2, 120, 0x71, 0xF705}, {SDLK_F3, 99, 0x72, 0xF706}, {SDLK_F4, 118, 0x73, 0xF707},
        {SDLK_F5, 96, 0x74, 0xF708}, {SDLK_F6, 97, 0x75, 0xF709}, {SDLK_F7, 98, 0x76, 0xF70A},
        {SDLK_F8, 100, 0x77, 0xF70B}, {SDLK_F9, 101, 0x78, 0xF70C}, {SDLK_F10, 109, 0x79, 0xF70D},
        {SDLK_F11, 103, 0x7A, 0xF70E}, {SDLK_F12, 111, 0x7B, 0xF70F}};
    if (k >= SDLK_a && k <= SDLK_z) {
        *ch = shift ? 'A' + (k - SDLK_a) : (int)k;
        *vk = 'A' + (k - SDLK_a);
        return letters[k - SDLK_a];
    }
    if (k >= SDLK_0 && k <= SDLK_9) { *ch = (int)k; *vk = (int)k; return digits[k - SDLK_0]; }
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (keys[i].sdl == (int)k) {
            *vk = keys[i].vk;
            *ch = keys[i].ch;
#ifndef __APPLE__
            if (*ch >= 0xF700) *ch = 0;  /* AppKit function-key codes are macOS only */
            if (*ch == 0x7F) *ch = 0x08;
#endif
            return keys[i].mac;
        }
    *vk = *ch = 0;
    return -1;
}

static void key(int w, uint32_t kind, const SDL_KeyboardEvent *e) {
    BrowserTab *tab = bw_tab(w);
    int vk = 0, ch = 0;
    int code = map_key(e->keysym.sym, (e->keysym.mod & KMOD_SHIFT) != 0, &vk, &ch);
    if (!tab || code < 0) return;
    Os999BrowserMsg m;
    memset(&m, 0, sizeof m);
    m.kind = kind; m.id = tab->id; m.x = vk;
    m.keycode = (uint32_t)code;
    m.ch = (uint32_t)ch;
    m.uch = (uint32_t)(e->keysym.sym >= SDLK_a && e->keysym.sym <= SDLK_z ? e->keysym.sym : ch);
    m.modifiers = cef_mods();
    if (e->keysym.mod & KMOD_GUI) m.modifiers |= F_CMD;
    bh_send_full(&m);
}

static void begin_edit(int w) {
    BrowserWin *b = &BS.win[w];
    BrowserTab *tab = bw_tab(w);
    b->editing = 1;
    b->edit_fresh = 1; b->edit_undo_valid = 0;
    snprintf(b->edit, sizeof b->edit, "%s", tab ? tab->url : "");
}

static void edit_snapshot(BrowserWin *b) {
    snprintf(b->edit_undo, sizeof b->edit_undo, "%s", b->edit);
    b->edit_undo_fresh = b->edit_fresh; b->edit_undo_valid = 1;
}

static int edit_key(int w, const SDL_KeyboardEvent *e) {
    BrowserWin *b = &BS.win[w];
    SDL_Keycode k = e->keysym.sym;
    if ((e->keysym.mod & KMOD_GUI) && k == SDLK_z) {
        if (b->edit_undo_valid) {
            char previous[OS999B_TEXT]; snprintf(previous, sizeof previous, "%s", b->edit_undo);
            int fresh = b->edit_undo_fresh;
            edit_snapshot(b);
            snprintf(b->edit, sizeof b->edit, "%s", previous); b->edit_fresh = fresh;
        }
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        b->editing = 0;
        if (b->edit[0]) bw_navigate(w, b->edit);
    } else if (k == SDLK_ESCAPE) {
        b->editing = 0;
    } else if (k == SDLK_BACKSPACE) {
        edit_snapshot(b);
        if (b->edit_fresh) b->edit[0] = 0;
        size_t n = strlen(b->edit);
        while (n > 0 && ((unsigned char)b->edit[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0) n--;
        b->edit[n] = 0;
        b->edit_fresh = 0;
    } else if ((e->keysym.mod & KMOD_GUI) && k == SDLK_v) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            edit_snapshot(b);
            if (b->edit_fresh) b->edit[0] = 0;
            strncat(b->edit, clip, sizeof b->edit - strlen(b->edit) - 1);
            SDL_free(clip);
        }
        b->edit_fresh = 0;
    } else if ((e->keysym.mod & KMOD_GUI) && k == SDLK_a) {
        b->edit_fresh = 1;
    }
    return 1;
}

static void set_focus(int w, int focus) {
    int old = BS.focus_win;
    if (old >= 0 && bw_tab(old) && (old != w || !focus))
        bh_send(OS999B_FOCUS, bw_tab(old)->id, 0, 0, 0, 0, NULL);
    BS.focus = focus;
    if (w >= 0) {
        BS.focus_win = w;
        if (focus && bw_tab(w)) bh_send(OS999B_FOCUS, bw_tab(w)->id, 0, 0, 1, 0, NULL);
    }
}

void bw_menu_edit(int action) {
    int w = BS.focus_win >= 0 ? BS.focus_win : bw_top();
    if (w < 0 || !BS.win[w].open) return;
    SDL_KeyboardEvent e = {0};
    e.keysym.mod = KMOD_GUI;
    e.keysym.sym = action == BROWSER_COPY ? SDLK_c : action == BROWSER_PASTE ? SDLK_v :
                   action == BROWSER_SELECT_ALL ? SDLK_a : SDLK_z;
    set_focus(w, 1); BS.overlay_focus = 0;
    if (BS.win[w].editing) {
        if (action == BROWSER_COPY) {
            if (BS.win[w].edit_fresh) SDL_SetClipboardText(BS.win[w].edit);
        } else edit_key(w, &e);
        return;
    }
    key(w, OS999B_KEY_DOWN, &e);
    key(w, OS999B_KEY_UP, &e);
}

static void update_cursor(int w, int over_content) {
    static SDL_Cursor *cursors[SDL_NUM_SYSTEM_CURSORS];
    SDL_SystemCursor id = SDL_SYSTEM_CURSOR_ARROW;
    BrowserTab *tab = w >= 0 ? bw_tab(w) : NULL;
    if (over_content && tab) {
        if (tab->cursor == 2) id = SDL_SYSTEM_CURSOR_HAND;
        else if (tab->cursor == 3) id = SDL_SYSTEM_CURSOR_IBEAM;
        else if (tab->cursor == 1) id = SDL_SYSTEM_CURSOR_CROSSHAIR;
        else if (tab->cursor == 4) id = SDL_SYSTEM_CURSOR_WAIT;
    }
    if (!cursors[id]) cursors[id] = SDL_CreateSystemCursor(id);
    if (cursors[id]) SDL_SetCursor(cursors[id]);
}

static int window_at(const Session *s, int x, int y) {
    if (y < TITLE_H) return -1;
    if (finder_apps_occluded(s, 4, x, y)) return -1;
    for (int k = BS.norder - 1; k >= 0; k--) {
        BrowserWin *b = &BS.win[BS.order[k]];
        if (b->fullscreen || bw_has(bw_outer(b), x, y)) return BS.order[k];
    }
    return -1;
}

static int chrome_hit(int w, int x, int y) {
    BrowserWin *b = &BS.win[w];
    if (bw_has(bw_close_rect(b), x, y)) return P_CLOSE;
    for (int i = 0; i < b->ntabs; i++) {
        if (bw_has(bw_tabx_rect(b, i), x, y)) return P_TABX + i;
        if (bw_has(bw_tab_rect(b, i), x, y)) return P_TAB + i;
    }
    if (b->ntabs < BW_MAX_TABS && bw_has(bw_plus_rect(b), x, y)) return P_PLUS;
    for (int i = 0; i < 6; i++) if (bw_has(bw_button_rect(b, i), x, y)) return P_BACK + i;
    return P_NONE;
}

static void press_action(int w, int p) {
    BrowserTab *tab = bw_tab(w);
    if (p == P_CLOSE) bw_close_window(w);
    else if (p == P_PLUS) bw_new_tab(w, "https://www.youtube.com", 1);
    else if (p >= P_TABX) bw_close_tab(w, p - P_TABX);
    else if (p >= P_TAB) bw_activate_tab(w, p - P_TAB);
    else if (!tab) return;
    else if (p == P_BACK) bh_send(OS999B_BACK, tab->id, 0, 0, 0, 0, NULL);
    else if (p == P_FORWARD) bh_send(OS999B_FORWARD, tab->id, 0, 0, 0, 0, NULL);
    else if (p == P_RELOAD) bh_send(tab->loading ? OS999B_STOP : OS999B_RELOAD, tab->id, 0, 0, 0, 0, NULL);
    else if (p == P_HOME) bw_navigate(w, "https://www.youtube.com");
    else if (p == P_INSTALL) bh_send(OS999B_APP_INSTALL, tab->id, 0, 0, 0, 0, NULL);
    else if (p == P_APPS) bw_navigate(w, "https://os999.invalid/apps/");
}

/* Browser shortcuts while a browser window has the keyboard. 1 = handled. */
static int shortcut(int w, const SDL_KeyboardEvent *e) {
    BrowserWin *b = &BS.win[w];
    SDL_Keycode k = e->keysym.sym;
    int cmd = (e->keysym.mod & KMOD_GUI) != 0, shift = (e->keysym.mod & KMOD_SHIFT) != 0;
    if ((e->keysym.mod & KMOD_CTRL) && k == SDLK_TAB && b->ntabs > 1) {
        bw_activate_tab(w, (b->active + (shift ? b->ntabs - 1 : 1)) % b->ntabs);
        return 1;
    }
    if (b->fullscreen && k == SDLK_ESCAPE) { bw_set_fullscreen(w, 0); return 1; }
    if (!cmd) return 0;
    if (shift && k == SDLK_u && bw_tab(w)) { bh_send(OS999B_APP_REMOVE, bw_tab(w)->id, 0, 0, 0, 0, NULL); return 1; }
    if (shift && k == SDLK_i) { press_action(w, P_INSTALL); return 1; }
    if (shift && k == SDLK_a) { press_action(w, P_APPS); return 1; }
    if (k == SDLK_t) { bw_new_tab(w, "https://www.youtube.com", 1); begin_edit(w); return 1; }
    if (k == SDLK_w) {
        if (b->ntabs) bw_close_tab(w, b->active); else bw_close_window(w);
        return 1;
    }
    if (k == SDLK_n || k == SDLK_b) { bw_open_window("https://www.youtube.com"); return 1; }
    if (k == SDLK_l) { begin_edit(w); return 1; }
    if (k == SDLK_r) { press_action(w, P_RELOAD); return 1; }
    if (k == SDLK_LEFTBRACKET && !shift) { press_action(w, P_BACK); return 1; }
    if (k == SDLK_RIGHTBRACKET && !shift) { press_action(w, P_FORWARD); return 1; }
    if ((k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET) && b->ntabs > 1) {
        int step = k == SDLK_RIGHTBRACKET ? 1 : b->ntabs - 1;
        bw_activate_tab(w, (b->active + step) % b->ntabs);
        return 1;
    }
    return 0;
}

static int button_of(const SDL_MouseButtonEvent *e) {
    return e->button == SDL_BUTTON_RIGHT ? 2 : e->button == SDL_BUTTON_MIDDLE ? 1 : 0;
}

void bw_cancel_interaction(void) {
    int w = BS.focus_win;
    if (BS.composing && w >= 0 && bw_tab(w))
        bh_send(OS999B_IME_CANCEL, bw_tab(w)->id, 0, 0, 0, 0, NULL);
    BS.capture = BS.drag = BS.grow = BS.pressed = BS.composing = 0;
}

static int window_event(Session *s, SDL_Window *win, const SDL_Event *ev) {
    bw_application_sync(s);
    if (finder_apps_pointer_capture(s) && (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONUP)) return 0;
    if (BS.norder == 0 || s->browser_hidden) return 0;
    if (finder_apps_modal(s)) {
        if (BS.focus) { BS.overlay_focus = 1; set_focus(-1, 0); }
        bw_cancel_interaction();
        return 0;
    }
    if (BS.overlay_focus) {
        BS.overlay_focus = 0;
        if (BS.focus_win >= 0 && BS.win[BS.focus_win].open) set_focus(BS.focus_win, 1);
    }
    int lx = 0, ly = 0, w;
    switch (ev->type) {
    case SDL_MOUSEBUTTONDOWN: {
        logical(win, ev->button.x, ev->button.y, &lx, &ly);
        if (app_switcher_hit(s,lx,ly)) return 0;
        if (ly < TITLE_H) return 0;
        w = window_at(s, lx, ly);
        if (w < 0) {  /* click on the desktop: browsers lose the keyboard */
            if (BS.focus_win >= 0) BS.win[BS.focus_win].editing = 0;
            set_focus(-1, 0);
            BS.focus_win = -1;
            s->browser_focused = 0; s->finder_hidden = 0;
            return 0;
        }
        s->browser_focused = 1; s->media_focused = s->guest_focused = 0;
        s->app_request = 4; finder_apps_note(s, 4); appearance_panel_deactivate();
        BrowserWin *b = &BS.win[w];
        if (bw_top() != w) bw_raise(w);
        Rect c = bw_content(b);
        if (bw_has(c, lx, ly)) {
            b->editing = 0;
            set_focus(w, 1);
            BS.capture = 1;
            BS.press_win = w;
            mouse(w, OS999B_MOUSE_DOWN, lx, ly, button_of(&ev->button), ev->button.clicks);
            return 1;
        }
        set_focus(w, 1);
        if (button_of(&ev->button) != 0) return 1;
        BS.press_win = w;
        BS.px = lx; BS.py = ly; BS.ow = b->w; BS.oh = b->h;
        BS.pressed = chrome_hit(w, lx, ly);
        if (BS.pressed >= P_TAB && BS.pressed < P_TABX) {  /* tabs switch on press */
            press_action(w, BS.pressed);
            BS.pressed = P_NONE;
        } else if (BS.pressed == P_NONE) {
            if (bw_has(bw_grow_rect(b), lx, ly)) BS.grow = 1;
            else if (bw_has(bw_url_rect(b), lx, ly)) begin_edit(w);
            else if (ly < b->y + BW_TITLE) {
                BS.drag = 1; BS.px = lx - b->x; BS.py = ly - b->y;
            }
        }
        return 1;
    }
    case SDL_MOUSEBUTTONUP:
        logical(win, ev->button.x, ev->button.y, &lx, &ly);
        w = BS.press_win;
        if (BS.capture && w >= 0) {
            mouse(w, OS999B_MOUSE_UP, lx, ly, button_of(&ev->button), ev->button.clicks);
            if (SDL_GetMouseState(NULL, NULL) == 0) BS.capture = 0;
            return 1;
        }
        if (BS.drag || BS.grow) { BS.drag = BS.grow = 0; return 1; }
        if (BS.pressed && w >= 0 && BS.win[w].open) {
            int p = BS.pressed;
            BS.pressed = P_NONE;
            if (chrome_hit(w, lx, ly) == p) press_action(w, p);
            return 1;
        }
        return window_at(s, lx, ly) >= 0;
    case SDL_MOUSEMOTION: {
        logical(win, ev->motion.x, ev->motion.y, &lx, &ly);
        w = BS.press_win;
        if ((BS.drag || BS.grow) && w >= 0) {
            BrowserWin *b = &BS.win[w];
            if (BS.drag) {
                b->x = lx - BS.px;
                b->y = ly - BS.py;
                if (b->y < TITLE_H) b->y = TITLE_H;
                if (b->x > OSPI_W - 60) b->x = OSPI_W - 60;
                if (b->x + b->w < 60) b->x = 60 - b->w;
                if (b->y > OSPI_H - 30) b->y = OSPI_H - 30;
            } else {
                b->w = BS.ow + lx - BS.px;
                b->h = BS.oh + ly - BS.py;
                if (b->w < BW_MIN_W) b->w = BW_MIN_W;
                if (b->h < BW_MIN_H) b->h = BW_MIN_H;
                if (b->x + b->w > OSPI_W) b->w = OSPI_W - b->x;
                if (b->y + b->h > OSPI_H) b->h = OSPI_H - b->y;
            }
            return 1;
        }
        int over = BS.capture ? BS.press_win : window_at(s, lx, ly);
        if (over >= 0 && BS.fd >= 0 && (BS.capture || bw_has(bw_content(&BS.win[over]), lx, ly))) {
            BS.hover = over + 1;
            update_cursor(over, 1);
            mouse(over, OS999B_MOUSE_MOVE, lx, ly, 0, 0);
            return 1;
        }
        if (BS.hover) {
            int was = BS.hover - 1;
            BS.hover = 0;
            update_cursor(-1, 0);
            if (BS.win[was].open) mouse(was, OS999B_MOUSE_LEAVE, lx, ly, 0, 0);
        }
        return over >= 0;
    }
    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        logical(win, mx, my, &lx, &ly);
        w = window_at(s, lx, ly);
        if (w < 0) return 0;
        if (BS.fd < 0 || !bw_has(bw_content(&BS.win[w]), lx, ly)) return 1;
        float fx = ev->wheel.preciseX, fy = ev->wheel.preciseY;
        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { fx = -fx; fy = -fy; }
        int notch = (fy == 1.0f || fy == -1.0f) && fx == 0.0f;
        mouse(w, OS999B_WHEEL, lx, ly, (int)(fx * 10.0f), (int)(fy * (notch ? 40.0f : 10.0f)));
        return 1;
    }
    case SDL_KEYDOWN:
        w = BS.focus_win;
        if (w < 0 || !BS.focus || !BS.win[w].open) return 0;
        if ((ev->key.keysym.mod & KMOD_GUI) && ev->key.keysym.sym == SDLK_q) return 0;
        if (BS.win[w].editing) {
            SDL_Keycode k = ev->key.keysym.sym;
            if ((ev->key.keysym.mod & KMOD_GUI) && k != SDLK_v && k != SDLK_a && shortcut(w, &ev->key))
                return 1;
            return edit_key(w, &ev->key);
        }
        if (shortcut(w, &ev->key)) return 1;
        if (BS.composing) return 1;  /* the input method owns this key */
        key(w, OS999B_KEY_DOWN, &ev->key);
        return 1;
    case SDL_KEYUP:
        w = BS.focus_win;
        if (w < 0 || !BS.focus || !BS.win[w].open) return 0;
        if (!BS.win[w].editing && !BS.composing) key(w, OS999B_KEY_UP, &ev->key);
        return 1;
    case SDL_TEXTEDITING:
    case SDL_TEXTEDITING_EXT: {
        w = BS.focus_win;
        if (w < 0 || !BS.focus || !BS.win[w].open || BS.win[w].editing || !bw_tab(w)) return 0;
        const char *text = ev->type == SDL_TEXTEDITING ? ev->edit.text : ev->editExt.text;
        int start = ev->type == SDL_TEXTEDITING ? ev->edit.start : ev->editExt.start;
        BS.composing = text && text[0];
        if (BS.composing) bh_send(OS999B_IME_SET, bw_tab(w)->id, 0, 0, start, 0, text);
        else bh_send(OS999B_IME_CANCEL, bw_tab(w)->id, 0, 0, 0, 0, NULL);
        if (ev->type == SDL_TEXTEDITING_EXT) SDL_free(ev->editExt.text);
        return 1;
    }
    case SDL_TEXTINPUT:
        w = BS.focus_win;
        if (w < 0 || !BS.focus || !BS.win[w].open) return 0;
        if (BS.win[w].editing) {
            BrowserWin *b = &BS.win[w];
            edit_snapshot(b);
            if (b->edit_fresh) b->edit[0] = 0;
            b->edit_fresh = 0;
            strncat(b->edit, ev->text.text, sizeof b->edit - strlen(b->edit) - 1);
        } else if (bw_tab(w) && !(SDL_GetModState() & (KMOD_GUI | KMOD_CTRL))) {
            bh_send(OS999B_CHAR, bw_tab(w)->id, 0, 0, 0, 0, ev->text.text);
        }
        BS.composing = 0;
        return 1;
    case SDL_WINDOWEVENT: {
        /* Leaving the app drops the browser's keyboard focus; coming back
         * restores it, so shortcuts and typing work without an extra click. */
        static int refocus;
        if (ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST && BS.focus_win >= 0) {
            refocus = BS.focus;
            set_focus(BS.focus_win, 0);
        } else if (ev->window.event == SDL_WINDOWEVENT_FOCUS_GAINED && refocus) {
            refocus = 0;
            if (BS.focus_win >= 0 && BS.win[BS.focus_win].open) set_focus(BS.focus_win, 1);
        }
        return 0;
    }
    default:
        return 0;
    }
}

int browser_window_event(Session *s, SDL_Window *win, const SDL_Event *ev) {
    int handled = window_event(s, win, ev);
    bw_publish_menu_state(s);
    return handled;
}
