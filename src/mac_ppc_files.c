#include "mac_ppc.h"

#include <string.h>

/* High-level File Manager calls. On the classic system these are library glue: each fills a parameter block and
 * makes the PB trap. The same is done here, with the block built on the 68K stack and the trap entered through the
 * bridge, so the existing File Manager services do the work. Parameter-block offsets are public interface facts. */
enum { PB_BYTES = 128, IO_RESULT = 16, IO_NAME = 18, IO_VREF = 22, IO_REFNUM = 24, IO_PERM = 27, IO_MISC = 28,
       IO_BUFFER = 32, IO_REQ = 36, IO_ACT = 40, IO_POSMODE = 44, IO_POSOFF = 46, IO_DIRID = 48 };

typedef struct { MacRuntime *rt; uint32_t pb, a7; } Block;

static int pb_open(Block *b, void *user) {
    b->rt = user;
    b->a7 = mac_cpu_get(b->rt->cpu, MAC_A7);
    b->pb = (b->a7 - PB_BYTES) & ~UINT32_C(1);
    if (b->a7 < 0x2000 + PB_BYTES) return mac_ppc_fail(b->rt, "No 68K stack for a File Manager parameter block.");
    memset(mac_cpu_ram(b->rt->cpu) + b->pb, 0, PB_BYTES);
    mac_cpu_set(b->rt->cpu, MAC_A7, b->pb);
    return 0;
}

/* Makes the trap and returns its OSErr sign-extended; the block stays readable until pb_close. */
static uint32_t pb_call(Block *b, const char *name, uint16_t trap, const char *sig) {
    uint32_t args[1] = {b->pb}, result = 0;
    mac_ppc_invoke(b->rt, name, trap, sig, args, 1, &result);
    return result;
}

static void pb_close(Block *b) { mac_cpu_set(b->rt->cpu, MAC_A7, b->a7); }
static void put(Block *b, unsigned offset, unsigned width, uint32_t value) { mac_cpu_write(b->rt->cpu, b->pb + offset, width, value); }
static uint32_t get(Block *b, unsigned offset, unsigned width) { return mac_cpu_read(b->rt->cpu, b->pb + offset, width); }

static int fs_close(PpcCpu *c, void *user) {
    Block b;
    if (pb_open(&b, user)) return 0;
    put(&b, IO_REFNUM, 2, c->gpr[3] & 0xffffu);
    c->gpr[3] = pb_call(&b, "FSClose", 0xa001, "@A0>D0w");
    pb_close(&b);
    return 0;
}

static int transfer(PpcCpu *c, void *user, const char *name, uint16_t trap) { /* (refNum, &count, buffer) */
    Block b;
    uint32_t count = 0, at = c->gpr[4];
    if (!ppc_mem_read(c, at, 4, &count) || pb_open(&b, user)) return 0;
    put(&b, IO_REFNUM, 2, c->gpr[3] & 0xffffu);
    put(&b, IO_BUFFER, 4, c->gpr[5]);
    put(&b, IO_REQ, 4, count);
    c->gpr[3] = pb_call(&b, name, trap, "@A0>D0w");
    ppc_mem_write(c, at, 4, get(&b, IO_ACT, 4));
    pb_close(&b);
    return 0;
}
static int fs_read(PpcCpu *c, void *user) { return transfer(c, user, "FSRead", 0xa002); }
static int fs_write(PpcCpu *c, void *user) { return transfer(c, user, "FSWrite", 0xa003); }

static int get_eof(PpcCpu *c, void *user) { /* (refNum, &logEOF) */
    Block b;
    uint32_t at = c->gpr[4];
    if (pb_open(&b, user)) return 0;
    put(&b, IO_REFNUM, 2, c->gpr[3] & 0xffffu);
    c->gpr[3] = pb_call(&b, "GetEOF", 0xa011, "@A0>D0w");
    ppc_mem_write(c, at, 4, get(&b, IO_MISC, 4));
    pb_close(&b);
    return 0;
}

