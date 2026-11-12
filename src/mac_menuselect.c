#include "mac_menu_int.h"
#include "mac_mdef.h"
#include <stdlib.h>
#include <string.h>
extern const uint8_t font8x8_basic[128][8];
extern const uint8_t font8x8_pi[8];

/* Menu tracking: MenuSelect ($A93D), HiliteMenu ($A938) and MenuKey ($A93E), on the menus the guest registered with InsertMenu (read
 * through the validated records of mac_menu_records.c).
 *
 * MenuSelect(startPt): Point, result longint above it. The press must be in the menu bar over an enabled menu's title (the title
 * cells are the ones DrawMenuBar lays out: 16 pixels of padding around each 8-pixel-per-character title). The title is highlighted and a
 * pull-down is drawn under it; each slice the pointer and mouse-up events are read until the button is released; releasing over an
 * enabled item returns (menuID << 16) | item and leaves the title highlighted until HiliteMenu(0); anywhere else returns 0 and
 * un-highlights. Moving along the bar to another enabled title switches menus. The trap rewinds and yields while tracking, as the
 * modal dialogs do.
 *
 * Drawing is this project's own chrome in the project's own 8x8 face (not a system menu font): a light panel with a dark frame, 12
 * pixels per item, a '-' item as a separator line, disabled items dithered, the item's mark character in the left column, a command key
 * shown as '#' and its letter, the highlighted item inverted. Icons, hierarchical (command key 27... mark = submenu ID) items
 * and colour menus are not supported and halt by name. Only an 8-bit screen is supported here.
 *
 * A menu with a custom definition procedure (see mac_mdef.c) is tracked by the same trap, but its sizing, drawing and choosing are the 68K
 * procedure's: the size message first (the menu may have changed since it was last sized), then for a pop-up the pop-up message, then the
 * draw message with the rectangle, then one choose message per slice with the pointer and the item chosen so far; the item it answers on the
 * last one, after the button is released, is the result. Each message is a guest call whose completion (sel_done) carries on the tracking,
 * so the trap stays "unfinished" across them exactly as it does across slices. The screen under the menu is saved and restored by us. */
enum { ITEM_H = 12, MAX_ITEMS = 31 };
typedef struct { int id, nitems; unsigned flags; char title[256]; int tlen; struct { char text[256]; int len, cmd, mark; } it[MAX_ITEMS]; } MenuView;
typedef struct MacMenuSel {
    MenuView mv;                                   /* snapshot of the tracked menu: the guest cannot change it under the tracker */
    uint32_t sp, pc;
    int index, id, item, shown;                    /* the menu being tracked (index in the bar), its ID, the item under the pointer (0 none) */
    int left, top, w, h;                           /* the pull-down rectangle */
    uint8_t *back;
    int px, py;                                    /* last pointer position */
    uint32_t popup;                                /* PopUpMenuSelect: the menu handle (a free-standing menu, not in the bar), else 0 */
    uint32_t mh;                                   /* the tracked menu's handle */
    int custom, phase, released;                   /* custom: its MDEF runs; phase: the message in flight; released: the button was up on this slice */
    int pop_top, pop_left, pop_item;               /* PopUpMenuSelect's arguments for a custom menu */
} Sel;
enum { P_SIZE = 1, P_POPUP, P_DRAW, P_TRACK, P_CHOOSE };

static int fail(MacRuntime *r, const char *why) { mac_cpu_halt(r->cpu, why); return -1; }
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
void mac_menuselect_destroy(MacRuntime *rt) { if (rt->menusel) { free(rt->menusel->back); free(rt->menusel); rt->menusel = NULL; } }

