#include "mac_runtime.h"
#include <stdio.h>

/* Help Manager dispatch (_Pack14, $A830). Pascal calling convention with the routine in the low word of D0: the
 * high byte is the number of argument WORDS, the low byte the routine number. Selector words, argument sizes and
 * result types come from the public MPW interface declarations:
 *   $0200 HMGetHelpMenuHandle(VAR mh: MenuHandle): OSErr             $0B01 HMShowBalloon(...11 words...): OSErr
 *   $0002 HMRemoveBalloon: OSErr   $0003 HMGetBalloons: BOOLEAN       $0104 HMSetBalloons(flag: BOOLEAN): OSErr
 *   $0E05 HMShowMenuBalloon(...14 words...): OSErr                    $0007 HMIsBalloon: BOOLEAN
 * The runtime has no Help menu and cannot show balloons, so the Help Manager is honest about being switched off:
 * balloons are off and cannot be turned on (hmHelpDisabled, hmOperationUnsupported), none is ever up (hmNoBalloonUp)
 * and no Help menu is set up (hmHelpManagerNotInited, handle returned nil). Gestalt 'help' is deliberately not
 * answered, so applications that ask Gestalt skip the Help Manager entirely. Other selectors halt, named.
 * The trap pops the argument words and leaves the 2-byte function result in their place. */
enum { HM_DISABLED = -850, HM_UNSUPPORTED = -851, HM_NOT_INITED = -855, HM_NO_BALLOON = -862 };

static int fail(MacRuntime *rt, const char *why) {
    mac_cpu_halt(rt->cpu, why);
    return -1;
}

int mac_help_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa830) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t selector = mac_cpu_get(cpu, MAC_D0) & 0xffff, sp = mac_cpu_get(cpu, MAC_A7);
    unsigned words = selector >> 8, routine = selector & 0xff;
    int boolean = 0, known = 1;
    int16_t result = 0;
    switch (selector) {
    case 0x0200: result = HM_NOT_INITED; break;
    case 0x0B01: case 0x0E05: result = HM_DISABLED; break;
    case 0x0002: result = HM_NO_BALLOON; break;
    case 0x0003: case 0x0007: boolean = 1; break;     /* HMGetBalloons / HMIsBalloon: FALSE */
    case 0x0104: break;                               /* HMSetBalloons: decided below from the argument */
    default: known = 0; break;
    }
    if (!known) {
        static char message[64];
        snprintf(message, sizeof message, "Help Manager selector 0x%04x is unsupported.", (unsigned)selector);
        return fail(rt, message);
    }
    size_t size = mac_cpu_ram_size(cpu), arg_bytes = (size_t)words * 2;
    if ((sp & 1) || sp > size || size - sp < arg_bytes + 2) return fail(rt, "Help Manager stack exceeds guest RAM.");
    if (selector == 0x0200) {                         /* VAR mh: write nil through the pointer */
        uint32_t pointer = mac_cpu_read(cpu, sp, 4);
        if (!pointer || (pointer & 1) || pointer > size || size - pointer < 4)
            return fail(rt, "HMGetHelpMenuHandle needs a valid MenuHandle variable.");
        mac_cpu_write(cpu, pointer, 4, 0);
    }
    if (selector == 0x0104)                           /* the Boolean is the high byte of its stack word */
        result = (mac_cpu_read(cpu, sp, 2) & 0xff00) ? HM_UNSUPPORTED : 0;
    (void)routine;
    mac_cpu_write(cpu, sp + (uint32_t)arg_bytes, 2, boolean ? 0 : (uint16_t)result);
    mac_cpu_set(cpu, MAC_A7, sp + (uint32_t)arg_bytes);
    return 1;
}
