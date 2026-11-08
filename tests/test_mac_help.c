#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

/* Help Manager dispatch ($A830): selectors, results, argument popping, and fail-closed behaviour. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x8000u
enum { DISABLED = -850, UNSUPPORTED = -851, NOT_INITED = -855, NO_BALLOON = -862 };

static MacRuntime *make(void) {
    static MacRuntime rt;
    memset(&rt, 0, sizeof rt);
    rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL);
    return &rt;
}
/* Call with `words` argument words at SP and a result slot above them preset to 0xBEEF. */
static int call(MacRuntime *rt, uint32_t selector, unsigned words) {
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (unsigned i = 0; i < words * 2 + 2; i++) ram[SP + i] = 0xff;
    mac_cpu_write(rt->cpu, SP + words * 2, 2, 0xbeef);
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    mac_cpu_set(rt->cpu, MAC_D0, selector | 0xabcd0000u);       /* only the low word selects */
    return mac_help_service(rt, 0xa830);
}
static int16_t result(MacRuntime *rt, unsigned words) { return (int16_t)mac_cpu_read(rt->cpu, SP + words * 2, 2); }

static void routines(void) {
    MacRuntime *rt = make();
    mac_cpu_set(rt->cpu, MAC_A0, 0x1234);
    uint32_t var = 0x6000;
    mac_cpu_write(rt->cpu, var, 4, 0xcafef00d);
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (unsigned i = 0; i < 64; i++) ram[0x9000 + i] = 0x5a;                          /* guard region above the frame */
    mac_cpu_write(rt->cpu, SP, 4, var);                                                  /* VAR mh */
    mac_cpu_write(rt->cpu, SP + 4, 2, 0xbeef);                                           /* result slot */
    mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_set(rt->cpu, MAC_D0, 0x0200);
    int rc = mac_help_service(rt, 0xa830);
    CHECK(rc == 1 && result(rt, 2) == NOT_INITED);                                      /* HMGetHelpMenuHandle: not set up */
    CHECK(mac_cpu_read(rt->cpu, var, 4) == 0);                                            /* handle returned nil */
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP + 4 && mac_cpu_get(rt->cpu, MAC_A0) == 0x1234);   /* args popped, A0 kept */
    for (unsigned i = 0; i < 64; i++) CHECK(ram[0x9000 + i] == 0x5a);

    CHECK(call(rt, 0x0B01, 11) == 1 && result(rt, 11) == DISABLED && mac_cpu_get(rt->cpu, MAC_A7) == SP + 22);   /* HMShowBalloon */
    CHECK(call(rt, 0x0E05, 14) == 1 && result(rt, 14) == DISABLED && mac_cpu_get(rt->cpu, MAC_A7) == SP + 28);   /* HMShowMenuBalloon */
    CHECK(call(rt, 0x0002, 0) == 1 && result(rt, 0) == NO_BALLOON && mac_cpu_get(rt->cpu, MAC_A7) == SP);       /* HMRemoveBalloon */
    CHECK(call(rt, 0x0003, 0) == 1 && result(rt, 0) == 0);                                                      /* HMGetBalloons: FALSE */
    CHECK(call(rt, 0x0007, 0) == 1 && result(rt, 0) == 0);                                                      /* HMIsBalloon: FALSE */
    mac_cpu_write(rt->cpu, SP, 2, 0); mac_cpu_set(rt->cpu, MAC_A7, SP);                                          /* HMSetBalloons(FALSE) */
    mac_cpu_write(rt->cpu, SP + 2, 2, 0xbeef); mac_cpu_set(rt->cpu, MAC_D0, 0x0104);
    CHECK(mac_help_service(rt, 0xa830) == 1 && result(rt, 1) == 0 && mac_cpu_get(rt->cpu, MAC_A7) == SP + 2);   /* already off: ok */
    mac_cpu_write(rt->cpu, SP, 2, 0x0100); mac_cpu_set(rt->cpu, MAC_A7, SP);                                     /* HMSetBalloons(TRUE) */
    mac_cpu_write(rt->cpu, SP + 2, 2, 0xbeef); mac_cpu_set(rt->cpu, MAC_D0, 0x0104);
    CHECK(mac_help_service(rt, 0xa830) == 1 && result(rt, 1) == UNSUPPORTED);                                    /* cannot be turned on */
    CHECK(mac_help_service(rt, 0xa831) == 0 && mac_help_service(rt, 0xa9ee) == 0);                              /* not our trap */
    CHECK(!mac_cpu_halted(rt->cpu));
    mac_cpu_destroy(rt->cpu);
}

