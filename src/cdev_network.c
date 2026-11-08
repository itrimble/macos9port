#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"
#include "modern_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    int connect_via;
    int configure_mode;
    int https_only;
    int guest_isolated;
    int allow_redirects;
    int timeout_sec;
} NetworkState;

static NetworkState g_net = {0, 0, 1, 1, 1, 20};

static const char *const k_via_labels[3] = {"Host HTTPS", "Tailscale", "Offline"};
static const char *const k_cfg_labels[2] = {"Using DHCP (Host)", "Manual Profile"};
static const int k_timeouts[4] = {10, 20, 30, 60};
static const char *const k_timeout_labels[4] = {"10s", "20s", "30s", "60s"};

static int detect_tailscale_cli(void) {
    static const char *const paths[] = {
        "/opt/homebrew/bin/tailscale",
        "/usr/local/bin/tailscale",
        "/usr/bin/tailscale",
        "/Applications/Tailscale.app/Contents/MacOS/Tailscale"
    };
    for (unsigned i = 0; i < sizeof paths / sizeof *paths; i++) {
        if (access(paths[i], X_OK) == 0) return 1;
    }
    return 0;
}

static void sync_session(const NetworkState *st, const os999_cdev_host_t *host) {
    Os999NetConfig cfg = {
        .connect_via = st->connect_via,
        .https_only = 1,
        .allow_redirects = !!st->allow_redirects,
        .guest_isolated = !!st->guest_isolated,
        .timeout_sec = st->timeout_sec
    };
    net_set_config(&cfg);
    if (!host || !host->session) return;
    Session *s = host->session;
    s->net_connect_via = st->connect_via;
    s->net_https_only = 1;
    s->net_allow_redirects = !!st->allow_redirects;
    s->net_guest_isolated = !!st->guest_isolated;
    s->net_timeout_sec = st->timeout_sec;
}

static int network_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_int) {
            int v = host->get_int(host->store, "network", "connect_via", 0);
            g_net.connect_via = (v >= 0 && v <= 2) ? v : 0;
            int cfg = host->get_int(host->store, "network", "configure_mode", 0);
            g_net.configure_mode = cfg ? 1 : 0;
            int t = host->get_int(host->store, "network", "timeout_sec", 20);
            g_net.timeout_sec = (t == 10 || t == 20 || t == 30 || t == 60) ? t : 20;
        }
        if (host->get_bool) {
            g_net.https_only = 1;
            g_net.guest_isolated = host->get_bool(host->store, "network", "guest_isolated", 1);
            g_net.allow_redirects = host->get_bool(host->store, "network", "allow_redirects", 1);
        }
    }
    sync_session(&g_net, host);
    return 0;
}

static int network_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_net;
    g_net = (NetworkState){0, 0, 1, 1, 1, 20};
    return network_load(self, host);
}

static void network_shutdown(os999_cdev_t *self) { self->priv = NULL; }

static int network_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_int) {
        host->set_int(host->store, "network", "connect_via", g_net.connect_via);
        host->set_int(host->store, "network", "configure_mode", g_net.configure_mode);
        host->set_int(host->store, "network", "timeout_sec", g_net.timeout_sec);
    }
    if (host->set_bool) {
        host->set_bool(host->store, "network", "https_only", 1);
        host->set_bool(host->store, "network", "guest_isolated", g_net.guest_isolated);
        host->set_bool(host->store, "network", "allow_redirects", g_net.allow_redirects);
    }
    return host->flush_store(host->store);
}

static Rect via_btn(Rect c, int i) { return (Rect){c.x + 166 + i * 136, c.y + 24, 128, 24}; }
static Rect cfg_btn(Rect c, int i) { return (Rect){c.x + 166 + i * 188, c.y + 58, 180, 24}; }
static Rect https_chk(Rect c) { return (Rect){c.x + 16, c.y + 148, 320, 24}; }
static Rect guest_iso_chk(Rect c) { return (Rect){c.x + 16, c.y + 180, 320, 24}; }
static Rect redirect_chk(Rect c) { return (Rect){c.x + 16, c.y + 212, 320, 24}; }
static Rect timeout_btn(Rect c, int i) { return (Rect){c.x + 96 + i * 60, c.y + 250, 54, 24}; }
static Rect tailscale_btn(Rect c) { return (Rect){c.x + 380, c.y + 224, 192, 26}; }
static Rect browser_btn(Rect c) { return (Rect){c.x + 380, c.y + 258, 192, 26}; }
static Rect reset_btn(Rect c) { return (Rect){c.x + 380, c.y + 292, 192, 26}; }

static void apply_and_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    sync_session(&g_net, host);
    network_save(self, host);
}

