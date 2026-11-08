/* Socket broker: nonblocking host sockets driven by poll(), bounded ring
 * buffers per connection, policy checked before any connect, listen or
 * datagram, timeouts for connects, and metadata-only logging. A guest TCP
 * connection to an address on an attached personal area network travels
 * over that link instead, through the PAN endpoints in gn_broker_pan.c. */
#include "gn_broker_int.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void gn_broker_log(GnBroker *b, const char *fmt, const char *what, const struct sockaddr *a,
                   const char *why) {
    if (!b->log) return;
    char ip[INET6_ADDRSTRLEN] = "?", line[400], safe[400];
    int port = 0;
    if (a && a->sa_family == AF_INET) {
        inet_ntop(AF_INET, &((const struct sockaddr_in *)a)->sin_addr, ip, sizeof ip);
        port = ntohs(((const struct sockaddr_in *)a)->sin_port);
    } else if (a && a->sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)a)->sin6_addr, ip, sizeof ip);
        port = ntohs(((const struct sockaddr_in6 *)a)->sin6_port);
    }
    snprintf(line, sizeof line, fmt, b->app, what, ip, port, why ? why : "");
    gn_redact(line, safe, sizeof safe);
    b->log(b->log_ctx, safe);
}

GnBroker *gn_broker_new(const GnPolicy *policy, const char *app, const GnLimits *lim, GnLogFn log,
                         void *log_ctx) {
    GnBroker *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    if (policy) b->policy = *policy; else gn_policy_init(&b->policy);
    snprintf(b->app, sizeof b->app, "%s", app ? app : "");
    GnLimits def = {64, 65536, 65536, 30000};
    b->lim = lim ? *lim : def;
    if (!b->lim.max_conns || b->lim.max_conns > 1024) b->lim.max_conns = 64;
    if (!b->lim.send_cap) b->lim.send_cap = 65536;
    if (!b->lim.recv_cap) b->lim.recv_cap = 65536;
    if (!b->lim.connect_timeout_ms) b->lim.connect_timeout_ms = 30000;
    b->log = log;
    b->log_ctx = log_ctx;
    b->c = calloc(b->lim.max_conns, sizeof *b->c);
    if (!b->c) { free(b); return NULL; }
    for (size_t i = 0; i < b->lim.max_conns; i++) b->c[i].fd = -1;
    return b;
}

void gn_broker_conn_free(GnConn *c) {
    if (c->fd >= 0) close(c->fd);
    if (c->tcp) {
        gn_pan_tcp_free(c->tcp);
        c->tcp = NULL;
    }
    free(c->in.p);
    free(c->out.p);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

void gn_broker_free(GnBroker *b) {
    if (!b) return;
    for (size_t i = 0; i < b->lim.max_conns; i++) gn_broker_conn_free(&b->c[i]);
    free(b->c);
    free(b);
}

GnConn *gn_broker_get(GnBroker *b, int id) {
    return b && id >= 0 && (size_t)id < b->lim.max_conns && b->c[id].kind ? &b->c[id] : NULL;
}

int gn_broker_slot(GnBroker *b, int kind, int *id) {
    for (size_t i = 0; i < b->lim.max_conns; i++) {
        if (b->c[i].kind) continue;
        GnConn *c = &b->c[i];
        memset(c, 0, sizeof *c);
        c->fd = -1;
        c->kind = kind;
        c->in.cap = b->lim.recv_cap;
        c->out.cap = b->lim.send_cap;
        c->in.p = malloc(c->in.cap);
        c->out.p = malloc(c->out.cap);
        if (!c->in.p || !c->out.p) { gn_broker_conn_free(c); return GN_ENOMEM; }
        *id = (int)i;
        return GN_OK;
    }
    return GN_ELIMIT;
}

static int nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ? -1 : 0;
}

int gn_broker_set_pan(GnBroker *b, GnPan *pan) {
    if (!b) return GN_EINVAL;
    b->pan = pan;
    return GN_OK;
}

uint32_t gn_broker_local_ip(GnBroker *b) { return b && b->pan ? gn_pan_local_ip(b->pan) : 0; }

uint32_t gn_broker_netmask(GnBroker *b) { return b && b->pan ? gn_pan_netmask(b->pan) : 0; }

