#include "../src/browser_window_int.h"
#include "browser_overlay.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "desktop_gesture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the real frame compositor with an opaque browser surface, without
 * launching Chromium, creating a host window, or making network requests. */
BrowserState BS;
static int failures, placements, hole_count, last_visible;
static int key_messages, focus_loss, focus_gain;
static uint32_t last_modifiers;
static int ime_cancellations;
static char clipboard[OS999B_TEXT];
static OverlayRect captured[BW_MAX_WIN + BW_MAX_OCCLUDERS];
static uint32_t frame[OSPI_DEFAULT_W * OSPI_DEFAULT_H];
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* No SDL dylib constructors: this test must remain headless, including when
 * instrumented. Only unused window-management paths need these symbols. */
int SDL_ShowCursor(int toggle) { return toggle; }
void SDL_SetCursor(SDL_Cursor *cursor) { (void)cursor; }
SDL_Cursor *SDL_GetDefaultCursor(void) { return NULL; }
SDL_bool SDL_SetHint(const char *name, const char *value) {
    (void)name; (void)value; return SDL_TRUE;
}
void SDL_GetWindowSize(SDL_Window *w, int *x, int *y) { (void)w; *x = OSPI_W; *y = OSPI_H; }
Uint32 SDL_GetWindowFlags(SDL_Window *w) { (void)w; return SDL_WINDOW_INPUT_FOCUS; }
SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }
Uint32 SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor id) { (void)id; return NULL; }
char *SDL_GetClipboardText(void) { return strdup(clipboard); }
int SDL_SetClipboardText(const char *text) { snprintf(clipboard, sizeof clipboard, "%s", text); return 0; }
void SDL_free(void *p) { free(p); }
void bh_send_full(Os999BrowserMsg *m) {
    if (m->kind == OS999B_KEY_DOWN) { key_messages++; last_modifiers = m->modifiers; }
}

int bh_ensure(const char *root) { (void)root; return 0; }
void bh_send(uint32_t k, int id, int x, int y, int a, int b, const char *t) {
    (void)k; (void)id; (void)x; (void)y; (void)a; (void)b; (void)t;
    if (k == OS999B_IME_CANCEL) ime_cancellations++;
    if (k == OS999B_FOCUS) { if (a) focus_gain++; else focus_loss++; }
}
void bh_drain(void) {}
void bh_stop(void) {}
void bw_script_tick(Session *s, SDL_Window *w) { (void)s; (void)w; }
void overlay_window_open(int w, char *target, size_t n) { (void)w; if (n) target[0] = 0; }
void overlay_window_close(int w) { (void)w; }
void overlay_bind(int w, int id) { (void)w; (void)id; }
float overlay_scale(SDL_Window *w, int logical) { (void)w; (void)logical; return 1; }
uint64_t overlay_frames(int w) { (void)w; return 1; }
void overlay_draw_cpu(int w, uint32_t *pixels, int fw, int fh, OverlayRect content) {
    (void)w; (void)pixels; (void)fw; (void)fh; (void)content;
}
void bw_paint(Fb *fb, int w, int key) {
    (void)w; (void)key;
    fb_fill(fb, 0, 0, OSPI_W, OSPI_H, 0xFF112233);
}
void overlay_place(SDL_Window *w, int lw, int lh, int slot, int visible,
                   int order, OverlayRect content, const OverlayRect *holes, int n) {
    (void)w; (void)lw; (void)lh; (void)slot; (void)visible; (void)order; (void)content;
    CHECK(n <= BW_MAX_WIN + BW_MAX_OCCLUDERS);
    placements++; hole_count = n; last_visible = visible;
    if (n <= BW_MAX_WIN + BW_MAX_OCCLUDERS) memcpy(captured, holes, (size_t)n * sizeof *holes);
}

