#include "control_panels_int.h"
#include "appearance_panel.h"
#include "cdev_ui.h"
#include "desktop_theme.h"
#include "os999_startup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ControlPanelsUI g_cp_ui = {.open_module = -1, .last_slot = -1};
static const ControlPanelsAudioHooks *g_audio_hooks;
static os999_cdev_host_t g_host;

void control_panels_set_audio_hooks(const ControlPanelsAudioHooks *hooks) {
    g_audio_hooks = hooks;
}

static void host_play_sound(Session *s, const char *sound_name) {
    if (!sound_name || !sound_name[0]) return;
    if (s && s->sound_muted) return;
    if (s) snprintf(s->sound_event, sizeof s->sound_event, "%s", sound_name);
    if (g_audio_hooks && g_audio_hooks->play_sound)
        g_audio_hooks->play_sound(sound_name);
}

static void host_set_volume(Session *s, int percent) {
    int pct = cp_clamp(percent, 0, 100);
    if (s) {
        s->system_volume = pct;
        s->volume_sync_request = 1;
    }
    if (g_audio_hooks && g_audio_hooks->set_volume)
        g_audio_hooks->set_volume(pct);
}

static void host_set_muted(Session *s, int muted) {
    int m = !!muted;
    if (s) {
        s->sound_muted = m;
        s->host_sound_enabled = !m;
    }
    menu_set_sound_enabled(!m);
    if (g_audio_hooks && g_audio_hooks->set_muted)
        g_audio_hooks->set_muted(m);
}

static int host_set_startup_chime(Session *s, int enabled) {
    if (!s) return -1;
    s->startup_chime_enabled = !!enabled;
    return 0;
}

static int host_set_startup_intro(Session *s, int enabled) {
    if (!s) return -1;
    int on = !!enabled;
    s->host_startup_enabled = on;
    return os999_startup_pref_write(s->root, on);
}

os999_cp_store_t *control_panels_store(Session *s) {
    return s ? (os999_cp_store_t *)s->cp_store : NULL;
}

const os999_cdev_host_t *control_panels_host(Session *s) {
    g_host = (os999_cdev_host_t){
        .session = s,
        .store = control_panels_store(s),
        .get_int = os999_cp_store_get_int,
        .set_int = os999_cp_store_set_int,
        .get_bool = os999_cp_store_get_bool,
        .set_bool = os999_cp_store_set_bool,
        .get_str = os999_cp_store_get_str,
        .set_str = os999_cp_store_set_str,
        .flush_store = os999_cp_store_save,
        .play_sound = host_play_sound,
        .set_volume = host_set_volume,
        .set_muted = host_set_muted,
        .set_startup_chime = host_set_startup_chime,
        .set_startup_intro = host_set_startup_intro
    };
    return &g_host;
}

static const AppearancePanelDelegate k_delegate = {
    .active = control_panels_active,
    .can_open = control_panels_can_open,
    .regions = control_panels_regions,
    .close = control_panels_close,
    .deactivate = control_panels_deactivate,
    .blur = control_panels_blur,
    .draw = control_panels_draw,
    .down = control_panels_down,
    .context = control_panels_context,
    .key = control_panels_key
};

static void ensure_builtin_modules(void) {
    if (!os999_cdev_find("general"))
        os999_cdev_register(os999_cdev_general_module());
    if (!os999_cdev_find("sound"))
        os999_cdev_register(os999_cdev_sound_module());
    if (!os999_cdev_find("appearance"))
        os999_cdev_register(os999_cdev_appearance_module());
}

