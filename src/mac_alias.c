#include "mac_runtime.h"
#include "mac_files_int.h"

/* AliasDispatch ($A823), selector $000C only: FUNCTION ResolveAliasFile(VAR theSpec: FSSpec; resolveAliasChains: BOOLEAN; VAR targetIsFolder, wasAliased:
 * BOOLEAN): OSErr (MPW listing; stack from SP: wasAliased pointer, targetIsFolder pointer, the chain flag as a byte in a word, the FSSpec pointer, then the
 * 2-byte result). This volume has no alias files, so a spec that names an existing file resolves to itself: wasAliased FALSE, targetIsFolder FALSE, noErr;
 * a spec whose file does not exist is fnfErr (-43). The FSSpec is vRefNum (2), parID (4), a Pascal name (63 bytes after its length byte) = 70 bytes; the name is
 * looked up in the flat namespace. Every other alias selector halts by name. */
int mac_alias_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa823) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t d0 = mac_cpu_get(cpu, MAC_D0) & 0xffff;
    if (d0 != 0x000c) { mac_cpu_halt(cpu, "AliasDispatch selector is unsupported (only ResolveAliasFile, $000C, is)."); return -1; }
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if ((sp & 1) || !mac_files_valid(cpu, sp, 16)) { mac_cpu_halt(cpu, "ResolveAliasFile stack is invalid."); return -1; }
    uint32_t was = mac_cpu_read(cpu, sp, 4), folder = mac_cpu_read(cpu, sp + 4, 4), spec = mac_cpu_read(cpu, sp + 10, 4);
    if (!was || !folder || !spec || !mac_files_valid(cpu, was, 1) || !mac_files_valid(cpu, folder, 1) || !mac_files_valid(cpu, spec, 70)) {
        mac_cpu_halt(cpu, "ResolveAliasFile pointer is invalid."); return -1;
    }
    char name[256];
    int error = mac_files_read_name(cpu, spec + 6, name, sizeof name);
    if (!error) {
        if (!rt->file_state) error = -35;
        else if (mac_files_classify(rt->file_state, name) == FILE_NONE) error = -43;
    }
    if (!error) { mac_cpu_write(cpu, was, 1, 0); mac_cpu_write(cpu, folder, 1, 0); }
    mac_cpu_write(cpu, sp + 14, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_A7, sp + 14);
    return 1;
}