int gn_tcp_connect(GnBroker *b, const struct sockaddr *addr, socklen_t len, const char *host, int *id) {
    if (!b || !addr || !id || len > sizeof(struct sockaddr_storage)) return GN_EINVAL;
    char why[160];
    int rc = gn_policy_check(&b->policy, b->app, GN_OUT, GN_TCP, addr, host, why, sizeof why);
    gn_broker_log(b, "%s tcp %s %s:%d (%s)", rc ? "denied" : "connect", addr, why);
    if (rc) return rc;
    /* An address on the personal area network travels over that link. */
    if (b->pan && addr->sa_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
        if (gn_pan_owns(b->pan, ntohl(a->sin_addr.s_addr)))
            return gn_pan_conn_connect(b, ntohl(a->sin_addr.s_addr), ntohs(a->sin_port), id);
    }
    if ((rc = gn_broker_slot(b, GN_K_TCP, id))) return rc;
    GnConn *c = &b->c[*id];
    c->fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (c->fd < 0 || nonblock(c->fd)) { gn_broker_conn_free(c); return GN_EIO; }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(c->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    memcpy(&c->remote, addr, len);
    c->remote_len = len;
    c->deadline = now_ms() + b->lim.connect_timeout_ms;
    if (connect(c->fd, addr, len) == 0) c->state = GN_S_OPEN;
    else if (errno == EINPROGRESS) c->state = GN_S_CONNECTING;
    else { gn_broker_conn_free(c); return GN_EIO; }
    socklen_t ll = sizeof c->local;
    getsockname(c->fd, (struct sockaddr *)&c->local, &ll);
    return GN_OK;
}

int gn_tcp_listen(GnBroker *b, int family, uint16_t port, int *id) {
    if (!b || !id || (family != AF_INET && family != AF_INET6)) return GN_EINVAL;
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    socklen_t len;
    int lan = b->policy.allow_lan_listen;
    if (family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        a->sin_family = AF_INET;
        a->sin_port = htons(port);
        a->sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
        len = sizeof *a;
    } else {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(port);
        a->sin6_addr = lan ? in6addr_any : in6addr_loopback;
        len = sizeof *a;
    }
    char why[160];
    int rc = gn_policy_check(&b->policy, b->app, GN_IN, GN_TCP, (struct sockaddr *)&ss, NULL, why, sizeof why);
    gn_broker_log(b, "%s listen %s %s:%d (%s)", rc ? "denied" : "allowed", (struct sockaddr *)&ss, why);
    if (rc) return rc;
    /* With a personal area network attached the guest listens on it. */
    if (b->pan && family == AF_INET) return gn_pan_conn_listen(b, port, id);
    if ((rc = gn_broker_slot(b, GN_K_LISTEN, id))) return rc;
    GnConn *c = &b->c[*id];
    int one = 1;
    c->fd = socket(family, SOCK_STREAM, 0);
    if (c->fd < 0 || nonblock(c->fd) || setsockopt(c->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) ||
        bind(c->fd, (struct sockaddr *)&ss, len) || listen(c->fd, 8)) {
        gn_broker_conn_free(c);
        return GN_EIO;
    }
    socklen_t ll = sizeof c->local;
    getsockname(c->fd, (struct sockaddr *)&c->local, &ll);
    c->state = GN_S_LISTENING;
    return GN_OK;
}

int gn_tcp_accept(GnBroker *b, int listener, int *id) {
    GnConn *l = gn_broker_get(b, listener);
    if (!l || !id) return GN_EINVAL;
    if (l->kind == GN_K_PAN_LISTEN) return gn_pan_conn_accept(b, listener, id);
    if (l->kind != GN_K_LISTEN) return GN_EINVAL;
    if (!l->naccepted) return GN_EAGAIN;
    *id = l->accepted[0];
    memmove(l->accepted, l->accepted + 1, (size_t)--l->naccepted * sizeof *l->accepted);
    return GN_OK;
}

long gn_send(GnBroker *b, int id, const void *p, size_t n) {
    GnConn *c = gn_broker_get(b, id);
    if (!c) return GN_EINVAL;
    if (c->kind == GN_K_PAN) return gn_pan_conn_send(b, c, p, n);
    if (c->kind != GN_K_TCP) return GN_EINVAL;
    if (c->state == GN_S_FAILED || c->state == GN_S_CLOSED) return GN_ECLOSED;
    if (c->closing) return GN_ECLOSED;
    return (long)gn_ring_put(&c->out, p, n);
}

long gn_recv(GnBroker *b, int id, void *p, size_t n) {
    GnConn *c = gn_broker_get(b, id);
    if (!c) return GN_EINVAL;
    if (c->kind == GN_K_PAN) return gn_pan_conn_recv(b, c, p, n);
    if (c->kind != GN_K_TCP) return GN_EINVAL;
    size_t k = gn_ring_get(&c->in, p, n);
    if (!k && (c->state == GN_S_PEER_CLOSED || c->state == GN_S_CLOSED || c->state == GN_S_FAILED))
        return GN_ECLOSED;
    return (long)k;
}

int gn_shutdown(GnBroker *b, int id) {
    GnConn *c = gn_broker_get(b, id);
    if (!c) return GN_EINVAL;
    if (c->kind == GN_K_PAN) return gn_pan_conn_shutdown(b, c);
    if (c->kind != GN_K_TCP) return GN_EINVAL;
    c->closing = 1;
    if (!c->out.len && c->fd >= 0 && c->state == GN_S_OPEN) shutdown(c->fd, SHUT_WR);
    return GN_OK;
}

int gn_abort(GnBroker *b, int id) {
    GnConn *c = gn_broker_get(b, id);
    if (!c) return GN_EINVAL;
    if (c->kind == GN_K_PAN) return gn_pan_conn_abort(b, c);
    if (c->fd >= 0) {
        struct linger lg = {1, 0};
        setsockopt(c->fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
        close(c->fd);
        c->fd = -1;
    }
    c->state = GN_S_CLOSED;
    c->in.len = c->out.len = 0;
    return GN_OK;
}

int gn_release(GnBroker *b, int id) {
    GnConn *c = gn_broker_get(b, id);
    if (!c) return GN_EINVAL;
    gn_broker_conn_free(c);
    return GN_OK;
}

int gn_udp_open(GnBroker *b, int family, uint16_t port, int *id) {
    if (!b || !id || (family != AF_INET && family != AF_INET6)) return GN_EINVAL;
    int rc = gn_broker_slot(b, GN_K_UDP, id);
    if (rc) return rc;
    GnConn *c = &b->c[*id];
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    socklen_t len;
    if (family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        a->sin_family = AF_INET;
        a->sin_port = htons(port);
        a->sin_addr.s_addr = htonl(b->policy.allow_lan_listen ? INADDR_ANY : INADDR_LOOPBACK);
        len = sizeof *a;
        if (!port) a->sin_addr.s_addr = htonl(INADDR_ANY); /* outbound-only socket */
    } else {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(port);
        a->sin6_addr = b->policy.allow_lan_listen || !port ? in6addr_any : in6addr_loopback;
        len = sizeof *a;
    }
    c->fd = socket(family, SOCK_DGRAM, 0);
    if (c->fd < 0 || nonblock(c->fd) || bind(c->fd, (struct sockaddr *)&ss, len)) {
        gn_broker_conn_free(c);
        return GN_EIO;
    }
    socklen_t ll = sizeof c->local;
    getsockname(c->fd, (struct sockaddr *)&c->local, &ll);
    c->state = GN_S_OPEN;
    return GN_OK;
}

long gn_udp_sendto(GnBroker *b, int id, const struct sockaddr *addr, socklen_t len, const void *p,
                   size_t n) {
    GnConn *c = gn_broker_get(b, id);
    if (!c || c->kind != GN_K_UDP || !addr) return GN_EINVAL;
    char why[160];
    int rc = gn_policy_check(&b->policy, b->app, GN_OUT, GN_UDP, addr, NULL, why, sizeof why);
    if (rc) { gn_broker_log(b, "%s udp %s %s:%d (%s)", "denied", addr, why); return rc; }
    if (n > 65507) return GN_ELIMIT;
    ssize_t k = sendto(c->fd, p, n, 0, addr, len);
    if (k < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? GN_EAGAIN : GN_EIO;
    return (long)k;
}

long gn_udp_recvfrom(GnBroker *b, int id, void *p, size_t n, struct sockaddr_storage *from) {
    GnConn *c = gn_broker_get(b, id);
    if (!c || c->kind != GN_K_UDP) return GN_EINVAL;
    for (;;) {
        socklen_t fl = sizeof *from;
        ssize_t k = recvfrom(c->fd, p, n, MSG_TRUNC, (struct sockaddr *)from, &fl);
        if (k < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? GN_EAGAIN : GN_EIO;
        /* Inbound datagrams obey the same policy as outbound ones. */
        if (gn_policy_check(&b->policy, b->app, GN_IN, GN_UDP, (struct sockaddr *)from, NULL, NULL, 0) &&
            gn_policy_check(&b->policy, b->app, GN_OUT, GN_UDP, (struct sockaddr *)from, NULL, NULL, 0))
            continue; /* dropped */
        if ((size_t)k > n) return GN_ELIMIT; /* truncated datagram */
        return (long)k;
    }
}

int gn_info(GnBroker *b, int id, GnInfo *out) {
    GnConn *c = gn_broker_get(b, id);
    if (!c || !out) return GN_EINVAL;
    if (c->kind == GN_K_PAN || c->kind == GN_K_PAN_LISTEN) return gn_pan_conn_info(c, out);
    memset(out, 0, sizeof *out);
    out->kind = c->kind;
    out->state = c->state;
    out->error = c->error;
    out->plaintext = 1;
    out->unread = c->in.len;
    out->unsent = c->out.len;
    out->local = c->local;
    out->remote = c->remote;
    return GN_OK;
}

static void fail(GnConn *c, int err) {
    c->state = GN_S_FAILED;
    c->error = err;
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
}

static void service(GnBroker *b, GnConn *c, short rev) {
    if (c->kind == GN_K_LISTEN) {
        while (c->naccepted < 16) {
            struct sockaddr_storage ss;
            socklen_t sl = sizeof ss;
            int fd = accept(c->fd, (struct sockaddr *)&ss, &sl);
            if (fd < 0) break;
            int id;
            if (gn_policy_check(&b->policy, b->app, GN_IN, GN_TCP, (struct sockaddr *)&ss, NULL, NULL, 0) ||
                gn_broker_slot(b, GN_K_TCP, &id) || nonblock(fd)) {
                close(fd);
                continue;
            }
            GnConn *n = &b->c[id];
            n->fd = fd;
#ifdef SO_NOSIGPIPE
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &(int){1}, sizeof(int));
#endif
            n->state = GN_S_OPEN;
            n->remote = ss;
            n->remote_len = sl;
            socklen_t ll = sizeof n->local;
            getsockname(fd, (struct sockaddr *)&n->local, &ll);
            c->accepted[c->naccepted++] = id;
        }
        return;
    }
    if (c->state == GN_S_CONNECTING && (rev & (POLLOUT | POLLERR | POLLHUP))) {
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) { fail(c, GN_EIO); return; }
        c->state = GN_S_OPEN;
        socklen_t ll = sizeof c->local;
        getsockname(c->fd, (struct sockaddr *)&c->local, &ll);
    }
    if (c->state != GN_S_OPEN && c->state != GN_S_PEER_CLOSED) return;
    while (c->out.len) {
        size_t first = c->out.cap - c->out.head < c->out.len ? c->out.cap - c->out.head : c->out.len;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t k = send(c->fd, c->out.p + c->out.head, first, flags);
        if (k <= 0) {
            if (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK) fail(c, GN_ECLOSED);
            break;
        }
        c->out.head = (c->out.head + (size_t)k) % c->out.cap;
        c->out.len -= (size_t)k;
    }
    if (c->fd >= 0 && c->closing && !c->out.len) shutdown(c->fd, SHUT_WR);
    while (c->fd >= 0 && c->state == GN_S_OPEN && c->in.len < c->in.cap) {
        size_t tail = (c->in.head + c->in.len) % c->in.cap;
        size_t room = c->in.cap - c->in.len, run = c->in.cap - tail < room ? c->in.cap - tail : room;
        ssize_t k = recv(c->fd, c->in.p + tail, run, 0);
        if (k > 0) { c->in.len += (size_t)k; continue; }
        if (k == 0) c->state = GN_S_PEER_CLOSED;
        else if (errno != EAGAIN && errno != EWOULDBLOCK) fail(c, GN_ECLOSED);
        break;
    }
}

int gn_pump(GnBroker *b, int timeout_ms) {
    if (!b) return GN_EINVAL;
    /* The personal area network has no file descriptor: drive it directly. */
    if (b->pan) gn_pan_tick(b->pan);
    struct pollfd *pf = calloc(b->lim.max_conns, sizeof *pf);
    int *map = calloc(b->lim.max_conns, sizeof *map);
    if (!pf || !map) { free(pf); free(map); return GN_ENOMEM; }
    nfds_t n = 0;
    uint64_t now = now_ms();
    for (size_t i = 0; i < b->lim.max_conns; i++) {
        GnConn *c = &b->c[i];
        if (!c->kind || c->fd < 0 || c->kind == GN_K_UDP) continue;
        if (c->state == GN_S_CONNECTING && now >= c->deadline) { fail(c, GN_ETIMEOUT); continue; }
        short ev = 0;
        if (c->kind == GN_K_LISTEN) ev = POLLIN;
        else if (c->state == GN_S_CONNECTING) ev = POLLOUT;
        else {
            if (c->in.len < c->in.cap && c->state == GN_S_OPEN) ev |= POLLIN;
            if (c->out.len) ev |= POLLOUT;
        }
        if (!ev) continue;
        pf[n].fd = c->fd;
        pf[n].events = ev;
        map[n++] = (int)i;
    }
    int rc = poll(pf, n, n ? timeout_ms : 0);
    if (!n && timeout_ms > 0) {
        struct timespec ts = {timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L};
        nanosleep(&ts, NULL);
    }
    for (nfds_t i = 0; rc > 0 && i < n; i++)
        if (pf[i].revents) service(b, &b->c[map[i]], pf[i].revents);
    free(pf);
    free(map);
    return rc < 0 && errno != EINTR ? GN_EIO : GN_OK;
}