int control_panels_session_init(Session *s) {
    if (!s) return -1;
    appearance_panel_set_delegate(&k_delegate);
    os999_cp_store_t *store = calloc(1, sizeof *store);
    if (!store) return -1;
    if (os999_cp_store_init(store, s->root) != 0) {
        free(store);
        return -1;
    }
    (void)os999_cp_store_load(store);
    s->cp_store = store;

    s->desktop_pattern = cp_clamp(
        os999_cp_store_get_int(store, "general", "desktop_pattern", 0), 0, 3);
    s->cursor_blink_ms = cp_clamp(
        os999_cp_store_get_int(store, "general", "cursor_blink_ms", 530), 0, 2000);
    s->cursor_blink_set = 1;
    s->menu_blink_count = cp_clamp(
        os999_cp_store_get_int(store, "general", "menu_blink_count", 3), 0, 3);
    s->system_volume = cp_clamp(
        os999_cp_store_get_int(store, "sound", "volume", 80), 0, 100);
    s->sound_muted = os999_cp_store_get_bool(store, "sound", "muted", 0);
    s->host_sound_enabled = !s->sound_muted;
    menu_set_sound_enabled(s->host_sound_enabled);
    s->startup_chime_enabled = os999_cp_store_get_bool(store, "sound", "startup_chime", 1);
    int intro_pref = os999_startup_pref_read(s->root);
    s->host_startup_enabled = os999_cp_store_get_bool(store, "sound", "startup_intro", intro_pref);
    os999_cp_store_get_str(store, "sound", "alert_sound",
                           s->alert_sound, sizeof s->alert_sound, "alert.beep");

    ensure_builtin_modules();
    char plugin_dir[OSPI_PATH + 64];
    int n = snprintf(plugin_dir, sizeof plugin_dir, "%s/System Folder/Control Panels", s->root);
    if (n > 0 && (size_t)n < sizeof plugin_dir)
        (void)os999_cdev_scan_directory(plugin_dir);

    const os999_cdev_host_t *host = control_panels_host(s);
    for (int i = 0; i < os999_cdev_count(); i++) {
        os999_cdev_t *m = os999_cdev_at(i);
        if (m && m->init) m->init(m, host);
    }
    return 0;
}

void control_panels_session_shutdown(Session *s) {
    if (!s) return;
    if (g_cp_ui.owner == s) control_panels_close();
    os999_cp_store_t *store = control_panels_store(s);
    if (store) {
        os999_cp_store_destroy(store);
        free(store);
        s->cp_store = NULL;
    }
}

Rect cp_window_rect(const Session *s) {
    int sw = (s && s->sw > 0) ? s->sw : OSPI_W;
    int sh = (s && s->sh > 0) ? s->sh : OSPI_H;
    int w = CP_WIN_W > sw ? sw : CP_WIN_W;
    int h = CP_WIN_H > sh - TITLE_H ? sh - TITLE_H : CP_WIN_H;
    int x = g_cp_ui.pos_valid ? g_cp_ui.win_x : (sw - w) / 2;
    int y = g_cp_ui.pos_valid ? g_cp_ui.win_y : (sh - h) / 2;
    x = cp_clamp(x, 0, sw - w);
    y = cp_clamp(y, TITLE_H, sh - h < TITLE_H ? TITLE_H : sh - h);
    return (Rect){x, y, w, h};
}

int cp_filtered_modules(int out[OS999_CDEV_MAX_MODULES]) {
    int n = 0;
    for (int i = 0; i < os999_cdev_count() && n < OS999_CDEV_MAX_MODULES; i++) {
        os999_cdev_t *m = os999_cdev_at(i);
        if (m && (g_cp_ui.category == OS999_CDEV_CAT_ALL || m->category == g_cp_ui.category))
            out[n++] = i;
    }
    return n;
}

int cp_max_grid_scroll(Rect grid, int nslots) {
    int content_h = 12 + ((nslots + 2) / 3) * CP_CARD_DY + 8;
    return content_h > grid.h ? content_h - grid.h : 0;
}

void cp_clamp_grid_state(const Session *s) {
    int indices[OS999_CDEV_MAX_MODULES];
    int n = cp_filtered_modules(indices);
    if (n <= 0) { g_cp_ui.selected_slot = g_cp_ui.scroll = 0; return; }
    g_cp_ui.selected_slot = cp_clamp(g_cp_ui.selected_slot, 0, n - 1);
    g_cp_ui.scroll = cp_clamp(g_cp_ui.scroll, 0, cp_max_grid_scroll(cp_grid_rect(cp_window_rect(s)), n));
}

