#include "os999_cdev.h"
#include <stddef.h>

typedef struct {
    int initialized;
    int use_24_hour;
    int utc_offset_min;
} DateTimePluginState;

static DateTimePluginState g_dt;

static int dt_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_bool)
            g_dt.use_24_hour = host->get_bool(host->store, "date_time", "use_24_hour", 0);
        if (host->get_int)
            g_dt.utc_offset_min = host->get_int(host->store, "date_time", "utc_offset_min", -360);
    }
    return 0;
}

static int dt_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    g_dt.initialized = 1;
    g_dt.use_24_hour = 0;
    g_dt.utc_offset_min = -360;
    self->priv = &g_dt;
    return dt_load(self, host);
}

static void dt_shutdown(os999_cdev_t *self) {
    g_dt.initialized = 0;
    self->priv = NULL;
}

static int dt_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_bool)
        host->set_bool(host->store, "date_time", "use_24_hour", g_dt.use_24_hour);
    if (host->set_int)
        host->set_int(host->store, "date_time", "utc_offset_min", g_dt.utc_offset_min);
    return host->flush_store(host->store);
}

static int dt_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                    const os999_cdev_event_t *ev, Rect content) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_DOWN) {
        if (ev->x >= content.x && ev->x < content.x + content.w &&
            ev->y >= content.y && ev->y < content.y + content.h) {
            g_dt.utc_offset_min += 60;
            if (host && host->play_sound) host->play_sound(host->session, "window.zoom");
            dt_save(self, host);
            return 1;
        }
    } else if (ev->type == OS999_CDEV_EV_KEY) {
        if (ev->key == '+') {
            g_dt.utc_offset_min += 60;
            dt_save(self, host);
            return 1;
        }
        if (ev->key == 't') {
            g_dt.use_24_hour = !g_dt.use_24_hour;
            dt_save(self, host);
            return 1;
        }
    }
    return 0;
}

static void dt_render(os999_cdev_t *self, const os999_cdev_host_t *host,
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
    uint32_t color = g_dt.use_24_hour ? 0xFF2088C8u : 0xFF28A448u;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++)
            fb->px[y * fb->w + x] = color;
    }
}

static const os999_cdev_t k_plugin_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "date_time",
    .title = "Date & Time",
    .summary = "Menu bar clock format and UTC time-zone offset",
    .icon_name = "clock",
    .category = OS999_CDEV_CAT_SYSTEM,
    .init = dt_init,
    .shutdown = dt_shutdown,
    .load_state = dt_load,
    .save_state = dt_save,
    .handle_event = dt_event,
    .render = dt_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_module(void) {
    return &k_plugin_cdev;
}