static int inside(OverlayRect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void check_frame(Session *s, int parent, int child, int fullscreen, int expected_holes) {
    s->menu = parent; s->menu_child = child;
    BS.win[0].fullscreen = fullscreen;
    placements = hole_count = 0;
    for (int i = 0; i < OSPI_W * OSPI_H; i++) frame[i] = 0xFF000000u | (uint32_t)i;
    browser_window_frame(s, NULL, frame, 0);
    CHECK(placements == 1);
    CHECK(hole_count == expected_holes);
    for (int i = 0; i < (parent < 0 ? 0 : child ? 2 : 1); i++) {
        Rect first, last;
        int menu = i ? child : parent;
        menu_item_rect(menu, 0, &first);
        menu_item_rect(menu, menu_nitems(menu) - 1, &last);
        CHECK(inside(captured[i], first.x, first.y));
        CHECK(inside(captured[i], last.x + last.w - 1, last.y + last.h - 1));
    }
    /* Check every pixel, including gaps between the two popup rectangles. */
    for (int y = 0; y < OSPI_H; y++) for (int x = 0; x < OSPI_W; x++) {
        int preserved = 0;
        for (int i = 0; i < hole_count; i++) preserved |= inside(captured[i], x, y);
        uint32_t expected = preserved ? 0xFF000000u | (uint32_t)(y * OSPI_W + x) : 0xFF112233;
        if (frame[y * OSPI_W + x] != expected) { CHECK(0); return; }
    }
}

static int coverage[20][30];
static void mark_region(OverlayRect r, void *context) {
    (void)context;
    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= 30 && r.y + r.h <= 20);
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) coverage[y][x]++;
}
static void check_mask(void) {
    OverlayRect holes[] = {{3, 2, 12, 9}, {10, 5, 14, 12}, {3, 2, 12, 9},
                           {-10, -10, 12, 13}, {27, 18, 50, 50}, {0, 0, 0, 5}};
    CHECK(overlay_visible_regions((OverlayRect){0, 0, 30, 20}, holes, 6, mark_region, NULL) == 0);
    for (int y = 0; y < 20; y++) for (int x = 0; x < 30; x++) {
        int hidden = 0;
        for (int i = 0; i < 6; i++) hidden |= inside(holes[i], x, y);
        CHECK(coverage[y][x] == !hidden);
    }
    CHECK(overlay_visible_regions((OverlayRect){0, 0, 30, 20}, holes, 65, mark_region, NULL) == -1);
}

