#ifndef GN_MACTCP_INT_H
#define GN_MACTCP_INT_H

#include "os999_guestnet.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

enum { GN_STREAMS = 64, GN_OPS = 128, GN_CACHE = 32 };
#define GN_STREAM_BASE 0x4E540000u /* opaque StreamPtr values handed to the guest */

typedef struct {
    int used, udp;
    uint32_t rcv, rcv_len;       /* guest receive area */
    uint32_t notify, user;       /* ASR and its userDataPtr */
    int conn, listener;          /* broker ids, -1 when none */
    int closing;                 /* TCPClose issued */
    int lent;                    /* bytes of rcv area lent through RDS / UDPRead */
    int note_data, note_close, note_term;
    uint32_t remote_ip;
    uint16_t remote_port, local_port;
    uint32_t filter_ip;          /* passive open: accept only from this host (0 = any) */
    uint16_t filter_port;
} GnStream;

typedef struct {
    int used, async;
    uint32_t pb;
    int code, stream;
    uint64_t deadline;           /* 0 = none */
    uint32_t sent;               /* TCPSend progress */
} GnOp;

struct GnMacTcp {
    GnBroker *b;
    GnResolver *r;
    uint32_t our_ip;
    GnStream s[GN_STREAMS];
    GnOp ops[GN_OPS];
    struct { char name[128]; uint32_t ip; } cache[GN_CACHE];
    int cache_next;
    GnGuestCall q[256];          /* guest calls waiting for gn_mactcp_poll */
    int nq;
};

static inline int gm_ok(const GnMem *m, uint32_t a, uint32_t n) { return a && a <= m->size && n <= m->size - a; }
static inline uint32_t gm_rd(const GnMem *m, uint32_t a, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | m->ram[a + (uint32_t)i];
    return v;
}
static inline void gm_wr(GnMem *m, uint32_t a, int n, uint32_t v) {
    for (int i = n - 1; i >= 0; i--) { m->ram[a + (uint32_t)i] = (uint8_t)v; v >>= 8; }
}

/* Shared helpers (gn_mactcp.c). */
GnStream *gn_mt_stream(GnMacTcp *m, uint32_t ptr, int *index);
int gn_mt_new_stream(GnMacTcp *m, int udp, uint32_t rcv, uint32_t len, uint32_t notify, uint32_t user, int *index);
void gn_mt_queue(GnMacTcp *m, int kind, uint32_t proc, uint32_t pb, uint32_t stream, uint32_t user, uint16_t ev, uint16_t reason);
int gn_mt_gather(GnMem *mem, uint32_t wds, uint8_t *out, size_t cap, size_t *len);
const char *gn_mt_host_for(GnMacTcp *m, uint32_t ip);
/* UDP (gn_mactcp_udp.c): handle a code; returns 1 if it stays pending. */
int gn_mt_udp(GnMacTcp *m, GnMem *mem, uint32_t pb, int code, uint64_t now, int *result);
int gn_mt_udp_progress(GnMacTcp *m, GnMem *mem, GnOp *op, uint64_t now, int *result);

#endif
