#include "modern_network.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
static Os999NetConfig g_net_cfg = {0, 1, 1, 1, 20};
static int init_result = -1;

void net_set_config(const Os999NetConfig *cfg) {
    if (!cfg) return;
    pthread_mutex_lock(&cfg_lock);
    g_net_cfg.connect_via = (cfg->connect_via >= 0 && cfg->connect_via <= 2) ? cfg->connect_via : 0;
    g_net_cfg.https_only = 1;
    g_net_cfg.allow_redirects = !!cfg->allow_redirects;
    g_net_cfg.guest_isolated = !!cfg->guest_isolated;
    g_net_cfg.timeout_sec = (cfg->timeout_sec >= 5 && cfg->timeout_sec <= 300) ? cfg->timeout_sec : 20;
    pthread_mutex_unlock(&cfg_lock);
}

void net_get_config(Os999NetConfig *out) {
    if (!out) return;
    pthread_mutex_lock(&cfg_lock);
    *out = g_net_cfg;
    pthread_mutex_unlock(&cfg_lock);
}
static void initialize(void) {
    init_result = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}
int net_init(void) {
    return pthread_once(&init_once, initialize) == 0 ? init_result : -1;
}

int net_https_url(const char *url) {
    if (!url || strlen(url) > 8192 || strncmp(url, "https://", 8)) return 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return 0;
    CURLU *u = curl_url();
    if (!u) return 0;
    char *host = NULL, *user = NULL, *password = NULL;
    int ok = curl_url_set(u, CURLUPART_URL, url, 0) == CURLUE_OK &&
             curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK && host && *host;
    if (curl_url_get(u, CURLUPART_USER, &user, 0) == CURLUE_OK ||
        curl_url_get(u, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK) ok = 0;
    curl_free(host); curl_free(user); curl_free(password); curl_url_cleanup(u);
    return ok;
}

const char *net_status_text(NetStatus s) {
    static const char *messages[] = {"Secure download complete.", "Network policy denied this request.",
        "Certificate or TLS verification failed.", "Network request timed out.",
        "Network request cancelled.", "Download exceeds the size limit.",
        "Server returned an unsuccessful HTTP response.", "Could not save the download.",
        "Network connection failed."};
    return (unsigned)s < sizeof messages / sizeof *messages ? messages[s] : "Network request failed.";
}

typedef struct {
    FILE *file;
    const NetPolicy *policy;
    NetResult result;
} Transfer;

static size_t write_body(char *data, size_t size, size_t count, void *context) {
    Transfer *t = context;
    if (size && count > SIZE_MAX / size) { t->result.status = NET_LIMIT; return 0; }
    size_t n = size * count;
    if (n > t->policy->max_bytes - t->result.bytes) { t->result.status = NET_LIMIT; return 0; }
    if (fwrite(data, 1, n, t->file) != n) { t->result.status = NET_IO; return 0; }
    t->result.bytes += n;
    return n;
}

static int progress(void *context, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)c; (void)d;
    Transfer *t = context;
    if (t->policy->progress) t->policy->progress(t->policy->progress_ctx, b > 0 ? (uint64_t)b : 0, a > 0 ? (uint64_t)a : 0);
    if (t->policy->cancel && atomic_load(t->policy->cancel)) {
        t->result.status = NET_CANCELLED;
        return 1;
    }
    return 0;
}

static long long monotonic_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

