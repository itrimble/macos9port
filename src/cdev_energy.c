#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"

#include <stdio.h>

typedef struct {
    int sleep_minutes;
    int power_profile;
    int dim_before_sleep;
    int prevent_guest_sleep;
    int wake_on_net;
    int dragging_sleep;
} EnergyState;

static EnergyState g_eng = {15, 0, 1, 1, 0, 0};

static const int k_presets[4] = {5, 15, 30, 60};
static const char *const k_preset_labels[4] = {"5 min", "15 min", "30 min", "60 min"};
static const char *const k_profile_labels[3] = {
    "Automatic (Balanced Desktop)",
    "Conservation (Low Idle CPU)",
    "Performance (60+ Hz Uncapped)"
};

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void sync_session(const EnergyState *st, const os999_cdev_host_t *host) {
    if (!host || !host->session) return;
    Session *s = host->session;
    s->energy_sleep_mins = clamp_int(st->sleep_minutes, 0, 120);
    s->power_profile = clamp_int(st->power_profile, 0, 2);
    s->energy_dim_before_sleep = !!st->dim_before_sleep;
    s->energy_prevent_guest_sleep = !!st->prevent_guest_sleep;
    s->energy_wake_on_net = !!st->wake_on_net;
}

static int energy_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_int) {
            g_eng.sleep_minutes = clamp_int(
                host->get_int(host->store, "energy_saver", "sleep_minutes", 15), 0, 120);
            g_eng.power_profile = clamp_int(
                host->get_int(host->store, "energy_saver", "power_profile", 0), 0, 2);
        }
        if (host->get_bool) {
            g_eng.dim_before_sleep = host->get_bool(
                host->store, "energy_saver", "dim_before_sleep", 1);
            g_eng.prevent_guest_sleep = host->get_bool(
                host->store, "energy_saver", "prevent_guest_sleep", 1);
            g_eng.wake_on_net = host->get_bool(
                host->store, "energy_saver", "wake_on_net", 0);
        }
    }
    sync_session(&g_eng, host);
    return 0;
}

static int energy_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_eng;
    g_eng = (EnergyState){15, 0, 1, 1, 0, 0};
    return energy_load(self, host);
}

static void energy_shutdown(os999_cdev_t *self) {
    g_eng.dragging_sleep = 0;
    self->priv = NULL;
}

static int energy_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_int) {
        host->set_int(host->store, "energy_saver", "sleep_minutes", g_eng.sleep_minutes);
        host->set_int(host->store, "energy_saver", "power_profile", g_eng.power_profile);
    }
    if (host->set_bool) {
        host->set_bool(host->store, "energy_saver", "dim_before_sleep", g_eng.dim_before_sleep);
        host->set_bool(host->store, "energy_saver", "prevent_guest_sleep", g_eng.prevent_guest_sleep);
        host->set_bool(host->store, "energy_saver", "wake_on_net", g_eng.wake_on_net);
    }
    return host->flush_store(host->store);
}

static Rect sleep_minus_btn(Rect c) { return (Rect){c.x + 62, c.y + 26, 32, 24}; }
static Rect sleep_track_rect(Rect c) { return (Rect){c.x + 102, c.y + 26, 244, 24}; }
static Rect sleep_plus_btn(Rect c) { return (Rect){c.x + 354, c.y + 26, 32, 24}; }
static Rect sleep_never_btn(Rect c) { return (Rect){c.x + 394, c.y + 26, 74, 24}; }
static Rect preset_btn(Rect c, int i) { return (Rect){c.x + 138 + i * 82, c.y + 62, 74, 24}; }
static Rect test_dim_btn(Rect c) { return (Rect){c.x + 474, c.y + 62, 98, 24}; }
static Rect profile_btn(Rect c, int i) { return (Rect){c.x + 14, c.y + 154 + i * 32, 316, 26}; }
static Rect dim_chk(Rect c) { return (Rect){c.x + 16, c.y + 258, 312, 24}; }
static Rect guest_chk(Rect c) { return (Rect){c.x + 16, c.y + 288, 312, 24}; }
static Rect wake_chk(Rect c) { return (Rect){c.x + 16, c.y + 318, 312, 24}; }

