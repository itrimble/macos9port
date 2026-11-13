/* International Utilities (Pack6): IUGetIntl for itl0 and itl1 (built at run time unless the program carries its own), IUDateString, IUTimeString and their P forms, and the refusals. */
#include "mac_runtime.h"
#include "mac_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#include "fork_builder.h"
#define SP 0x8000u
#define OUT 0x4000u
static uint8_t fork_bytes[FB_BYTES];
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { CHECK(!mac_cpu_write(r->cpu, p, n, v)); }
static MacRuntime rig(const FbFork *f) {
    MacRuntime r; memset(&r, 0, sizeof r); r.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(r.cpu);
    r.heap_next = 0x100000; r.heap_limit = 0x600000;
    if (f) { size_t n = fb_build(f, fork_bytes); CHECK(!mac_resource_open(&r.fork, fork_bytes, n, NULL, 0)); }
    return r;
}
static int pack6(MacRuntime *r, unsigned sel) { wr(r, SP, 2, sel); mac_cpu_set(r->cpu, MAC_A7, SP); return mac_intl_service(r, 0xa9ed); }
static uint32_t get_intl(MacRuntime *r, int id) { wr(r, SP + 2, 2, (uint16_t)id); wr(r, SP + 4, 4, 0xdead); if (pack6(r, 6) != 1) return 0; CHECK(mac_cpu_get(r->cpu, MAC_A7) == SP + 4); return rd(r, SP + 4, 4); }
static const char *pstr(MacRuntime *r, uint32_t at) { static char b[300]; unsigned n = rd(r, at, 1); for (unsigned i = 0; i < n; i++) b[i] = (char)rd(r, at + 1 + i, 1); b[n] = 0; return b; }
static const char *date(MacRuntime *r, uint32_t secs, int form, uint32_t h) {          /* IUDateString (h == 0) or IUDatePString */
    wr(r, OUT, 1, 0xee);
    if (h) { wr(r, SP + 2, 4, h); wr(r, SP + 6, 4, OUT); wr(r, SP + 10, 2, (uint16_t)form); wr(r, SP + 12, 4, secs); if (pack6(r, 0xe) != 1) return NULL; CHECK(mac_cpu_get(r->cpu, MAC_A7) == SP + 16); }
    else { wr(r, SP + 2, 4, OUT); wr(r, SP + 6, 2, (uint16_t)form); wr(r, SP + 8, 4, secs); if (pack6(r, 0) != 1) return NULL; CHECK(mac_cpu_get(r->cpu, MAC_A7) == SP + 12); }
    return pstr(r, OUT);
}
static const char *tim(MacRuntime *r, uint32_t secs, int seconds, uint32_t h) {
    if (h) { wr(r, SP + 2, 4, h); wr(r, SP + 6, 4, OUT); wr(r, SP + 10, 1, seconds); wr(r, SP + 11, 1, 0); wr(r, SP + 12, 4, secs); if (pack6(r, 0x10) != 1) return NULL; }
    else { wr(r, SP + 2, 4, OUT); wr(r, SP + 6, 1, seconds); wr(r, SP + 7, 1, 0); wr(r, SP + 8, 4, secs); if (pack6(r, 2) != 1) return NULL; }
    return pstr(r, OUT);
}
int main(void) {
    MacRuntime r = rig(NULL);
    /* the built-in resources */
    uint32_t h0 = get_intl(&r, 0), h1 = get_intl(&r, 1); CHECK(h0 && h1 && h0 != h1 && get_intl(&r, 0) == h0 && get_intl(&r, 1) == h1);          /* made once, handed out again */
    uint32_t p0 = rd(&r, h0, 4), p1 = rd(&r, h1, 4);
    CHECK(rd(&r, p0, 1) == '.' && rd(&r, p0 + 1, 1) == ',' && rd(&r, p0 + 2, 1) == ';' && rd(&r, p0 + 3, 1) == '$' && rd(&r, p0 + 7, 1) == 0 && rd(&r, p0 + 9, 1) == '/' && rd(&r, p0 + 10, 1) == 255 && rd(&r, p0 + 20, 1) == ':' && rd(&r, p0 + 29, 1) == 0);
    CHECK(!strcmp(pstr(&r, p1), "Sunday") && !strcmp(pstr(&r, p1 + 16 * 5), "Friday") && !strcmp(pstr(&r, p1 + 112), "January") && !strcmp(pstr(&r, p1 + 112 + 16 * 10), "November") && rd(&r, p1 + 307, 1) == 3);
    /* dates and times: 1904-01-02 (a Saturday) and 2026-11-13 (a Friday), 14:05:09 and 00:30 */
    CHECK(!strcmp(date(&r, 86400, 0, 0), "1/2/1904") && !strcmp(date(&r, 86400, 1, 0), "Saturday, January 2, 1904") && !strcmp(date(&r, 86400, 2, 0), "Sat, Jan 2, 1904"));
    CHECK(!strcmp(date(&r, 3877372800u, 0, 0), "11/13/2026") && !strcmp(date(&r, 3877372800u, 1, h1), "Friday, November 13, 2026") && !strcmp(date(&r, 3034627200u, 0, h0), "2/29/2000"));   /* the P form with an itl0 handle */
    CHECK(!strcmp(tim(&r, 3877372800u + 14 * 3600 + 5 * 60 + 9, 0, 0), "2:05 PM") && !strcmp(tim(&r, 3877372800u + 14 * 3600 + 5 * 60 + 9, 1, 0), "2:05:09 PM") && !strcmp(tim(&r, 3877372800u + 30 * 60, 0, 0), "12:30 AM") && !strcmp(tim(&r, 3877372800u + 12 * 3600, 0, h0), "12:00 PM"));
    /* itl0 changed in place (the handle is the program's to edit): day first, two-digit year, 24-hour clock */
    wr(&r, p0 + 7, 1, 1); wr(&r, p0 + 8, 1, 32 | 64); wr(&r, p0 + 9, 1, '.'); wr(&r, p0 + 10, 1, 0); wr(&r, p0 + 11, 1, 128 | 64);
    CHECK(!strcmp(date(&r, 3877372800u, 0, 0), "13.11.26") && !strcmp(date(&r, 86400, 0, 0), "02.01.04") && !strcmp(tim(&r, 3877372800u + 5 * 3600 + 7 * 60, 0, 0), "05:07"));
    /* refusals: an id with no resource, another selector, a date form that does not exist, a result pointer outside RAM, a handle that is not a resource */
    CHECK(get_intl(&r, 2) == 0 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "itl2")); mac_cpu_destroy(r.cpu);
    r = rig(NULL); CHECK(pack6(&r, 0xa) == -1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "$000A")); mac_cpu_destroy(r.cpu);
    r = rig(NULL); CHECK(date(&r, 0, 7, 0) == NULL && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
    r = rig(NULL); wr(&r, SP + 2, 4, 0xfffffff0u); wr(&r, SP + 6, 2, 0); wr(&r, SP + 8, 4, 0); CHECK(pack6(&r, 0) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
    r = rig(NULL); CHECK(date(&r, 0, 0, 0x1234) == NULL && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
    r = rig(NULL); CHECK(mac_intl_service(&r, 0xa9ee) == 0); mac_cpu_destroy(r.cpu);
    /* a program that carries its own itl0 and itl1 gets those handles instead */
    FbFork f; memset(&f, 0, sizeof f);
    uint8_t itl0[32] = {0}; itl0[0] = ','; itl0[1] = '.'; itl0[7] = 1; itl0[9] = '-'; itl0[10] = 0; itl0[11] = 128 | 64; itl0[20] = ':';
    uint8_t itl1[332] = {0}; itl1[0] = 3; memcpy(itl1 + 1, "Son", 3);
    fb_add(&f, 0x69746c30, 0, NULL, itl0, sizeof itl0); fb_add(&f, 0x69746c31, 0, NULL, itl1, sizeof itl1);
    r = rig(&f); uint32_t o0 = get_intl(&r, 0), o1 = get_intl(&r, 1);
    CHECK(o0 && o1 && rd(&r, rd(&r, o0, 4), 1) == ',' && !strcmp(pstr(&r, rd(&r, o1, 4)), "Son") && !r.intl_handle[0] && !r.intl_handle[1]);
    CHECK(!strcmp(date(&r, 86400, 0, 0), "2-1-04") && !strcmp(tim(&r, 3600, 0, 0), "01:00"));
    CHECK(date(&r, 86400, 1, 0) == NULL && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "built-in itl1"));
    mac_cpu_destroy(r.cpu);
    puts("International Utilities checks passed");
    return 0;
}
