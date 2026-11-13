#define _DARWIN_C_SOURCE 1
#include "mac_runtime.h"
#include "mac_files_int.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* CreateResFile ($A9B1): PROCEDURE CreateResFile(fileName: Str255) (MPW listing). Gives the named file on the writable save overlay an empty resource
 * fork: the file itself is created first if it does not exist, then its ".rsrc" sidecar (see mac_files_rf.c) is written with the Resource Manager's own
 * file layout for a file with no resources: a 256-byte header (data offset 256, map offset 256, data length 0, map length 30, zeros for the rest) followed
 * by the 30-byte map (a copy of the first 16 header bytes, next-map handle 0, file reference 0, attributes 0, type list offset 28, name list offset 30
 * and a type count of $FFFF, "minus one": no types). A fork that already has bytes is left alone (the real call changes nothing then, and sets ResErr to
 * dupFNErr). The result goes to ResErr (low memory $A60); the call itself has no result. The layout is from my memory of Inside Macintosh: More Macintosh
 * Toolbox; the test round-trips it through this project's own resource parser. A name on the read-only application volume, or one too long for the
 * ".rsrc" suffix, is refused (permErr / paramErr in ResErr). */
enum { EMPTY_FORK = 256 + 30 };
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xffff); }
void mac_files_empty_fork(uint8_t out[EMPTY_FORK]) {
    memset(out, 0, EMPTY_FORK);
    put32(out, 256); put32(out + 4, 256); put32(out + 8, 0); put32(out + 12, 30);
    memcpy(out + 256, out, 16);                 /* the map starts with a copy of the header */
    put16(out + 256 + 24, 28); put16(out + 256 + 26, 30); put16(out + 256 + 28, 0xffff);
}

static int create_fork(MacRuntime *rt, const char *name) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    if (s->saves < 0) return -46;
    if (strlen(name) + 5 >= DIRS_PATH_MAX || !mr_sidecar_ok(strrchr(name, '/') ? strrchr(name, '/') + 1 : name)) return -50;
    int kind = mac_files_classify(s, name);
    if (kind == FILE_DATA || kind == FILE_DATA_UNSUPPORTED || kind == FILE_SAVE_UNSUPPORTED) return -54;
    int error;
    if (kind == FILE_NONE && (error = mac_files_create_name(rt, name))) return error;
    char sidecar[DIRS_PATH_MAX];
    snprintf(sidecar, sizeof sidecar, "%s.rsrc", name);
    int sk = mac_files_classify(s, sidecar);
    if (sk == FILE_NONE && (error = mac_files_create_name(rt, sidecar))) return error;
    if (sk != FILE_NONE && sk != FILE_SAVE) return -54;
    if (mac_files_open_by_name(s, sidecar)) return -49;                      /* already open: opWrErr */
    int fd = mac_dirs_openat(s->saves, sidecar, O_RDWR, 0);
    if (fd < 0) return -36;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return -36; }
    if (st.st_size > 0) { close(fd); return -48; }                           /* has a fork already: dupFNErr, untouched */
    uint8_t fork[EMPTY_FORK]; mac_files_empty_fork(fork);
    ssize_t put = pwrite(fd, fork, sizeof fork, 0);
    if (put != (ssize_t)sizeof fork) { close(fd); return -36; }
    fsync(fd); close(fd);
    return 0;
}

int mac_files_crf_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9b1) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if ((sp & 1) || !mac_files_valid(cpu, sp, 4)) { mac_cpu_halt(cpu, "CreateResFile stack is invalid."); return -1; }
    char name[256];
    int error = mac_files_ptr_path(rt, mac_cpu_read(cpu, sp, 4), 0, 0, name);
    if (!error && !name[0]) error = -50;
    if (!error) error = create_fork(rt, name);
    mac_cpu_write(cpu, 0xa60, 2, (uint16_t)(error == -48 ? -49 : error));    /* ResErr */
    mac_cpu_set(cpu, MAC_A7, sp + 4);
    return 1;
}
