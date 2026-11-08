/* The desktop's Bluetooth manager.
 *
 * It owns the list of devices an inquiry found, the state of the link and
 * the personal area network behind the link. It never talks to a radio
 * itself: the driver reports what it finds and how the link came up, so the
 * macOS radio and the controller simulator are interchangeable. */
#include "desktop_bluetooth.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct DesktopBtManager {
    const DesktopBtDriverOps *ops;
    DesktopBtDriver *driver;
    DesktopGuestNet *net;
    DesktopBtCallbacks cb;
    DesktopBtDevice devices[DBT_MAX_DEVICES];
    int ndevices;
    int state;
    int error;
    BtAddress peer;
    int have_peer;
};

static void manager_found(void *ctx, const BtAddress *addr, const char *name, uint32_t cod) {
    DesktopBtManager *m = ctx;
    if (!m || !addr || m->state != DBT_SCANNING) return;
    for (int i = 0; i < m->ndevices; i++) {
        if (bt_addr_cmp(&m->devices[i].addr, addr) == 0) {
            /* A device we already know keeps its name. */
            if (name && name[0] && !m->devices[i].name[0])
                snprintf(m->devices[i].name, sizeof m->devices[i].name, "%s", name);
            return;
        }
    }
    if (m->ndevices >= DBT_MAX_DEVICES) return;
    DesktopBtDevice *d = &m->devices[m->ndevices++];
    memset(d, 0, sizeof *d);
    d->addr = *addr;
    d->class_of_device = cod;
    if (name) snprintf(d->name, sizeof d->name, "%s", name);
}

static void manager_scan_done(void *ctx) {
    DesktopBtManager *m = ctx;
    if (!m || m->state != DBT_SCANNING) return;
    m->state = DBT_IDLE;
}

static void manager_failed(void *ctx, int error);

static void manager_up(void *ctx) {
    DesktopBtManager *m = ctx;
    if (!m || m->state != DBT_CONNECTING) return;
    GnPan *pan = m->ops->pan(m->driver);
    int rc = pan ? desktop_guestnet_set_pan(m->net, pan) : BT_EIO;
    if (rc != BT_OK) {
        manager_failed(m, rc);
        return;
    }
    m->state = DBT_UP;
    m->error = 0;
}

static void manager_down(void *ctx) {
    DesktopBtManager *m = ctx;
    if (!m) return;
    m->state = DBT_IDLE;
    m->have_peer = 0;
    if (m->net) desktop_guestnet_set_pan(m->net, NULL);
}

static void manager_failed(void *ctx, int error) {
    DesktopBtManager *m = ctx;
    if (!m) return;
    if (m->state == DBT_IDLE || m->state == DBT_FAILED) return;
    manager_down(m);
    m->ops->link_down(m->driver);
    m->state = DBT_FAILED;
    m->error = error;
}

DesktopBtManager *desktop_bt_new(const DesktopBtDriverOps *ops, DesktopGuestNet *net) {
    if (!net || !ops || !ops->create || !ops->scan || !ops->link_up ||
        !ops->link_down || !ops->pan || !ops->free) return NULL;
    DesktopBtManager *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->ops = ops;
    m->net = net;
    m->cb.found = manager_found;
    m->cb.scan_done = manager_scan_done;
    m->cb.up = manager_up;
    m->cb.down = manager_down;
    m->cb.failed = manager_failed;
    m->cb.ctx = m;
    m->driver = ops->create(&m->cb);
    if (!m->driver) {
        free(m);
        return NULL;
    }
    return m;
}

void desktop_bt_free(DesktopBtManager *m) {
    if (!m) return;
    desktop_bt_disconnect(m);
    m->ops->free(m->driver);
    free(m);
}