static void expect_region(Rect r) {
    int found = 0;
    for (int i = 0; i < hole_count; i++)
        found |= captured[i].x == r.x && captured[i].y == r.y &&
                 captured[i].w == r.w && captured[i].h == r.h;
    CHECK(found);
}
static int media_bounds(const Session *s, Rect *r) {
    (void)s; *r = (Rect){300, 200, 540, 400}; return 1;
}
static int media_active(void) { return 1; }
static void check_input_overlay(Session *s) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = SDLK_a;
    BS.focus_win = 0; BS.focus = 1; BS.overlay_focus = 0;
    BS.win[0].ntabs = 1; BS.win[0].tabs[0].id = 1;
    BS.capture = BS.drag = BS.grow = BS.composing = 1;
    int before = key_messages, lost = focus_loss, cancellations = ime_cancellations;
    CHECK(!browser_window_event(s, NULL, &event));
    CHECK(key_messages == before && focus_loss == lost + 1);
    CHECK(!BS.focus && !BS.capture && !BS.drag && !BS.grow && !BS.composing);
    CHECK(ime_cancellations == cancellations + 1);
    CHECK(!browser_window_event(s, NULL, &event));
    CHECK(focus_loss == lost + 1 && ime_cancellations == cancellations + 1);
}
static void check_input_restore(Session *s) {
    SDL_Event event = {.type = SDL_KEYDOWN}; event.key.keysym.sym = SDLK_a;
    int before = key_messages, gained = focus_gain;
    CHECK(browser_window_event(s, NULL, &event));
    CHECK(key_messages == before + 1 && focus_gain == gained + 1 && BS.focus);
}
static void check_overlays(Session *s) {
    s->sw = OSPI_W; s->sh = OSPI_H;
    s->dialog = 1;
    check_frame(s, -1, 0, 0, 2);
    Rect r, cancel, ok;
    dialog_layout(s, &r, &cancel, &ok); r.w += 3; r.h += 3;
    expect_region(r);
    expect_region((Rect){0, 0, OSPI_W, TITLE_H});
    check_input_overlay(s);
    s->dialog = 0;
    check_input_restore(s);
    session_command(s, CMD_APPEARANCE_DESKTOP);
    check_frame(s, -1, 0, 0, 2);
    expect_region((Rect){(OSPI_W - 600) / 2, (OSPI_H - 420) / 2, 603, 423});
    /* Appearance's own contextual menu is also a separate occluder. */
    CHECK(appearance_panel_context(s, 300, 300));
    Rect panels[2]; CHECK(appearance_panel_regions(s, panels) == 2);
    check_frame(s, -1, 0, 1, 3); expect_region(panels[1]);
    check_input_overlay(s);
    session_command(s, CMD_APP_BROWSER);
    check_frame(s, -1, 0, 0, 0);
    CHECK(!appearance_panel_active());
    CHECK(!appearance_panel_key(s, 27, 0));
    CHECK(appearance_panel_regions(s, panels) == 1); /* retained panel, popup dismissed */
    SDL_Event key = {.type = SDL_KEYDOWN}; key.key.keysym.sym = SDLK_a;
    int sent = key_messages;
    CHECK(browser_window_event(s, NULL, &key)); CHECK(key_messages == sent + 1);
    session_command(s, CMD_APP_FINDER);
    CHECK(appearance_panel_active());
    check_frame(s, -1, 0, 0, 2);
    appearance_panel_close();
    session_command(s, CMD_APP_BROWSER); bw_application_sync(s);
    CHECK(context_open(s, 20, 300));
    CHECK(context_bounds(s, &r));
    check_frame(s, -1, 0, 0, 2); expect_region(r);
    check_input_overlay(s);
    context_close();
    check_input_restore(s);
    static const SessionMediaHooks hooks = {.bounds = media_bounds, .active = media_active};
    session_set_media_hooks(&hooks);
    check_frame(s, -1, 0, 0, 2);
    expect_region((Rect){300, 200, 540, 400});
    check_input_overlay(s);
    session_set_media_hooks(NULL);
    check_input_restore(s);
    /* Startup/app hiding must suppress CPU chrome as well as the GPU layer. */
    for (int i = 0; i < OSPI_W * OSPI_H; i++) frame[i] = 0xFFABCDEF;
    browser_window_frame(s, NULL, frame, 1);
    CHECK(!last_visible);
    for (int i = 0; i < OSPI_W * OSPI_H; i++) if (frame[i] != 0xFFABCDEF) { CHECK(0); break; }
}

static void check_guest_stacking(Session *s) {
    s->guest_active = 1; s->guest_bounds = (Rect){180, 120, 400, 300};
    session_command(s, CMD_APP_GUEST);
    check_frame(s, -1, 0, 0, 2);
    expect_region((Rect){180, 120, 403, 303});
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.x = 200; click.button.y = 160; click.button.button = SDL_BUTTON_LEFT;
    CHECK(!browser_window_event(s, NULL, &click));
    CHECK(s->guest_focused && !s->browser_focused);
    click.button.x = 900; click.button.y = 500;
    CHECK(browser_window_event(s, NULL, &click));
    CHECK(s->browser_focused && !s->guest_focused);
    int focus = 1; finder_apps_sync(s, 1, "Fixture Guest", &focus);
    CHECK(!focus && s->browser_focused);
    check_frame(s, -1, 0, 0, 0);
    /* A main-loop click on the exposed guest requests that application. */
    s->app_request = 2; focus = 1;
    finder_apps_sync(s, 1, "Fixture Guest", &focus);
    CHECK(focus && s->guest_focused && !s->browser_focused);
    s->guest_hidden = 1;
    check_frame(s, -1, 0, 0, 0);
    s->guest_active = s->guest_focused = s->guest_hidden = 0;
    session_command(s, CMD_APP_BROWSER); bw_application_sync(s);
}

