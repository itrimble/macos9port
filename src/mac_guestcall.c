#include "mac_runtime.h"

/* Calling guest code from inside a trap service.
 *
 * The CPU trap callback may not run the CPU, so a native service cannot simply call a guest routine and wait for it.
 * Instead the service builds the frame a Pascal caller would build and lets the CPU run on:
 *
 *     [return address = native stub of key 0xFE][arguments][result slot]   <- A7 at the return address, PC = routine
 *
 * The routine pops its arguments and returns (RTS) into that stub. The stub is the A-line word the runtime already
 * installs for every trap key; the trap handler recognises "guest call active and PC == stub" and calls
 * mac_guest_return, which reads the result, restores A7 to its value before the call, and runs the caller's `done`
 * callback. `done` either starts the next call (sequential, e.g. one filter call per file) or returns, in which case PC
 * is the resume address saved when the first call started. Nested calls are refused. Pascal calling convention only:
 * the routine preserves everything except D0-D2/A0-A1, which the interrupted trap's caller already treats as scratch. */
#define RETURN_STUB (0x2000u + 0xfeu * 8)

static int fail(MacRuntime *rt, const char *why) {
    mac_cpu_halt(rt->cpu, why);
    return -1;
}

int mac_guest_call(MacRuntime *rt, uint32_t routine, const uint8_t *args, unsigned arg_bytes,
                   unsigned result_bytes, MacGuestDone done, void *ctx) {
    MacCpu *cpu = rt->cpu;
    if (mac_cpu_halted(cpu)) return -1;
    if (rt->guest.active) return fail(rt, "Nested guest calls from a native service are unsupported.");
    if (!done || (arg_bytes & 1) || arg_bytes > 64 || (result_bytes != 0 && result_bytes != 2 && result_bytes != 4))
        return fail(rt, "Guest call has an invalid argument or result size.");
    if ((routine & 1) || routine < 0x400 || routine >= mac_cpu_ram_size(cpu) - 2)
        return fail(rt, "Guest call target is outside guest code memory or unaligned.");
    /* mac_guest_return restores A7 and PC before running `done`, so a chained call starts from the same state. */
    uint32_t sp = mac_cpu_get(cpu, MAC_A7), resume = mac_cpu_get(cpu, MAC_PC);
    uint32_t frame = result_bytes + arg_bytes + 4;
    if (sp & 1 || sp < frame + 0x1000 || sp > mac_cpu_ram_size(cpu))
        return fail(rt, "Guest call stack is invalid or exhausted.");
    uint32_t at = sp - frame;
    /* Low to high: return address (A7), arguments, result slot - the order a Pascal caller builds it. */
    if (mac_cpu_write(cpu, at, 4, RETURN_STUB)) return -1;
    for (unsigned i = 0; i < arg_bytes; i++)
        if (mac_cpu_write(cpu, at + 4 + i, 1, args[i])) return -1;
    for (unsigned i = 0; i < result_bytes; i++)
        if (mac_cpu_write(cpu, at + 4 + arg_bytes + i, 1, 0)) return -1;
    rt->guest.active = 1;
    rt->guest.done = done;
    rt->guest.ctx = ctx;
    rt->guest.sp = sp;
    rt->guest.resume_pc = resume;
    rt->guest.result_bytes = result_bytes;
    mac_cpu_set(cpu, MAC_A7, at);
    mac_cpu_set(cpu, MAC_PC, routine);
    return 0;
}

int mac_guest_return(MacRuntime *rt) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7), bytes = rt->guest.result_bytes;
    if (sp + bytes != rt->guest.sp) return fail(rt, "Guest routine returned with an unbalanced stack.");
    uint32_t result = bytes ? mac_cpu_read(cpu, sp, bytes) : 0;
    MacGuestDone done = rt->guest.done;
    void *ctx = rt->guest.ctx;
    uint32_t resume = rt->guest.resume_pc, base = rt->guest.sp;
    rt->guest.active = 0;
    mac_cpu_set(cpu, MAC_A7, base);
    mac_cpu_set(cpu, MAC_PC, resume);
    int rc = done(rt, ctx, result);
    if (rc < 0 && !mac_cpu_halted(cpu)) mac_cpu_halt(cpu, "Guest call completion failed.");
    return rc < 0 ? -1 : 0;
}

