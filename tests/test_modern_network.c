#include "modern_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
/* Test-only syscall substitution: cancel exactly during final disk flushing,
   without a timing-sensitive thread or a hook in production code. */
static atomic_bool *cancel_on_flush;
static int test_fsync(int fd) {
    int result = fsync(fd);
    if (cancel_on_flush) atomic_store(cancel_on_flush, 1);
    return result;
}
#define fsync test_fsync
#include "../src/modern_network.c"
#undef fsync
static void *cancel_later(void *value) {
    struct timespec delay = {0, 100000000};
    nanosleep(&delay, NULL);
    atomic_store((atomic_bool *)value, 1);
    return NULL;
}
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
static int allowed(const char *url, void *context) {
    return !context || !strstr(url, (const char *)context);
}
int main(int argc, char **argv) {
    CHECK(net_init() == 0);
    if (argc >= 6) {
        atomic_bool cancel = argc > 7 && !strcmp(argv[7], "cancel");
        NetPolicy p = {.max_bytes = strtoul(argv[4], NULL, 10), .timeout_ms = strtol(argv[5], NULL, 10),
            .ca_file = strcmp(argv[3], "-") ? argv[3] : NULL, .allow_url = allowed,
            .context = argc > 6 && strcmp(argv[6], "-") ? argv[6] : NULL, .cancel = &cancel};
        if (argc > 7 && !strcmp(argv[7], "flush")) cancel_on_flush = &cancel;
        if (argc > 9) { p.client_cert = argv[8]; p.client_key = argv[9]; }
        pthread_t thread;
        int delayed = argc > 7 && !strcmp(argv[7], "later");
        if (delayed && pthread_create(&thread, NULL, cancel_later, &cancel)) return 99;
        NetResult r = net_download(argv[1], argv[2], &p);
        if (delayed) pthread_join(thread, NULL);
        printf("%d %ld %zu %d\n", r.status, r.http_status, r.bytes, r.transport_code);
        return r.status;
    }
    const char *bad[] = {NULL, "", "http://example.com", "https://", "https://a@b/", "https://a:b@c/",
        "https://example.com/\n", "https://example.com\\@evil.test", "file:///tmp/x", "https://:443/"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!net_https_url(bad[i]));
    CHECK(net_https_url("https://example.com/path?q=1"));
    CHECK(net_https_url("https://[::1]:443/"));
    CHECK(net_download("https://example.com", "/tmp/unused", NULL).status == NET_POLICY);
    NetPolicy p = {.max_bytes = 1024, .timeout_ms = 100};
    CHECK(net_download("https://example.com", "/tmp/unused", &p).status == NET_POLICY);
    p.allow_url = allowed; p.client_cert = "identity.pem";
    CHECK(net_download("https://example.com", "/tmp/unused", &p).status == NET_POLICY);
    p.client_cert = NULL;
    p.referer = "http://example.com";
    CHECK(net_download("https://example.com", "/tmp/unused", &p).status == NET_POLICY);
    p.referer = NULL;
    CHECK(net_download("https://example.com", "/no/such/os999/directory/file", &p).status == NET_IO);
    if (!fails) puts("modern network policy ok");
    return fails ? 1 : 0;
}
