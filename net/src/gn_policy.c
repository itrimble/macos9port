/* Guest network policy: deny by default, first matching rule wins, local
 * destinations need an explicit opt-in, guest traffic needs the legacy
 * (plaintext) profile for its program. */
#include "os999_guestnet.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *gn_status_text(int code) {
    switch (code) {
    case GN_OK: return "ok";
    case GN_EINVAL: return "invalid argument";
    case GN_EDENIED: return "denied by policy";
    case GN_ELIMIT: return "limit reached";
    case GN_EIO: return "network error";
    case GN_EAGAIN: return "not ready";
    case GN_ECLOSED: return "connection closed";
    case GN_ENOENT: return "not found";
    case GN_ETIMEOUT: return "timed out";
    case GN_ENOMEM: return "out of memory";
    default: return "unknown";
    }
}

void gn_policy_init(GnPolicy *p) { memset(p, 0, sizeof *p); }

static int parse_ports(const char *s, uint16_t *lo, uint16_t *hi) {
    if (!strcmp(s, "*")) { *lo = 0; *hi = 65535; return 0; }
    char *end;
    long a = strtol(s, &end, 10), b = a;
    if (*end == '-') b = strtol(end + 1, &end, 10);
    if (*end || a < 0 || b < a || b > 65535) return -1;
    *lo = (uint16_t)a;
    *hi = (uint16_t)b;
    return 0;
}

static int app_matches(const char *pat, const char *app) {
    return !pat[0] || !strcmp(pat, "*") || (app && !strcasecmp(pat, app));
}

int gn_policy_parse(GnPolicy *p, const char *text) {
    int line = 0;
    for (const char *s = text; s && *s;) {
        line++;
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char buf[512];
        if (len >= sizeof buf) return line;
        memcpy(buf, s, len);
        buf[len] = 0;
        s = e ? e + 1 : s + len;
        char *hash = strchr(buf, '#');
        if (hash) *hash = 0;
        char *tok[8];
        int n = 0;
        for (char *t = strtok(buf, " \t\r"); t && n < 8; t = strtok(NULL, " \t\r")) tok[n++] = t;
        if (!n) continue;
        char app[32] = "*";
        if (!strncmp(tok[n - 1], "app=", 4)) {
            if (strlen(tok[n - 1] + 4) >= sizeof app || !tok[n - 1][4]) return line;
            strcpy(app, tok[n - 1] + 4);
            n--;
        }
        if (!strcmp(tok[0], "legacy") || !strcmp(tok[0], "local") || !strcmp(tok[0], "lan-listen")) {
            if (n != 2 || (strcmp(tok[1], "on") && strcmp(tok[1], "off"))) return line;
            int on = !strcmp(tok[1], "on");
            if (!strcmp(tok[0], "local")) p->allow_local = on;
            else if (!strcmp(tok[0], "lan-listen")) p->allow_lan_listen = on;
            else if (on) {
                if (p->nlegacy == 16) return line;
                strcpy(p->legacy_apps[p->nlegacy++], app);
            }
            continue;
        }
        if (n != 5 || (strcmp(tok[0], "allow") && strcmp(tok[0], "deny")) || p->nrules == 64) return line;
        GnRule *r = &p->rules[p->nrules];
        memset(r, 0, sizeof *r);
        r->allow = !strcmp(tok[0], "allow");
        r->dir = !strcmp(tok[1], "out") ? GN_OUT : !strcmp(tok[1], "in") ? GN_IN : !strcmp(tok[1], "both") ? GN_OUT | GN_IN : 0;
        r->proto = !strcmp(tok[2], "tcp") ? GN_TCP : !strcmp(tok[2], "udp") ? GN_UDP : !strcmp(tok[2], "any") ? GN_TCP | GN_UDP : 0;
        if (!r->dir || !r->proto || strlen(tok[3]) >= sizeof r->host) return line;
        strcpy(r->host, tok[3]);
        for (char *c = r->host; *c; c++) *c = (char)tolower((unsigned char)*c);
        /* A port list "80,443" becomes one rule per entry. */
        char ports[64];
        if (strlen(tok[4]) >= sizeof ports) return line;
        strcpy(ports, tok[4]);
        GnRule base = *r;
        strcpy(base.app, app);
        int count = 0;
        for (char *q = strtok(ports, ","); q; q = strtok(NULL, ",")) {
            if (p->nrules == 64) return line;
            GnRule *d = &p->rules[p->nrules];
            *d = base;
            if (parse_ports(q, &d->port_lo, &d->port_hi)) return line;
            p->nrules++;
            count++;
        }
        if (!count) return line;
    }
    return 0;
}

