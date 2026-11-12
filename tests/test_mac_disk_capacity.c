/* Guest view of the internal disk: PBHGetVInfo reports the defined capacity and
 * free space under the classic 16-bit block limit, under the desktop's disk name,
 * and a guest write that would pass capacity fails with dskFulErr (-34).
 * Headless; temporary directories and sparse files only, never the live volume. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "mac_runtime.h"
#include "mac_drive_queue.h"
#include "volume_capacity.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define MIB (UINT64_C(1) << 20)
enum { PB = 0x100, NAME = 0x300, DATA = 0x1000 };

static void put(MacRuntime *rt, unsigned offset, unsigned width, uint32_t v) { CHECK(!mac_cpu_write(rt->cpu, PB + offset, width, v)); }
static uint32_t get(MacRuntime *rt, unsigned offset, unsigned width) { return mac_cpu_read(rt->cpu, PB + offset, width); }
static int call(MacRuntime *rt, uint16_t op) {
    mac_cpu_set(rt->cpu, MAC_A0, PB);
    CHECK(mac_file_service(rt, op) == 1);
    return (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
}
static void pstring(MacRuntime *rt, unsigned at, const char *s) {
    size_t n = strlen(s);
    mac_cpu_ram(rt->cpu)[at] = (uint8_t)n;
    memcpy(mac_cpu_ram(rt->cpu) + at + 1, s, n);
}
static void clear_pb(MacRuntime *rt) { memset(mac_cpu_ram(rt->cpu) + PB, 0, 128); }
static int create(MacRuntime *rt, const char *name) {
    clear_pb(rt); pstring(rt, NAME, name); put(rt, 18, 4, NAME); put(rt, 22, 2, 0);
    return call(rt, 0xa008);
}
static int open_write(MacRuntime *rt, const char *name) {
    clear_pb(rt); pstring(rt, NAME, name);
    put(rt, 18, 4, NAME); put(rt, 22, 2, 0); put(rt, 27, 1, 2);
    int r = call(rt, 0xa000);
    return r ? r : (int16_t)get(rt, 24, 2);
}
static int write_at(MacRuntime *rt, int ref, int offset, int count) {
    clear_pb(rt);
    put(rt, 24, 2, (uint16_t)ref); put(rt, 32, 4, DATA); put(rt, 36, 4, (uint32_t)count);
    put(rt, 44, 2, 1); put(rt, 46, 4, (uint32_t)offset);
    return call(rt, 0xa003);
}
static int close_ref(MacRuntime *rt, int ref) { clear_pb(rt); put(rt, 24, 2, (uint16_t)ref); return call(rt, 0xa001); }
static int vinfo(MacRuntime *rt, uint32_t name_ptr) {
    clear_pb(rt); put(rt, 18, 4, name_ptr); put(rt, 22, 2, 0); put(rt, 28, 2, 0);
    return call(rt, 0xa207);
}
static void sparse(const char *dir, const char *name, uint64_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0);
    if (fd >= 0) { CHECK(ftruncate(fd, (off_t)size) == 0); close(fd); }
}

int main(void) {
    char parent[] = "/tmp/os999-guestdisk-XXXXXX";
    CHECK(mkdtemp(parent) != NULL);
    char root[1024], data[1024], saves[1024], path[1024];
    snprintf(root, sizeof root, "%s/Volume", parent);
    snprintf(data, sizeof data, "%s/Volume/Game", parent);
    snprintf(saves, sizeof saves, "%s/Saves", parent);
    CHECK(mkdir(root, 0700) == 0 && mkdir(data, 0700) == 0 && mkdir(saves, 0700) == 0);
    snprintf(path, sizeof path, "%s/Saves/Game", parent);
    sparse(data, "Game Data", 1024);

    /* A 64 MiB disk holding a bulk file that leaves roughly 8 KiB free. */
    char msg[200];
    CHECK(volume_capacity_set(root, 64 * MIB, msg, sizeof msg) == 0);
    sparse(root, "bulk", 64 * MIB - 8192 - 1024 - 64);

    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(65536);
    rt.heap_next = 0x800; rt.heap_limit = 0xe00;
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    CHECK(mac_files_root(&rt, data) == 0);
    CHECK(mac_files_writable(&rt, path, 8, UINT64_C(1) << 30) == 0);

    /* Before the disk is attached: old behaviour, read-only attribute only if no overlay (there is one). */
    CHECK(vinfo(&rt, 0) == 0 && get(&rt, 62, 2) == 0);

    CHECK(mac_files_disk(&rt, root) == 0);
    CHECK(vinfo(&rt, 0) == 0);
    uint32_t total = get(&rt, 46, 2), size = get(&rt, 48, 4), free_blocks = get(&rt, 62, 2);
    CHECK(total == 43690 && size == 1536);                       /* 64 MiB in at most 65535 blocks of 512-byte multiples */
    CHECK(free_blocks <= 6 && free_blocks <= total);             /* ~8 KiB free of 64 MiB */
    CHECK(get(&rt, 38, 2) == 0);                                 /* writable: the save overlay is mounted */
    uint8_t *ram = mac_cpu_ram(rt.cpu);
    CHECK(vinfo(&rt, NAME) == 0);
    CHECK(ram[NAME] == strlen(VOLUME_DISK_NAME) && !memcmp(ram + NAME + 1, VOLUME_DISK_NAME, strlen(VOLUME_DISK_NAME)));
    CHECK(!strcmp(VOLUME_DISK_NAME, "OS999 HD"));                /* what the desktop disk icon says (src/paint.c) */

    /* Writes: the first fits, the one that would pass capacity is refused with dskFulErr, a small one still fits. */
    CHECK(create(&rt, "Pilot 1") == 0);
    int ref = open_write(&rt, "Pilot 1");
    CHECK(ref >= 128);
    CHECK(write_at(&rt, ref, 0, 4096) == 0);
    CHECK(write_at(&rt, ref, 4096, 8192) == -34);
    struct stat st;
    snprintf(path, sizeof path, "%s/Saves/Game/.stage-Pilot 1", parent);
    CHECK(stat(path, &st) == 0 && st.st_size == 4096);           /* the refused write changed nothing */
    CHECK(write_at(&rt, ref, 4096, 2048) == 0);
    CHECK(close_ref(&rt, ref) == 0);
    snprintf(path, sizeof path, "%s/Saves/Game/Pilot 1", parent);
    CHECK(stat(path, &st) == 0 && st.st_size == 6144);

    /* Free space shown to the guest dropped; a disk this full cannot start another large file. */
    CHECK(vinfo(&rt, 0) == 0 && get(&rt, 62, 2) == 1);          /* about 2 KiB left: one 1536-byte block */
    CHECK(open_write(&rt, "Pilot 1") == -34);                    /* the staging copy of the 6 KiB file would not fit */

    mac_files_destroy(&rt);
    mac_drive_destroy(&rt);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("guest disk capacity checks passed");
    return 0;
}