void cp_ensure_slot_visible(const Session *s, int slot) {
    int indices[OS999_CDEV_MAX_MODULES];
    int n = cp_filtered_modules(indices);
    if (slot < 0 || slot >= n) return;
    Rect grid = cp_grid_rect(cp_window_rect(s));
    int top = 12 + (slot / 3) * CP_CARD_DY, bot = top + CP_CARD_H + 8;
    if (top < g_cp_ui.scroll) g_cp_ui.scroll = top;
    else if (bot > g_cp_ui.scroll + grid.h) g_cp_ui.scroll = bot - grid.h;
    g_cp_ui.scroll = cp_clamp(g_cp_ui.scroll, 0, cp_max_grid_scroll(grid, n));
}

void cp_save_active_module(Session *s) {
    if (g_cp_ui.open_module < 0) return;
    os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
    if (m && m->save_state && s) m->save_state(m, control_panels_host(s));
}

static void enter_module_index(Session *s, int reg_index) {
    os999_cdev_t *m = os999_cdev_at(reg_index);
    if (!m) return;
    const os999_cdev_host_t *host = control_panels_host(s);
    if (!m->priv && m->init) m->init(m, host);
    else if (m->load_state) m->load_state(m, host);
    g_cp_ui.open_module = reg_index;
    g_cp_ui.context_open = 0;
}

void cp_open_selected_slot(Session *s) {
    int indices[OS999_CDEV_MAX_MODULES];
    int n = cp_filtered_modules(indices);
    if (n > 0) enter_module_index(s, indices[cp_clamp(g_cp_ui.selected_slot, 0, n - 1)]);
}

void control_panels_open(Session *s) {
    if (!s) return;
    ensure_builtin_modules();
    appearance_panel_close_local();
    if (!g_cp_ui.open || g_cp_ui.owner != s) {
        g_cp_ui.pos_valid = 0;
        g_cp_ui.category = OS999_CDEV_CAT_ALL;
        g_cp_ui.selected_slot = g_cp_ui.scroll = 0;
    }
    g_cp_ui.owner = s;
    g_cp_ui.open = g_cp_ui.focused = 1;
    g_cp_ui.open_module = -1;
    g_cp_ui.context_open = g_cp_ui.dragging_win = 0;
    s->down = s->drag = 0;
    s->menu = s->menu_hot = -1;
    cp_clamp_grid_state(s);
}

int control_panels_open_module(Session *s, const char *module_id) {
    if (!s || !module_id || !module_id[0]) return -1;
    ensure_builtin_modules();
    for (int i = 0; i < os999_cdev_count(); i++) {
        os999_cdev_t *m = os999_cdev_at(i);
        if (m && m->id && strcmp(m->id, module_id) == 0) {
            control_panels_open(s);
            enter_module_index(s, i);
            return 0;
        }
    }
    return -1;
}

void control_panels_return_to_grid(Session *s) {
    if (!g_cp_ui.open) return;
    cp_save_active_module(s ? s : g_cp_ui.owner);
    g_cp_ui.open_module = -1;
    g_cp_ui.context_open = 0;
    cp_clamp_grid_state(s ? s : g_cp_ui.owner);
}

const char *control_panels_active_module_id(void) {
    if (!g_cp_ui.open || g_cp_ui.open_module < 0) return NULL;
    os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
    return m ? m->id : NULL;
}

int control_panels_selected_index(void) { return g_cp_ui.selected_slot; }
int control_panels_category_filter(void) { return g_cp_ui.category; }

void control_panels_set_category_filter(int category) {
    g_cp_ui.category = (category >= 0 && category < CP_CAT_COUNT) ? category : OS999_CDEV_CAT_ALL;
    g_cp_ui.selected_slot = g_cp_ui.scroll = 0;
    cp_clamp_grid_state(g_cp_ui.owner);
}

int control_panels_active(void) {
    return g_cp_ui.open && g_cp_ui.focused && g_cp_ui.owner &&
           !g_cp_ui.owner->media_focused && !g_cp_ui.owner->browser_focused &&
           !g_cp_ui.owner->guest_focused && !g_cp_ui.owner->finder_hidden;
}

int control_panels_can_open(void) {
    int indices[OS999_CDEV_MAX_MODULES];
    return control_panels_active() && g_cp_ui.open_module < 0 && cp_filtered_modules(indices) > 0;
}

