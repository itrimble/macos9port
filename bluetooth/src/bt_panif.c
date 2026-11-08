/* Binding a Bluetooth personal area network to the guest IP stack.
 *
 * The Bluetooth stack carries Ethernet frames over BNEP; the guest network
 * stack owns IP, ARP and TCP. This file is the seam: it hands the guest
 * stack's frames to the link and the link's frames to the guest stack.
 * The guest keeps its own address on the PAN, so its TCP runs over the
 * Bluetooth link instead of a host socket. */
#include "os999_bt_panif.h"

#include <stdlib.h>
#include <string.h>

enum { BT_PANIF_FRAME = 8192 };

struct BtPanIf {
    BtPan *pan;
    int id;
    GnPan *gn;
};

/* Hand one Ethernet frame to the Bluetooth link. */
static int panif_tx(void *ctx, const void *frame, size_t len) {
    BtPanIf *p = ctx;
    if (!p || !p->pan) return GN_EIO;
    int rc = bt_pan_send(p->pan, p->id, frame, len);
    return rc == BT_OK ? GN_OK : GN_EIO;
}

BtPanIf *bt_panif_new(BtPan *pan, int id, uint32_t local_ip, uint32_t netmask, const uint8_t mac[6]) {
    if (!pan) return NULL;
    uint8_t local[6];
    if (mac) {
        memcpy(local, mac, 6);
    } else if (bt_pan_local_mac(pan, local) != BT_OK) {
        return NULL;
    }
    BtPanIf *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->pan = pan;
    p->id = id;
    GnPanLink link;
    link.tx = panif_tx;
    link.ctx = p;
    p->gn = gn_pan_new(&link, local_ip, netmask, local);
    if (!p->gn) {
        free(p);
        return NULL;
    }
    return p;
}

void bt_panif_free(BtPanIf *p) {
    if (!p) return;
    gn_pan_free(p->gn);
    free(p);
}

GnPan *bt_panif_pan(BtPanIf *p) { return p ? p->gn : NULL; }

int bt_panif_set_peer(BtPanIf *p, uint32_t peer_ip, const uint8_t peer_mac[6]) {
    if (!p || !p->gn || !peer_mac) return GN_EINVAL;
    return gn_pan_set_peer(p->gn, peer_ip, peer_mac);
}

int bt_panif_pump(BtPanIf *p) {
    if (!p || !p->gn) return GN_EINVAL;
    uint8_t frame[BT_PANIF_FRAME];
    int n = 0;
    for (;;) {
        long k = bt_pan_recv(p->pan, p->id, frame, sizeof frame);
        if (k <= 0) break;
        gn_pan_rx(p->gn, frame, (size_t)k);
        n++;
    }
    return n;
}
