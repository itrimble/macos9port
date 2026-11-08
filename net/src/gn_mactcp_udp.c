/* MacTCP UDP streams and the DNR StrToAddr service. UDPiopb fields
 * (public layout): create rcvBuff @32 len @36 notify @40 localPort @44
 * user @46; send remoteHost @34 remotePort @38 wds @40; receive timeOut
 * @32 remoteHost @34 remotePort @38 rcvBuff @40 rcvBuffLen @44. */
#include "gn_mactcp_int.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

static int udp_read_once(GnMacTcp *m, GnMem *mem, GnStream *s, uint32_t pb) {
    if (s->lent) return GN_EAGAIN; /* previous datagram not returned yet */
    struct sockaddr_storage from;
    uint32_t cap = s->rcv_len > 65535 ? 65535 : s->rcv_len;
    long k = gn_udp_recvfrom(m->b, s->conn, mem->ram + s->rcv, cap, &from);
    if (k == GN_ELIMIT) return MT_INVALID_LENGTH; /* datagram larger than the receive area */
    if (k < 0) return (int)k;
    const struct sockaddr_in *a = (const struct sockaddr_in *)&from;
    gm_wr(mem, pb + 34, 4, from.ss_family == AF_INET ? ntohl(a->sin_addr.s_addr) : 0);
    gm_wr(mem, pb + 38, 2, from.ss_family == AF_INET ? ntohs(a->sin_port) : 0);
    gm_wr(mem, pb + 40, 4, s->rcv);
    gm_wr(mem, pb + 44, 2, (uint32_t)k);
    s->lent = k ? (int)k : 1;
    return 0;
}

int gn_mt_udp_progress(GnMacTcp *m, GnMem *mem, GnOp *op, uint64_t now, int *result) {
    GnStream *s = &m->s[op->stream];
    if (!s->used) { *result = MT_INVALID_STREAM; return 0; }
    int rc = udp_read_once(m, mem, s, op->pb);
    if (rc == GN_EAGAIN) {
        if (op->deadline && now >= op->deadline) { *result = MT_COMMAND_TIMEOUT; return 0; }
        return 1;
    }
    *result = rc < 0 && rc > -100 ? MT_CONNECTION_TERMINATED : rc;
    return 0;
}

