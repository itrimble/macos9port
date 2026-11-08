#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

/* Apple Event Manager handler-table subset ($A816): selectors vs declared argument sizes, table semantics,
 * interaction level, and fail-closed behaviour. Nothing here delivers an event: handlers are only recorded. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x8000u
#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
enum { NOT_FOUND = -1717, MEM_FULL = -108, PARAM = -50 };
#define AEVT FOURCC('a', 'e', 'v', 't')
#define OAPP FOURCC('o', 'a', 'p', 'p')
#define QUIT FOURCC('q', 'u', 'i', 't')
#define WILD FOURCC('*', '*', '*', '*')
#define VAR_H 0x6000u
#define VAR_R 0x6010u

static MacRuntime *make(void) {
    static MacRuntime rt;
    memset(&rt, 0, sizeof rt);
    rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL);
    return &rt;
}

/* Declared argument sizes in bytes from the MPW interface text; the high byte of each selector must be their sum / 2. */
static const struct { const char *name; uint32_t selector; unsigned sizes[6]; } table[] = {
    {"AEInstallEventHandler",   0x091F, {4, 4, 4, 4, 2, 0}},
    {"AERemoveEventHandler",    0x0720, {4, 4, 4, 2, 0, 0}},
    {"AEGetEventHandler",       0x0921, {4, 4, 4, 4, 2, 0}},
    {"AEGetInteractionAllowed", 0x021D, {4, 0, 0, 0, 0, 0}},
    {"AESetInteractionAllowed", 0x011E, {2, 0, 0, 0, 0, 0}},
};

/* Run a selector with its argument words already written at SP; the result slot above them is preset to 0xBEEF. */
static int run(MacRuntime *rt, uint32_t selector) {
    unsigned words = (selector >> 8) & 0xff;
    mac_cpu_write(rt->cpu, SP + words * 2, 2, 0xbeef);
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    mac_cpu_set(rt->cpu, MAC_D0, selector | 0xabcd0000u);               /* only the low word selects */
    return mac_appleevents_service(rt, 0xa816);
}
static int16_t res(MacRuntime *rt, unsigned words) { return (int16_t)mac_cpu_read(rt->cpu, SP + words * 2, 2); }
static void put_bool(MacRuntime *rt, int v) { mac_cpu_write(rt->cpu, SP, 2, v ? 0x0100 : 0x0000); }

static int16_t install(MacRuntime *rt, uint32_t cls, uint32_t id, uint32_t handler, uint32_t refcon, int sys) {
    put_bool(rt, sys);
    mac_cpu_write(rt->cpu, SP + 2, 4, refcon); mac_cpu_write(rt->cpu, SP + 6, 4, handler);
    mac_cpu_write(rt->cpu, SP + 10, 4, id); mac_cpu_write(rt->cpu, SP + 14, 4, cls);
    CHECK(run(rt, 0x091F) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 18);
    return res(rt, 9);
}
static int16_t remove_h(MacRuntime *rt, uint32_t cls, uint32_t id, uint32_t handler, int sys) {
    put_bool(rt, sys);
    mac_cpu_write(rt->cpu, SP + 2, 4, handler); mac_cpu_write(rt->cpu, SP + 6, 4, id); mac_cpu_write(rt->cpu, SP + 10, 4, cls);
    CHECK(run(rt, 0x0720) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 14);
    return res(rt, 7);
}
static int16_t get_h(MacRuntime *rt, uint32_t cls, uint32_t id, int sys, uint32_t *handler, uint32_t *refcon) {
    mac_cpu_write(rt->cpu, VAR_H, 4, 0x11111111); mac_cpu_write(rt->cpu, VAR_R, 4, 0x22222222);
    put_bool(rt, sys);
    mac_cpu_write(rt->cpu, SP + 2, 4, VAR_R); mac_cpu_write(rt->cpu, SP + 6, 4, VAR_H);
    mac_cpu_write(rt->cpu, SP + 10, 4, id); mac_cpu_write(rt->cpu, SP + 14, 4, cls);
    CHECK(run(rt, 0x0921) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 18);
    *handler = mac_cpu_read(rt->cpu, VAR_H, 4); *refcon = mac_cpu_read(rt->cpu, VAR_R, 4);
    return res(rt, 9);
}
static int16_t set_level(MacRuntime *rt, uint16_t word) {
    mac_cpu_write(rt->cpu, SP, 2, word);
    CHECK(run(rt, 0x011E) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 2);
    return res(rt, 1);
}
static int get_level(MacRuntime *rt, uint8_t *level) {
    mac_cpu_write(rt->cpu, VAR_H, 4, 0x99999999);
    mac_cpu_write(rt->cpu, SP, 4, VAR_H);
    CHECK(run(rt, 0x021D) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 4);
    *level = (uint8_t)mac_cpu_read(rt->cpu, VAR_H, 1);
    CHECK(mac_cpu_read(rt->cpu, VAR_H + 1, 1) == 0x99 && mac_cpu_read(rt->cpu, VAR_H + 2, 2) == 0x9999);                /* exactly one byte is written */
    return res(rt, 2);
}

