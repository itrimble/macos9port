/* OS999 guest networking (checklist N01-N08).
 *
 * Classic programs never touch host sockets directly. Their MacTCP calls
 * (parameter blocks sent to the ".IPP" driver) are translated by
 * gn_mactcp_* into broker operations; the broker owns the host sockets,
 * applies the policy before any connect, listen or datagram, keeps every
 * buffer bounded, and never blocks. Guest traffic is plaintext by nature
 * (MacTCP has no TLS), so it is off unless the policy turns on the legacy
 * profile for that program, and every connection reports itself as
 * unencrypted. Diagnostics carry metadata only and pass through the
 * redactor. C11 + POSIX sockets + pthreads; no SDL. */
#ifndef OS999_GUESTNET_H
#define OS999_GUESTNET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

enum { GN_OK = 0, GN_EINVAL = -1, GN_EDENIED = -2, GN_ELIMIT = -3, GN_EIO = -4, GN_EAGAIN = -5,
       GN_ECLOSED = -6, GN_ENOENT = -7, GN_ETIMEOUT = -8, GN_ENOMEM = -9 };
const char *gn_status_text(int code);

/* The guest IP stack over a link that carries Ethernet frames. */
typedef struct GnPan GnPan;
typedef struct GnPanTcp GnPanTcp;

/* ---- policy (N06, N08) ---- */
enum { GN_OUT = 1, GN_IN = 2 };
enum { GN_TCP = 1, GN_UDP = 2 };
typedef struct {
    int allow;           /* 1 allow, 0 deny */
    int dir, proto;      /* bit sets of GN_OUT|GN_IN, GN_TCP|GN_UDP */
    char app[32];        /* "*" or a program name / creator code */
    char host[128];      /* "*", "name", "*.suffix", dotted IPv4, IPv6, or CIDR */
    uint16_t port_lo, port_hi;
} GnRule;

typedef struct {
    GnRule rules[64];
    int nrules;
    char legacy_apps[16][32]; /* programs allowed plaintext guest traffic ("*" = all) */
    int nlegacy;
    int allow_local;          /* permit loopback/private/link-local destinations */
    int allow_lan_listen;     /* listeners bind to all interfaces instead of loopback */
} GnPolicy;

void gn_policy_init(GnPolicy *p); /* deny everything */
/* Lines: "allow out tcp *.example.org 80,443 app=Fetch", "deny out any 10.0.0.0/8 *",
 * "legacy on app=Fetch", "local on", "lan-listen on", "# comment".
 * Returns 0, or the 1-based line number of the first bad line. */
int gn_policy_parse(GnPolicy *p, const char *text);
/* Decide one flow. host may be NULL (address only). why receives a reason. */
int gn_policy_check(const GnPolicy *p, const char *app, int dir, int proto, const struct sockaddr *addr,
                    const char *host, char *why, size_t why_cap);
/* 1 for loopback, private, link-local, CGNAT, ULA, multicast and unspecified. */
int gn_addr_is_local(const struct sockaddr *addr);

/* ---- diagnostics (N08) ---- */
/* Mask credentials in text bound for logs: PASS/USER commands, Authorization
 * and Cookie headers, password= and token= parameters, URL userinfo. */
void gn_redact(const char *in, char *out, size_t cap);
typedef void (*GnLogFn)(void *ctx, const char *line);

/* ---- broker (N01, N02) ---- */
typedef struct GnBroker GnBroker;
typedef struct {
    int kind;            /* 0 free, 1 tcp, 2 listener, 3 udp */
    int state;           /* GN_S_* */
    int error;           /* GN_* for failed or aborted connections */
    int plaintext;       /* always 1 for guest flows (disclosure, N07) */
    size_t unread, unsent;
    struct sockaddr_storage local, remote;
} GnInfo;
enum { GN_S_CONNECTING = 1, GN_S_LISTENING, GN_S_OPEN, GN_S_PEER_CLOSED, GN_S_CLOSED, GN_S_FAILED };