int gn_mt_udp(GnMacTcp *m, GnMem *mem, uint32_t pb, int code, uint64_t now, int *result) {
    int idx = -1;
    if (code == GN_UDP_CREATE) {
        uint32_t rcv = gm_rd(mem, pb + 32, 4), len = gm_rd(mem, pb + 36, 4);
        uint16_t port = (uint16_t)gm_rd(mem, pb + 44, 2);
        if (!rcv || !gm_ok(mem, rcv, len)) { *result = MT_INVALID_BUF_PTR; return 0; }
        if (len < 2048) { *result = MT_INVALID_LENGTH; return 0; }
        if ((*result = gn_mt_new_stream(m, 1, rcv, len, gm_rd(mem, pb + 40, 4), gm_rd(mem, pb + 46, 4), &idx))) return 0;
        GnStream *s = &m->s[idx];
        if (gn_udp_open(m->b, AF_INET, port, &s->conn)) {
            s->used = 0;
            *result = MT_DUPLICATE_SOCKET;
            return 0;
        }
        GnInfo in;
        gn_info(m->b, s->conn, &in);
        s->local_port = ntohs(((struct sockaddr_in *)&in.local)->sin_port);
        gm_wr(mem, pb + 44, 2, s->local_port);
        gm_wr(mem, pb + 28, 4, GN_STREAM_BASE + (uint32_t)idx * 16);
        *result = 0;
        return 0;
    }
    GnStream *s = gn_mt_stream(m, gm_rd(mem, pb + 28, 4), &idx);
    if (!s || !s->udp) { *result = MT_INVALID_STREAM; return 0; }
    switch (code) {
    case GN_UDP_RELEASE:
        gn_release(m->b, s->conn);
        gm_wr(mem, pb + 32, 4, s->rcv);
        gm_wr(mem, pb + 36, 4, s->rcv_len);
        for (int i = 0; i < GN_OPS; i++)
            if (m->ops[i].used && m->ops[i].stream == idx) {
                gm_wr(mem, m->ops[i].pb + 16, 2, (uint32_t)(uint16_t)(int16_t)MT_CONNECTION_TERMINATED);
                if (m->ops[i].async) gn_mt_queue(m, 1, gm_rd(mem, m->ops[i].pb + 12, 4), m->ops[i].pb, 0, 0, 0, 0);
                m->ops[i].used = 0;
            }
        s->used = 0;
        *result = 0;
        return 0;
    case GN_UDP_WRITE: {
        uint8_t buf[65535];
        size_t len;
        if ((*result = gn_mt_gather(mem, gm_rd(mem, pb + 40, 4), buf, sizeof buf, &len))) return 0;
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(gm_rd(mem, pb + 34, 4));
        a.sin_port = htons((uint16_t)gm_rd(mem, pb + 38, 2));
        long k = gn_udp_sendto(m->b, s->conn, (struct sockaddr *)&a, sizeof a, buf, len);
        *result = k == GN_EDENIED ? MT_OPEN_FAILED : k == GN_ELIMIT ? MT_INVALID_LENGTH : k < 0 ? MT_IP_DEST_DEAD : 0;
        return 0;
    }
    case GN_UDP_BFR_RETURN:
        s->lent = 0;
        *result = 0;
        return 0;
    case GN_UDP_MTU:
        gm_wr(mem, pb + 32, 2, 1472);
        *result = 0;
        return 0;
    case GN_UDP_READ: {
        int rc = udp_read_once(m, mem, s, pb);
        if (rc != GN_EAGAIN) { *result = rc < 0 && rc > -100 ? MT_CONNECTION_TERMINATED : rc; return 0; }
        uint32_t t = gm_rd(mem, pb + 32, 2);
        for (int i = 0; i < GN_OPS; i++) {
            if (m->ops[i].used) continue;
            m->ops[i] = (GnOp){1, 0, pb, code, idx, t ? now + (uint64_t)t * 1000 : 0, 0};
            return 1;
        }
        *result = MT_INSUFFICIENT_RESOURCES;
        return 0;
    }
    default:
        *result = MT_INVALID_CSCODE;
        return 0;
    }
}

static int dotted(const char *s, uint32_t *ip) {
    unsigned a, b, c, d;
    char extra;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return -1;
    *ip = a << 24 | b << 16 | c << 8 | d;
    return 0;
}

int gn_mactcp_str_to_addr(GnMacTcp *m, const char *name, uint32_t *addr, int wait_ms) {
    if (!m || !name || !addr) return MT_PARAM_ERR;
    if (!dotted(name, addr)) return 0;
    for (int i = 0; i < GN_CACHE; i++)
        if (m->cache[i].name[0] && !strcasecmp(m->cache[i].name, name)) { *addr = m->cache[i].ip; return 0; }
    int id;
    int rc = gn_resolve_start(m->r, name, AF_INET, &id);
    if (rc == GN_EINVAL) return MT_NAME_SYNTAX;
    if (rc) return MT_NO_ANSWER;
    struct sockaddr_storage out[4];
    int n = 0;
    for (int waited = 0;; waited += 10) {
        rc = gn_resolve_poll(m->r, id, out, 4, &n);
        if (rc != GN_EAGAIN) break;
        if (waited >= wait_ms) { gn_resolve_cancel(m->r, id); return MT_NO_ANSWER; }
        struct timespec ts = {0, 10 * 1000000L};
        nanosleep(&ts, NULL);
    }
    if (rc || !n) return MT_AUTH_NAME;
    *addr = ntohl(((struct sockaddr_in *)&out[0])->sin_addr.s_addr);
    /* Remember the name so host-name policy rules match the later connect. */
    snprintf(m->cache[m->cache_next].name, sizeof m->cache[0].name, "%s", name);
    m->cache[m->cache_next].ip = *addr;
    m->cache_next = (m->cache_next + 1) % GN_CACHE;
    return 0;
}
