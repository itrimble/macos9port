#ifndef GN_TEST_H
#define GN_TEST_H

#include "os999_guestnet.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define GN_TEST_NOSIG MSG_NOSIGNAL
#else
#define GN_TEST_NOSIG 0
#endif

static int gn_checks, gn_failures;
#define CHECK(c)                                                                                       \
    do {                                                                                               \
        gn_checks++;                                                                                   \
        if (!(c)) { gn_failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); }      \
    } while (0)

static inline int gn_done(const char *name) {
    printf("%s: %d checks, %d failures\n", name, gn_checks, gn_failures);
    return gn_failures != 0;
}

static inline uint64_t gn_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static inline void gn_sin(struct sockaddr_in *a, const char *ip, uint16_t port) {
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    inet_pton(AF_INET, ip, &a->sin_addr);
}

/* A loopback echo server on its own thread: echoes each connection until
 * EOF, then closes. mode 1 closes right after the first read (peer close). */
typedef struct { int fd, family, mode; uint16_t port; pthread_t t; } GnEcho;

static void *gn_echo_main(void *arg) {
    GnEcho *e = arg;
    for (;;) {
        int c = accept(e->fd, NULL, NULL);
        if (c < 0) return NULL;
        char buf[8192];
        ssize_t k;
        while ((k = read(c, buf, sizeof buf)) > 0) {
            ssize_t off = 0;
            while (off < k) { ssize_t w = send(c, buf + off, (size_t)(k - off), GN_TEST_NOSIG); if (w <= 0) break; off += w; }
            if (e->mode == 1) break;
        }
        close(c);
    }
}

static inline int gn_echo_start(GnEcho *e, int family, int mode) {
    memset(e, 0, sizeof *e);
    e->family = family;
    e->mode = mode;
    e->fd = socket(family, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(e->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    socklen_t len;
    if (family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        a->sin_family = AF_INET;
        a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        len = sizeof *a;
    } else {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        a->sin6_family = AF_INET6;
        a->sin6_addr = in6addr_loopback;
        len = sizeof *a;
    }
#ifdef SO_NOSIGPIPE
    setsockopt(e->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    if (bind(e->fd, (struct sockaddr *)&ss, len) || listen(e->fd, 8)) return -1;
    getsockname(e->fd, (struct sockaddr *)&ss, &len);
    e->port = ntohs(family == AF_INET ? ((struct sockaddr_in *)&ss)->sin_port : ((struct sockaddr_in6 *)&ss)->sin6_port);
    return pthread_create(&e->t, NULL, gn_echo_main, e);
}

static inline void gn_echo_stop(GnEcho *e) {
    shutdown(e->fd, SHUT_RDWR);
    close(e->fd);
    pthread_join(e->t, NULL);
}

static inline GnPolicy gn_open_policy(void) {
    GnPolicy p;
    gn_policy_init(&p);
    gn_policy_parse(&p, "legacy on\nlocal on\nallow both any * *\n");
    return p;
}

#endif