static const Session *modeless_owner;
static int modeless_running(const Session *s) { return s == modeless_owner; }
static int modeless_active(void) { return modeless_owner && modeless_owner->media_focused; }
static void check_background_order(Session *s) {
    static const SessionMediaHooks hooks = {.bounds=media_bounds, .running=modeless_running, .active=modeless_active};
    modeless_owner = s; session_set_media_hooks(&hooks);
    s->nwin = 1; s->win[0] = (Window){.x=80, .y=100, .w=300, .h=250, .kind=WIN_FOLDER};
    s->guest_active = 1; s->guest_bounds = (Rect){180, 120, 400, 300};
    session_command(s, CMD_APP_MEDIA); session_command(s, CMD_APP_FINDER);
    session_command(s, CMD_APP_BROWSER); session_command(s, CMD_APP_GUEST);
    check_frame(s, -1, 0, 0, 2); /* Only guest and menu bar are above Browser. */
    expect_region((Rect){180, 120, 403, 303});
    SDL_Event click = {.type=SDL_MOUSEBUTTONDOWN}; click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 110; click.button.y = 140;
    CHECK(browser_window_event(s, NULL, &click)); /* Browser is above this Finder window. */
    session_command(s, CMD_APP_GUEST); click.button.x = 200; click.button.y = 160;
    CHECK(!browser_window_event(s, NULL, &click));
    session_command(s, CMD_APP_FINDER); check_frame(s, -1, 0, 0, 3);
    Layout lay; window_layout(&s->win[0], &lay); Rect r = lay.outer; r.w += 3; r.h += 3;
    expect_region(r); expect_region((Rect){180, 120, 403, 303});
    session_command(s, CMD_APP_MEDIA); check_frame(s, -1, 0, 0, 4);
    CHECK(modeless_active() && !finder_apps_modal(s));
    click.button.x = 900; click.button.y = 600;
    CHECK(browser_window_event(s, NULL, &click)); /* Exposed Browser works beside player. */
    CHECK(s->browser_focused && !s->media_focused);
    session_set_media_hooks(NULL); modeless_owner = NULL;
    s->guest_active = s->guest_focused = s->guest_hidden = 0; s->nwin = 0;
    session_command(s, CMD_APP_BROWSER);
}

