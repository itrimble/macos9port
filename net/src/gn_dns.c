/* Asynchronous name resolution: one detached worker thread per lookup
 * (at most GN_DNS_JOBS at once) calls the host's getaddrinfo. A job is
 * shared by the resolver and its thread through a reference count, so
 * cancelling or freeing the resolver never waits on a slow lookup. */
#include "os999_guestnet.h"

#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum { GN_DNS_JOBS = 16, GN_DNS_ADDRS = 8 };

typedef struct {
    pthread_mutex_t mu;
    int refs, done, cancelled, status, count, family;
    char name[256];
    struct sockaddr_storage addrs[GN_DNS_ADDRS];
} Job;

struct GnResolver {
    Job *jobs[GN_DNS_JOBS];
};

static void job_unref(Job *j) {
    pthread_mutex_lock(&j->mu);
    int left = --j->refs;
    pthread_mutex_unlock(&j->mu);
    if (!left) {
        pthread_mutex_destroy(&j->mu);
        free(j);
    }
}

static void *worker(void *arg) {
    Job *j = arg;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = j->family;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(j->name, NULL, &hints, &res);
    pthread_mutex_lock(&j->mu);
    if (!rc) {
        for (struct addrinfo *a = res; a && j->count < GN_DNS_ADDRS; a = a->ai_next) {
            if (a->ai_family != AF_INET && a->ai_family != AF_INET6) continue;
            if (a->ai_addrlen > sizeof j->addrs[0]) continue;
            memcpy(&j->addrs[j->count++], a->ai_addr, a->ai_addrlen);
        }
    }
    j->status = rc || !j->count ? GN_ENOENT : GN_OK;
    j->done = 1;
    pthread_mutex_unlock(&j->mu);
    if (res) freeaddrinfo(res);
    job_unref(j);
    return NULL;
}

GnResolver *gn_resolver_new(void) { return calloc(1, sizeof(GnResolver)); }

void gn_resolver_free(GnResolver *r) {
    if (!r) return;
    for (int i = 0; i < GN_DNS_JOBS; i++) if (r->jobs[i]) gn_resolve_cancel(r, i);
    free(r);
}

static int valid_name(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 253) return 0;
    for (const char *c = s; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
              *c == '-' || *c == '.' || *c == ':' || *c == '_'))
            return 0;
    return 1;
}

int gn_resolve_start(GnResolver *r, const char *name, int family, int *id) {
    if (!r || !name || !id || !valid_name(name)) return GN_EINVAL;
    int slot = -1;
    for (int i = 0; i < GN_DNS_JOBS; i++) if (!r->jobs[i]) { slot = i; break; }
    if (slot < 0) return GN_ELIMIT;
    Job *j = calloc(1, sizeof *j);
    if (!j) return GN_ENOMEM;
    pthread_mutex_init(&j->mu, NULL);
    j->refs = 2;
    j->family = family;
    strcpy(j->name, name);
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&t, &at, worker, j);
    pthread_attr_destroy(&at);
    if (rc) {
        pthread_mutex_destroy(&j->mu);
        free(j);
        return GN_EIO;
    }
    r->jobs[slot] = j;
    *id = slot;
    return GN_OK;
}

int gn_resolve_poll(GnResolver *r, int id, struct sockaddr_storage *addrs, int cap, int *count) {
    if (!r || id < 0 || id >= GN_DNS_JOBS || !r->jobs[id]) return GN_EINVAL;
    Job *j = r->jobs[id];
    pthread_mutex_lock(&j->mu);
    int done = j->done, status = j->status, n = j->count < cap ? j->count : cap;
    if (done) memcpy(addrs, j->addrs, (size_t)(n > 0 ? n : 0) * sizeof *addrs);
    pthread_mutex_unlock(&j->mu);
    if (!done) return GN_EAGAIN;
    if (count) *count = n;
    r->jobs[id] = NULL;
    job_unref(j);
    return status;
}

void gn_resolve_cancel(GnResolver *r, int id) {
    if (!r || id < 0 || id >= GN_DNS_JOBS || !r->jobs[id]) return;
    Job *j = r->jobs[id];
    pthread_mutex_lock(&j->mu);
    j->cancelled = 1;
    pthread_mutex_unlock(&j->mu);
    r->jobs[id] = NULL;
    job_unref(j);
}