static int mins_from_x(Rect track, int x) {
    int span = track.w - 12;
    if (span < 1) return 15;
    int rel = clamp_int(x - (track.x + 6), 0, span);
    int m = 5 + (rel * 115 + span / 2) / span;
    return ((m + 2) / 5) * 5;
}

static void apply_and_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    sync_session(&g_eng, host);
    energy_save(self, host);
}

static int energy_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                        const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_MOVE && g_eng.dragging_sleep) {
        g_eng.sleep_minutes = mins_from_x(sleep_track_rect(c), ev->x);
        apply_and_save(self, host);
        return 1;
    }
    if (ev->type == OS999_CDEV_EV_UP && g_eng.dragging_sleep) {
        g_eng.dragging_sleep = 0;
        return 1;
    }
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key == '-' || ev->key == '[') {
            g_eng.sleep_minutes = clamp_int(g_eng.sleep_minutes - 5, 0, 120);
            apply_and_save(self, host);
            return 1;
        }
        if (ev->key == '=' || ev->key == '+' || ev->key == ']') {
            g_eng.sleep_minutes = clamp_int(g_eng.sleep_minutes + 5, 5, 120);
            apply_and_save(self, host);
            return 1;
        }
        if (ev->key >= '1' && ev->key <= '3') {
            g_eng.power_profile = ev->key - '1';
            apply_and_save(self, host);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    if (rect_has(sleep_minus_btn(c), x, y)) {
        int next = g_eng.sleep_minutes <= 5 ? 0 : g_eng.sleep_minutes - 5;
        g_eng.sleep_minutes = next;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(sleep_plus_btn(c), x, y)) {
        int next = g_eng.sleep_minutes == 0 ? 5 : clamp_int(g_eng.sleep_minutes + 5, 5, 120);
        g_eng.sleep_minutes = next;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(sleep_never_btn(c), x, y)) {
        g_eng.sleep_minutes = g_eng.sleep_minutes == 0 ? 15 : 0;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(sleep_track_rect(c), x, y)) {
        g_eng.dragging_sleep = 1;
        g_eng.sleep_minutes = mins_from_x(sleep_track_rect(c), x);
        apply_and_save(self, host);
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (rect_has(preset_btn(c, i), x, y)) {
            g_eng.sleep_minutes = k_presets[i];
            apply_and_save(self, host);
            return 1;
        }
    }
    if (rect_has(test_dim_btn(c), x, y)) {
        if (host && host->session)
            host->session->display_dimmed = !host->session->display_dimmed;
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        if (rect_has(profile_btn(c, i), x, y)) {
            g_eng.power_profile = i;
            apply_and_save(self, host);
            return 1;
        }
    }
    if (rect_has(dim_chk(c), x, y)) {
        g_eng.dim_before_sleep = !g_eng.dim_before_sleep;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(guest_chk(c), x, y)) {
        g_eng.prevent_guest_sleep = !g_eng.prevent_guest_sleep;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(wake_chk(c), x, y)) {
        g_eng.wake_on_net = !g_eng.wake_on_net;
        apply_and_save(self, host);
        return 1;
    }
    return 0;
}

