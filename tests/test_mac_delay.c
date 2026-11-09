#include "mac_runtime.h"
#include "mac_time.h"

#include <stdio.h>
#include <string.h>

/* _Delay ($A03B): A0 = numTicks in, D0 = final tick count out. Runs real 68k code through mac_runtime_trap and
 * drives time the way each run mode does: instruction slices with one tick per slice (headless), and cycle
 * slices (probe/windowed) with the test advancing rt->ticks and the clock between slices. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define CODE 0x1000u
#define SP 0x8000u
#define SPIN (CODE + 8)        /* BRA.S * after the trap */
#define COUNTER 0x3100u
#define TICK_US 16667u

static MacRuntime rt;
/* MOVE.L #n,A0 ; _Delay ; BRA.S * */
static void boot(uint32_t n, uint32_t ticks) {
    memset(&rt, 0, sizeof rt);
    rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL);
    mac_traps_init(&rt);
    mac_cpu_set_trap(rt.cpu, mac_runtime_trap, &rt);
    mac_cpu_write(rt.cpu, CODE, 2, 0x207c); mac_cpu_write(rt.cpu, CODE + 2, 4, n);
    mac_cpu_write(rt.cpu, CODE + 6, 2, 0xa03b); mac_cpu_write(rt.cpu, CODE + 8, 2, 0x60fe);
    mac_cpu_set(rt.cpu, MAC_PC, CODE); mac_cpu_set(rt.cpu, MAC_A7, SP); mac_cpu_set(rt.cpu, MAC_SR, 0x2000);
    mac_cpu_set(rt.cpu, MAC_D0, 0xdeadbeef);
    rt.ticks = ticks;
}
static void tick(void) { rt.ticks++; CHECK(!mac_time_set(&rt, rt.clock_us + TICK_US)); }
/* Headless loop: one tick per instruction slice. Returns ticks that elapsed before the guest reached SPIN. */
static unsigned run_headless(unsigned max_slices, size_t slice) {
    unsigned n = 0;
    while (n < max_slices && !mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) != SPIN) {
        tick(); n++;
        mac_runtime_run(&rt, slice);
    }
    return n;
}
static unsigned run_cycles(unsigned max_slices, uint64_t cycles) {
    unsigned n = 0;
    while (n < max_slices && !mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) != SPIN) {
        tick(); n++;
        mac_runtime_run_cycles(&rt, cycles);
    }
    return n;
}

static void headless_wait(void) {
    boot(30, 1000);
    unsigned slices = run_headless(200, 10000);
    CHECK(!mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) == SPIN);
    /* The loop ticks before each slice, so the trap first runs at 1001; final ticks = 1001 + numTicks. */
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 1031);
    CHECK(rt.ticks == 1031 && slices == 31);                      /* exactly 30 ticks of virtual time waited */
    CHECK(rt.instructions < 31 * 4 + 10000);                      /* only the final slice spins on BRA.S */                              /* slices end early: no budget burned spinning */
    CHECK(!rt.delay.active && mac_cpu_get(rt.cpu, MAC_A7) == SP && mac_cpu_get(rt.cpu, MAC_A0) == 30);
    mac_cpu_destroy(rt.cpu);
}

static void cycle_wait(void) {
    boot(45, 5);
    unsigned slices = run_cycles(500, 200000);                    /* a large slice must still end at the trap */
    CHECK(!mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) == SPIN);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 51 && rt.ticks == 51 && slices == 46);
    CHECK(rt.instructions < 46 * 6 && !rt.delay.active);
    mac_cpu_destroy(rt.cpu);
}

static void zero_and_edges(void) {
    boot(0, 777);                                                 /* numTicks == 0: returns at once, D0 = Ticks */
    mac_runtime_run(&rt, 10);
    CHECK(mac_cpu_get(rt.cpu, MAC_PC) == SPIN && mac_cpu_get(rt.cpu, MAC_D0) == 777 && !rt.delay.active);
    mac_cpu_destroy(rt.cpu);

    boot(1, 0xfffffff0u);                                         /* 1 tick: not done until the next tick */
    mac_runtime_run(&rt, 10);
    CHECK(mac_cpu_get(rt.cpu, MAC_PC) == CODE + 6 && rt.delay.active);
    tick();
    mac_runtime_run(&rt, 10);
    CHECK(mac_cpu_get(rt.cpu, MAC_PC) == SPIN && mac_cpu_get(rt.cpu, MAC_D0) == 0xfffffff1u);
    mac_cpu_destroy(rt.cpu);

    boot(0x20, 0xfffffff0u);                                      /* tick counter wraps while waiting */
    unsigned slices = run_headless(100, 10000);
    CHECK(slices == 0x21 && mac_cpu_get(rt.cpu, MAC_D0) == 0x11 && rt.ticks == 0x11);
    mac_cpu_destroy(rt.cpu);

    boot(0x80000000u, 0);                                         /* more than a year: named halt */
    mac_runtime_run(&rt, 10);
    CHECK(mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "Delay") && !rt.delay.active);
    mac_cpu_destroy(rt.cpu);
}