/* A private copy of one registered menu (so nothing the guest changes mid-track can be read twice inconsistently). */
static int view_handle(MacRuntime *r, uint32_t handle, MenuView *v) {
    memset(v, 0, sizeof *v);
    unsigned items = 0; MacHandle *m = mac_menu_record(r, handle, &items);
    if (!m) return 0;
    const uint8_t *b = mac_cpu_ram(r->cpu) + m->pointer;
    v->id = (int16_t)(b[0] << 8 | b[1]); v->flags = (unsigned)b[10] << 24 | (unsigned)b[11] << 16 | (unsigned)b[12] << 8 | b[13];
    v->tlen = b[14]; memcpy(v->title, b + 15, (size_t)v->tlen); v->nitems = (int)items;
    size_t p = 15u + b[14];
    for (unsigned i = 0; i < items; i++) {
        unsigned n = b[p];
        v->it[i].len = (int)n; memcpy(v->it[i].text, b + p + 1, n); v->it[i].cmd = b[p + n + 2]; v->it[i].mark = b[p + n + 3];
        p += n + 5u;
    }
    return 1;
}
static int view(MacRuntime *r, unsigned index, MenuView *v) {
    unsigned count = 0; MacHandle *list = mac_menu_list(r, &count);
    if (!list || index >= count) return memset(v, 0, sizeof *v), 0;
    return view_handle(r, rd(r, list->pointer + 2 + 4 * index, 4), v);
}
static uint32_t bar_handle(MacRuntime *r, unsigned index) {
    unsigned c = 0; MacHandle *l = mac_menu_list(r, &c);
    return l && index < c ? rd(r, l->pointer + 2 + 4 * index, 4) : 0;
}
static int handle_custom(MacRuntime *r, uint32_t handle) { MacHandle *m = mac_menu_record(r, handle, NULL); return m && mac_mdef_of_menu(r, m) != 0; }
static unsigned bar_count(MacRuntime *r) { unsigned c = 0; return mac_menu_list(r, &c) ? c : 0; }
static int bar_height(MacRuntime *r) { return (int)rd(r, 0xbaa, 2); }
/* Title cell of menu `index`: x range [x0, x1). */
static int cell(MacRuntime *r, unsigned index, int *x0, int *x1) {
    int left = 16;
    for (unsigned i = 0; i <= index; i++) {
        MenuView v; if (!view(r, i, &v)) return 0;
        if (i == index) { *x0 = left - 8; *x1 = left + v.tlen * 8 + 8; return 1; }
        left += v.tlen * 8 + 16;
    }
    return 0;
}
static int title_at(MacRuntime *r, int x, int y) {
    if (y < 0 || y >= bar_height(r)) return -1;
    for (unsigned i = 0, n = bar_count(r); i < n; i++) { int a, b; if (cell(r, i, &a, &b) && x >= a && x < b) return (int)i; }
    return -1;
}

