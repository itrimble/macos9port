/* Policy parsing and decisions, local-address classification, redaction. */
#include "gn_test.h"

static int decide(const GnPolicy *p, const char *app, int dir, int proto, const char *ip, uint16_t port, const char *host) {
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    if (strchr(ip, ':')) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(port);
        inet_pton(AF_INET6, ip, &a->sin6_addr);
    } else {
        gn_sin((struct sockaddr_in *)&ss, ip, port);
    }
    return gn_policy_check(p, app, dir, proto, (struct sockaddr *)&ss, host, NULL, 0);
}

static void test_defaults(void) {
    GnPolicy p;
    gn_policy_init(&p);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "93.184.216.34", 80, NULL) == GN_EDENIED);
    /* A rule alone is not enough: guest traffic is plaintext and needs the legacy profile. */
    CHECK(gn_policy_parse(&p, "allow out tcp * 80") == 0);
    char why[160];
    struct sockaddr_in a;
    gn_sin(&a, "93.184.216.34", 80);
    CHECK(gn_policy_check(&p, "Fetch", GN_OUT, GN_TCP, (struct sockaddr *)&a, NULL, why, sizeof why) == GN_EDENIED);
    CHECK(strstr(why, "unencrypted") != NULL);
    CHECK(gn_policy_parse(&p, "legacy on app=Fetch") == 0);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "93.184.216.34", 80, NULL) == GN_OK);
    CHECK(decide(&p, "Other", GN_OUT, GN_TCP, "93.184.216.34", 80, NULL) == GN_EDENIED);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "93.184.216.34", 81, NULL) == GN_EDENIED);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_UDP, "93.184.216.34", 80, NULL) == GN_EDENIED);
    CHECK(decide(&p, "Fetch", GN_IN, GN_TCP, "93.184.216.34", 80, NULL) == GN_EDENIED);
}

static void test_rules(void) {
    GnPolicy p;
    gn_policy_init(&p);
    const char *text =
        "# Fetch may reach one FTP host and the web\n"
        "legacy on app=Fetch\n"
        "deny out tcp 203.0.113.0/24 * app=Fetch\n"
        "allow out tcp ftp.example.org 21,20 app=Fetch\n"
        "allow out tcp *.example.com 80,443,8000-8080 app=Fetch\n"
        "allow out udp 2001:db8::/32 53\n";
    CHECK(gn_policy_parse(&p, text) == 0);
    CHECK(p.nrules == 7);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "198.51.100.7", 21, "ftp.example.org") == GN_OK);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "198.51.100.7", 21, "evil.example.org") == GN_EDENIED);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "198.51.100.8", 8042, "www.example.com") == GN_OK);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "198.51.100.8", 80, "example.com") == GN_EDENIED); /* *.x needs a label */
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "198.51.100.8", 80, "badexample.com") == GN_EDENIED);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_TCP, "203.0.113.9", 80, "www.example.com") == GN_EDENIED); /* first match */
    CHECK(decide(&p, "Fetch", GN_OUT, GN_UDP, "2001:db8::1", 53, NULL) == GN_OK);
    CHECK(decide(&p, "Fetch", GN_OUT, GN_UDP, "2001:db9::1", 53, NULL) == GN_EDENIED);
    const char *bad[] = {"allow sideways tcp * 80", "allow out icmp * 80", "allow out tcp * 99999",
                         "allow out tcp * 80-20", "legacy maybe", "allow out tcp *", "frobnicate on",
                         "allow out tcp * 80 app="};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        GnPolicy q;
        gn_policy_init(&q);
        char doc[200];
        snprintf(doc, sizeof doc, "legacy on\n%s\n", bad[i]);
        CHECK(gn_policy_parse(&q, doc) == 2);
    }
}

static void test_local(void) {
    const char *local[] = {"127.0.0.1", "10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.1", "169.254.9.9",
                           "100.64.0.1", "0.0.0.0", "224.0.0.1", "255.255.255.255", "::1", "::", "fe80::1",
                           "fd00::5", "ff02::1", "::ffff:192.168.0.1"};
    const char *global[] = {"8.8.8.8", "172.32.0.1", "100.128.0.1", "93.184.216.34", "2606:4700::1111",
                            "::ffff:8.8.8.8"};
    for (size_t i = 0; i < sizeof local / sizeof *local; i++) {
        struct sockaddr_storage ss;
        memset(&ss, 0, sizeof ss);
        if (strchr(local[i], ':')) { ss.ss_family = AF_INET6; inet_pton(AF_INET6, local[i], &((struct sockaddr_in6 *)&ss)->sin6_addr); }
        else gn_sin((struct sockaddr_in *)&ss, local[i], 1);
        CHECK(gn_addr_is_local((struct sockaddr *)&ss) == 1);
    }
    for (size_t i = 0; i < sizeof global / sizeof *global; i++) {
        struct sockaddr_storage ss;
        memset(&ss, 0, sizeof ss);
        if (strchr(global[i], ':')) { ss.ss_family = AF_INET6; inet_pton(AF_INET6, global[i], &((struct sockaddr_in6 *)&ss)->sin6_addr); }
        else gn_sin((struct sockaddr_in *)&ss, global[i], 1);
        CHECK(gn_addr_is_local((struct sockaddr *)&ss) == 0);
    }
    GnPolicy p;
    gn_policy_init(&p);
    CHECK(gn_policy_parse(&p, "legacy on\nallow out any * *\n") == 0);
    CHECK(decide(&p, "X", GN_OUT, GN_TCP, "192.168.1.1", 80, NULL) == GN_EDENIED); /* LAN/host stay off */
    CHECK(decide(&p, "X", GN_OUT, GN_TCP, "127.0.0.1", 80, NULL) == GN_EDENIED);
    CHECK(gn_policy_parse(&p, "local on") == 0);
    CHECK(decide(&p, "X", GN_OUT, GN_TCP, "127.0.0.1", 80, NULL) == GN_OK);
}

static void test_redact(void) {
    struct { const char *in, *must, *never; } cases[] = {
        {"PASS hunter2\r\nRETR file", "PASS [redacted]", "hunter2"},
        {"USER ian\r\nPASS s3cret", "USER ian", "s3cret"},
        {"GET /?q=1&password=abc123&x=2", "password=[redacted]&x=2", "abc123"},
        {"Authorization: Basic aWFuOnB3", "Authorization: [redacted]", "aWFuOnB3"},
        {"Cookie: session=xyz", "Cookie: [redacted]", "xyz"},
        {"ftp://ian:pw@ftp.example.org/pub", "://[redacted]@ftp.example.org", "ian:pw"},
        {"token=AAA;next", "token=[redacted];next", "AAA"},
        {"compass=north", "compass=north", "[redacted]"}, /* 'pass=' inside a word is not a key */
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char out[200];
        gn_redact(cases[i].in, out, sizeof out);
        CHECK(strstr(out, cases[i].must) != NULL);
        CHECK(strstr(out, cases[i].never) == NULL);
    }
    char tiny[8];
    gn_redact("PASS secretpassword", tiny, sizeof tiny);
    CHECK(strlen(tiny) == 7 && !strstr(tiny, "secret"));
}

int main(void) {
    test_defaults();
    test_rules();
    test_local();
    test_redact();
    return gn_done("test_gn_policy");
}