static void fail_closed(void) {
    const uint32_t unsupported[] = {0x1306, 0x0108, 0x0109, 0x0400, 0x0000, 0xffff, 0x0201, 0x0300};
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
        MacRuntime *rt = make();
        int rc = call(rt, unsupported[i], 2);
        char hex[16]; snprintf(hex, sizeof hex, "0x%04x", (unsigned)(unsupported[i] & 0xffff));
        CHECK(rc == -1 && mac_cpu_halted(rt->cpu) && strstr(mac_cpu_error(rt->cpu), hex));   /* named, never guessed */
        CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP);                                           /* nothing popped */
        mac_cpu_destroy(rt->cpu);
    }
    {   /* VAR pointer: nil, odd, off the end of RAM, and straddling the end */
        const uint32_t bad[] = {0, 0x6001, 0x10000, 0xfffe, 0xffffffff};
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
            MacRuntime *rt = make();
            uint8_t before[8]; memcpy(before, mac_cpu_ram(rt->cpu) + 0xfff8, 8);
            mac_cpu_write(rt->cpu, SP, 4, bad[i]); mac_cpu_write(rt->cpu, SP + 4, 2, 0xbeef);
            mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_set(rt->cpu, MAC_D0, 0x0200);
            CHECK(mac_help_service(rt, 0xa830) == -1 && mac_cpu_halted(rt->cpu));
            CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP && !memcmp(before, mac_cpu_ram(rt->cpu) + 0xfff8, 8));
            mac_cpu_destroy(rt->cpu);
        }
        MacRuntime *rt = make();                                                            /* last valid longword is accepted */
        mac_cpu_write(rt->cpu, SP, 4, 0xfffc); mac_cpu_write(rt->cpu, 0xfffc, 4, 0x11111111);
        mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_set(rt->cpu, MAC_D0, 0x0200);
        CHECK(mac_help_service(rt, 0xa830) == 1 && mac_cpu_read(rt->cpu, 0xfffc, 4) == 0);
        mac_cpu_destroy(rt->cpu);
    }
    {   /* stack frames that do not fit, or are misaligned */
        MacRuntime *rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, 0x10000); mac_cpu_set(rt->cpu, MAC_D0, 0x0003);          /* no room even for the result */
        CHECK(mac_help_service(rt, 0xa830) == -1 && mac_cpu_halted(rt->cpu));
        mac_cpu_destroy(rt->cpu);
        rt = make();                                                                         /* a 0-word frame at the last word fits */
        mac_cpu_set(rt->cpu, MAC_A7, 0xfffe); mac_cpu_set(rt->cpu, MAC_D0, 0x0003);
        CHECK(mac_help_service(rt, 0xa830) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == 0xfffe);
        mac_cpu_destroy(rt->cpu);
        rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, 0xffe8); mac_cpu_set(rt->cpu, MAC_D0, 0x0B01);         /* 22 args + 2 result need 24, 24 left: ok */
        CHECK(mac_help_service(rt, 0xa830) == 1 && mac_cpu_get(rt->cpu, MAC_A7) == 0xffe8 + 22);
        mac_cpu_destroy(rt->cpu);
        rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, 0xffea); mac_cpu_set(rt->cpu, MAC_D0, 0x0B01);         /* one word short */
        CHECK(mac_help_service(rt, 0xa830) == -1 && mac_cpu_halted(rt->cpu));
        mac_cpu_destroy(rt->cpu);
        rt = make();
        mac_cpu_set(rt->cpu, MAC_A7, SP + 1); mac_cpu_set(rt->cpu, MAC_D0, 0x0003);          /* odd stack */
        CHECK(mac_help_service(rt, 0xa830) == -1 && mac_cpu_halted(rt->cpu));
        mac_cpu_destroy(rt->cpu);
        rt = make();                                                                         /* an already halted CPU does no work */
        mac_cpu_halt(rt->cpu, "earlier failure"); mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_set(rt->cpu, MAC_D0, 0x0003);
        CHECK(mac_help_service(rt, 0xa830) == -1 && mac_cpu_get(rt->cpu, MAC_A7) == SP);
        mac_cpu_destroy(rt->cpu);
    }
}

int main(void) {
    routines();
    fail_closed();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("help manager checks ok");
    return 0;
}