/* ---- pixels (8-bit screen) ---- */
typedef struct { uint8_t *base; unsigned row, w, h; unsigned light, dark; } Scr;
static int scr_of(MacRuntime *r, Scr *s) {
    if (r->screen_depth != 8 || !r->screen_width || !r->screen_height || r->screen_row_bytes < r->screen_width) return fail(r, "Menu tracking needs the initialized 8-bit screen.");
    size_t z = mac_cpu_ram_size(r->cpu), need = (size_t)r->screen_row_bytes * r->screen_height;
    if (r->screen_base > z || need > z - r->screen_base) return fail(r, "Menu tracking screen memory is invalid.");
    s->base = mac_cpu_ram(r->cpu) + r->screen_base; s->row = r->screen_row_bytes; s->w = r->screen_width; s->h = r->screen_height;
    unsigned min = ~0u, max = 0; s->dark = s->light = 0;
    for (unsigned i = 0; i < 256; i++) { uint32_t c = r->palette[i]; unsigned v = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); if (v < min) { min = v; s->dark = i; } if (v >= max) { max = v; s->light = i; } }
    return 0;
}
static void put(const Scr *s, int x, int y, unsigned c) { if (x >= 0 && y >= 0 && (unsigned)x < s->w && (unsigned)y < s->h) s->base[(size_t)y * s->row + (unsigned)x] = (uint8_t)c; }
static void invert(const Scr *s, int l, int t, int r, int b) {
    for (int y = t; y < b; y++) for (int x = l; x < r; x++)
        if (x >= 0 && y >= 0 && (unsigned)x < s->w && (unsigned)y < s->h) { uint8_t *p = &s->base[(size_t)y * s->row + (unsigned)x]; if (*p == s->light) *p = (uint8_t)s->dark; else if (*p == s->dark) *p = (uint8_t)s->light; }
}
static void glyph(const Scr *s, int x, int y, unsigned ch, int dim) {
    const uint8_t *g = ch == 0x14 ? font8x8_pi : (ch < 128 ? font8x8_basic[ch] : font8x8_basic['?']);
    for (int gy = 0; gy < 8; gy++) for (int gx = 0; gx < 8; gx++) if ((g[gy] & (1u << gx)) && !(dim && ((x + gx + y + gy) & 1))) put(s, x + gx, y + gy, s->dark);
}
static void hilite_title(MacRuntime *r, unsigned index) {
    Scr s; int a, b; if (scr_of(r, &s) || !cell(r, index, &a, &b)) return;
    invert(&s, a, 0, b, bar_height(r) - 1);
}
static int item_enabled(const MenuView *v, int item) { return (v->flags & 1) && (item > 31 || (v->flags & (1u << item))); }
static int is_sep(const MenuView *v, int i) { return v->it[i].len == 1 && v->it[i].text[0] == '-'; }
static void geometry(MacRuntime *r, const MenuView *v, int index, Sel *s) {
    int a, b, w = 0; cell(r, (unsigned)index, &a, &b);
    for (int i = 0; i < v->nitems; i++) { int iw = v->it[i].len * 8 + 24 + (v->it[i].cmd ? 24 : 0); if (iw > w) w = iw; }
    s->w = w + 8; s->h = v->nitems * ITEM_H + 4; s->left = a + 8 + s->w > (int)r->screen_width ? (int)r->screen_width - s->w : a + 8; s->top = bar_height(r) - 1;
}
static void draw_item(const Scr *sc, const MenuView *v, const Sel *s, int i, int hot) {
    int y = s->top + 2 + i * ITEM_H;
    for (int yy = y; yy < y + ITEM_H; yy++) for (int xx = s->left + 2; xx < s->left + s->w - 2; xx++) put(sc, xx, yy, sc->light);
    if (is_sep(v, i)) { for (int xx = s->left + 2; xx < s->left + s->w - 2; xx += 2) put(sc, xx, y + ITEM_H / 2, sc->dark); return; }
    int dim = !item_enabled(v, i + 1);
    if (v->it[i].mark > 0 && v->it[i].mark < 128) glyph(sc, s->left + 4, y + 2, (unsigned)v->it[i].mark == 0x12 ? 'x' : (unsigned)v->it[i].mark, dim);
    for (int k = 0; k < v->it[i].len; k++) glyph(sc, s->left + 16 + k * 8, y + 2, (unsigned char)v->it[i].text[k], dim);
    if (v->it[i].cmd) { glyph(sc, s->left + s->w - 28, y + 2, '#', dim); glyph(sc, s->left + s->w - 20, y + 2, (unsigned char)v->it[i].cmd, dim); }
    if (hot) invert(sc, s->left + 2, y, s->left + s->w - 2, y + ITEM_H);
}
static int save_under(MacRuntime *r, const Scr *sc, Sel *s) {
    size_t n = (size_t)s->w * (size_t)s->h; free(s->back); s->back = malloc(n);
    if (!s->back) return fail(r, "Menu tracking could not save the screen under the menu.");
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) { int sx = s->left + x, sy = s->top + y; s->back[(size_t)y * s->w + x] = (sx >= 0 && sy >= 0 && (unsigned)sx < sc->w && (unsigned)sy < sc->h) ? sc->base[(size_t)sy * sc->row + (unsigned)sx] : 0; }
    return 0;
}
static int draw_menu(MacRuntime *r, const MenuView *v, Sel *s) {
    Scr sc; if (scr_of(r, &sc) || save_under(r, &sc, s)) return -1;
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) put(&sc, s->left + x, s->top + y, (x == 0 || y == s->h - 1 || x == s->w - 1 || (s->popup && y == 0)) ? sc.dark : sc.light);
    for (int i = 0; i < v->nitems; i++) draw_item(&sc, v, s, i, i + 1 == s->item);
    s->shown = 1; return 0;
}
static void erase_menu(MacRuntime *r, Sel *s) {
    Scr sc; if (!s->shown || !s->back || scr_of(r, &sc)) return;
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) put(&sc, s->left + x, s->top + y, s->back[(size_t)y * s->w + x]);
    s->shown = 0;
}
static int item_at(const MenuView *v, const Sel *s, int x, int y) {
    if (x <= s->left || x >= s->left + s->w - 1 || y < s->top + 2 || y >= s->top + 2 + v->nitems * ITEM_H) return 0;
    return (y - s->top - 2) / ITEM_H + 1;
}
static void result(MacRuntime *r, uint32_t sp, uint32_t value, unsigned pop) { mac_cpu_write(r->cpu, sp + pop, 4, value); mac_cpu_set(r->cpu, MAC_A7, sp + pop); }