static void selectors(void) {
    for (unsigned i = 0; i < sizeof table / sizeof *table; i++) {
        unsigned bytes = 0;
        for (unsigned k = 0; k < 6; k++) bytes += table[i].sizes[k];
        CHECK(bytes % 2 == 0 && (table[i].selector >> 8) == bytes / 2);        /* high byte = argument WORDS */
        MacRuntime *rt = make();
        uint8_t *ram = mac_cpu_ram(rt->cpu);
        for (unsigned k = 0; k < 64; k++) ram[SP + k] = 0;
        if (table[i].selector == 0x021D) mac_cpu_write(rt->cpu, SP, 4, VAR_H);   /* valid VARs for the VAR routines */
        else { mac_cpu_write(rt->cpu, SP + 2, 4, VAR_R); mac_cpu_write(rt->cpu, SP + 6, 4, VAR_H); }
        CHECK(run(rt, table[i].selector) == 1);
        CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP + bytes);                      /* exactly the declared bytes popped */
        CHECK(!mac_cpu_halted(rt->cpu));
        mac_cpu_destroy(rt->cpu);
    }
}

static void tables(void) {
    MacRuntime *rt = make();
    uint32_t h, r;
    mac_cpu_set(rt->cpu, MAC_A0, 0x1234);
    CHECK(install(rt, AEVT, OAPP, 0x4000, 7, 0) == 0);
    CHECK(mac_cpu_get(rt->cpu, MAC_A0) == 0x1234);                              /* registers other than A7 kept */
    CHECK(get_h(rt, AEVT, OAPP, 0, &h, &r) == 0 && h == 0x4000 && r == 7);
    CHECK(install(rt, AEVT, OAPP, 0x5000, 9, 0) == 0);                          /* same key replaces */
    CHECK(get_h(rt, AEVT, OAPP, 0, &h, &r) == 0 && h == 0x5000 && r == 9);
    CHECK(rt->appleevents.app_count == 1 && rt->appleevents.sys_count == 0);
    CHECK(get_h(rt, AEVT, OAPP, 1, &h, &r) == NOT_FOUND && h == 0x11111111 && r == 0x22222222);   /* sys table separate; VARs untouched */
    CHECK(install(rt, AEVT, OAPP, 0x6000, 1, 1) == 0);
    CHECK(get_h(rt, AEVT, OAPP, 1, &h, &r) == 0 && h == 0x6000 && r == 1);
    CHECK(get_h(rt, AEVT, OAPP, 0, &h, &r) == 0 && h == 0x5000);                  /* app entry unchanged */
    CHECK(rt->appleevents.app_count == 1 && rt->appleevents.sys_count == 1);
    CHECK(install(rt, AEVT, QUIT, 0x7000, 2, 0) == 0);
    CHECK(get_h(rt, AEVT, 0x6f646f63, 0, &h, &r) == NOT_FOUND);                   /* different id: missing */
    CHECK(get_h(rt, 0x61657677, QUIT, 0, &h, &r) == NOT_FOUND);                   /* different class: missing */
    CHECK(remove_h(rt, AEVT, OAPP, 0, 1) == 0);                                   /* remove from sys only */
    CHECK(get_h(rt, AEVT, OAPP, 1, &h, &r) == NOT_FOUND && get_h(rt, AEVT, OAPP, 0, &h, &r) == 0);
    CHECK(remove_h(rt, AEVT, OAPP, 0, 1) == NOT_FOUND);                           /* already gone */
    CHECK(remove_h(rt, AEVT, OAPP, 0x5000, 0) == 0);
    CHECK(get_h(rt, AEVT, OAPP, 0, &h, &r) == NOT_FOUND);
    CHECK(get_h(rt, AEVT, QUIT, 0, &h, &r) == 0 && h == 0x7000 && r == 2);         /* the other entry survived the swap-remove */
    CHECK(remove_h(rt, AEVT, OAPP, 0, 0) == NOT_FOUND);
    CHECK(rt->appleevents.app_count == 1);
    mac_cpu_destroy(rt->cpu);
}

