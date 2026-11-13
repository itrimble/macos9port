#include "mac_runtime.h"
#include <stdio.h>

/* Printing Manager for a Macintosh with no printer available (trap $A8FD, "PrGlue"). The selector is a long pushed after the arguments: its first byte
 * names the routine, its third byte is the argument size, and PrError, the Boolean routines and PrOpenDoc leave a result above the arguments. Selector
 * values are from the public Printing Manager chapter (read 12 November 2026, not copied).
 *
 * There is no printer driver here and nothing is ever printed. PrOpen reports an error in PrintErr ($944), which PrError returns: -4101 (the code the
 * chapter lists for "printer not found, closed or not selected"; the chapter does not say outright that this is what an unselected Chooser gives, so this is
 * the most conservative documented code, not an observed one). Every routine that needs the driver (PrintDefault, PrValidate, the two dialogs, PrJobMerge,
 * PrOpenDoc and the page and document calls) then leaves the caller's records untouched, shows no dialog, returns FALSE or nil, and sets the same error.
 * PrClose, PrDrvrClose, PrError and PrSetError never fail. The application is told the truth: nothing is faked, no page is spooled. */
#define NO_PRINTER (-4101)
#define PRINT_ERR 0x944
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
int mac_print_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa8fd) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7), sel = mac_cpu_read(cpu, sp, 4);
    unsigned routine = sel >> 24, args = (sel >> 8) & 0xff, result = 0;
    size_t z = mac_cpu_ram_size(cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 4u + args + 4u) return fail(rt, "Printing Manager call has an invalid stack.");
    int16_t error = NO_PRINTER;
    switch (routine) {
    case 0xc8: case 0x80: if (args) goto bad; break;                         /* PrOpen, PrDrvrOpen: no driver to open */
    case 0xd0: case 0x88: if (args) goto bad; error = 0; break;              /* PrClose, PrDrvrClose: nothing is open, nothing to fail */
    case 0xba: if (args) goto bad; result = 2; error = (int16_t)mac_cpu_read(cpu, PRINT_ERR, 2); break;   /* PrError */
    case 0xc0: if (args != 2) goto bad; error = (int16_t)mac_cpu_read(cpu, sp + 4, 2); break;             /* PrSetError(iErr) */
    case 0x20: case 0x58: case 0x08: case 0x18: case 0x10: case 0x60: break;    /* PrintDefault, PrJobMerge, PrCloseDoc, PrClosePage, PrOpenPage, PrPicFile */
    case 0x52: case 0x2a: case 0x32: result = 2; break;                      /* PrValidate, PrStlDialog, PrJobDialog: Boolean, FALSE */
    case 0x04: result = 4; break;                                            /* PrOpenDoc: a nil port */
    default: goto bad;
    }
    {
        static const struct { uint8_t routine, args; } sizes[] = {{0x20, 4}, {0x58, 8}, {0x08, 4}, {0x18, 4}, {0x10, 8}, {0x60, 20}, {0x52, 4}, {0x2a, 4}, {0x32, 4}, {0x04, 12}};
        for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) if (sizes[i].routine == routine && sizes[i].args != args) goto bad;
    }
    if (routine != 0xba) mac_cpu_write(cpu, PRINT_ERR, 2, (uint16_t)error);
    uint32_t top = sp + 4 + args;
    if (result == 2) mac_cpu_write(cpu, top, 2, routine == 0xba ? (uint16_t)error : 0);
    else if (result == 4) mac_cpu_write(cpu, top, 4, 0);
    mac_cpu_set(cpu, MAC_A7, top);
    return 1;
bad: {
        char why[96];
        snprintf(why, sizeof why, "Printing Manager selector %08X is unsupported.", (unsigned)sel);
        return fail(rt, why);
    }
}