static void energy_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                          Fb *fb, Rect c) {
    (void)self;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    const Session *s = host ? host->session : NULL;

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 118}, "Display Dimming & Idle Timer");
    cdev_ui_icon(fb, c.x + 16, c.y + 28, "battery");
    cdev_ui_button(fb, sleep_minus_btn(c), "-", 0, g_eng.sleep_minutes > 0);
    cdev_ui_slider(fb, sleep_track_rect(c),
                   g_eng.sleep_minutes == 0 ? 120 : g_eng.sleep_minutes, 120,
                   g_eng.sleep_minutes == 0);
    cdev_ui_button(fb, sleep_plus_btn(c), "+", 0, g_eng.sleep_minutes < 120);
    cdev_ui_button(fb, sleep_never_btn(c), "Never", g_eng.sleep_minutes == 0, 1);

    char mins_lbl[32];
    if (g_eng.sleep_minutes == 0) snprintf(mins_lbl, sizeof mins_lbl, "Never");
    else snprintf(mins_lbl, sizeof mins_lbl, "%d min", g_eng.sleep_minutes);
    fb_text(fb, c.x + 480, c.y + 31, fg, mins_lbl);

    fb_text(fb, c.x + 62, c.y + 67, fg, "Presets:");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, preset_btn(c, i), k_preset_labels[i],
                       g_eng.sleep_minutes == k_presets[i], 1);
    cdev_ui_button(fb, test_dim_btn(c),
                   (s && s->display_dimmed) ? "Wake Now" : "Dim Preview",
                   s && s->display_dimmed, 1);
    fb_text(fb, c.x + 62, c.y + 96, desktop_theme_color(THEME_DISABLED),
            "Idle desktop dims after inactivity and wakes on mouse or keyboard input.");

    cdev_ui_group(fb, (Rect){c.x, c.y + 128, 344, 228}, "Power & Frame Pacing");
    for (int i = 0; i < 3; i++)
        cdev_ui_button(fb, profile_btn(c, i), k_profile_labels[i], g_eng.power_profile == i, 1);
    cdev_ui_checkbox(fb, dim_chk(c), "Dim Display Before Sleep", g_eng.dim_before_sleep);
    cdev_ui_checkbox(fb, guest_chk(c), "Keep Awake During 68k Apps", g_eng.prevent_guest_sleep);
    cdev_ui_checkbox(fb, wake_chk(c), "Wake for Network Transfers", g_eng.wake_on_net);

    Rect tel = {c.x + 356, c.y + 128, c.w - 356, 228};
    cdev_ui_group(fb, tel, "Efficiency Status");
    int cells_lit = g_eng.power_profile == 1 ? 8 : g_eng.power_profile == 0 ? 6 : 3;
    uint32_t bar_col = g_eng.power_profile == 2 ? 0xFFD08C28u : 0xFF28A448u;
    fb_text(fb, tel.x + 16, tel.y + 28, fg, "Host Energy Index:");
    for (int i = 0; i < 8; i++) {
        Rect cell = {tel.x + 16 + i * 25, tel.y + 52, 21, 20};
        fb_fill(fb, cell.x, cell.y, cell.w, cell.h,
                i < cells_lit ? bar_col : desktop_theme_color(THEME_TRACK));
        fb_frame(fb, cell.x, cell.y, cell.w, cell.h, desktop_theme_color(THEME_FRAME));
    }
    const char *arch = (s && s->arch[0]) ? s->arch : host_arch();
    char arch_line[64], pace_line[64], state_line[64];
    snprintf(arch_line, sizeof arch_line, "Host CPU: %s", arch);
    snprintf(pace_line, sizeof pace_line, "Idle Pacing: %s",
             g_eng.power_profile == 1 ? "30 fps cap" :
             g_eng.power_profile == 2 ? "Uncapped" : "VSync 60 Hz");
    snprintf(state_line, sizeof state_line, "Display: %s",
             (s && s->display_dimmed) ? "Dimmed" : "Active");
    fb_text(fb, tel.x + 16, tel.y + 92, fg, arch_line);
    fb_text(fb, tel.x + 16, tel.y + 118, fg, pace_line);
    fb_text(fb, tel.x + 16, tel.y + 144, fg, state_line);
    fb_text(fb, tel.x + 16, tel.y + 182, desktop_theme_color(THEME_DISABLED), "Conservation mode");
    fb_text(fb, tel.x + 16, tel.y + 202, desktop_theme_color(THEME_DISABLED), "reduces idle wakeups.");
}

static const os999_cdev_t k_energy_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "energy_saver",
    .title = "Energy Saver",
    .summary = "Idle dimming, sleep timer, and power profile",
    .icon_name = "battery",
    .category = OS999_CDEV_CAT_HARDWARE,
    .init = energy_init,
    .shutdown = energy_shutdown,
    .load_state = energy_load,
    .save_state = energy_save,
    .handle_event = energy_event,
    .render = energy_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_energy_module(void) { return &k_energy_cdev; }
