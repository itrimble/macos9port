/* L2CAP signalling plane: connection, configuration and disconnection.
 *
 * Signalling PDUs ride on CID 0x0001 and carry the L2CAP header:
 * length (2), CID (2), code (1), identifier (1), data length (2), data.
 * The length field counts the signalling payload, so it is four more
 * than the data length. */
#include "bt_l2cap_int.h"

#include <stdlib.h>

int bt_l2cap_sig_send(BtL2cap *l, uint16_t handle, uint8_t code, uint8_t lid,
                      const uint8_t *data, size_t n) {
    if (!l->send) return BT_EIO;
    uint8_t pkt[72];
    if (n + 8 > sizeof pkt) return BT_EINVAL;
    bt_wr16(pkt, (uint16_t)(4 + n));
    bt_wr16(pkt + 2, BT_L2CAP_CID_SIG);
    pkt[4] = code;
    pkt[5] = lid;
    bt_wr16(pkt + 6, (uint16_t)n);
    if (n) memcpy(pkt + 8, data, n);
    long w = l->send(l->send_ctx, handle, BT_ACL_PB_FIRST, pkt, n + 8);
    return w < 0 ? BT_EIO : BT_OK;
}

/* Send a configuration request that offers our MTU. */
static int send_config_req(BtL2cap *l, BtL2capChan *c) {
    uint8_t d[16];
    bt_wr16(d, (uint16_t)c->remote_cid);
    bt_wr16(d + 2, 0x0000);   /* flags: no more options */
    d[4] = 0x01;              /* MTU option */
    d[5] = 0x02;
    bt_wr16(d + 6, c->mtu);
    return bt_l2cap_sig_send(l, c->handle, BT_SIG_CONFIG_REQ, bt_l2cap_next_lid(l), d, 8);
}

static int send_conn_rsp(BtL2cap *l, uint16_t handle, int dest_cid, int src_cid,
                         uint16_t result) {
    uint8_t d[8];
    bt_wr16(d, (uint16_t)dest_cid);
    bt_wr16(d + 2, (uint16_t)src_cid);
    bt_wr16(d + 4, result);
    bt_wr16(d + 6, 0);
    return bt_l2cap_sig_send(l, handle, BT_SIG_CONN_RSP, bt_l2cap_next_lid(l), d, sizeof d);
}

static int send_config_rsp(BtL2cap *l, BtL2capChan *c, uint16_t status) {
    uint8_t d[16];
    bt_wr16(d, (uint16_t)c->cid);
    bt_wr16(d + 2, 0x0000);
    bt_wr16(d + 4, status);
    size_t n = 6;
    if (!status) {
        d[6] = 0x01;
        d[7] = 0x02;
        bt_wr16(d + 8, c->mtu);
        n = 10;
    }
    return bt_l2cap_sig_send(l, c->handle, BT_SIG_CONFIG_RSP, bt_l2cap_next_lid(l), d, n);
}

/* Read the MTU option out of a configuration payload. */
static void apply_mtu(BtL2capChan *c, const uint8_t *opts, size_t n) {
    size_t off = 0;
    while (off + 2 <= n) {
        uint8_t otype = opts[off], olen = opts[off + 1];
        if (off + 2 + olen > n) break;
        if (otype == 0x01 && olen == 2) {
            uint16_t m = bt_rd16(opts + off + 2);
            if (m >= BT_L2CAP_MTU_MIN) {
                if (m > BT_HCI_BUF - 16) m = BT_HCI_BUF - 16;
                c->peer_mtu = m;
            }
        }
        off += 2 + olen;
    }
}

