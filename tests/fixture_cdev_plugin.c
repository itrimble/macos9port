#include "os999_cdev.h"
#include <stddef.h>

typedef struct {
    int initialized;
    int sleep_minutes;
    int low_power;
} EnergySaverState;

static EnergySaverState g_energy;

static int energy_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_int)
            g_energy.sleep_minutes = host->get_int(host->store, "energy_saver", "sleep_minutes", 15);
        if (host->get_bool)
            g_energy.low_power = host->get_bool(host->store, "energy_saver", "low_power", 0);
    }
    return 0;
}

static int energy_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    g_energy.initialized = 1;
    g_energy.sleep_minutes = 15;
    g_energy.low_power = 0;
    self->priv = &g_energy;
    return energy_load(self, host);
}

static void energy_shutdown(os999_cdev_t *self) {
    g_energy.initialized = 0;
    self->priv = NULL;
}

static int energy_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_int)
        host->set_int(host->store, "energy_saver", "sleep_minutes", g_energy.sleep_minutes);
    if (host->set_bool)
        host->set_bool(host->store, "energy_saver", "low_power", g_energy.low_power);
    return host->flush_store(host->store);
}

static int energy_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                        const os999_cdev_event_t *ev, Rect content) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_DOWN) {
        if (ev->x >= content.x && ev->x < content.x + content.w &&
            ev->y >= content.y && ev->y < content.y + content.h) {
            g_energy.sleep_minutes += 5;
            if (g_energy.sleep_minutes > 120) g_energy.sleep_minutes = 5;
            if (host && host->play_sound) host->play_sound(host->session, "window.zoom");
            energy_save(self, host);
            return 1;
        }
    } else if (ev->type == OS999_CDEV_EV_KEY) {
        if (ev->key == '+' || ev->key == '=') {
            g_energy.sleep_minutes += 5;
            energy_save(self, host);
            return 1;
        }
        if (ev->key == 'p') {
            g_energy.low_power = !g_energy.low_power;
            energy_save(self, host);
            return 1;
        }
    }
    return 0;
}

static void energy_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                          Fb *fb, Rect content) {
    (void)self;
    (void)host;
    if (!fb || !fb->px) return;
    int x0 = content.x + 20, y0 = content.y + 24;
    int x1 = x0 + 160, y1 = y0 + 28;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb->w) x1 = fb->w;
    if (y1 > fb->h) y1 = fb->h;
    uint32_t color = g_energy.low_power ? 0xFF2088C8u : 0xFF28A448u;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++)
            fb->px[y * fb->w + x] = color;
    }
}

static const os999_cdev_t k_plugin_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "energy_saver",
    .title = "Energy Saver",
    .summary = "Display sleep timer and hardware power saving mode",
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

const os999_cdev_t *os999_cdev_module(void) {
    return &k_plugin_cdev;
}