static int addr_port(const struct sockaddr *a) {
    if (a->sa_family == AF_INET) return ntohs(((const struct sockaddr_in *)a)->sin_port);
    if (a->sa_family == AF_INET6) return ntohs(((const struct sockaddr_in6 *)a)->sin6_port);
    return -1;
}

int gn_addr_is_local(const struct sockaddr *a) {
    if (a->sa_family == AF_INET) {
        uint32_t ip = ntohl(((const struct sockaddr_in *)a)->sin_addr.s_addr);
        return (ip >> 24) == 0 || (ip >> 24) == 10 || (ip >> 24) == 127 || (ip >> 16) == 0xA9FE ||
               (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 22) == (100u << 2 | 1) ||
               (ip >> 28) == 0xE || ip == 0xFFFFFFFFu;
    }
    if (a->sa_family == AF_INET6) {
        const uint8_t *b = ((const struct sockaddr_in6 *)a)->sin6_addr.s6_addr;
        static const uint8_t zero[15];
        if (!memcmp(b, zero, 15) && b[15] <= 1) return 1;             /* :: and ::1 */
        if ((b[0] & 0xFE) == 0xFC || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) || b[0] == 0xFF) return 1;
        if (!memcmp(b, zero, 10) && b[10] == 0xFF && b[11] == 0xFF) { /* v4-mapped */
            struct sockaddr_in v4 = {0};
            v4.sin_family = AF_INET;
            memcpy(&v4.sin_addr, b + 12, 4);
            return gn_addr_is_local((const struct sockaddr *)&v4);
        }
        return 0;
    }
    return 1;
}

static int host_matches(const char *pat, const struct sockaddr *a, const char *host) {
    if (!strcmp(pat, "*")) return 1;
    char ip[INET6_ADDRSTRLEN] = "";
    const void *raw = a->sa_family == AF_INET ? (const void *)&((const struct sockaddr_in *)a)->sin_addr
                                               : (const void *)&((const struct sockaddr_in6 *)a)->sin6_addr;
    inet_ntop(a->sa_family, raw, ip, sizeof ip);
    const char *slash = strchr(pat, '/');
    if (slash) { /* CIDR */
        char net[64];
        snprintf(net, sizeof net, "%.*s", (int)(slash - pat), pat);
        int bits = atoi(slash + 1);
        uint8_t want[16], have[16];
        int fam = strchr(net, ':') ? AF_INET6 : AF_INET, len = fam == AF_INET ? 4 : 16;
        if (fam != a->sa_family || inet_pton(fam, net, want) != 1 || bits < 0 || bits > len * 8) return 0;
        memcpy(have, raw, (size_t)len);
        for (int i = 0; i < bits; i++)
            if (((want[i / 8] ^ have[i / 8]) >> (7 - i % 8)) & 1) return 0;
        return 1;
    }
    if (!strcasecmp(pat, ip)) return 1;
    if (!host || !*host) return 0;
    if (!strncmp(pat, "*.", 2)) {
        size_t hl = strlen(host), sl = strlen(pat + 1);
        return hl > sl && !strcasecmp(host + hl - sl, pat + 1);
    }
    return !strcasecmp(pat, host);
}

int gn_policy_check(const GnPolicy *p, const char *app, int dir, int proto, const struct sockaddr *addr,
                    const char *host, char *why, size_t why_cap) {
    char tmp[1];
    if (!why) { why = tmp; why_cap = sizeof tmp; }
    if (!p || !addr || (addr->sa_family != AF_INET && addr->sa_family != AF_INET6)) {
        snprintf(why, why_cap, "no usable address");
        return GN_EDENIED;
    }
    int legacy = 0;
    for (int i = 0; i < p->nlegacy; i++) legacy |= app_matches(p->legacy_apps[i], app);
    if (!legacy) {
        snprintf(why, why_cap, "unencrypted guest networking is off for %s", app ? app : "this program");
        return GN_EDENIED;
    }
    if (dir == GN_OUT && gn_addr_is_local(addr) && !p->allow_local) {
        snprintf(why, why_cap, "local and private destinations are blocked");
        return GN_EDENIED;
    }
    int port = addr_port(addr);
    for (int i = 0; i < p->nrules; i++) {
        const GnRule *r = &p->rules[i];
        if (!(r->dir & dir) || !(r->proto & proto) || !app_matches(r->app, app)) continue;
        if (port < r->port_lo || port > r->port_hi || !host_matches(r->host, addr, host)) continue;
        snprintf(why, why_cap, "rule %d %s", i + 1, r->allow ? "allows" : "denies");
        return r->allow ? GN_OK : GN_EDENIED;
    }
    snprintf(why, why_cap, "no rule allows it");
    return GN_EDENIED;
}