static void capacity(void) {
    MacRuntime *rt = make();
    uint32_t h, r;
    for (uint32_t i = 0; i < MAC_AE_HANDLERS; i++) CHECK(install(rt, AEVT, 0x1000 + i, 0x4000 + i, i, 0) == 0);
    CHECK(install(rt, AEVT, 0x2000, 1, 1, 0) == MEM_FULL);                        /* the 65th fails, loudly */
    CHECK(rt->appleevents.app_count == MAC_AE_HANDLERS);
    CHECK(get_h(rt, AEVT, 0x2000, 0, &h, &r) == NOT_FOUND);
    CHECK(install(rt, AEVT, 0x1000 + 5, 0x9999, 55, 0) == 0);                     /* replacing at full capacity still works */
    CHECK(get_h(rt, AEVT, 0x1000 + 5, 0, &h, &r) == 0 && h == 0x9999 && r == 55);
    CHECK(get_h(rt, AEVT, 0x1000 + 63, 0, &h, &r) == 0 && h == 0x4000 + 63);      /* the last entry is intact */
    CHECK(install(rt, AEVT, 0x2000, 1, 1, 1) == 0);                               /* the sys table has its own room */
    CHECK(remove_h(rt, AEVT, 0x1000, 0, 0) == 0 && install(rt, AEVT, 0x2000, 1, 1, 0) == 0);   /* space freed by remove */
    mac_cpu_destroy(rt->cpu);
}

static void lookup(void) {
    MacRuntime *rt = make();
    uint32_t h = 0, r = 0;
    CHECK(!mac_ae_find_handler(rt, AEVT, OAPP, &h, &r));
    CHECK(install(rt, WILD, WILD, 0xa, 1, 1) == 0);
    CHECK(mac_ae_find_handler(rt, AEVT, OAPP, &h, &r) && h == 0xa && r == 1);       /* wild/wild in the sys table */
    CHECK(install(rt, WILD, OAPP, 0xb, 2, 1) == 0);
    CHECK(mac_ae_find_handler(rt, AEVT, OAPP, &h, &r) && h == 0xb);                 /* '****'+id beats wild/wild */
    CHECK(install(rt, AEVT, WILD, 0xc, 3, 1) == 0);
    CHECK(mac_ae_find_handler(rt, AEVT, OAPP, &h, &r) && h == 0xc);                 /* class+'****' beats '****'+id */
    CHECK(install(rt, AEVT, OAPP, 0xd, 4, 1) == 0);
    CHECK(mac_ae_find_handler(rt, AEVT, OAPP, &h, &r) && h == 0xd && r == 4);       /* exact beats all */
    CHECK(mac_ae_find_handler(rt, AEVT, QUIT, &h, &r) && h == 0xc);
    CHECK(install(rt, WILD, WILD, 0xe, 5, 0) == 0);
    CHECK(mac_ae_find_handler(rt, AEVT, OAPP, &h, &r) && h == 0xe);                 /* application table is searched first */
    CHECK(mac_ae_find_handler(rt, 0x12345678, 0x9abcdef0, NULL, NULL));             /* NULL outputs are accepted */
    CHECK(get_h(rt, AEVT, OAPP, 0, &h, &r) == NOT_FOUND);                           /* Get is exact: '****' is not expanded */
    CHECK(get_h(rt, WILD, WILD, 0, &h, &r) == 0 && h == 0xe && r == 5);             /* ...a stored '****' key is an ordinary key */
    CHECK(remove_h(rt, WILD, WILD, 0, 0) == 0 && get_h(rt, WILD, WILD, 0, &h, &r) == NOT_FOUND);
    CHECK(!mac_ae_find_handler(NULL, AEVT, OAPP, &h, &r));
    mac_cpu_destroy(rt->cpu);
}