static int set_fpos(PpcCpu *c, void *user) { /* (refNum, posMode, posOff) */
    Block b;
    if (pb_open(&b, user)) return 0;
    put(&b, IO_REFNUM, 2, c->gpr[3] & 0xffffu);
    put(&b, IO_POSMODE, 2, c->gpr[4] & 0xffffu);
    put(&b, IO_POSOFF, 4, c->gpr[5]);
    c->gpr[3] = pb_call(&b, "SetFPos", 0xa044, "@A0>D0w");
    pb_close(&b);
    return 0;
}

static int get_fpos(PpcCpu *c, void *user) { /* (refNum, &filePos) */
    Block b;
    uint32_t at = c->gpr[4];
    if (pb_open(&b, user)) return 0;
    put(&b, IO_REFNUM, 2, c->gpr[3] & 0xffffu);
    c->gpr[3] = pb_call(&b, "GetFPos", 0xa018, "@A0>D0w");
    ppc_mem_write(c, at, 4, get(&b, IO_POSOFF, 4));
    pb_close(&b);
    return 0;
}

static int hopen(PpcCpu *c, void *user, const char *name, uint16_t trap, const char *sig) { /* (vRefNum, dirID, name, permission, &refNum) */
    Block b;
    uint32_t at = c->gpr[7];
    if (pb_open(&b, user)) return 0;
    put(&b, IO_VREF, 2, c->gpr[3] & 0xffffu);
    put(&b, IO_DIRID, 4, c->gpr[4]);
    put(&b, IO_NAME, 4, c->gpr[5]);
    put(&b, IO_PERM, 1, c->gpr[6] & 0xffu);
    c->gpr[3] = pb_call(&b, name, trap, sig);
    ppc_mem_write(c, at, 2, get(&b, IO_REFNUM, 2));
    pb_close(&b);
    return 0;
}
static int h_open(PpcCpu *c, void *user) { return hopen(c, user, "HOpen", 0xa200, "@A0>D0w"); }
static int h_open_df(PpcCpu *c, void *user) { return hopen(c, user, "HOpenDF", 0xa260, "@A0,D0=1A>D0w"); }
static int h_open_rf(PpcCpu *c, void *user) { return hopen(c, user, "HOpenRF", 0xa20a, "@A0>D0w"); }

static int get_vol(PpcCpu *c, void *user) { /* (volName, &vRefNum) */
    Block b;
    uint32_t at = c->gpr[4];
    if (pb_open(&b, user)) return 0;
    put(&b, IO_NAME, 4, c->gpr[3]);
    c->gpr[3] = pb_call(&b, "GetVol", 0xa014, "@A0>D0w");
    ppc_mem_write(c, at, 2, get(&b, IO_VREF, 2));
    pb_close(&b);
    return 0;
}

static int set_vol(PpcCpu *c, void *user) { /* (volName, vRefNum) */
    Block b;
    if (pb_open(&b, user)) return 0;
    put(&b, IO_NAME, 4, c->gpr[3]);
    put(&b, IO_VREF, 2, c->gpr[4] & 0xffffu);
    c->gpr[3] = pb_call(&b, "SetVol", 0xa015, "@A0>D0w");
    pb_close(&b);
    return 0;
}

#define F(name, fn) {"InterfaceLib", name, fn, NULL}
static const MacCfmHostImport imports[] = {
    F("FSClose", fs_close), F("FSRead", fs_read), F("FSWrite", fs_write), F("GetEOF", get_eof), F("SetFPos", set_fpos),
    F("GetFPos", get_fpos), F("HOpen", h_open), F("HOpenDF", h_open_df), F("HOpenRF", h_open_rf), F("GetVol", get_vol),
    F("SetVol", set_vol),
};

const MacCfmHostImport *mac_ppc_file_imports(size_t *count) {
    *count = sizeof imports / sizeof imports[0];
    return imports;
}