int control_panels_regions(const Session *s, Rect out[2]) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s || s->finder_hidden) return 0;
    Rect r = cp_window_rect(s);
    out[0] = (Rect){r.x, r.y, r.w + 3, r.h + 3};
    if (g_cp_ui.context_open) { out[1] = g_cp_ui.context_box; return 2; }
    return 1;
}

void control_panels_close(void) {
    if (g_cp_ui.open && g_cp_ui.owner) {
        cp_save_active_module(g_cp_ui.owner);
        g_cp_ui.owner->down = g_cp_ui.owner->drag = 0;
    }
    g_cp_ui.open = g_cp_ui.focused = g_cp_ui.context_open = g_cp_ui.dragging_win = 0;
    g_cp_ui.open_module = -1;
    g_cp_ui.owner = NULL;
}

void control_panels_deactivate(void) { g_cp_ui.context_open = g_cp_ui.dragging_win = 0; }
void control_panels_blur(void) { g_cp_ui.focused = g_cp_ui.context_open = g_cp_ui.dragging_win = 0; }

static void draw_chrome(Fb *fb, Rect r, const char *title, int focused) {
    fb_fill(fb, r.x + 3, r.y + 3, r.w, r.h, 0xFF26304Au);
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FRAME));
    if (focused) {
        uint32_t st = desktop_theme_current() ? desktop_theme_color(THEME_STRIPE_B) : C_GRAY;
        for (int y = 4; y < TITLE_H - 2; y += 2) fb_hline(fb, r.x + 3, r.y + y, r.w - 6, st);
    }
    int tw = fb_text_width(title);
    fb_fill(fb, r.x + (r.w - tw) / 2 - 6, r.y + 1, tw + 12, TITLE_H - 2, desktop_theme_color(THEME_MENU));
    fb_text(fb, r.x + (r.w - tw) / 2, r.y + 3,
            focused ? desktop_theme_color(THEME_FOREGROUND) : desktop_theme_color(THEME_DISABLED), title);
    fb_hline(fb, r.x, r.y + TITLE_H, r.w, desktop_theme_color(THEME_FRAME));
    if (focused) {
        Rect cb = cp_close_rect(r);
        fb_fill(fb, cb.x, cb.y, cb.w, cb.h, desktop_theme_color(THEME_PANEL));
        fb_frame(fb, cb.x, cb.y, cb.w, cb.h, desktop_theme_color(THEME_FRAME));
    }
}

static void draw_grid(Fb *fb, Rect r) {
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    for (int c = 0; c < CP_CAT_COUNT; c++)
        cdev_ui_button(fb, cp_cat_rect(r, c), os999_cdev_category_label(c), g_cp_ui.category == c, 1);
    int indices[OS999_CDEV_MAX_MODULES];
    int n = cp_filtered_modules(indices);
    cdev_ui_button(fb, cp_open_btn_rect(r), "Open", 0, n > 0);

    Rect grid = cp_grid_rect(r);
    fb_fill(fb, grid.x, grid.y, grid.w, grid.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, grid.x, grid.y, grid.w, grid.h, desktop_theme_color(THEME_FRAME));
    fb_clip(fb, grid.x + 1, grid.y + 1, grid.w - 2, grid.h - 2);
    for (int slot = 0; slot < n; slot++) {
        os999_cdev_t *m = os999_cdev_at(indices[slot]);
        Rect card = cp_card_rect(grid, slot, g_cp_ui.scroll);
        if (card.y + card.h < grid.y || card.y > grid.y + grid.h) continue;
        int sel = (slot == g_cp_ui.selected_slot);
        fb_fill(fb, card.x, card.y, card.w, card.h,
                sel ? desktop_theme_color(THEME_MENU) : desktop_theme_color(THEME_PANEL));
        fb_frame(fb, card.x, card.y, card.w, card.h,
                 sel ? cdev_ui_selection_color() : desktop_theme_color(THEME_FRAME));
        if (sel) fb_frame(fb, card.x + 1, card.y + 1, card.w - 2, card.h - 2, cdev_ui_selection_color());
        cdev_ui_icon(fb, card.x + (card.w - 32) / 2, card.y + 12, m->icon_name);
        int tw = fb_text_width(m->title);
        if (sel) {
            int bw = tw + 12 > card.w - 12 ? card.w - 12 : tw + 12;
            fb_fill(fb, card.x + (card.w - bw) / 2, card.y + 50, bw,
                    typeface_line_height(0) + 4, cdev_ui_selection_color());
        }
        fb_clip(fb, card.x + 6, card.y + 48, card.w - 12, 24);
        fb_text(fb, card.x + (card.w - tw) / 2, card.y + 52, sel ? cdev_ui_accent_text() : fg, m->title);
        fb_unclip(fb);
        fb_clip(fb, grid.x + 1, grid.y + 1, grid.w - 2, grid.h - 2);
        const char *cat = os999_cdev_category_label(m->category);
        fb_text(fb, card.x + (card.w - fb_text_width(cat)) / 2, card.y + 74,
                desktop_theme_color(THEME_DISABLED), cat);
        if (m->summary && m->summary[0]) {
            fb_clip(fb, card.x + 8, card.y + 94, card.w - 16, 46);
            text_draw(fb, card.x + 10, card.y + 96, card.w - 20, fg, m->summary);
            fb_unclip(fb);
            fb_clip(fb, grid.x + 1, grid.y + 1, grid.w - 2, grid.h - 2);
        }
    }
    fb_unclip(fb);
    char footer[96];
    snprintf(footer, sizeof footer, "%d control panel%s — Double-click or press Return to open",
             n, n == 1 ? "" : "s");
    fb_text(fb, r.x + 18, r.y + r.h - 24, desktop_theme_color(THEME_DISABLED), footer);
}

