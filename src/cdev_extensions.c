#include "os999_cdev.h"
#include "ext_ui.h"

#include <string.h>

static ExtManager g_mgr;
static int g_open;

static int ext_init_cb(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_mgr;
    g_open = host && host->session && ext_manager_open(&g_mgr, host->session->root, 0) >= 0;
    return 0;
}

static int ext_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                     const os999_cdev_event_t *ev, Rect c) {
    (void)self; (void)host;
    if (!g_open || !ev) return 0;
    if (ev->type == OS999_CDEV_EV_DOWN) return ext_manager_click(&g_mgr, c, ev->x, ev->y);
    if (ev->type == OS999_CDEV_EV_WHEEL) return ext_manager_wheel(&g_mgr, ev->dy);
    return 0;
}

static void ext_render(os999_cdev_t *self, const os999_cdev_host_t *host, Fb *fb, Rect c) {
    (void)self; (void)host;
    if (g_open) ext_manager_render(&g_mgr, fb, c);
}

static const os999_cdev_t k_ext_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "extensions",
    .title = "Extensions Manager",
    .summary = "Turn startup extensions on or off, and keep sets of them",
    .icon_name = "settings",
    .category = OS999_CDEV_CAT_SYSTEM,
    .init = ext_init_cb,
    .handle_event = ext_event,
    .render = ext_render,
};

const os999_cdev_t *os999_cdev_extensions_module(void) { return &k_ext_cdev; }
