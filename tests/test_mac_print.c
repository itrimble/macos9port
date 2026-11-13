/* Printing Manager with no printer: every call that needs the driver reports the error, shows nothing, returns FALSE or nil and leaves the caller's
 * records alone; PrClose and PrError and PrSetError behave; unknown selectors halt by name. */
#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define SP 0x8000u
#define NO_PRINTER (-4101)
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
/* a call with `args` argument bytes already stored at SP+4: pushes the selector and runs the trap; returns the service result */
static int pr(MacRuntime *r, uint32_t selector) { wr(r, SP, 4, selector); mac_cpu_set(r->cpu, MAC_A7, SP); return mac_print_service(r, 0xa8fd); }
static int err(MacRuntime *r) { return (int16_t)rd(r, 0x944, 2); }
int main(void) {
    MacRuntime r; memset(&r, 0, sizeof r); r.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(r.cpu);
    CHECK(mac_print_service(&r, 0xa8fe) == 0);                                              /* another trap is not ours */
    wr(&r, 0x944, 2, 77);
    CHECK(pr(&r, 0xc8000000u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4 && err(&r) == NO_PRINTER);                 /* PrOpen */
    wr(&r, SP + 4, 2, 0xbeef); CHECK(pr(&r, 0xba000000u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4 && (int16_t)rd(&r, SP + 4, 2) == NO_PRINTER);   /* PrError */
    CHECK(pr(&r, 0xd0000000u) == 1 && err(&r) == 0);                                        /* PrClose */
    wr(&r, SP + 4, 2, (uint16_t)-27); CHECK(pr(&r, 0xc0000200u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 6 && err(&r) == -27);   /* PrSetError(iIOAbort) */
    CHECK(pr(&r, 0x80000000u) == 1 && err(&r) == NO_PRINTER);                               /* PrDrvrOpen */
    CHECK(pr(&r, 0x88000000u) == 1 && err(&r) == 0);                                        /* PrDrvrClose */
    /* the routines that need a driver: a print record stays exactly as the caller left it */
    memset(mac_cpu_ram(r.cpu) + 0x4000, 0xa5, 120);
    wr(&r, SP + 4, 4, 0x4000); CHECK(pr(&r, 0x20040480u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 8 && err(&r) == NO_PRINTER);       /* PrintDefault */
    wr(&r, SP + 4, 4, 0x4000); wr(&r, SP + 8, 2, 0x7777); CHECK(pr(&r, 0x52040498u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 8 && rd(&r, SP + 8, 1) == 0);   /* PrValidate: FALSE */
    wr(&r, SP + 4, 4, 0x4000); wr(&r, SP + 8, 2, 0x7777); CHECK(pr(&r, 0x2a040484u) == 1 && rd(&r, SP + 8, 1) == 0 && err(&r) == NO_PRINTER);   /* PrStlDialog: no dialog, FALSE */
    wr(&r, SP + 4, 4, 0x4000); wr(&r, SP + 8, 2, 0x7777); CHECK(pr(&r, 0x32040488u) == 1 && rd(&r, SP + 8, 1) == 0);                /* PrJobDialog */
    wr(&r, SP + 4, 4, 0x4000); wr(&r, SP + 8, 4, 0x4000); CHECK(pr(&r, 0x5804089cu) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 12);   /* PrJobMerge */
    wr(&r, SP + 4, 4, 0); wr(&r, SP + 8, 4, 0); wr(&r, SP + 12, 4, 0x4000); wr(&r, SP + 16, 4, 0x1234);
    CHECK(pr(&r, 0x04000c00u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 16 && rd(&r, SP + 16, 4) == 0);                              /* PrOpenDoc: nil port */
    wr(&r, SP + 4, 4, 0); CHECK(pr(&r, 0x08000484u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 8);                                   /* PrCloseDoc */
    wr(&r, SP + 4, 4, 0); wr(&r, SP + 8, 4, 0); CHECK(pr(&r, 0x10000808u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 12);           /* PrOpenPage */
    wr(&r, SP + 4, 4, 0); CHECK(pr(&r, 0x1800040cu) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 8);                                   /* PrClosePage */
    CHECK(pr(&r, 0x60051480u) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 24);                                                         /* PrPicFile */
    { int same = 1; for (unsigned i = 0; i < 120; i++) if (mac_cpu_ram(r.cpu)[0x4000 + i] != 0xa5) same = 0; CHECK(same); }
    /* refusals: an unknown selector, a known routine with the wrong argument size, an odd stack */
    CHECK(pr(&r, 0x12345678u) == -1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu), "12345678"));
    mac_cpu_destroy(r.cpu);
    memset(&r, 0, sizeof r); r.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(pr(&r, 0x52000098u) == -1 && mac_cpu_halted(r.cpu));                               /* PrValidate with no argument bytes */
    mac_cpu_destroy(r.cpu);
    memset(&r, 0, sizeof r); r.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    mac_cpu_set(r.cpu, MAC_A7, SP + 1); CHECK(mac_print_service(&r, 0xa8fd) == -1 && mac_cpu_halted(r.cpu));
    mac_cpu_destroy(r.cpu);
    if (!fails) puts("Printing Manager (no printer) checks passed");
    return fails ? 1 : 0;
}
