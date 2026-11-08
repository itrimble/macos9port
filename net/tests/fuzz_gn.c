/* Deterministic mutation fuzzing: policy text, redaction, and arbitrary
 * MacTCP parameter blocks (any csCode, any pointers, async or not) against
 * guest RAM. Run under ASan/UBSan; a watchdog catches hangs. */
#include "gn_test.h"
#include "gn_mactcp_int.h"

#include <signal.h>

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }

static const char *const seeds[] = {
    "legacy on app=Fetch\nallow out tcp *.example.org 21,80,443 app=Fetch\n",
    "deny out any 10.0.0.0/8 *\nallow both udp 2001:db8::/32 53,123\nlocal on\nlan-listen on\n",
    "# c\nallow in tcp 192.168.1.0/24 8000-8100\nlegacy on\n",
    "PASS hunter2\r\nAuthorization: Basic xyz\r\nGET /?password=1&token=2 ftp://u:p@h/",
};

static void mutate(char *buf, size_t cap) {
    size_t n = strlen(buf);
    int ops = 1 + (int)(rnd() % 8);
    while (ops--) {
        uint32_t k = rnd() % 4, at = n ? rnd() % (uint32_t)n : 0;
        if (k == 0 && n) buf[at] = (char)(rnd() & 0xFF ? rnd() & 0x7F : 0);
        else if (k == 1 && n + 1 < cap) { memmove(buf + at + 1, buf + at, n - at + 1); buf[at] = " \n*,-/:=."[rnd() % 9]; n++; }
        else if (k == 2 && n) { memmove(buf + at, buf + at + 1, n - at); n--; }
        else if (k == 3 && n * 2 < cap) { memcpy(buf + n, buf, n); n *= 2; buf[n] = 0; }
        n = strlen(buf);
    }
}

static void fuzz_text(int iters) {
    char buf[4096], out[512];
    for (int i = 0; i < iters; i++) {
        snprintf(buf, sizeof buf, "%s", seeds[rnd() % 4]);
        mutate(buf, sizeof buf);
        GnPolicy p;
        gn_policy_init(&p);
        int line = gn_policy_parse(&p, buf);
        CHECK(line >= 0 && p.nrules <= 64);
        struct sockaddr_in a;
        gn_sin(&a, "93.184.216.34", (uint16_t)rnd());
        char why[64];
        int d = gn_policy_check(&p, "Fetch", 1 + (int)(rnd() % 2), 1 + (int)(rnd() % 2), (struct sockaddr *)&a,
                                rnd() & 1 ? "www.example.org" : NULL, why, sizeof why);
        CHECK(d == GN_OK || d == GN_EDENIED);
        gn_redact(buf, out, 1 + rnd() % sizeof out);
    }
}

enum { RAM = 1 << 16 };
static uint8_t ram[RAM];

static void fuzz_pb(int iters, uint16_t echo_port) {
    GnPolicy p;
    gn_policy_init(&p);
    gn_policy_parse(&p, "legacy on\nlocal on\nallow both any 127.0.0.1 *\n");
    GnBroker *b = gn_broker_new(&p, "Fuzz", NULL, NULL, NULL);
    GnMacTcp *m = gn_mactcp_new(b, NULL, 0x0A000202u);
    GnMem mem = {ram, RAM};
    uint32_t streams[8] = {0};
    GnGuestCall calls[32];
    for (int i = 0; i < iters; i++) {
        uint32_t pb = (rnd() % 4 == 0) ? rnd() : 0x100 + (rnd() % 64) * 0x40;
        if (!gm_ok(&mem, pb, 64)) { /* out-of-range PBs must be refused, not touched */
            CHECK(gn_mactcp_control(m, &mem, pb, 0, gn_now()) == MT_PARAM_ERR);
            continue;
        }
        for (int k = 0; k < 64; k++) ram[pb + (uint32_t)k] = (uint8_t)rnd();
        int code = rnd() % 3 ? (int)(15 + rnd() % 30) : (int)(int16_t)rnd();
        gm_wr(&mem, pb + 26, 2, (uint32_t)code);
        uint32_t sp = streams[rnd() % 8];
        if (rnd() % 4) gm_wr(&mem, pb + 28, 4, sp ? sp : GN_STREAM_BASE + (rnd() % 70) * 16);
        if (code == GN_TCP_CREATE || code == GN_UDP_CREATE) { /* mostly-valid buffers so streams exist */
            gm_wr(&mem, pb + 32, 4, 0x4000 + (rnd() % 8) * 0x1000);
            gm_wr(&mem, pb + 36, 4, rnd() % 8 ? 4096 : rnd());
            gm_wr(&mem, pb + 40, 4, rnd() % 2 ? 0x80 : 0);
        }
        if (code == GN_TCP_ACTIVE_OPEN) {
            gm_wr(&mem, pb + 36, 4, 0x7F000001u);
            if (rnd() % 2) gm_wr(&mem, pb + 40, 2, echo_port);
        }
        if (code == GN_UDP_WRITE) gm_wr(&mem, pb + 34, 4, 0x7F000001u);
        if (code == GN_TCP_SEND || code == GN_UDP_WRITE) { /* a valid WDS half the time */
            uint32_t wds = 0x3000 + (rnd() % 16) * 16;
            gm_wr(&mem, wds, 2, rnd() % 600); gm_wr(&mem, wds + 2, 4, 0x8000 + rnd() % 0x4000); gm_wr(&mem, wds + 6, 2, 0);
            if (rnd() % 2) gm_wr(&mem, pb + (code == GN_TCP_SEND ? 38 : 40), 4, wds);
        }
        if (code == GN_TCP_RCV && rnd() % 2) { gm_wr(&mem, pb + 36, 4, 0xC000); gm_wr(&mem, pb + 40, 2, 1 + rnd() % 2048); }
        if (code == GN_TCP_NOCOPY_RCV && rnd() % 2) { gm_wr(&mem, pb + 42, 4, 0x3800); gm_wr(&mem, pb + 46, 2, 1 + rnd() % 6); }
        gm_wr(&mem, pb + 32, 1, rnd() % 2); /* short timeouts keep pending calls brief */
        gn_mactcp_control(m, &mem, pb, (int)(rnd() & 1), gn_now());
        if ((code == GN_TCP_CREATE || code == GN_UDP_CREATE) && !(int16_t)gm_rd(&mem, pb + 16, 2))
            streams[rnd() % 8] = gm_rd(&mem, pb + 28, 4);
        gn_mactcp_poll(m, &mem, gn_now() + (rnd() % 3) * 2000, calls, 32);
    }
    gn_mactcp_free(m);
    gn_broker_free(b);
}

static void on_alarm(int sig) { (void)sig; fprintf(stderr, "fuzz_gn: watchdog timeout\n"); _exit(3); }

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 20000;
    signal(SIGALRM, on_alarm);
    signal(SIGPIPE, SIG_IGN);
    alarm(300);
    fuzz_text(iters);
    GnEcho e;
    gn_echo_start(&e, AF_INET, 0);
    fuzz_pb(iters / 4, e.port);
    gn_echo_stop(&e);
    return gn_done("fuzz_gn");
}
