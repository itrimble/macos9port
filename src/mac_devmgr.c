#include "mac_runtime.h"

/* Device Manager Control ($A004, with the immediate bit $0200 and the asynchronous bit $0400 accepted). The parameter block at A0 has
 * ioResult at +16, ioRefNum at +24 and csCode at +26 (the layout this runtime's File Manager code uses for ioRefNum too).
 *
 * This runtime has no drivers: the one-word sound-driver style calls programs make on the negative reference numbers (the application
 * Glider 4.10 sends csCode 2 to reference -4) are answered with controlErr (-17), "the driver does not respond to this Control", which is a
 * result a program has to be ready for; any other reference number gets badUnitErr (-21). Nothing is changed. An asynchronous call completes
 * at once with the same result. Status, Read, Write, Open and Close on drivers are not here. The meaning of the call is not identified, so
 * nothing is faked as success. */
int mac_devmgr_service(MacRuntime *rt, uint16_t opcode) {
    if ((opcode & 0xf9ffu) != 0xa004) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t pb = mac_cpu_get(cpu, MAC_A0), z = (uint32_t)mac_cpu_ram_size(cpu);
    if ((pb & 1) || pb < 0x400 || pb > z || z - pb < 30) { mac_cpu_halt(cpu, "PBControl has an invalid parameter block."); return -1; }
    int16_t ref = (int16_t)mac_cpu_read(cpu, pb + 24, 2);
    int error = ref < 0 && ref >= -32 ? -17 : -21;
    mac_cpu_write(cpu, pb + 16, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)error);
    mac_cpu_set(cpu, MAC_SR, (mac_cpu_get(cpu, MAC_SR) & ~15u) | 8u);
    return 1;
}