static void interaction(void) {
    MacRuntime *rt = make();
    uint8_t level = 0xee;
    CHECK(get_level(rt, &level) == 0 && level == 1);                               /* default: kAEInteractWithLocal (unverified) */
    CHECK(set_level(rt, 0x0274) == 0);                                              /* Glider PRO's real word: high byte 2, low byte junk */
    CHECK(get_level(rt, &level) == 0 && level == 2);
    CHECK(set_level(rt, 0x0000) == 0 && get_level(rt, &level) == 0 && level == 0);   /* kAEInteractWithSelf is a real setting */
    CHECK(set_level(rt, 0x0100) == 0 && get_level(rt, &level) == 0 && level == 1);
    CHECK(set_level(rt, 0x0300) == PARAM);                                          /* out of range: error, level unchanged */
    CHECK(get_level(rt, &level) == 0 && level == 1);
    CHECK(set_level(rt, 0xff00) == PARAM && get_level(rt, &level) == 0 && level == 1);
    mac_cpu_destroy(rt->cpu);
    rt = make();                                                                    /* a fresh runtime does not inherit state */
    CHECK(get_level(rt, &level) == 0 && level == 1);
    mac_cpu_destroy(rt->cpu);
}

static void halts(MacRuntime *rt, uint32_t selector, const char *needle) {
    int rc = run(rt, selector);
    CHECK(rc == -1 && mac_cpu_halted(rt->cpu));
    CHECK(needle == NULL || strstr(mac_cpu_error(rt->cpu), needle));
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP);                                      /* nothing popped */
}

