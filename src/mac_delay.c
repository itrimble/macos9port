#include "mac_runtime.h"

/* Time Manager _Delay ($A03B), register-based OS trap. Public convention (Inside Macintosh Vol. III trap summary,
 * "_Delay  A0: numTicks (long)  D0: finalTicks (long)"; MPW interface: PROCEDURE Delay(numTicks: LONGINT;
 * VAR finalTicks: LONGINT)): wait numTicks ticks (1/60 s), then return with D0 = the tick count at the end.
 *
 * The runtime has no separate guest "task" to park, and every run mode advances rt->ticks outside the CPU slice
 * (headless: once per slice; probe: once per 1/60 s of virtual time; windowed: from the host clock). So a delay is
 * a deadline against rt->ticks. While it has not passed, the trap rewinds the PC onto itself and ends the CPU slice
 * early (mac_cpu_yield). The driver loop then advances time and runs the Time Manager, VBL and sound polls as usual
 * before the trap executes again, one re-execution per slice. Nothing is added to rt->ticks, so real-time runs wait
 * real time and virtual-time runs wait virtual time. The deadline lives in rt->delay, keyed by the trap address and
 * stack pointer of the caller, and is cleared on completion. Every re-execution is counted by rt->traps.
 * Limits: numTicks >= 2^31 (a delay of more than a year) halts; a Delay reached through a guest-patched trap vector
 * (the native stub path) halts, because the PC cannot be rewound onto the caller there. */
#define DELAY_STUB (0x2000u + 0x3bu * 8) /* native_stub(0x3b) in mac_services.c */

static int fail(MacRuntime *rt, const char *why) {
    mac_cpu_halt(rt->cpu, why);
    return -1;
}

int mac_delay_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa03b) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t pc = (mac_cpu_get(cpu, MAC_PC) - 2) & mac_cpu_addr_mask(cpu), sp = mac_cpu_get(cpu, MAC_A7);
    if (rt->last_trap_pc == DELAY_STUB) return fail(rt, "Delay reached through a patched trap vector is unsupported.");
    if (!rt->delay.active) {
        uint32_t count = mac_cpu_get(cpu, MAC_A0); /* 0 completes at once below: deadline == ticks */
        if (count >= 0x80000000u) return fail(rt, "Delay of 2^31 ticks or more is not implemented.");
        rt->delay.active = 1;
        rt->delay.deadline = rt->ticks + count;
        rt->delay.pc = pc;
        rt->delay.sp = sp;
    } else if (rt->delay.pc != pc || rt->delay.sp != sp) {
        return fail(rt, "Delay called again while another Delay is in progress.");
    }
    if ((int32_t)(rt->ticks - rt->delay.deadline) < 0) {
        mac_cpu_set(cpu, MAC_PC, pc);   /* run this trap again after time has advanced */
        mac_cpu_yield(cpu);
        return 1;
    }
    rt->delay.active = 0;
    mac_cpu_set(cpu, MAC_D0, rt->ticks);
    return 1;
}