/* The button is up: restore the screen, report the item (0 for none) and leave the trap. */
static int finish(MacRuntime *rt, Sel *s) {
    erase_menu(rt, s);
    uint32_t sp = s->sp; int id = s->id, chosen = s->item, index = s->index, popup = s->popup != 0;
    mac_menuselect_destroy(rt);
    if (popup) { result(rt, sp, chosen ? ((uint32_t)(uint16_t)id << 16) | (uint32_t)chosen : 0, 10); return 1; }   /* a pop-up leaves nothing highlighted in the bar */
    if (chosen) { rt->menu_hilite = (int16_t)id; result(rt, sp, ((uint32_t)(uint16_t)id << 16) | (uint32_t)chosen, 4); }
    else { hilite_title(rt, (unsigned)index); rt->menu_hilite = 0; result(rt, sp, 0, 4); }
    return 1;
}
static void rewind_trap(MacRuntime *rt, Sel *s) { mac_cpu_set(rt->cpu, MAC_PC, s->pc); mac_cpu_yield(rt->cpu); }

/* ---- custom definitions ---- */
static int sel_done(void *vrt, void *ctx, uint32_t unused);
static int custom_msg(MacRuntime *rt, Sel *s, int phase, int message, const int16_t rect[4], int v, int h, int item) {
    s->phase = phase;
    return mac_mdef_send(rt, s->mh, message, rect, v, h, item, sel_done, rt) < 0 ? -1 : 1;
}
static int custom_size(MacRuntime *rt, Sel *s) { static const int16_t none[4] = {0, 0, 0, 0}; return custom_msg(rt, s, P_SIZE, MDEF_SIZE, none, 0, 0, 0); }
static int custom_draw(MacRuntime *rt, Sel *s) {
    Scr sc; if (scr_of(rt, &sc) || save_under(rt, &sc, s)) return -1;
    int16_t rr[4] = {(int16_t)s->top, (int16_t)s->left, (int16_t)(s->top + s->h), (int16_t)(s->left + s->w)};
    return custom_msg(rt, s, P_DRAW, MDEF_DRAW, rr, 0, 0, s->item);
}
static int usable_size(MacRuntime *rt, int w, int h) { return w > 0 && h > 0 && w <= (int)rt->screen_width && h <= (int)rt->screen_height; }
static int sel_done(void *vrt, void *ctx, uint32_t unused) {
    MacRuntime *rt = vrt; Sel *s = rt->menusel; (void)ctx; (void)unused;
    if (!s || !s->custom) return -1;
    int16_t rr[4]; int item;
    switch (s->phase) {
    case P_SIZE: {
        MacHandle *m = mac_menu_record(rt, s->mh, NULL);
        if (!m) return fail(rt, "MenuSelect: the menu record is invalid after the definition's size message.");
        int w = (int16_t)rd(rt, m->pointer + 2, 2), h = (int16_t)rd(rt, m->pointer + 4, 2);
        if (!usable_size(rt, w, h)) return fail(rt, "A menu definition answered the size message with an unusable width or height.");
        s->w = w; s->h = h;
        if (s->popup) { int16_t pr[4] = {0, 0, (int16_t)h, (int16_t)w}; return custom_msg(rt, s, P_POPUP, MDEF_POPUP, pr, s->pop_top, s->pop_left, s->pop_item); }
        int a, b; if (!cell(rt, (unsigned)s->index, &a, &b)) return fail(rt, "MenuSelect: the tracked menu left the menu bar.");
        s->left = a + 8 + w > (int)rt->screen_width ? (int)rt->screen_width - w : a + 8; s->top = bar_height(rt) - 1;
        return custom_draw(rt, s);
    }
    case P_POPUP: {
        mac_mdef_out(rt, rr, &item);
        int w = rr[3] - rr[1], h = rr[2] - rr[0];
        if (!usable_size(rt, w, h)) return fail(rt, "A menu definition answered the pop-up message with an unusable rectangle.");
        s->w = w; s->h = h; s->left = rr[1]; s->top = rr[0];
        if (s->left + s->w > (int)rt->screen_width) s->left = (int)rt->screen_width - s->w;
        if (s->top + s->h > (int)rt->screen_height) s->top = (int)rt->screen_height - s->h;
        if (s->left < 0) s->left = 0;
        if (s->top < 0) s->top = 0;
        s->item = s->pop_item;
        return custom_draw(rt, s);
    }
    case P_DRAW: s->shown = 1; s->phase = P_TRACK; rewind_trap(rt, s); return 0;
    case P_CHOOSE:
        mac_mdef_out(rt, rr, &item);
        if (item < 0 || item > s->mv.nitems) return fail(rt, "A menu definition chose an item that is not in the menu.");
        s->item = item; s->phase = P_TRACK;
        if (s->released) return finish(rt, s) < 0 ? -1 : 0;
        rewind_trap(rt, s); return 0;
    }
    return fail(rt, "Menu definition message completed in an unknown tracking phase.");
}

