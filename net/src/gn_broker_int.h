/* Internals of the socket broker: the connection slots and the helpers the
 * host socket and PAN halves share. */
#ifndef GN_BROKER_INT_H
#define GN_BROKER_INT_H

#include "gn_ring.h"
#include "os999_guestnet.h"

#include <stdint.h>
#include <sys/socket.h>

/* What a connection slot carries. */
enum { GN_K_TCP = 1, GN_K_LISTEN = 2, GN_K_UDP = 3, GN_K_PAN = 4, GN_K_PAN_LISTEN = 5 };

typedef struct {
    int fd, kind, state, error, closing;
    GnRing in, out;
    uint64_t deadline;
    struct sockaddr_storage local, remote;
    socklen_t remote_len;
    int accepted[16], naccepted; /* listener: ready connection ids */
    GnPanTcp *tcp;               /* the PAN endpoint for kinds 4 and 5 */
} GnConn;

struct GnBroker {
    GnPolicy policy;
    char app[32];
    GnLimits lim;
    GnLogFn log;
    void *log_ctx;
    GnConn *c;
    GnPan *pan; /* optional: a personal area network to carry guest TCP */
};

void gn_broker_log(GnBroker *b, const char *fmt, const char *what, const struct sockaddr *a, const char *why);
GnConn *gn_broker_get(GnBroker *b, int id);
int gn_broker_slot(GnBroker *b, int kind, int *id);
void gn_broker_conn_free(GnConn *c);

/* The PAN endpoints (gn_broker_pan.c). */
int gn_pan_conn_connect(GnBroker *b, uint32_t ip, uint16_t port, int *id);
int gn_pan_conn_listen(GnBroker *b, uint16_t port, int *id);
int gn_pan_conn_accept(GnBroker *b, int listener, int *id);
long gn_pan_conn_send(GnBroker *b, GnConn *c, const void *p, size_t n);
long gn_pan_conn_recv(GnBroker *b, GnConn *c, void *p, size_t n);
int gn_pan_conn_shutdown(GnBroker *b, GnConn *c);
int gn_pan_conn_abort(GnBroker *b, GnConn *c);
int gn_pan_conn_info(GnConn *c, GnInfo *out);
void gn_pan_conn_refresh(GnConn *c);

#endif