int desktop_bt_scan(DesktopBtManager *m, int seconds) {
    if (!m || !m->ops->scan) return BT_EINVAL;
    if (m->state == DBT_UP || m->state == DBT_CONNECTING ||
        m->state == DBT_SCANNING) return BT_EBUSY;
    if (seconds < 1 || seconds > 60) return BT_EINVAL;
    m->ndevices = 0;
    m->state = DBT_SCANNING;
    m->error = 0;
    int rc = m->ops->scan(m->driver, seconds);
    if (rc != BT_OK) {
        manager_failed(m, rc);
    }
    return rc;
}

int desktop_bt_connect(DesktopBtManager *m, const BtAddress *peer) {
    if (!m || !peer || !m->ops->link_up) return BT_EINVAL;
    if (m->state == DBT_UP || m->state == DBT_CONNECTING ||
        m->state == DBT_SCANNING) return BT_EBUSY;
    if (!desktop_bt_device_find(m, peer)) return BT_ENOENT;
    m->peer = *peer;
    m->have_peer = 1;
    m->state = DBT_CONNECTING;
    m->error = 0;
    int rc = m->ops->link_up(m->driver, peer);
    if (rc == BT_OK) {
        /* The link is already up. */
        manager_up(m);
        return m->state == DBT_UP ? BT_OK : m->error;
    }
    if (rc == BT_EAGAIN) return BT_OK; /* The driver reports it later. */
    manager_failed(m, rc);
    return rc;
}

void desktop_bt_disconnect(DesktopBtManager *m) {
    if (!m) return;
    /* Detach broker endpoints before the driver frees their link. */
    manager_down(m);
    m->ops->link_down(m->driver);
}

void desktop_bt_poll(DesktopBtManager *m) {
    if (!m) return;
    if (m->ops->poll) m->ops->poll(m->driver);
}

int desktop_bt_state(const DesktopBtManager *m) { return m ? m->state : DBT_IDLE; }

const char *desktop_bt_state_text(const DesktopBtManager *m) {
    switch (desktop_bt_state(m)) {
    case DBT_SCANNING: return "Searching for devices...";
    case DBT_CONNECTING: return "Connecting...";
    case DBT_UP: return "Connected.";
    case DBT_FAILED: return "The link could not be brought up.";
    default: return "Ready.";
    }
}

int desktop_bt_error(const DesktopBtManager *m) { return m ? m->error : 0; }

int desktop_bt_device_count(const DesktopBtManager *m) { return m ? m->ndevices : 0; }

const DesktopBtDevice *desktop_bt_device_at(const DesktopBtManager *m, int index) {
    if (!m || index < 0 || index >= m->ndevices) return NULL;
    return &m->devices[index];
}

const DesktopBtDevice *desktop_bt_device_find(const DesktopBtManager *m, const BtAddress *addr) {
    if (!m || !addr) return NULL;
    for (int i = 0; i < m->ndevices; i++)
        if (bt_addr_cmp(&m->devices[i].addr, addr) == 0) return &m->devices[i];
    return NULL;
}

GnPan *desktop_bt_pan(const DesktopBtManager *m) {
    if (!m || m->state != DBT_UP || !m->ops->pan) return NULL;
    return m->ops->pan(m->driver);
}

const BtAddress *desktop_bt_peer(const DesktopBtManager *m) {
    if (!m || !m->have_peer) return NULL;
    return &m->peer;
}

uint32_t desktop_bt_local_ip(const DesktopBtManager *m) {
    if (!m || !m->net) return 0;
    return desktop_guestnet_local_ip(m->net);
}

/* The desktop keeps one manager. */
static DesktopBtManager *g_manager;

int desktop_bt_start(const DesktopBtDriverOps *ops, DesktopGuestNet *net) {
    desktop_bt_stop();
    const DesktopBtDriverOps *use = ops ? ops : desktop_bt_platform_ops();
    if (!use) return BT_ENOENT;
    g_manager = desktop_bt_new(use, net);
    return g_manager ? BT_OK : BT_ENOMEM;
}

void desktop_bt_stop(void) {
    desktop_bt_free(g_manager);
    g_manager = NULL;
}

DesktopBtManager *desktop_bt_manager(void) { return g_manager; }