NetResult net_download(const char *url, const char *dest, const NetPolicy *p) {
    Transfer t = {.policy = p, .result = {.status = NET_POLICY}};
    Os999NetConfig cfg;
    net_get_config(&cfg);
    if (cfg.connect_via == 2 || !p || !dest || !*dest || strlen(dest) > 4096 || !net_https_url(url) ||
        !p->allow_url || !p->allow_url(url, p->context) || !p->max_bytes || p->max_bytes > 1000000000 ||
        p->timeout_ms < 1 || p->timeout_ms > 600000 ||
        (p->referer && !net_https_url(p->referer)) ||
        (!!p->client_cert != !!p->client_key)) return t.result;
    if (p->cancel && atomic_load(p->cancel)) { t.result.status = NET_CANCELLED; return t.result; }
    if (net_init() != 0) { t.result.status = NET_TRANSPORT; return t.result; }
    char *temp = malloc(strlen(dest) + 16), *current = strdup(url);
    CURL *curl = curl_easy_init();
    t.result.status = NET_IO;
    if (!temp || !current || !curl) goto done;
    snprintf(temp, strlen(dest) + 16, "%s.net-XXXXXX", dest);
    int fd = mkstemp(temp);
    if (fd < 0) goto done;
    t.file = fdopen(fd, "w+b");
    if (!t.file) { close(fd); unlink(temp); goto done; }
    int max_hops = cfg.allow_redirects ? 5 : 0;
    long long deadline = monotonic_ms() + p->timeout_ms;
    for (int hop = 0; hop <= max_hops; hop++) {
        if (!net_https_url(current) || !p->allow_url(current, p->context)) {
            t.result.status = NET_POLICY; break;
        }
        long long remaining = deadline - monotonic_ms();
        if (remaining <= 0) { t.result.status = NET_TIMEOUT; break; }
        t.result.status = NET_OK;
        curl_easy_reset(curl);
#define OPT(key, value) do { if (curl_easy_setopt(curl, key, value) != CURLE_OK) { \
    t.result.status = NET_TRANSPORT; goto finish; } } while (0)
        OPT(CURLOPT_URL, current);
        OPT(CURLOPT_PROTOCOLS_STR, "https");
        OPT(CURLOPT_REDIR_PROTOCOLS_STR, "https");
        OPT(CURLOPT_FOLLOWLOCATION, 0L);
        OPT(CURLOPT_SSL_VERIFYPEER, 1L);
        OPT(CURLOPT_SSL_VERIFYHOST, 2L);
        OPT(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
        OPT(CURLOPT_PROXY, "");
        OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
        OPT(CURLOPT_NOSIGNAL, 1L);
        OPT(CURLOPT_TIMEOUT_MS, (long)remaining);
        OPT(CURLOPT_CONNECTTIMEOUT_MS, (long)(remaining < 10000 ? remaining : 10000));
        OPT(CURLOPT_WRITEFUNCTION, write_body);
        OPT(CURLOPT_WRITEDATA, &t);
        OPT(CURLOPT_NOPROGRESS, 0L);
        OPT(CURLOPT_XFERINFOFUNCTION, progress);
        OPT(CURLOPT_XFERINFODATA, &t);
        OPT(CURLOPT_USERAGENT, "OS999/1.1");
        if (p->referer) { OPT(CURLOPT_REFERER, p->referer); }
        if (p->ca_file) { OPT(CURLOPT_CAINFO, p->ca_file); }
#if defined(__APPLE__) && LIBCURL_VERSION_NUM >= 0x080f00
        else { OPT(CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA); }
#endif
        if (p->client_cert) {
            /* Redirects with client identities are denied below. */
            OPT(CURLOPT_SSLCERT, p->client_cert);
            OPT(CURLOPT_SSLKEY, p->client_key);
        }
        CURLcode code = curl_easy_perform(curl);
        t.result.transport_code = code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &t.result.http_status);
        if (code != CURLE_OK) {
            if (t.result.status == NET_OK) {
                if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
                    code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_CERTPROBLEM)
                    t.result.status = NET_TLS;
                else if (code == CURLE_OPERATION_TIMEDOUT) t.result.status = NET_TIMEOUT;
                else t.result.status = NET_TRANSPORT;
            }
            break;
        }
        long status = t.result.http_status;
        /* This API requests complete files, never byte ranges. */
        if (status >= 200 && status < 300 && status != 206) break;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            char *redirect = NULL;
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect);
            if (!redirect || hop == max_hops || p->client_cert) { t.result.status = NET_POLICY; break; }
            char *next = strdup(redirect);
            if (!next) { t.result.status = NET_IO; break; }
            free(current); current = next;
            if (fflush(t.file) || ftruncate(fileno(t.file), 0) || fseek(t.file, 0, SEEK_SET)) {
                t.result.status = NET_IO; break;
            }
            /* bytes counts all hops to bound total received data. */
        } else { t.result.status = NET_HTTP; break; }
    }
finish:
    if (t.result.status == NET_OK && p->cancel && atomic_load(p->cancel)) t.result.status = NET_CANCELLED;
    if (fflush(t.file) || fsync(fileno(t.file))) t.result.status = NET_IO;
    if (fclose(t.file)) t.result.status = NET_IO;
    t.file = NULL;
    if (t.result.status == NET_OK && p->cancel && atomic_load(p->cancel)) t.result.status = NET_CANCELLED;
    if (t.result.status == NET_OK && rename(temp, dest)) t.result.status = NET_IO;
    unlink(temp);
done:
    if (curl) curl_easy_cleanup(curl);
    free(temp); free(current);
    return t.result;
}