static int network_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                         const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key >= '1' && ev->key <= '3') {
            g_net.connect_via = ev->key - '1';
            apply_and_save(self, host);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    for (int i = 0; i < 3; i++) {
        if (rect_has(via_btn(c, i), x, y)) {
            g_net.connect_via = i;
            apply_and_save(self, host);
            return 1;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (rect_has(cfg_btn(c, i), x, y)) {
            /* Host addressing is not implemented by this panel. */
            return 1;
        }
    }
    if (rect_has(https_chk(c), x, y)) {
        g_net.https_only = 1;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(guest_iso_chk(c), x, y)) {
        /* No guest broker/runtime enforcement is wired here yet. */
        return 1;
    }
    if (rect_has(redirect_chk(c), x, y)) {
        g_net.allow_redirects = !g_net.allow_redirects;
        apply_and_save(self, host);
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (rect_has(timeout_btn(c, i), x, y)) {
            g_net.timeout_sec = k_timeouts[i];
            apply_and_save(self, host);
            return 1;
        }
    }
    if (rect_has(tailscale_btn(c), x, y)) {
        if (host && host->session) session_command(host->session, CMD_TAILSCALE);
        return 1;
    }
    if (rect_has(browser_btn(c), x, y)) {
        if (host && host->session) session_command(host->session, CMD_BROWSER);
        return 1;
    }
    if (rect_has(reset_btn(c), x, y)) {
        g_net = (NetworkState){0, 0, 1, 1, 1, 20};
        apply_and_save(self, host);
        return 1;
    }
    return 0;
}

static void network_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                           Fb *fb, Rect c) {
    (void)self;
    (void)host;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    int ts_installed = detect_tailscale_cli();
    int tls_ok = net_https_url("https://remnantsecurity.com");

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 114}, "Connect Via & Addressing");
    cdev_ui_icon(fb, c.x + 16, c.y + 26, "network");
    fb_text(fb, c.x + 60, c.y + 29, fg, "Connect via:");
    for (int i = 0; i < 3; i++)
        cdev_ui_button(fb, via_btn(c, i), k_via_labels[i], g_net.connect_via == i, 1);

    fb_text(fb, c.x + 60, c.y + 63, fg, "Configure:");
    for (int i = 0; i < 2; i++)
        cdev_ui_button(fb, cfg_btn(c, i), k_cfg_labels[i],
                       g_net.configure_mode == i, 0);

    char transport[112];
    snprintf(transport, sizeof transport, "HTTPS/TLS Validator: %s   |   Tailscale CLI: %s",
             tls_ok ? "Ready" : "Unavailable", ts_installed ? "Detected" : "Not Installed");
    fb_text(fb, c.x + 60, c.y + 92, desktop_theme_color(THEME_DISABLED), transport);

    cdev_ui_group(fb, (Rect){c.x, c.y + 124, 352, 232}, "TLS Policy & Legacy Isolation");
    cdev_ui_checkbox(fb, https_chk(c), "Require HTTPS & Verified TLS", 1);
    fb_text(fb, c.x + 16, c.y + 186, desktop_theme_color(THEME_DISABLED),
            "Guest isolation: not connected");
    cdev_ui_checkbox(fb, redirect_chk(c), "Allow HTTPS Mirror Redirects", g_net.allow_redirects);
    fb_text(fb, c.x + 16, c.y + 255, fg, "Timeout:");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, timeout_btn(c, i), k_timeout_labels[i],
                       g_net.timeout_sec == k_timeouts[i], 1);
    fb_text(fb, c.x + 16, c.y + 294, desktop_theme_color(THEME_DISABLED),
            "Host addressing is managed by macOS.");
    fb_text(fb, c.x + 16, c.y + 316, desktop_theme_color(THEME_DISABLED),
            "No guest trust-store or proxy injection.");

    Rect svc = {c.x + 364, c.y + 124, c.w - 364, 232};
    cdev_ui_group(fb, svc, "Network Tools");
    cdev_ui_icon(fb, svc.x + 16, svc.y + 26, "wireless");
    uint32_t dot = g_net.connect_via == 2 ? 0xFFC83838u : 0xFF888888u;
    fb_fill(fb, svc.x + 58, svc.y + 34, 12, 12, dot);
    fb_frame(fb, svc.x + 58, svc.y + 34, 12, 12, desktop_theme_color(THEME_FRAME));
    fb_text(fb, svc.x + 78, svc.y + 33, fg,
            g_net.connect_via == 2 ? "Offline Mode" :
            g_net.connect_via == 1 ? "Tailscale preference" : "Host HTTPS policy");
    fb_text(fb, svc.x + 16, svc.y + 70, desktop_theme_color(THEME_DISABLED),
            g_net.https_only ? "TLS 1.2/1.3 enforced" : "Mixed transport");
    cdev_ui_button(fb, tailscale_btn(c), "Open Tailscale...", 0, 1);
    cdev_ui_button(fb, browser_btn(c), "Open Chromium...", 0, 1);
    cdev_ui_button(fb, reset_btn(c), "Restore Defaults", 0, 1);
}

static const os999_cdev_t k_network_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "network",
    .title = "TCP/IP & Network",
    .summary = "HTTPS TLS policy, Tailscale, and MacTCP",
    .icon_name = "network",
    .category = OS999_CDEV_CAT_SYSTEM,
    .init = network_init,
    .shutdown = network_shutdown,
    .load_state = network_load,
    .save_state = network_save,
    .handle_event = network_event,
    .render = network_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_network_module(void) { return &k_network_cdev; }