static void fail_closed(void) {
    /* Not implemented: descriptors, AEProcessAppleEvent, lists, coercions, specials, plus wrong word counts. */
    const uint32_t unsupported[] = {0x0825, 0x0A02, 0x0204, 0x0405, 0x0D17, 0x0A22, 0x0500, 0x0818, 0x0000, 0xffff,
                                    0x0A1F, 0x0920, 0x0721, 0x031D, 0x021E, 0x0120, 0x0610};
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
        MacRuntime *rt = make();
        char hex[16]; snprintf(hex, sizeof hex, "0x%04x", (unsigned)unsupported[i]);
        halts(rt, unsupported[i], hex);                                             /* named, never guessed */
        mac_cpu_destroy(rt->cpu);
    }
    const uint32_t bad[] = {0, 0x6001, 0x10000, 0xfffe, 0xffffffff, 0xffff};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        for (int which = 0; which < 3; which++) {                                   /* level VAR, handler VAR, refcon VAR */
            if (which == 0 && (bad[i] == 0x6001 || bad[i] == 0xfffe || bad[i] == 0xffff)) continue;   /* valid Byte VARs */
            MacRuntime *rt = make();
            uint8_t *ram = mac_cpu_ram(rt->cpu);
            uint8_t before[16]; memcpy(before, ram + 0xfff0, 16);
            if (which == 0) { mac_cpu_write(rt->cpu, SP, 4, bad[i]); halts(rt, 0x021D, "level"); }
            else {
                mac_cpu_write(rt->cpu, SP + 2, 4, which == 1 ? VAR_R : bad[i]);
                mac_cpu_write(rt->cpu, SP + 6, 4, which == 1 ? bad[i] : VAR_H);
                mac_cpu_write(rt->cpu, SP + 10, 4, OAPP); mac_cpu_write(rt->cpu, SP + 14, 4, AEVT);
                CHECK(install(rt, AEVT, OAPP, 1, 1, 0) == 0);                       /* a hit exists, still must halt */
                mac_cpu_write(rt->cpu, SP + 2, 4, which == 1 ? VAR_R : bad[i]);
                mac_cpu_write(rt->cpu, SP + 6, 4, which == 1 ? bad[i] : VAR_H);
                mac_cpu_write(rt->cpu, SP + 10, 4, OAPP); mac_cpu_write(rt->cpu, SP + 14, 4, AEVT);
                halts(rt, 0x0921, "variables");
            }
            CHECK(!memcmp(before, ram + 0xfff0, 16));                               /* nothing written near the end of RAM */
            mac_cpu_destroy(rt->cpu);
        }
    }
    {   /* a Byte VAR may be odd, and the very last byte of RAM is valid for it */
        MacRuntime *rt = make();
        mac_cpu_write(rt->cpu, SP, 4, 0xffff);
        CHECK(run(rt, 0x021D) == 1 && res(rt, 2) == 0 && mac_cpu_read(rt->cpu, 0xffff, 1) == 1);
        mac_cpu_destroy(rt->cpu);
    }
    {   /* the last valid longword is accepted */
        MacRuntime *rt = make();
        CHECK(install(rt, AEVT, OAPP, 0xaaaa, 0xbbbb, 0) == 0);
        mac_cpu_write(rt->cpu, SP, 1, 0); mac_cpu_write(rt->cpu, SP + 2, 4, 0xfffc);
        mac_cpu_write(rt->cpu, SP + 6, 4, 0xfff8);
        mac_cpu_write(rt->cpu, SP + 10, 4, OAPP); mac_cpu_write(rt->cpu, SP + 14, 4, AEVT);
        CHECK(run(rt, 0x0921) == 1 && res(rt, 9) == 0);
        CHECK(mac_cpu_read(rt->cpu, 0xfff8, 4) == 0xaaaa && mac_cpu_read(rt->cpu, 0xfffc, 4) == 0xbbbb);
        mac_cpu_destroy(rt->cpu);
    }
    {   /* stack frames that do not fit, or are misaligned */
        MacRuntime *rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, 0x10000); mac_cpu_set(rt->cpu, MAC_D0, 0x011E);
        CHECK(mac_appleevents_service(rt, 0xa816) == -1 && mac_cpu_halted(rt->cpu));
        mac_cpu_destroy(rt->cpu);
        rt = make();                                                                /* 18 args + 2 result = 20 bytes, exactly 20 left */
        mac_cpu_set(rt->cpu, MAC_A7, 0xffec); mac_cpu_set(rt->cpu, MAC_D0, 0x091F);
        CHECK(mac_appleevents_service(rt, 0xa816) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == 0xffec + 18);
        mac_cpu_destroy(rt->cpu);
        rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, 0xffee); mac_cpu_set(rt->cpu, MAC_D0, 0x091F);   /* one word short */
        CHECK(mac_appleevents_service(rt, 0xa816) == -1 && mac_cpu_halted(rt->cpu) && mac_cpu_get(rt->cpu, MAC_A7) == 0xffee);
        mac_cpu_destroy(rt->cpu);
        rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, SP + 1); mac_cpu_set(rt->cpu, MAC_D0, 0x011E);  /* odd stack */
        CHECK(mac_appleevents_service(rt, 0xa816) == -1 && mac_cpu_halted(rt->cpu));
        mac_cpu_destroy(rt->cpu);
        rt = make();                                                                /* an already halted CPU does no work */
        mac_cpu_halt(rt->cpu, "earlier failure"); mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_set(rt->cpu, MAC_D0, 0x011E);
        CHECK(mac_appleevents_service(rt, 0xa816) == -1 && mac_cpu_get(rt->cpu, MAC_A7) == SP);
        mac_cpu_destroy(rt->cpu);
        rt = make();
        CHECK(mac_appleevents_service(rt, 0xa830) == 0 && mac_appleevents_service(rt, 0xa817) == 0);   /* not our trap */
        mac_cpu_destroy(rt->cpu);
    }
}

int main(void) {
    selectors();
    tables();
    capacity();
    lookup();
    interaction();
    fail_closed();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("apple event manager checks ok");
    return 0;
}
