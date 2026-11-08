#ifndef MODERN_NETWORK_H
#define MODERN_NETWORK_H
#include <stddef.h>
#include <stdatomic.h>

typedef enum {
    NET_OK, NET_POLICY, NET_TLS, NET_TIMEOUT, NET_CANCELLED,
    NET_LIMIT, NET_HTTP, NET_IO, NET_TRANSPORT
} NetStatus;

typedef struct {
    size_t max_bytes;
    long timeout_ms;
    /* NULL uses the host libcurl trust store. Explicit PEM supplies custom CAs.
       Never load trust or client keys from a guest volume or environment. */
    const char *ca_file;
    const char *client_cert;
    const char *client_key;
    const char *referer; /* Optional explicit HTTPS referrer; no automatic forwarding. */
    /* Called before every hop, including redirects. NULL denies all traffic. */
    int (*allow_url)(const char *url, void *context);
    void *context;
    const atomic_bool *cancel;
} NetPolicy;

typedef struct {
    NetStatus status;
    long http_status;
    size_t bytes;
    int transport_code;
} NetResult;

/* Call once at host startup, before worker threads. */
int net_init(void);
int net_https_url(const char *url);
const char *net_status_text(NetStatus status);
/* Blocking worker API; independent calls are safe after net_init. No ambient
   proxy, credentials, cookies, .curlrc, or guest trust. Atomic destination replace
   on success only; unsolicited partial (206) responses are rejected.
   Cancellation is checked after flushing, immediately before rename. Rename
   commits the download; cancellation racing with or following that commit
   cannot undo success. Destination directory must be owned by the trusted caller. */
NetResult net_download(const char *url, const char *destination, const NetPolicy *policy);
#endif
