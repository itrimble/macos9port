/* Resource-fork sidecars on the save overlay (PBOpenRF/PBHOpenRF), CreateResFile, PBHGetVol/PBHSetVol, CmpString and ResolveAliasFile. */
#include "mac_runtime.h"
#include "mac_resource.h"
#include "mac_resource_chain.h"
int mac_fsspec_service(MacRuntime *rt, uint16_t opcode);
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
enum { PB = 0x100, NAME = 0x300, DATA = 0x500, RAM = 0x10000 };
static uint32_t rd(MacRuntime *r, uint32_t a, unsigned n) { const uint8_t *m = mac_cpu_ram(r->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | m[a + i]; return v; }
static void w(MacRuntime *r, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, a, n, v); }
static void pstr(MacRuntime *r, uint32_t at, const char *s) { mac_cpu_ram(r->cpu)[at] = (uint8_t)strlen(s); memcpy(mac_cpu_ram(r->cpu) + at + 1, s, strlen(s)); }
static int call(MacRuntime *r, uint16_t op) {
    mac_cpu_set(r->cpu, MAC_A0, PB); CHECK(mac_file_service(r, op) == 1);
    int res = (int16_t)rd(r, PB + 16, 2); CHECK((int32_t)mac_cpu_get(r->cpu, MAC_D0) == res); return res;
}
static int open_rf(MacRuntime *r, uint16_t op, const char *name, int perm, int vol) { pstr(r, NAME, name); memset(mac_cpu_ram(r->cpu) + PB, 0, 64); w(r, PB + 18, 4, NAME); w(r, PB + 22, 2, (uint16_t)vol); w(r, PB + 27, 1, perm); return call(r, op); }
static int create(MacRuntime *r, const char *name) { pstr(r, NAME, name); memset(mac_cpu_ram(r->cpu) + PB, 0, 64); w(r, PB + 18, 4, NAME); return call(r, 0xa008); }
static int write_ref(MacRuntime *r, int ref, const char *s) { memcpy(mac_cpu_ram(r->cpu) + DATA, s, strlen(s)); memset(mac_cpu_ram(r->cpu) + PB, 0, 64); w(r, PB + 24, 2, (uint16_t)ref); w(r, PB + 32, 4, DATA); w(r, PB + 36, 4, (uint32_t)strlen(s)); w(r, PB + 44, 2, 1); w(r, PB + 46, 4, 0); return call(r, 0xa003); }
static int read_ref(MacRuntime *r, int ref, unsigned n) { memset(mac_cpu_ram(r->cpu) + DATA, 0, 64); memset(mac_cpu_ram(r->cpu) + PB, 0, 64); w(r, PB + 24, 2, (uint16_t)ref); w(r, PB + 32, 4, DATA); w(r, PB + 36, 4, n); w(r, PB + 44, 2, 1); w(r, PB + 46, 4, 0); return call(r, 0xa002); }
static int close_ref(MacRuntime *r, int ref) { memset(mac_cpu_ram(r->cpu) + PB, 0, 64); w(r, PB + 24, 2, (uint16_t)ref); return call(r, 0xa001); }
static int create_res(MacRuntime *r, const char *name) { pstr(r, NAME, name); mac_cpu_set(r->cpu, MAC_A7, 0xf000); w(r, 0xf000, 4, NAME); w(r, 0xa60, 2, 0x5555); CHECK(mac_files_crf_service(r, 0xa9b1) == 1 && mac_cpu_get(r->cpu, MAC_A7) == 0xf004); return (int16_t)rd(r, 0xa60, 2); }
static int cmp(MacRuntime *r, uint16_t op, const char *a, const char *b, unsigned la, unsigned lb) {
    memcpy(mac_cpu_ram(r->cpu) + 0x1000, a, strlen(a)); memcpy(mac_cpu_ram(r->cpu) + 0x1100, b, strlen(b));
    mac_cpu_set(r->cpu, MAC_A0, 0x1000); mac_cpu_set(r->cpu, MAC_A1, 0x1100); mac_cpu_set(r->cpu, MAC_D0, la << 16 | lb);
    int rc = mac_cmpstring_service(r, op); return rc == 1 ? (int)mac_cpu_get(r->cpu, MAC_D0) : -9;
}
int main(void) {
    char data[] = "/tmp/ospi-rf-data-XXXXXX", saves[] = "/tmp/ospi-rf-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    char p[600]; snprintf(p, sizeof p, "%s/App", data); int fd = open(p, O_WRONLY | O_CREAT, 0600); CHECK(write(fd, "code", 4) == 4); close(fd);
    snprintf(p, sizeof p, "%s/App.rsrc", data); fd = open(p, O_WRONLY | O_CREAT, 0600); CHECK(write(fd, "FORK", 4) == 4); close(fd);
    MacRuntime r = {0}; r.cpu = mac_cpu_create(RAM); CHECK(r.cpu != NULL); r.heap_next = 0x3000; r.heap_limit = 0x8000;
    CHECK(mac_files_root(&r, data) == 0 && mac_files_writable(&r, saves, 8, 1 << 20) == 0);
    /* PBHOpenRF on a save-overlay file: the sidecar appears empty, behaves as a file, persists */
    CHECK(create(&r, "House") == 0);
    CHECK(open_rf(&r, 0xa20a, "House", 3, 0) == 0);
    int ref = (int16_t)rd(&r, PB + 24, 2); CHECK(ref >= 128);
    CHECK(write_ref(&r, ref, "resource bytes") == 0 && close_ref(&r, ref) == 0);
    snprintf(p, sizeof p, "%s/House.rsrc", saves); struct stat st; CHECK(stat(p, &st) == 0 && st.st_size == 14);
    CHECK(open_rf(&r, 0xa00a, "House", 1, -1) == 0); ref = (int16_t)rd(&r, PB + 24, 2);
    CHECK(read_ref(&r, ref, 14) == 0 && !memcmp(mac_cpu_ram(r.cpu) + DATA, "resource bytes", 14) && close_ref(&r, ref) == 0);                   /* PBOpenRF, read-only, same bytes */
    CHECK(open_rf(&r, 0xa20a, "Nothing", 1, 0) == -43);                                                                                          /* no such file */
    CHECK(open_rf(&r, 0xa20a, "House", 1, 7) == -35);                                                                                            /* another volume */
    CHECK(open_rf(&r, 0xa20a, "A Name That Is Rather Long.", 1, 0) == -50);                                                                       /* 27 + 5 > 31 */
    CHECK(create(&r, "AVeryLongHouseNameIsHere123") == 0 && open_rf(&r, 0xa20a, "AVeryLongHouseNameIsHere123", 1, 0) == -50);                    /* 27 + 5 > 31 */
    CHECK(open_rf(&r, 0xa20a, "AVeryLongHouseNameIsHer", 1, 0) == -43);                                                                           /* 23 chars is not too long: simply no such file */
    /* the application volume: its export opens read-only, never for writing; a file without an export is not found */
    CHECK(open_rf(&r, 0xa20a, "App", 1, 0) == 0); ref = (int16_t)rd(&r, PB + 24, 2); CHECK(read_ref(&r, ref, 4) == 0 && !memcmp(mac_cpu_ram(r.cpu) + DATA, "FORK", 4) && close_ref(&r, ref) == 0);
    CHECK(open_rf(&r, 0xa20a, "App", 3, 0) == -54);
    /* one writer per fork */
    CHECK(open_rf(&r, 0xa20a, "House", 3, 0) == 0); ref = (int16_t)rd(&r, PB + 24, 2); CHECK(open_rf(&r, 0xa20a, "House", 3, 0) == -49 && close_ref(&r, ref) == 0);
    /* CreateResFile: an empty resource fork in the real file layout, readable by this project's own parser */
    CHECK(create_res(&r, "Fresh") == 0);                                                                                                       /* creates the file and its fork */
    snprintf(p, sizeof p, "%s/Fresh", saves); CHECK(stat(p, &st) == 0 && st.st_size == 0);
    snprintf(p, sizeof p, "%s/Fresh.rsrc", saves); CHECK(stat(p, &st) == 0 && st.st_size == 286);
    {   uint8_t bytes[300]; fd = open(p, O_RDONLY); CHECK(fd >= 0 && read(fd, bytes, 300) == 286); close(fd);
        MacResourceFork fk; char err[160]; CHECK(mac_resource_open(&fk, bytes, 286, err, sizeof err) == 0); MacResource one; CHECK(mac_resource_at(&fk, 0, &one) != 0);   /* parses, holds no resources */
        CHECK(bytes[3] == 0 && bytes[2] == 1 && bytes[7] == 0 && bytes[6] == 1 && bytes[15] == 30 && !memcmp(bytes, bytes + 256, 16) && bytes[256 + 25] == 28 && bytes[256 + 29] == 0xff); }
    CHECK(create_res(&r, "Fresh") == -49);                                                                                                       /* a fork with bytes: left alone, dupFNErr in ResErr */
    CHECK(create_res(&r, "App") == -54);                                                                                                         /* on the application volume */
    CHECK(create_res(&r, "AVeryLongHouseNameIsHere123") == -50);
    CHECK(create_res(&r, "bad:name") == -50);
    CHECK(create_res(&r, "House") == -49);                                                                                                       /* House's fork already holds 14 bytes */
    {   MacRuntime q = {0}; q.cpu = mac_cpu_create(RAM); CHECK(create_res(&q, "X") == -35); mac_cpu_destroy(q.cpu); }                          /* no file system */
    /* FSpOpenResFile (HighLevelFSDispatch selector 13) opens the overlay sidecar as a Resource Manager file: a reference number in the result, ResError 0 */
    r.fork = (MacResourceFork){0};
    {   int16_t refs[3];
        for (unsigned k = 0; k < 3; k++) {
            const char *nm = k == 2 ? "Missing" : "Fresh"; memset(mac_cpu_ram(r.cpu) + 0x2000, 0, 80); w(&r, 0x2000, 2, 0); w(&r, 0x2002, 4, 2); pstr(&r, 0x2006, nm);
            mac_cpu_set(r.cpu, MAC_A7, 0xf000); mac_cpu_set(r.cpu, MAC_D0, 13); w(&r, 0xf000, 2, 0); w(&r, 0xf002, 4, 0x2000); w(&r, 0xf006, 2, 0x4444); w(&r, 0xa60, 2, 0x5555);
            CHECK(mac_fsspec_service(&r, 0xaa52) == 1 && mac_cpu_get(r.cpu, MAC_A7) == 0xf006); refs[k] = (int16_t)rd(&r, 0xf006, 2);
            if (k < 2) CHECK(refs[k] >= 256 && rd(&r, 0xa60, 2) == 0); else CHECK(refs[k] == -1 && (int16_t)rd(&r, 0xa60, 2) == -43);
        }
        CHECK(refs[0] == refs[1]);                                                                                                          /* opening it again returns the same reference */
        memset(mac_cpu_ram(r.cpu) + 0x2000, 0, 80); w(&r, 0x2002, 4, 1); pstr(&r, 0x2006, "OS999 HD");                                          /* parID 1: the volume root itself */
        mac_cpu_set(r.cpu, MAC_A7, 0xf000); mac_cpu_set(r.cpu, MAC_D0, 13); w(&r, 0xf000, 2, 0); w(&r, 0xf002, 4, 0x2000);
        CHECK(mac_fsspec_service(&r, 0xaa52) == 1 && (int16_t)rd(&r, 0xf006, 2) == -1 && (int16_t)rd(&r, 0xa60, 2) != 0); }
    mac_cpu_set(r.cpu, MAC_A7, RAM - 2); CHECK(mac_files_crf_service(&r, 0xa9b1) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);
    /* PBHGetVol / PBHSetVol: the one volume, root directory 2 */
    r = (MacRuntime){0}; r.cpu = mac_cpu_create(RAM); r.heap_next = 0x3000; r.heap_limit = 0x8000; CHECK(mac_files_root(&r, data) == 0);
    pstr(&r, NAME, ""); memset(mac_cpu_ram(r.cpu) + PB, 0, 64); w(&r, PB + 18, 4, NAME + 0x80); w(&r, PB + 48, 4, 0x77777777);
    CHECK(call(&r, 0xa214) == 0 && rd(&r, PB + 22, 2) == 0xffff && rd(&r, PB + 48, 4) == 2 && rd(&r, PB + 28, 4) == 0 && rd(&r, NAME + 0x80, 1) == 8);
    memset(mac_cpu_ram(r.cpu) + PB, 0, 64); w(&r, PB + 48, 4, 2); CHECK(call(&r, 0xa215) == 0);
    w(&r, PB + 48, 4, 0); CHECK(call(&r, 0xa215) == 0);
    w(&r, PB + 48, 4, 99); CHECK(call(&r, 0xa215) == -120);
    mac_files_destroy(&r); mac_cpu_destroy(r.cpu);
    /* CmpString: equal / not equal, case folding on by default and off with bit 9, lengths, range checks */
    r = (MacRuntime){0}; r.cpu = mac_cpu_create(RAM);
    CHECK(cmp(&r, 0xa03c, "Abc", "aBC", 3, 3) == 0 && (mac_cpu_get(r.cpu, MAC_SR) & 4) != 0);
    CHECK(cmp(&r, 0xa03c, "Abc", "abd", 3, 3) == 1 && (mac_cpu_get(r.cpu, MAC_SR) & 4) == 0);
    CHECK(cmp(&r, 0xa23c, "Abc", "aBC", 3, 3) == 1 && cmp(&r, 0xa23c, "Abc", "Abc", 3, 3) == 0);                                                /* case sensitive */
    CHECK(cmp(&r, 0xa03c, "Abc", "Abcd", 3, 4) == 1 && cmp(&r, 0xa03c, "", "", 0, 0) == 0 && cmp(&r, 0xa03c, "ab", "", 2, 0) == 1);
    CHECK(cmp(&r, 0xa03c, "A@", "a`", 2, 2) == 1);                                                                                                  /* only letters fold */
    CHECK(cmp(&r, 0xa03b, "a", "a", 1, 1) == -9 && cmp(&r, 0xa83c, "a", "a", 1, 1) == -9);                                                         /* neighbours / toolbox numbers are not ours */
    mac_cpu_set(r.cpu, MAC_A0, RAM - 2); mac_cpu_set(r.cpu, MAC_A1, 0x1100); mac_cpu_set(r.cpu, MAC_D0, 8u << 16 | 3); CHECK(mac_cmpstring_service(&r, 0xa03c) == -1 && mac_cpu_halted(r.cpu));
    mac_cpu_destroy(r.cpu);
    /* ResolveAliasFile: an existing file resolves to itself; a missing one is fnfErr; bad pointers halt */
    r = (MacRuntime){0}; r.cpu = mac_cpu_create(RAM); r.heap_next = 0x3000; r.heap_limit = 0x8000; CHECK(mac_files_root(&r, data) == 0);
    memset(mac_cpu_ram(r.cpu) + 0x2000, 0, 80); w(&r, 0x2000, 2, 0xffff); w(&r, 0x2002, 4, 2); pstr(&r, 0x2006, "App");
    mac_cpu_set(r.cpu, MAC_A7, 0xf000); mac_cpu_set(r.cpu, MAC_D0, 0x0000000c); w(&r, 0xf000, 4, 0x2100); w(&r, 0xf004, 4, 0x2102); w(&r, 0xf008, 2, 0x0100); w(&r, 0xf00a, 4, 0x2000); w(&r, 0xf00e, 2, 0x4444);
    w(&r, 0x2100, 1, 0x77); w(&r, 0x2102, 1, 0x77);
    CHECK(mac_alias_service(&r, 0xa823) == 1 && rd(&r, 0xf00e, 2) == 0 && rd(&r, 0x2100, 1) == 0 && rd(&r, 0x2102, 1) == 0 && mac_cpu_get(r.cpu, MAC_A7) == 0xf00e);
    pstr(&r, 0x2006, "Missing"); mac_cpu_set(r.cpu, MAC_A7, 0xf000); w(&r, 0x2100, 1, 0x77); CHECK(mac_alias_service(&r, 0xa823) == 1 && (int16_t)rd(&r, 0xf00e, 2) == -43 && rd(&r, 0x2100, 1) == 0x77);
    mac_cpu_set(r.cpu, MAC_A7, 0xf000); mac_cpu_set(r.cpu, MAC_D0, 0x0003); CHECK(mac_alias_service(&r, 0xa823) == -1 && mac_cpu_halted(r.cpu));                      /* another selector */
    mac_files_destroy(&r); mac_cpu_destroy(r.cpu);
    char cmd[700]; snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", data, saves); (void)!system(cmd);
    if (!failures) puts("Resource fork sidecar, CreateResFile, HGetVol, CmpString and alias checks passed");
    return failures ? 1 : 0;
}