static int track(MacRuntime *rt, Sel *s) {
    uint32_t s0 = s->sp;
    MenuView *vp = &s->mv, v0; (void)v0;
    { unsigned cnt = bar_count(rt); MenuView cur; int ok = s->popup ? view_handle(rt, s->popup, &cur) : (unsigned)s->index < cnt && view(rt, (unsigned)s->index, &cur);
      if (!ok || cur.id != s->id || cur.nitems != vp->nitems) return fail(rt, "MenuSelect: the menu list changed while tracking."); }
    MenuView v = *vp;
    MacEvent e; int released = 0, x = rt->mouse_x, y = rt->mouse_y;
    if (!s->custom && !s->shown && draw_menu(rt, &v, s)) return -1;
    while (mac_dialog_next_event(rt, &e)) { if (e.what == 2) { released = 1; x = e.x; y = e.y; break; } if (e.what == 1) { x = e.x; y = e.y; } }
    if (!released) { x = rt->mouse_x; y = rt->mouse_y; }
    int t = s->popup ? -1 : title_at(rt, x, y);
    MenuView w;
    if (t >= 0 && t != s->index && view(rt, (unsigned)t, &w) && (w.flags & 1)) {         /* moved along the bar onto another enabled menu */
        erase_menu(rt, s); hilite_title(rt, (unsigned)s->index);
        if (released) { mac_menuselect_destroy(rt); rt->menu_hilite = 0; result(rt, s0, 0, 4); return 1; }
        s->index = t; s->id = w.id; s->item = 0; s->mv = w; hilite_title(rt, (unsigned)t);
        s->mh = bar_handle(rt, (unsigned)t); s->custom = handle_custom(rt, s->mh);
        if (s->custom) return custom_size(rt, s);                                      /* the rest follows from sel_done */
        geometry(rt, &w, t, s);
        if (draw_menu(rt, &w, s)) return -1;
        rewind_trap(rt, s); return 1;
    }
    if (s->custom) {                                                                   /* the definition decides which item the pointer is on */
        int16_t rr[4] = {(int16_t)s->top, (int16_t)s->left, (int16_t)(s->top + s->h), (int16_t)(s->left + s->w)};
        s->released = released;
        return custom_msg(rt, s, P_CHOOSE, MDEF_CHOOSE, rr, y, x, s->item);
    }
    int item = item_at(&v, s, x, y);
    if (item && (is_sep(&v, item - 1) || !item_enabled(&v, item))) item = 0;
    if (item != s->item) {
        Scr sc; if (scr_of(rt, &sc)) return -1;
        if (s->item) draw_item(&sc, &v, s, s->item - 1, 0);
        s->item = item; if (item) draw_item(&sc, &v, s, item - 1, 1);
    }
    if (!released) { mac_cpu_set(rt->cpu, MAC_PC, s->pc); mac_cpu_yield(rt->cpu); return 1; }
    return finish(rt, s);
}

