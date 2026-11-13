#include "mac_runtime.h"
#include "mac_files_int.h"

#include <stdio.h>
#include <string.h>

/* FindFolder, selector 0 of the same trap: FUNCTION FindFolder(vRefNum: INTEGER; folderType: OSType; createFolder: BOOLEAN; VAR foundVRefNum: INTEGER;
 * VAR foundDirID: LONGINT): OSErr. Stack from SP: foundDirID pointer, foundVRefNum pointer, the create flag as a byte in a word, the type, the volume, the
 * result. The data volume has no System Folder of its own: the folders live in the guest volume's namespace (the data folder merged with the save overlay),
 * placed as the public layout has them: the System Folder at the root with Preferences, Control Panels, Extensions, Apple Menu Items, Startup Items, Fonts and
 * Shutdown Items inside it; Temporary Items, Trash, Desktop Folder and PrintMonitor Documents at the root (type codes from the public Folders.h). A folder exists
 * if it was made earlier (by this call with createFolder, which also makes the System Folder above it, or by DirCreate), so one never made is fnfErr (-43), an
 * unknown folder type is fnfErr, another volume is nsvErr (-35), and creating one needs the save overlay: nothing is ever made outside it.
 * kOnSystemDisk ($8000), the default volume (0), the data volume (-1) and the System volume (-2) all mean this one namespace. */
static const struct { uint32_t type; const char *name; } FOLDERS[] = {
    {0x6d616373, "System Folder"}, {0x70726566, "System Folder:Preferences"}, {0x6374726c, "System Folder:Control Panels"}, {0x6578746e, "System Folder:Extensions"},
    {0x616d6e75, "System Folder:Apple Menu Items"}, {0x73747274, "System Folder:Startup Items"}, {0x666f6e74, "System Folder:Fonts"}, {0x73686466, "System Folder:Shutdown Items"},
    {0x74656d70, "Temporary Items"}, {0x74727368, "Trash"}, {0x6465736b, "Desktop Folder"}, {0x70726e74, "PrintMonitor Documents"},
};
static int find_folder(MacRuntime *rt) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if ((sp & 1) || !mac_files_valid(cpu, sp, 18)) { mac_cpu_halt(cpu, "FindFolder stack is invalid."); return -1; }
    uint32_t dir = mac_cpu_read(cpu, sp, 4), vol = mac_cpu_read(cpu, sp + 4, 4), type = mac_cpu_read(cpu, sp + 10, 4);
    int create = mac_cpu_read(cpu, sp + 8, 1) != 0, vref = (int16_t)mac_cpu_read(cpu, sp + 14, 2);
    if (!dir || !vol || !mac_files_valid(cpu, dir, 4) || !mac_files_valid(cpu, vol, 2)) { mac_cpu_halt(cpu, "FindFolder pointer is invalid."); return -1; }
    int error = -43;
    uint32_t id = 0;
    if (!rt->file_state || !(vref == -32768 || vref == 0 || vref == -1 || (vref == -2 && rt->system_volume))) error = -35;
    else for (size_t i = 0; i < sizeof FOLDERS / sizeof FOLDERS[0]; i++)
        if (FOLDERS[i].type == type) { error = mac_files_folder_find(rt, FOLDERS[i].name, create, &id); break; }
    if (!error) { mac_cpu_write(cpu, vol, 2, 0xffff); mac_cpu_write(cpu, dir, 4, id); }
    if (rt->trace) fprintf(stderr, "find-folder vref=%d type=%08x create=%d -> error=%d dirid=%u\n", vref, type, create, error, error ? 0u : id);
    mac_cpu_write(cpu, sp + 16, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_A7, sp + 16);
    return 1;
}

/* AliasDispatch ($A823), selector $000C: FUNCTION ResolveAliasFile(VAR theSpec: FSSpec; resolveAliasChains: BOOLEAN; VAR targetIsFolder, wasAliased:
 * BOOLEAN): OSErr (MPW listing; stack from SP: wasAliased pointer, targetIsFolder pointer, the chain flag as a byte in a word, the FSSpec pointer, then the
 * 2-byte result). This volume has no alias files, so a spec that names an existing file resolves to itself: wasAliased FALSE, targetIsFolder FALSE, noErr;
 * a spec whose file does not exist is fnfErr (-43). The FSSpec is vRefNum (2), parID (4), a Pascal name (63 bytes after its length byte) = 70 bytes; the name is
 * looked up in the flat namespace. Every selector other than 0 and $000C halts. */
int mac_alias_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa823) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t d0 = mac_cpu_get(cpu, MAC_D0) & 0xffff;
    if (d0 == 0) return find_folder(rt);
    if (d0 != 0x000c) {
        char msg[96]; snprintf(msg, sizeof msg, "AliasDispatch selector $%04X is unsupported (only FindFolder, 0, and ResolveAliasFile, $000C, are).", (unsigned)d0);
        mac_cpu_halt(cpu, msg); return -1;
    }
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