void bt_l2cap_handle_sig(BtL2cap *l, uint16_t handle, const uint8_t *p, size_t len) {
    if (len < 8) return;
    uint8_t code = p[4];
    uint8_t lid = p[5];
    uint16_t dlen = bt_rd16(p + 6);
    if (dlen > len - 8) return;
    const uint8_t *d = p + 8;

    switch (code) {
    case BT_SIG_CONN_REQ: {
        if (dlen < 4) return;
        uint16_t psm = bt_rd16(d);
        uint16_t src_cid = bt_rd16(d + 2);
        BtListener *ls = NULL;
        for (int i = 0; i < l->nlistens; i++)
            if (l->listens[i].psm == psm) ls = &l->listens[i];
        if (!ls) {
            send_conn_rsp(l, handle, src_cid, 0, BT_SIG_RESULT_PSM_NOT_SUPPORTED);
            return;
        }
        int cid = -1;
        for (int candidate = BT_L2CAP_CID_FIRST; candidate < BT_L2CAP_CID_FIRST + BT_MAX_CHANS;
             candidate++) {
            int taken = 0;
            for (int i = 0; i < BT_MAX_CHANS; i++) {
                BtL2capChan *e = &l->chans[i];
                if (e->used && e->handle == handle && e->cid == candidate) taken = 1;
            }
            if (!taken) { cid = candidate; break; }
        }
        if (cid < 0) {
            send_conn_rsp(l, handle, src_cid, 0, BT_SIG_RESULT_NO_RESOURCES);
            return;
        }
        BtL2capChan *c = bt_l2cap_chan_alloc(l, handle, cid, psm);
        if (!c) {
            send_conn_rsp(l, handle, src_cid, 0, BT_SIG_RESULT_NO_RESOURCES);
            return;
        }
        c->remote_cid = src_cid;
        c->on_open = ls->on_open;
        c->open_ctx = ls->ctx;
        send_conn_rsp(l, handle, src_cid, cid, BT_SIG_RESULT_OK);
        break;
    }
    case BT_SIG_CONN_RSP: {
        if (dlen < 8) return;
        uint16_t dest_cid = bt_rd16(d);
        uint16_t src_cid = bt_rd16(d + 2);
        uint16_t result = bt_rd16(d + 4);
        BtL2capChan *c = bt_l2cap_chan_find_link(l, handle, dest_cid);
        if (!c) return;
        if (result != BT_SIG_RESULT_OK) {
            bt_l2cap_chan_close(l, c);
            return;
        }
        c->remote_cid = src_cid;
        c->state = BT_L2CAP_WAIT_CONFIG_RSP;
        send_config_req(l, c);
        break;
    }
    case BT_SIG_CONFIG_REQ: {
        if (dlen < 4) return;
        uint16_t dest_cid = bt_rd16(d);
        BtL2capChan *c = bt_l2cap_chan_find_link(l, handle, dest_cid);
        if (!c) return;
        apply_mtu(c, d + 4, dlen - 4);
        send_config_rsp(l, c, BT_OK);
        if (c->state != BT_L2CAP_OPEN) {
            c->state = BT_L2CAP_OPEN;
            if (c->on_open) c->on_open(c->open_ctx, c);
        }
        break;
    }
    case BT_SIG_CONFIG_RSP: {
        if (dlen < 6) return;
        uint16_t src_cid = bt_rd16(d);
        uint16_t status = bt_rd16(d + 4);
        BtL2capChan *c = bt_l2cap_chan_find_link(l, handle, src_cid);
        if (!c) return;
        if (status != BT_OK) {
            bt_l2cap_chan_close(l, c);
            return;
        }
        apply_mtu(c, d + 6, dlen - 6);
        if (c->state != BT_L2CAP_OPEN) {
            c->state = BT_L2CAP_OPEN;
            if (c->on_open) c->on_open(c->open_ctx, c);
        }
        break;
    }
    case BT_SIG_DISCONN_REQ: {
        if (dlen < 2) return;
        uint16_t dest_cid = bt_rd16(d);
        uint8_t rsp[2];
        bt_wr16(rsp, dest_cid);
        bt_l2cap_sig_send(l, handle, BT_SIG_DISCONN_RSP, lid, rsp, sizeof rsp);
        BtL2capChan *c = bt_l2cap_chan_find_link(l, handle, dest_cid);
        if (c) bt_l2cap_chan_close(l, c);
        break;
    }
    case BT_SIG_DISCONN_RSP: {
        if (dlen < 2) return;
        uint16_t dest_cid = bt_rd16(d);
        BtL2capChan *c = bt_l2cap_chan_find_link(l, handle, dest_cid);
        if (c) bt_l2cap_chan_close(l, c);
        break;
    }
    case BT_SIG_ECHO_REQ:
        bt_l2cap_sig_send(l, handle, BT_SIG_ECHO_RSP, lid, d, dlen);
        break;
    default:
        break;
    }
}