static int menu_select(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 8) return fail(rt, "MenuSelect has an invalid stack.");
    if (rt->menusel) {
        if (pc != rt->menusel->pc || sp != rt->menusel->sp) return fail(rt, "MenuSelect called again while a menu is being tracked.");
        return track(rt, rt->menusel);
    }
    int y = (int16_t)rd(rt, sp, 2), x = (int16_t)rd(rt, sp + 2, 2);
    Scr sc; if (scr_of(rt, &sc)) return -1;
    if (rt->menu_hilite) { for (unsigned i = 0, n = bar_count(rt); i < n; i++) { MenuView q; if (view(rt, i, &q) && q.id == rt->menu_hilite) hilite_title(rt, i); } rt->menu_hilite = 0; }
    int t = title_at(rt, x, y); MenuView v;
    if (t < 0 || !view(rt, (unsigned)t, &v) || !(v.flags & 1)) { result(rt, sp, 0, 4); return 1; }          /* not on an enabled title */
    Sel *s = calloc(1, sizeof *s);
    if (!s) return fail(rt, "MenuSelect could not allocate its state.");
    s->sp = sp; s->pc = pc; s->index = t; s->id = v.id; s->px = x; s->py = y;
    s->mv = v; s->mh = bar_handle(rt, (unsigned)t); s->custom = handle_custom(rt, s->mh);
    if (!s->custom) geometry(rt, &v, t, s);
    rt->menusel = s; hilite_title(rt, (unsigned)t);
    if (s->custom) return custom_size(rt, s);
    return track(rt, s);
}

/* PopUpMenuSelect(menu, top, left, popUpItem): LONGINT (from memory of Inside Macintosh). The menu (an owned menu, usually InsertMenu(menu, -1)'d) is drawn so that item popUpItem lies at (top, left)
 * in global coordinates, moved to fit the screen, with that item highlighted; the button is tracked as MenuSelect does; releasing over an enabled item returns (menuID << 16) | item, anywhere else 0. */
