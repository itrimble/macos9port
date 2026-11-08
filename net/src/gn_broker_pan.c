/* The broker's PAN endpoints: a guest TCP connection to an address on the
 * PAN subnet travels over that link instead of a host socket, so the guest
 * keeps its own address and its own TCP. */
#include "gn_broker_int.h"
#include "gn_pan_int.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

/* How the guest sees a TCP state. */
static int pan_state(GnPanTcp *t) {
    switch (gn_pan_tcp_state(t)) {
    case GN_TS_SYN_SENT:
    case GN_TS_SYN_RECEIVED: return GN_S_CONNECTING;
    case GN_TS_ESTABLISHED:
    case GN_TS_FIN_WAIT_1:
    case GN_TS_FIN_WAIT_2: return GN_S_OPEN;
    case GN_TS_CLOSE_WAIT:
    case GN_TS_CLOSING:
    case GN_TS_LAST_ACK:
    case GN_TS_TIME_WAIT: return GN_S_PEER_CLOSED;
    default: return GN_S_CLOSED;
    }
}

void gn_pan_conn_refresh(GnConn *c) {
    if (!c || !c->tcp) return;
    if (c->tcp->reset) {
        c->state = GN_S_FAILED;
        c->error = GN_ECLOSED;
        return;
    }
    c->state = pan_state(c->tcp);
}

static void set_addr(struct sockaddr_storage *ss, uint32_t ip, uint16_t port) {
    memset(ss, 0, sizeof *ss);
    struct sockaddr_in *a = (struct sockaddr_in *)ss;
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(ip);
    a->sin_port = htons(port);
}

int gn_pan_conn_connect(GnBroker *b, uint32_t ip, uint16_t port, int *id) {
    if (!b || !b->pan || !id) return GN_EINVAL;
    int rc = gn_broker_slot(b, GN_K_PAN, id);
    if (rc != GN_OK) return rc;
    GnConn *c = &b->c[*id];
    GnPanTcp *t = gn_pan_tcp_new(b->pan);
    if (!t) {
        gn_broker_conn_free(c);
        return GN_ENOMEM;
    }
    c->tcp = t;
    if (gn_pan_tcp_connect(t, ip, port, 0) != GN_OK) {
        gn_broker_conn_free(c);
        return GN_EIO;
    }
    set_addr(&c->local, gn_pan_local_ip(b->pan), gn_pan_tcp_local_port(t));
    set_addr(&c->remote, ip, port);
    c->remote_len = sizeof(struct sockaddr_in);
    c->state = GN_S_CONNECTING;
    return GN_OK;
}

int gn_pan_conn_listen(GnBroker *b, uint16_t port, int *id) {
    if (!b || !b->pan || !id) return GN_EINVAL;
    int rc = gn_broker_slot(b, GN_K_PAN_LISTEN, id);
    if (rc != GN_OK) return rc;
    GnConn *c = &b->c[*id];
    GnPanTcp *t = gn_pan_tcp_new(b->pan);
    if (!t) {
        gn_broker_conn_free(c);
        return GN_ENOMEM;
    }
    c->tcp = t;
    if (gn_pan_tcp_listen(t, port) != GN_OK) {
        gn_broker_conn_free(c);
        return GN_EIO;
    }
    set_addr(&c->local, gn_pan_local_ip(b->pan), gn_pan_tcp_local_port(t));
    c->state = GN_S_LISTENING;
    return GN_OK;
}

int gn_pan_conn_accept(GnBroker *b, int listener, int *id) {
    GnConn *l = gn_broker_get(b, listener);
    if (!l || l->kind != GN_K_PAN_LISTEN || !l->tcp || !id) return GN_EINVAL;
    GnPanTcp *child = NULL;
    int rc = gn_pan_tcp_accept(l->tcp, &child);
    if (rc != GN_OK) return rc;
    rc = gn_broker_slot(b, GN_K_PAN, id);
    if (rc != GN_OK) {
        gn_pan_tcp_free(child);
        return rc;
    }
    GnConn *c = &b->c[*id];
    c->tcp = child;
    set_addr(&c->local, gn_pan_local_ip(b->pan), gn_pan_tcp_local_port(child));
    set_addr(&c->remote, child->peer_ip, child->peer_port);
    c->remote_len = sizeof(struct sockaddr_in);
    gn_pan_conn_refresh(c);
    return GN_OK;
}

long gn_pan_conn_send(GnBroker *b, GnConn *c, const void *p, size_t n) {
    (void)b;
    if (!c || !c->tcp) return GN_EINVAL;
    gn_pan_conn_refresh(c);
    if (c->state == GN_S_FAILED || c->state == GN_S_CLOSED) return GN_ECLOSED;
    if (c->closing) return GN_ECLOSED;
    return gn_pan_tcp_send(c->tcp, p, n);
}

long gn_pan_conn_recv(GnBroker *b, GnConn *c, void *p, size_t n) {
    (void)b;
    if (!c || !c->tcp) return GN_EINVAL;
    gn_pan_conn_refresh(c);
    return gn_pan_tcp_recv(c->tcp, p, n);
}

int gn_pan_conn_shutdown(GnBroker *b, GnConn *c) {
    (void)b;
    if (!c || !c->tcp) return GN_EINVAL;
    c->closing = 1;
    gn_pan_tcp_shutdown(c->tcp);
    gn_pan_conn_refresh(c);
    return GN_OK;
}

int gn_pan_conn_abort(GnBroker *b, GnConn *c) {
    (void)b;
    if (!c || !c->tcp) return GN_EINVAL;
    gn_pan_tcp_abort(c->tcp);
    c->state = GN_S_CLOSED;
    c->in.len = c->out.len = 0;
    return GN_OK;
}

int gn_pan_conn_info(GnConn *c, GnInfo *out) {
    if (!c || !c->tcp || !out) return GN_EINVAL;
    memset(out, 0, sizeof *out);
    gn_pan_conn_refresh(c);
    out->kind = c->kind;
    out->state = c->state;
    out->error = c->error;
    out->plaintext = 1;
    out->unread = c->tcp->rx.len;
    out->unsent = c->tcp->tx.len;
    out->local = c->local;
    out->remote = c->remote;
    return GN_OK;
}