void control_panels_draw(Session *s, Fb *fb) {
    if (!s || !fb || !g_cp_ui.open || g_cp_ui.owner != s || s->finder_hidden) return;
    Rect r = cp_window_rect(s);
    int active = control_panels_active();
    if (g_cp_ui.open_module < 0) {
        draw_chrome(fb, r, "Control Panels", active);
        draw_grid(fb, r);
    } else {
        os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
        draw_chrome(fb, r, m ? m->title : "Control Panel", active);
        cdev_ui_button(fb, cp_back_btn_rect(r), "< All Panels", 0, 1);
        cdev_ui_button(fb, cp_save_btn_rect(r), "Save", 0, 1);
        if (m) {
            cdev_ui_icon(fb, r.x + 138, r.y + 24, m->icon_name);
            fb_clip(fb, r.x + 176, r.y + 26, r.w - 274, 28);
            fb_text(fb, r.x + 176, r.y + 32, desktop_theme_color(THEME_FOREGROUND),
                    m->summary ? m->summary : m->title);
            fb_unclip(fb);
            Rect content = cp_content_rect(r);
            fb_clip(fb, content.x, content.y, content.w, content.h);
            if (m->render) m->render(m, control_panels_host(s), fb, content);
            fb_unclip(fb);
        }
    }
    if (g_cp_ui.context_open) {
        Rect cb = g_cp_ui.context_box;
        fb_fill(fb, cb.x + 3, cb.y + 3, cb.w, cb.h, 0xFF26304Au);
        fb_fill(fb, cb.x, cb.y, cb.w, cb.h, desktop_theme_color(THEME_PANEL));
        fb_frame(fb, cb.x, cb.y, cb.w, cb.h, desktop_theme_color(THEME_FRAME));
        const char *labels[2] = {
            g_cp_ui.open_module < 0 ? "Open Panel" : "All Control Panels",
            g_cp_ui.open_module < 0 ? "Show All Categories" : "Save Settings"
        };
        for (int i = 0; i < 2; i++) {
            Rect row = {cb.x + 1, cb.y + 1 + i * 24, cb.w - 2, 24};
            int hot = rect_has(row, s->mx, s->my);
            if (hot) fb_fill(fb, row.x, row.y, row.w, row.h, cdev_ui_selection_color());
            fb_text(fb, row.x + 12, row.y + 4,
                    hot ? cdev_ui_accent_text() : desktop_theme_color(THEME_FOREGROUND), labels[i]);
        }
    }
}
