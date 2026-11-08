/* Production driver over the IOBluetooth wrapper. No discovery or device
 * connection occurs until the caller explicitly requests it. */
#include "desktop_bluetooth.h"
#include "bt_darwin.h"
#include "os999_bt_panif.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    DesktopBtCallbacks cb;
    BtDarwin *host;
    BtL2cap *l2;
    BtBnep *bnep;
    BtPan *pan;
    BtPanIf *iface;
    BtAddress peer, local;
    int cid, pan_id, connecting, up, error, closed;
    uint16_t handle;
    uint64_t deadline;
} DarwinDriver;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void release_link(DarwinDriver *d) {
    bt_darwin_close_l2cap(d->host); /* suppress callbacks before freeing contexts */
    bt_panif_free(d->iface); d->iface = NULL;
    bt_pan_free(d->pan); d->pan = NULL;
    bt_bnep_free(d->bnep); d->bnep = NULL;
    bt_l2cap_free(d->l2); d->l2 = NULL;
    d->cid = -1;
    d->connecting = d->up = d->error = d->closed = 0;
}

static DesktopBtDriver *create(const DesktopBtCallbacks *cb) {
    if (!cb) return NULL;
    DarwinDriver *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cb = *cb;
    d->cid = -1;
    d->host = bt_darwin_new();
    if (!d->host) { free(d); return NULL; }
    return (DesktopBtDriver *)d;
}

static void found(void *ctx, const BtAddress *addr, const char *name, uint32_t cod) {
    DarwinDriver *d = ctx;
    if (d->cb.found) d->cb.found(d->cb.ctx, addr, name, cod);
}
static void scan_done(void *ctx, int status) {
    DarwinDriver *d = ctx;
    if (status != BT_OK) { d->error = status; return; }
    if (d->cb.scan_done) d->cb.scan_done(d->cb.ctx);
}
static int scan(DesktopBtDriver *driver, int seconds) {
    DarwinDriver *d = (DarwinDriver *)driver;
    if (!d || seconds < 1 || seconds > 60) return BT_EINVAL;
    return bt_darwin_discover_with_done(d->host, (uint32_t)seconds, found, scan_done, d);
}

static void opened(void *ctx, uint16_t handle, const BtDarwinPipe *pipe) {
    DarwinDriver *d = ctx;
    if (!d->connecting || !pipe) return;
    d->handle = handle;
    BtL2capPipe p = {.write = pipe->write, .read = pipe->read, .ctx = pipe->ctx};
    int rc = bt_l2cap_adopt(d->l2, handle, &d->peer, BT_PSM_BNEP, 1691, &p, &d->cid);
    if (rc == BT_OK) rc = bt_bnep_adopt(d->bnep, d->cid, &d->peer);
    if (rc != BT_OK) d->error = rc;
}
static void received(void *ctx, const uint8_t *p, size_t n) {
    DarwinDriver *d = ctx;
    if (d->l2 && d->cid >= 0) bt_l2cap_pipe_rx(d->l2, d->cid, p, n);
    if (d->connecting && n == 4 && p[0] == 1 && p[1] == 2 && (p[2] || p[3]))
        d->error = BT_EDENIED;
}
static void closed(void *ctx) { ((DarwinDriver *)ctx)->closed = 1; }

static int link_up(DesktopBtDriver *driver, const BtAddress *peer) {
    DarwinDriver *d = (DarwinDriver *)driver;
    if (!d || !peer) return BT_EINVAL;
    if (d->connecting || d->up) return BT_EBUSY;
    release_link(d);
    bt_darwin_stop_discover(d->host);
    int rc = bt_darwin_local_address(d->host, &d->local);
    if (rc != BT_OK) return rc;
    d->peer = *peer;
    d->l2 = bt_l2cap_new();
    d->bnep = bt_bnep_new(d->l2);
    if (!d->l2 || !d->bnep) { release_link(d); return BT_ENOMEM; }
    d->connecting = 1;
    d->deadline = now_ms() + 15000;
    rc = bt_darwin_open_l2cap(d->host, peer, BT_PSM_BNEP, opened, received, closed, d);
    if (rc != BT_OK) { release_link(d); return rc; }
    return BT_EAGAIN;
}

static void link_down(DesktopBtDriver *driver) {
    DarwinDriver *d = (DarwinDriver *)driver;
    if (!d) return;
    bt_darwin_stop_discover(d->host);
    release_link(d);
}

static int finish_setup(DarwinDriver *d) {
    d->pan = bt_pan_new(d->bnep);
    if (!d->pan) return BT_ENOMEM;
    bt_pan_set_local_mac(d->pan, d->local.b);
    int rc = bt_pan_open(d->pan, d->handle, &d->peer, d->peer.b, &d->pan_id);
    if (rc != BT_OK) return rc;
    d->iface = bt_panif_new(d->pan, d->pan_id, DBT_PAN_LOCAL_IP, DBT_PAN_NETMASK, d->local.b);
    if (!d->iface) return BT_ENOMEM;
    return bt_panif_set_peer(d->iface, DBT_PAN_PEER_IP, d->peer.b);
}

static void poll_driver(DesktopBtDriver *driver) {
    DarwinDriver *d = (DarwinDriver *)driver;
    if (!d) return;
    bt_darwin_poll(d->host);
    if (d->closed) {
        d->closed = 0;
        /* Broker must detach before the driver's PAN disappears. */
        if (d->cb.down) d->cb.down(d->cb.ctx);
        release_link(d);
        return;
    }
    if (d->connecting && !d->error) {
        BtBnepInfo info;
        if (bt_bnep_info(d->bnep, d->cid, &info) == BT_OK && info.up) {
            d->error = finish_setup(d);
            if (!d->error) {
                d->connecting = 0;
                d->up = 1;
                if (d->cb.up) d->cb.up(d->cb.ctx);
            }
        } else if (now_ms() >= d->deadline) d->error = BT_ETIMEOUT;
    }
    if (d->error) {
        int error = d->error;
        d->error = 0;
        if (d->cb.failed) d->cb.failed(d->cb.ctx, error);
        /* Manager's failed callback cancels the driver; direct users must too. */
        return;
    }
    if (d->iface) bt_panif_pump(d->iface);
}

static GnPan *pan(DesktopBtDriver *driver) {
    DarwinDriver *d = (DarwinDriver *)driver;
    return d && d->up ? bt_panif_pan(d->iface) : NULL;
}
static void destroy(DesktopBtDriver *driver) {
    DarwinDriver *d = (DarwinDriver *)driver;
    if (!d) return;
    link_down(driver);
    bt_darwin_free(d->host);
    free(d);
}
static const DesktopBtDriverOps ops = {create, scan, link_up, link_down, poll_driver, pan, destroy};
const DesktopBtDriverOps *desktop_bt_darwin_ops(void) { return &ops; }
const DesktopBtDriverOps *desktop_bt_platform_ops(void) { return &ops; }
int desktop_bt_available(void) { return bt_darwin_available(); }
const char *desktop_bt_unavailable_reason(void) { return "Bluetooth is unavailable or powered off"; }