static void check_palette_and_drag(Session *s) {
    s->sw=OSPI_W;s->sh=OSPI_H;
    session_command(s,CMD_APP_SWITCHER); Rect r; CHECK(app_switcher_bounds(s,&r));
    check_frame(s,-1,0,0,2);expect_region(r);
    SDL_Event event={.type=SDL_MOUSEBUTTONDOWN}; event.button.button=SDL_BUTTON_LEFT;
    event.button.x=r.x+30;event.button.y=r.y+10;
    CHECK(!browser_window_event(s,NULL,&event) && s->browser_focused);
    session_down(s,event.button.x,event.button.y,1);CHECK(s->switcher.gesture);
    event.type=SDL_MOUSEMOTION;event.motion.x=200;event.motion.y=300;
    CHECK(!browser_window_event(s,NULL,&event));
    session_move(s,200,300);session_up(s,200,300);CHECK(s->browser_focused);
    s->switcher.open=0;s->down=1;s->drag=3;s->press_zone=Z_ITEM;s->mx=600;s->my=500;
    check_frame(s,-1,0,0,9); /* Eight outline edges and the menu bar. */
    Rect edges[DESKTOP_GESTURE_REGIONS];int n=session_gesture_regions(s,edges);
    for(int i=0;i<n;i++) expect_region(edges[i]);
    int center_masked=0;for(int i=0;i<hole_count;i++)center_masked|=inside(captured[i],600,500);
    CHECK(!center_masked);
    CHECK(!browser_window_event(s,NULL,&event));
    event.type=SDL_MOUSEBUTTONUP;event.button.x=600;event.button.y=500;
    CHECK(!browser_window_event(s,NULL,&event));
    s->down=s->drag=0;s->press_zone=Z_NONE;
}
static void check_application(Session *s) {
    s->menu = -1; s->menu_child = 0;
    CHECK(finder_apps_enabled(s, CMD_APP_BROWSER));
    BS.composing = 1; int cancelled = ime_cancellations;
    session_command(s, CMD_APP_HIDE);
    CHECK(s->browser_hidden && !s->browser_focused && !s->finder_hidden);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(!last_visible && !BS.focus && !BS.composing);
    CHECK(ime_cancellations == cancelled + 1);
    SDL_Event key = {.type = SDL_KEYDOWN}; key.key.keysym.sym = SDLK_a;
    CHECK(!browser_window_event(s, NULL, &key));
    session_command(s, CMD_APP_SHOW_ALL);
    CHECK(!s->browser_hidden);
    session_command(s, CMD_APP_BROWSER);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(s->browser_focused && BS.focus);
    session_command(s, CMD_APP_HIDE_OTHERS);
    CHECK(s->finder_hidden && !s->browser_hidden);
    session_command(s, CMD_APP_FINDER);
    CHECK(!s->finder_hidden && !s->browser_focused);
    s->nwin = 1;
    s->win[0] = (Window){.x = 80, .y = 100, .w = 300, .h = 250, .kind = WIN_FOLDER};
    for (int i = 0; i < OSPI_W * OSPI_H; i++) frame[i] = 0xFFABCDEF;
    browser_window_frame(s, NULL, frame, 0);
    Layout lay; window_layout(&s->win[0], &lay);
    CHECK(frame[(lay.outer.y + 30) * OSPI_W + lay.outer.x + 30] == 0xFFABCDEF);
    CHECK(!browser_window_event(s, NULL, &key));
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.x = lay.outer.x + 30; click.button.y = lay.outer.y + 30;
    click.button.button = SDL_BUTTON_LEFT;
    CHECK(!browser_window_event(s, NULL, &click));
    click.button.x = 900; click.button.y = 200;
    CHECK(browser_window_event(s, NULL, &click));
    CHECK(s->browser_focused && !s->media_focused && !s->guest_focused);
    BS.fd = 42; bw_publish_menu_state(s);
    int before_keys = key_messages;
    session_command(s, CMD_COPY);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(key_messages == before_keys + 1 && (last_modifiers & F_CMD));
    BrowserWin *b = &BS.win[bw_top()];
    b->editing = 1; b->edit_fresh = 0; snprintf(b->edit, sizeof b->edit, "about:blank");
    bw_publish_menu_state(s);
    session_command(s, CMD_SELECT_ALL); browser_window_frame(s, NULL, frame, 0);
    CHECK(b->edit_fresh);
    session_command(s, CMD_COPY); browser_window_frame(s, NULL, frame, 0);
    CHECK(!strcmp(clipboard, "about:blank"));
    snprintf(clipboard, sizeof clipboard, "https://example.test/");
    session_command(s, CMD_PASTE); browser_window_frame(s, NULL, frame, 0);
    CHECK(!strcmp(b->edit, clipboard) && !b->edit_fresh);
    session_command(s, CMD_UNDO); browser_window_frame(s, NULL, frame, 0);
    CHECK(!strcmp(b->edit, "about:blank") && b->edit_fresh);
    session_command(s, CMD_UNDO); browser_window_frame(s, NULL, frame, 0);
    CHECK(!strcmp(b->edit, clipboard) && !b->edit_fresh);
    CHECK(key_messages == before_keys + 1);
    b->editing = 0; BS.fd = -1;
    int before_windows = BS.norder;
    session_command(s, CMD_NEW_FOLDER);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(BS.norder == before_windows + 1 && s->nwin == 1 && !s->dialog);
    session_command(s, CMD_CLOSE_ALL);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(!BS.norder && s->browser_active && !s->quit && s->nwin == 1);
    CHECK(!s->browser_windows && !s->browser_edit_flags);
    menu_update(s);
    CHECK(!menu_cmd_for_key('w') && !menu_cmd_for_key('c') && !menu_cmd_for_key('z'));
    CHECK(menu_cmd_for_key('n') == CMD_NEW_FOLDER && menu_cmd_for_key('q') == CMD_QUIT);
    int inactive[] = {CMD_CLOSE, CMD_CLOSE_ALL, CMD_COPY, CMD_PASTE, CMD_SELECT_ALL, CMD_UNDO};
    for (unsigned i = 0; i < sizeof inactive / sizeof inactive[0]; i++) {
        session_command(s, inactive[i]); CHECK(!s->browser_action);
    }
    CHECK(s->nwin == 1 && !s->quit);
    session_command(s, CMD_APP_BROWSER);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(BS.norder == 1 && s->browser_focused);
    /* A disconnected helper must not leave an unclosable empty window. */
    CHECK(!BS.win[bw_top()].ntabs);
    CHECK(s->browser_windows == 1 && !s->browser_edit_flags);
    menu_update(s); CHECK(menu_cmd_for_key('w') == CMD_CLOSE && !menu_cmd_for_key('c'));
    BrowserWin *empty = &BS.win[bw_top()]; empty->editing = 1; empty->edit[0] = 0;
    bw_publish_menu_state(s); menu_update(s);
    CHECK(s->browser_edit_flags == BROWSER_CAN_PASTE);
    CHECK(menu_cmd_for_key('v') == CMD_PASTE && !menu_cmd_for_key('a') && !menu_cmd_for_key('z'));
    empty->editing = 0;
    key.key.keysym.sym = SDLK_w; key.key.keysym.mod = KMOD_GUI;
    CHECK(browser_window_event(s, NULL, &key));
    CHECK(!BS.norder && s->browser_active && !s->quit);
    s->guest_active = 1;
    session_command(s, CMD_APP_GUEST); session_command(s, CMD_APP_BROWSER);
    browser_window_frame(s, NULL, frame, 0);
    session_command(s, CMD_QUIT);
    browser_window_frame(s, NULL, frame, 0);
    CHECK(s->guest_focused && s->app_request == 2);
    CHECK(!BS.norder && !s->browser_active && !s->browser_focused && !s->quit);
    CHECK(!finder_apps_enabled(s, CMD_APP_BROWSER));
    s->guest_active = s->guest_focused = 0;
    s->nwin = 0;
}