typedef struct {
    size_t max_conns;    /* default 64, MacTCP's stream limit */
    size_t send_cap;     /* bytes buffered per connection, default 64 KiB */
    size_t recv_cap;     /* default 64 KiB */
    unsigned connect_timeout_ms; /* default 30 s */
} GnLimits;

GnBroker *gn_broker_new(const GnPolicy *policy, const char *app, const GnLimits *lim, GnLogFn log, void *log_ctx);
void gn_broker_free(GnBroker *b);
int gn_tcp_connect(GnBroker *b, const struct sockaddr *addr, socklen_t len, const char *host, int *id);
int gn_tcp_listen(GnBroker *b, int family, uint16_t port, int *id);
int gn_tcp_accept(GnBroker *b, int listener, int *id); /* GN_EAGAIN when none waiting */
long gn_send(GnBroker *b, int id, const void *p, size_t n);   /* bytes queued, or <0 */
long gn_recv(GnBroker *b, int id, void *p, size_t n);         /* 0 = nothing yet; GN_ECLOSED at EOF */
int gn_shutdown(GnBroker *b, int id); /* graceful: FIN after queued data drains */
int gn_abort(GnBroker *b, int id);    /* reset now */
int gn_release(GnBroker *b, int id);  /* free the slot */
int gn_udp_open(GnBroker *b, int family, uint16_t port, int *id);
long gn_udp_sendto(GnBroker *b, int id, const struct sockaddr *addr, socklen_t len, const void *p, size_t n);
long gn_udp_recvfrom(GnBroker *b, int id, void *p, size_t n, struct sockaddr_storage *from);
int gn_info(GnBroker *b, int id, GnInfo *out);
/* Move bytes and complete connects/accepts; waits at most timeout_ms. */
int gn_pump(GnBroker *b, int timeout_ms);
/* Attach a PAN so TCP to its subnet travels over that link instead of a host
 * socket. The broker does not own the PAN. */
int gn_broker_set_pan(GnBroker *b, GnPan *pan);
uint32_t gn_broker_local_ip(GnBroker *b);   /* the PAN address, 0 without one */
uint32_t gn_broker_netmask(GnBroker *b);   /* the PAN mask, 0 without one */

/* ---- PAN: IP and TCP over a link that carries Ethernet frames ----
 *
 * A Bluetooth personal area network presents an Ethernet-like link, so the
 * guest's own TCP can run over it and keep its address. net/ owns the
 * protocol side (ARP, IPv4, TCP); the transport supplies the frames. */
typedef struct {
    /* Hand one Ethernet frame to the link. 0 on success, negative on error. */
    int (*tx)(void *ctx, const void *frame, size_t len);
    void *ctx;
} GnPanLink;

GnPan *gn_pan_new(const GnPanLink *link, uint32_t local_ip, uint32_t netmask, const uint8_t mac[6]);
void gn_pan_free(GnPan *p);
/* Name the peer so its frames can be addressed without waiting for ARP. */
int gn_pan_set_peer(GnPan *p, uint32_t peer_ip, const uint8_t mac[6]);
int gn_pan_owns(GnPan *p, uint32_t ip);      /* ip is on our subnet */
uint32_t gn_pan_local_ip(GnPan *p);
uint32_t gn_pan_netmask(GnPan *p);
/* Feed one inbound Ethernet frame. */
int gn_pan_rx(GnPan *p, const void *frame, size_t len);
/* Drive retransmissions and expiry. */
int gn_pan_tick(GnPan *p);

/* A TCP endpoint over the PAN. */
GnPanTcp *gn_pan_tcp_new(GnPan *p);
void gn_pan_tcp_free(GnPanTcp *t);
int gn_pan_tcp_connect(GnPanTcp *t, uint32_t peer_ip, uint16_t peer_port, uint16_t local_port);
int gn_pan_tcp_listen(GnPanTcp *t, uint16_t local_port);
int gn_pan_tcp_accept(GnPanTcp *t, GnPanTcp **out); /* GN_EAGAIN when none */
long gn_pan_tcp_send(GnPanTcp *t, const void *p, size_t n);  /* bytes queued */
long gn_pan_tcp_recv(GnPanTcp *t, void *p, size_t n);        /* 0 = nothing yet */
int gn_pan_tcp_state(GnPanTcp *t);
int gn_pan_tcp_shutdown(GnPanTcp *t);
int gn_pan_tcp_abort(GnPanTcp *t);
uint16_t gn_pan_tcp_local_port(GnPanTcp *t);

