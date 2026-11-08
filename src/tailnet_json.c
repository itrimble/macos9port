/* Bounded JSON reader for the documented Tailscale status subset. */
#include "tailnet.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t start, end; int next; char type; } Token;
typedef struct { const char *s; size_t n, p; int count; Token t[32768]; } Json;
static void space(Json *j) { while (j->p < j->n && strchr(" \t\r\n", j->s[j->p])) j->p++; }
static int value(Json *j, int depth) {
    space(j);
    if (depth > 32 || j->p >= j->n || j->count >= 32768) return -1;
    int id = j->count++; Token *t = &j->t[id];
    t->start = j->p; t->type = j->s[j->p++];
    if (t->type == '{' || t->type == '[') {
        char close = t->type == '{' ? '}' : ']'; space(j);
        if (j->p < j->n && j->s[j->p] != close) for (;;) {
            if (t->type == '{') {
                space(j);
                if (j->p >= j->n || j->s[j->p] != '"' || value(j, depth + 1) < 0) return -1;
                space(j); if (j->p >= j->n || j->s[j->p++] != ':') return -1;
            }
            if (value(j, depth + 1) < 0) return -1;
            space(j); if (j->p >= j->n) return -1;
            if (j->s[j->p] == close) break;
            if (j->s[j->p++] != ',') return -1;
        }
        if (j->p >= j->n || j->s[j->p++] != close) return -1;
    } else if (t->type == '"') {
        int closed = 0;
        while (j->p < j->n) {
            unsigned char c = j->s[j->p++];
            if (c == '"') { closed = 1; break; }
            if (c < 32) return -1;
            if (c == '\\') {
                if (j->p >= j->n) return -1;
                c = j->s[j->p++];
                if (c == 'u') {
                    for (int k = 0; k < 4; k++) if (j->p >= j->n || !isxdigit((unsigned char)j->s[j->p++])) return -1;
                } else if (!strchr("\"\\/bfnrt", c)) return -1;
            }
        }
        if (!closed) return -1;
    } else {
        j->p--;
        const char *literal = t->type == 't' ? "true" : t->type == 'f' ? "false" : t->type == 'n' ? "null" : NULL;
        if (literal) {
            size_t len = strlen(literal);
            if (j->n - j->p < len || memcmp(j->s + j->p, literal, len)) return -1;
            j->p += len;
        } else {
            if (j->s[j->p] == '-') j->p++;
            if (j->p >= j->n || !isdigit((unsigned char)j->s[j->p])) return -1;
            if (j->s[j->p++] != '0') while (j->p < j->n && isdigit((unsigned char)j->s[j->p])) j->p++;
            if (j->p < j->n && j->s[j->p] == '.') {
                j->p++; size_t start = j->p;
                while (j->p < j->n && isdigit((unsigned char)j->s[j->p])) j->p++;
                if (start == j->p) return -1;
            }
            if (j->p < j->n && strchr("eE", j->s[j->p])) {
                j->p++; if (j->p < j->n && strchr("+-", j->s[j->p])) j->p++;
                size_t start = j->p;
                while (j->p < j->n && isdigit((unsigned char)j->s[j->p])) j->p++;
                if (start == j->p) return -1;
            }
        }
    }
    t->end = j->p; t->next = j->count; return id;
}
static Json *parse(const char *s, size_t n) {
    if (!s || !n || n > TAILNET_JSON_MAX || memchr(s, 0, n)) return NULL;
    Json *j = calloc(1, sizeof *j); if (!j) return NULL;
    j->s = s; j->n = n;
    if (value(j, 0) != 0 || j->t[0].type != '{') { free(j); return NULL; }
    space(j); if (j->p != n) { free(j); return NULL; }
    return j;
}
static int field(Json *j, int obj, const char *key) {
    if (obj < 0 || j->t[obj].type != '{') return -1;
    for (int i = obj + 1; i < j->t[obj].next;) {
        Token *t = &j->t[i]; int v = i + 1;
        if (t->end - t->start == strlen(key) + 2 && !memcmp(j->s + t->start + 1, key, strlen(key))) return v;
        i = j->t[v].next;
    }
    return -1;
}
static int string(Json *j, int id, char *out, size_t size) {
    if (id < 0 || j->t[id].type != '"' || !size) return -1;
    Token *t = &j->t[id]; size_t w = 0;
    for (size_t p = t->start + 1; p + 1 < t->end; p++) {
        unsigned char c = j->s[p];
        if (c == '\\') {
            c = j->s[++p];
            if (c == 'u') { p += 4; c = '?'; }
            else if (c != '/' && c != '\\' && c != '"') c = '?';
        }
        /* Never let device metadata inject terminal escape/control sequences. */
        if (c < 32 || c > 126) c = '?';
        if (w + 1 >= size) return -1;
        out[w++] = (char)c;
    }
    out[w] = 0; return 0;
}
static int boolean(Json *j, int id) {
    if (id < 0) return -1;
    return j->t[id].type == 't' ? 1 : j->t[id].type == 'f' ? 0 : -1;
}
static int compare(const void *a, const void *b) {
    return strcmp(((const TailnetPeer *)a)->name, ((const TailnetPeer *)b)->name);
}
int tailnet_parse(const char *json, size_t length, Tailnet *net) {
    if (!net) return -1;
    memset(net, 0, sizeof *net); Json *j = parse(json, length); if (!j) return -1;
    if (string(j, field(j, 0, "BackendState"), net->state, sizeof net->state)) { free(j); return -1; }
    int self = field(j, 0, "Self"), peers = field(j, 0, "Peer");
    string(j, field(j, self, "HostName"), net->self, sizeof net->self);
    if (peers >= 0 && j->t[peers].type == '{') {
        for (int i = peers + 1; i < j->t[peers].next;) {
            int obj = i + 1; i = j->t[obj].next;
            TailnetPeer peer = {0};
            if (string(j, field(j, obj, "HostName"), peer.name, sizeof peer.name)) continue;
            string(j, field(j, obj, "OS"), peer.os, sizeof peer.os);
            peer.online = boolean(j, field(j, obj, "Online")) == 1;
            int ips = field(j, obj, "TailscaleIPs");
            if (ips < 0 || j->t[ips].type != '[') continue;
            for (int ip = ips + 1; ip < j->t[ips].next; ip = j->t[ip].next) {
                char address[64]; unsigned char bytes[16];
                if (string(j, ip, address, sizeof address)) continue;
                if (inet_pton(AF_INET, address, bytes) == 1 || inet_pton(AF_INET6, address, bytes) == 1) {
                    strcpy(peer.address, address); break;
                }
            }
            if (!peer.address[0]) continue;
            if (net->count >= TAILNET_MAX_PEERS) { free(j); return -1; }
            net->peers[net->count++] = peer;
        }
    } else if (peers >= 0 && j->t[peers].type != 'n') { free(j); return -1; }
    qsort(net->peers, (size_t)net->count, sizeof net->peers[0], compare);
    free(j); return 0;
}
int tailnet_prefs(const char *json, size_t length, int *dns, int *routes, int *shields) {
    Json *j = parse(json, length); if (!j || !dns || !routes || !shields) { free(j); return -1; }
    *dns = boolean(j, field(j, 0, "CorpDNS"));
    *routes = boolean(j, field(j, 0, "RouteAll"));
    *shields = boolean(j, field(j, 0, "ShieldsUp"));
    free(j); return *dns < 0 || *routes < 0 || *shields < 0 ? -1 : 0;
}
int tailnet_valid_user(const char *user) {
    if (!user || !*user || strlen(user) > 64 || user[0] == '-') return 0;
    for (const unsigned char *p = (const unsigned char *)user; *p; p++)
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.') return 0;
    return 1;
}
