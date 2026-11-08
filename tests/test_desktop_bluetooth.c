/* The desktop Bluetooth manager over a fake driver: devices arrive, a link
 * comes up, the guest stack gets the address on the link, and the link goes
 * down again. */
#include "desktop_bluetooth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
static int delayed;
static struct FakeDriverTag *latest;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* ---- the fake driver ---- */

typedef struct FakeDriverTag {
    DesktopBtCallbacks cb;
    GnPanLink link;
    GnPan *pan;
    BtAddress peer;
    int scanned, linked, closed, freed;
} FakeDriver;

static int fake_tx(void *ctx, const void *frame, size_t len) {
    (void)ctx; (void)frame; (void)len;
    return GN_OK;
}

static DesktopBtDriver *fake_create(const DesktopBtCallbacks *cb) {
    FakeDriver *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cb = *cb;
    latest = d;
    d->link.tx = fake_tx;
    d->link.ctx = d;
    return (DesktopBtDriver *)d;
}

static int fake_scan(DesktopBtDriver *driver, int seconds) {
    FakeDriver *d = (FakeDriver *)driver;
    (void)seconds;
    d->scanned++;
    BtAddress a, b;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_addr_parse("00:00:00:00:00:08", &b);
    d->cb.found(d->cb.ctx, &a, "Peer One", 0x00010C);
    d->cb.found(d->cb.ctx, &b, "Peer Two", 0x000104);
    d->cb.found(d->cb.ctx, &a, "Peer One", 0x00010C); /* The same device again. */
    d->cb.scan_done(d->cb.ctx);
    return BT_OK;
}

static int fake_link_up(DesktopBtDriver *driver, const BtAddress *peer) {
    FakeDriver *d = (FakeDriver *)driver;
    d->linked++;
    d->peer = *peer;
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t peer_mac[6];
    memcpy(peer_mac, peer->b, 6);
    d->pan = gn_pan_new(&d->link, DBT_PAN_LOCAL_IP, DBT_PAN_NETMASK, mac);
    if (!d->pan) return BT_ENOMEM;
    gn_pan_set_peer(d->pan, DBT_PAN_PEER_IP, peer_mac);
    return delayed ? BT_EAGAIN : BT_OK;
}

static void fake_link_down(DesktopBtDriver *driver) {
    FakeDriver *d = (FakeDriver *)driver;
    d->closed++;
    gn_pan_free(d->pan);
    d->pan = NULL;
}

static void fake_poll(DesktopBtDriver *driver) { (void)driver; }
static GnPan *fake_pan(DesktopBtDriver *driver) { return ((FakeDriver *)driver)->pan; }
static void fake_free(DesktopBtDriver *driver) {
    FakeDriver *d = (FakeDriver *)driver;
    d->freed++;
    gn_pan_free(d->pan);
    free(d);
}

static const DesktopBtDriverOps k_fake_ops = {
    .create = fake_create,
    .scan = fake_scan,
    .link_up = fake_link_up,
    .link_down = fake_link_down,
    .poll = fake_poll,
    .pan = fake_pan,
    .free = fake_free,
};

/* ---- the test ---- */

int main(void) {
    DesktopGuestNet *net = desktop_guestnet_new();
    CHECK(net != NULL);
    DesktopBtManager *m = desktop_bt_new(&k_fake_ops, net);
    CHECK(m != NULL);
    if (!m) return 1;

    /* Nothing has happened yet. */
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_device_count(m) == 0);
    CHECK(desktop_bt_pan(m) == NULL);
    CHECK(desktop_bt_local_ip(m) == 0);

    /* A scan reports the devices that answer, once each. */
    CHECK(desktop_bt_scan(m, 5) == BT_OK);
    CHECK(desktop_bt_device_count(m) == 2);
    const DesktopBtDevice *d0 = desktop_bt_device_at(m, 0);
    const DesktopBtDevice *d1 = desktop_bt_device_at(m, 1);
    CHECK(d0 && strcmp(d0->name, "Peer One") == 0);
    CHECK(d1 && strcmp(d1->name, "Peer Two") == 0);
    CHECK(d0 && d0->class_of_device == 0x00010C);
    CHECK(desktop_bt_device_at(m, 2) == NULL);

    /* The manager finds a device by address. */
    BtAddress peer;
    bt_addr_parse("00:00:00:00:00:08", &peer);
    const DesktopBtDevice *found = desktop_bt_device_find(m, &peer);
    CHECK(found && found == d1);

    /* Clicking a device brings the link up. */
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    CHECK(desktop_bt_state(m) == DBT_UP);
    CHECK(desktop_bt_pan(m) != NULL);
    CHECK(desktop_bt_peer(m) && bt_addr_cmp(desktop_bt_peer(m), &peer) == 0);

    /* The guest stack keeps its own address on the link. */
    CHECK(desktop_guestnet_local_ip(net) == DBT_PAN_LOCAL_IP);
    CHECK(desktop_guestnet_netmask(net) == DBT_PAN_NETMASK);
    CHECK(desktop_bt_local_ip(m) == DBT_PAN_LOCAL_IP);

    /* The link goes down and the guest stack is untouched again. */
    desktop_bt_disconnect(m);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_pan(m) == NULL);
    CHECK(desktop_guestnet_local_ip(net) == 0);

    /* A peer that is not in the list is refused. */
    BtAddress unknown;
    bt_addr_parse("00:00:00:00:00:09", &unknown);
    CHECK(desktop_bt_connect(m, &unknown) == BT_ENOENT);

    /* Pending connections cannot be replaced; cancel detaches before free. */
    delayed = 1;
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    CHECK(desktop_bt_state(m) == DBT_CONNECTING);
    CHECK(desktop_bt_connect(m, &peer) == BT_EBUSY);
    CHECK(desktop_bt_scan(m, 5) == BT_EBUSY);
    desktop_bt_disconnect(m);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    latest->cb.up(latest->cb.ctx); /* stale completion after cancellation */
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_guestnet_local_ip(net) == 0);
    CHECK(desktop_bt_scan(m, 0) == BT_EINVAL);
    CHECK(desktop_bt_scan(m, 61) == BT_EINVAL);
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    latest->cb.failed(latest->cb.ctx, BT_ETIMEOUT);
    CHECK(desktop_bt_state(m) == DBT_FAILED);
    CHECK(desktop_bt_error(m) == BT_ETIMEOUT);
    CHECK(desktop_bt_pan(m) == NULL);
    CHECK(desktop_bt_peer(m) == NULL);
    latest->cb.up(latest->cb.ctx);
    CHECK(desktop_bt_state(m) == DBT_FAILED);
    delayed = 0;
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    desktop_bt_free(m);
    CHECK(desktop_guestnet_local_ip(net) == 0);
    desktop_guestnet_pump(net);
    desktop_guestnet_free(net);
    printf("desktop bluetooth: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