static int popup_select(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 14) return fail(rt, "PopUpMenuSelect has an invalid stack.");
    if (rt->menusel) {
        if (pc != rt->menusel->pc || sp != rt->menusel->sp || !rt->menusel->popup) return fail(rt, "PopUpMenuSelect called while another menu is being tracked.");
        return track(rt, rt->menusel);
    }
    int item = (int16_t)rd(rt, sp, 2), left = (int16_t)rd(rt, sp + 2, 2), top = (int16_t)rd(rt, sp + 4, 2); uint32_t h = rd(rt, sp + 6, 4);
    MenuView v; if (!view_handle(rt, h, &v) || v.nitems < 1) return fail(rt, "PopUpMenuSelect needs an owned, non-empty menu.");
    Scr sc; if (scr_of(rt, &sc)) return -1;
    Sel *s = calloc(1, sizeof *s);
    if (!s) return fail(rt, "PopUpMenuSelect could not allocate its state.");
    s->sp = sp; s->pc = pc; s->id = v.id; s->popup = h; s->mv = v; s->px = rt->mouse_x; s->py = rt->mouse_y;
    if (item < 1 || item > v.nitems) item = 1;
    if (handle_custom(rt, h)) {                                                    /* size, pop-up and draw are the definition's; sel_done carries on */
        s->mh = h; s->custom = 1; s->pop_top = top; s->pop_left = left; s->pop_item = item;
        rt->menusel = s;
        return custom_size(rt, s);
    }
    int w = 0; for (int i = 0; i < v.nitems; i++) { int iw = v.it[i].len * 8 + 24 + (v.it[i].cmd ? 24 : 0); if (iw > w) w = iw; }
    s->w = w + 8; s->h = v.nitems * ITEM_H + 4;
    s->left = left; s->top = top - 2 - (item - 1) * ITEM_H;
    if (s->left + s->w > (int)sc.w) s->left = (int)sc.w - s->w;
    if (s->top + s->h > (int)sc.h) s->top = (int)sc.h - s->h;
    if (s->left < 0) s->left = 0;
    if (s->top < 0) s->top = 0;
    s->item = item_enabled(&v, item) && !is_sep(&v, item - 1) ? item : 0;
    rt->menusel = s;
    return track(rt, s);
}

int mac_menuselect_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode == 0xa80b) {
        if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
        return popup_select(rt, mac_cpu_get(rt->cpu, MAC_A7), (mac_cpu_get(rt->cpu, MAC_PC) - 2) & mac_cpu_addr_mask(rt->cpu));
    }
    if (opcode != 0xa93d && opcode != 0xa938 && opcode != 0xa93e) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), pc = (mac_cpu_get(rt->cpu, MAC_PC) - 2) & mac_cpu_addr_mask(rt->cpu);
    if (!(rt->manager_flags & 4)) return fail(rt, "Menu tracking requires initialized Menu Manager state.");
    if (opcode == 0xa93d) return menu_select(rt, sp, pc);
    size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < (opcode == 0xa938 ? 2u : 6u)) return fail(rt, "Menu call has an invalid stack.");
    if (rt->menusel) return fail(rt, "Menu call while a menu is being tracked.");
    Scr sc; if (scr_of(rt, &sc)) return -1;
    if (opcode == 0xa938) {                                                  /* HiliteMenu(menuID) */
        int id = (int16_t)rd(rt, sp, 2);
        for (unsigned i = 0, n = bar_count(rt); i < n; i++) { MenuView q; if (!view(rt, i, &q)) continue; if (rt->menu_hilite && q.id == rt->menu_hilite) hilite_title(rt, i); }
        rt->menu_hilite = 0;
        if (id) for (unsigned i = 0, n = bar_count(rt); i < n; i++) { MenuView q; if (view(rt, i, &q) && q.id == id) { hilite_title(rt, i); rt->menu_hilite = (int16_t)id; break; } }
        mac_cpu_set(rt->cpu, MAC_A7, sp + 2); return 1;
    }
    /* MenuKey(ch): the first enabled item of an enabled menu whose command key matches (case-insensitively) */
    unsigned ch = rd(rt, sp, 1); if (ch >= 'a' && ch <= 'z') ch -= 32;
    uint32_t found = 0;
    for (unsigned i = 0, n = bar_count(rt); i < n && !found; i++) {
        MenuView q; if (!view(rt, i, &q) || !(q.flags & 1)) continue;
        for (int k = 0; k < q.nitems && !found; k++) {
            unsigned c = (unsigned)q.it[k].cmd; if (c >= 'a' && c <= 'z') c -= 32;
            if (c && c == ch && item_enabled(&q, k + 1) && !is_sep(&q, k)) {
                for (unsigned j = 0; j < n; j++) { MenuView o; if (view(rt, j, &o) && o.id == rt->menu_hilite && rt->menu_hilite) hilite_title(rt, j); }
                hilite_title(rt, i); rt->menu_hilite = (int16_t)q.id; found = ((uint32_t)(uint16_t)q.id << 16) | (uint32_t)(k + 1);
            }
        }
    }
    result(rt, sp, found, 2); return 1;
}