static void check_tab_closure(void) {
    BS = (BrowserState){.fd = -1, .norder = 1, .order = {0}, .focus_win = 0, .focus = 1};
    BrowserWin *b = &BS.win[0];
    *b = (BrowserWin){.open = 1, .ntabs = 4, .active = 2, .editing = 1};
    for (int i = 0; i < 4; i++) b->tabs[i].id = 10 + i;
    snprintf(b->edit, sizeof b->edit, "unfinished search");
    int gained = focus_gain, lost = focus_loss;
    bw_close_tab(0, 0);                         /* background tab before active */
    CHECK(b->ntabs == 3 && b->active == 1 && bw_tab(0)->id == 12);
    CHECK(b->editing && !strcmp(b->edit, "unfinished search"));
    CHECK(focus_gain == gained && focus_loss == lost);
    bw_close_tab(0, 2);                         /* background tab after active */
    CHECK(b->ntabs == 2 && b->active == 1 && bw_tab(0)->id == 12);
    CHECK(b->editing && focus_gain == gained && focus_loss == lost);
    bw_close_tab(0, 1);                         /* active last tab selects left */
    CHECK(b->ntabs == 1 && b->active == 0 && bw_tab(0)->id == 11);
    CHECK(!b->editing && focus_gain == gained + 1);
    bw_close_tab(0, 0);
    CHECK(!b->open && !BS.norder && BS.focus_win == -1);
    BS.norder = 1; BS.order[0] = 0; BS.focus_win = 0;
    *b = (BrowserWin){.open = 1, .ntabs = 3, .active = 1};
    for (int i = 0; i < 3; i++) b->tabs[i].id = 20 + i;
    bw_close_tab(0, 1);                         /* active middle selects right */
    CHECK(b->ntabs == 2 && b->active == 1 && bw_tab(0)->id == 22);
    browser_window_close();
}

int main(void) {
    check_mask();
    check_tab_closure();
    unsetenv("OSPI_BROWSER_URL");
    Session *s = calloc(1, sizeof *s);
    if (!s) return 1;
    BS.fd = -1; BS.norder = 1; BS.order[0] = 0; BS.focus_win = 0;
    BS.win[0] = (BrowserWin){.open = 1, .x = 40, .y = 30, .w = 940, .h = 720};
    s->browser_active = s->browser_focused = 1;
    menu_update(s);
    check_frame(s, -1, 0, 0, 0);
    check_frame(s, 1, 0, 0, 2);
    check_frame(s, 1, MENU_LABELS, 0, 3);
    check_frame(s, 3, MENU_ARRANGE, 0, 3);
    check_frame(s, 0, MENU_PANELS, 0, 3);
    check_frame(s, 7, 0, 0, 2);
    check_frame(s, 1, MENU_LABELS, 1, 3);
    check_overlays(s);
    check_guest_stacking(s);
    check_background_order(s);
    check_palette_and_drag(s);
    check_application(s);
    free(s);
    printf("Browser menu composition: %d failures\n", failures);
    return failures != 0;
}