static void huge_delay_budget(void) {
    boot(0x7fffffffu, 0);                                         /* enormous wait must not wedge the budget */
    size_t budget = 5000;
    while (rt.instructions < budget && !mac_cpu_halted(rt.cpu)) {
        tick();
        size_t remain = budget - (size_t)rt.instructions;
        mac_runtime_run(&rt, remain < 10000 ? remain : 10000);
    }
    CHECK(!mac_cpu_halted(rt.cpu) && rt.instructions >= budget && rt.instructions <= budget + 1);
    CHECK(rt.delay.active && mac_cpu_get(rt.cpu, MAC_PC) == CODE + 6);
    CHECK(rt.ticks > 1000 && rt.ticks < 5000);                    /* time moved on its own, one tick per slice */
    mac_cpu_destroy(rt.cpu);
}

static void timer_runs_while_waiting(void) {
    boot(30, 0);
    /* Callback at 0x3000: ADDQ.L #1,(COUNTER).L ; RTS.  Task record at 0x3200 (qLink, qType, tmAddr at +6). */
    mac_cpu_write(rt.cpu, COUNTER, 4, 0);
    mac_cpu_write(rt.cpu, 0x3000, 2, 0x52b9); mac_cpu_write(rt.cpu, 0x3002, 4, COUNTER);
    mac_cpu_write(rt.cpu, 0x3006, 2, 0x4e75);
    mac_cpu_write(rt.cpu, 0x3200 + 6, 4, 0x3000);
    mac_cpu_set(rt.cpu, MAC_A0, 0x3200);
    CHECK(mac_time_service(&rt, 0xa058) == 1);                    /* InsTime */
    mac_cpu_set(rt.cpu, MAC_A0, 0x3200); mac_cpu_set(rt.cpu, MAC_D0, 100);
    CHECK(mac_time_service(&rt, 0xa05a) == 1);                    /* PrimeTime: 100 ms (about 6 ticks) */
    mac_cpu_set(rt.cpu, MAC_A0, 30);
    run_headless(200, 10000);
    CHECK(!mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) == SPIN);
    CHECK(mac_cpu_read(rt.cpu, COUNTER, 4) == 1);                 /* ran once, during the wait */
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 31 && rt.ticks == 31 && mac_cpu_get(rt.cpu, MAC_A7) == SP);
    mac_cpu_destroy(rt.cpu);
}

static void fail_closed(void) {
    boot(10, 0);
    CHECK(mac_delay_service(&rt, 0xa03a) == 0 && mac_delay_service(&rt, 0xa13b) == 0 && mac_delay_service(&rt, 0xa83b) == 0);
    mac_runtime_run(&rt, 10);
    CHECK(rt.delay.active && !mac_cpu_halted(rt.cpu));
    mac_cpu_set(rt.cpu, MAC_A7, SP - 4);                          /* a different caller while one is waiting */
    mac_cpu_set(rt.cpu, MAC_PC, CODE + 6);
    mac_runtime_run(&rt, 10);
    CHECK(mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "another Delay"));
    mac_cpu_destroy(rt.cpu);

    boot(10, 0);                                                  /* same stack, different call site */
    mac_runtime_run(&rt, 10);
    CHECK(rt.delay.active && !mac_cpu_halted(rt.cpu));
    mac_cpu_write(rt.cpu, CODE + 0x100, 2, 0xa03b);
    mac_cpu_set(rt.cpu, MAC_PC, CODE + 0x100);
    mac_runtime_run(&rt, 10);
    CHECK(mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "another Delay"));
    mac_cpu_destroy(rt.cpu);

    boot(10, 0);
    rt.last_trap_pc = 0x2000u + 0x3bu * 8;                        /* arrived through the native stub (patched vector) */
    mac_cpu_set(rt.cpu, MAC_A0, 10);
    CHECK(mac_delay_service(&rt, 0xa03b) == -1 && mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "patched"));
    CHECK(!rt.delay.active);
    mac_cpu_destroy(rt.cpu);
}

int main(void) {
    headless_wait();
    cycle_wait();
    zero_and_edges();
    huge_delay_budget();
    timer_runs_while_waiting();
    fail_closed();
    if (fails) fprintf(stderr, "%d failure(s)\n", fails);
    else puts("mac_delay: ok");
    return fails ? 1 : 0;
}