/* ---- resolver (N02) ---- */
typedef struct GnResolver GnResolver;
GnResolver *gn_resolver_new(void);
void gn_resolver_free(GnResolver *r); /* cancels pending lookups */
int gn_resolve_start(GnResolver *r, const char *name, int family, int *id);
/* GN_EAGAIN while pending; GN_OK with up to cap addresses; GN_ENOENT if none. */
int gn_resolve_poll(GnResolver *r, int id, struct sockaddr_storage *addrs, int cap, int *count);
void gn_resolve_cancel(GnResolver *r, int id);

/* ---- MacTCP adapter (N04) ---- */
typedef struct { uint8_t *ram; uint32_t size; } GnMem; /* guest RAM, big-endian */
typedef struct GnMacTcp GnMacTcp;
/* A callback the runtime must run on the guest CPU. */
typedef struct {
    int kind;            /* 1 = ioCompletion(pb) in A0, 2 = notify (ASR) */
    uint32_t proc, pb;
    uint32_t stream, user_data;
    uint16_t event, reason;
} GnGuestCall;

GnMacTcp *gn_mactcp_new(GnBroker *b, GnResolver *r, uint32_t our_ip);
void gn_mactcp_free(GnMacTcp *m);
/* Handle one _Control call to .IPP. Writes ioResult (1 = in progress for
 * async calls). Sync calls that must wait return 1; the caller then
 * repeats gn_mactcp_poll until the result is no longer 1. */
int gn_mactcp_control(GnMacTcp *m, GnMem *mem, uint32_t pb, int async, uint64_t now_ms);
/* Progress pending calls; returns how many guest calls were queued. */
int gn_mactcp_poll(GnMacTcp *m, GnMem *mem, uint64_t now_ms, GnGuestCall *calls, int cap);
/* MacTCP DNR StrToAddr for dotted or host names (host names resolve via the broker's resolver). */
int gn_mactcp_str_to_addr(GnMacTcp *m, const char *name, uint32_t *addr, int wait_ms);

/* MacTCP command codes and errors (MacTCP Programmer's Guide, public interface). */
enum { GN_IPCTL_GETADDR = 15,
       GN_UDP_CREATE = 20, GN_UDP_READ, GN_UDP_BFR_RETURN, GN_UDP_WRITE, GN_UDP_RELEASE, GN_UDP_MTU,
       GN_TCP_CREATE = 30, GN_TCP_PASSIVE_OPEN, GN_TCP_ACTIVE_OPEN, GN_TCP_SEND = 34, GN_TCP_NOCOPY_RCV,
       GN_TCP_RCV_BFR_RETURN, GN_TCP_RCV, GN_TCP_CLOSE, GN_TCP_ABORT, GN_TCP_STATUS, GN_TCP_EXT_STAT,
       GN_TCP_RELEASE, GN_TCP_GLOBAL_INFO };
enum { MT_CONNECTION_CLOSING = -23005, MT_INVALID_LENGTH = -23006, MT_CONNECTION_EXISTS = -23007,
       MT_CONNECTION_DOESNT_EXIST = -23008, MT_INSUFFICIENT_RESOURCES = -23009, MT_INVALID_STREAM = -23010,
       MT_STREAM_ALREADY_OPEN = -23011, MT_CONNECTION_TERMINATED = -23012, MT_INVALID_BUF_PTR = -23013,
       MT_INVALID_RDS = -23014, MT_INVALID_WDS = -23014, MT_OPEN_FAILED = -23015, MT_COMMAND_TIMEOUT = -23016,
       MT_DUPLICATE_SOCKET = -23017, MT_IP_DEST_DEAD = -23033, MT_NAME_SYNTAX = -23041,
       MT_AUTH_NAME = -23045, MT_NO_ANSWER = -23046, MT_PARAM_ERR = -50, MT_INVALID_CSCODE = -17 };

#endif
