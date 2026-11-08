/* Staged writes become visible before close: lookups, listings, read-only opens and FlushVol publish an open file's staging copy (mac_files_flush.c). */
#include "mac_runtime.h"
#include "mac_drive_queue.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
enum { PB = 0x100, NAME = 0x200, NAME2 = 0x300, DATA = 0x400 };

static void put(MacRuntime *rt, unsigned offset, unsigned width, uint32_t v) {
    CHECK(!mac_cpu_write(rt->cpu, PB + offset, width, v));
}
static int call(MacRuntime *rt, uint16_t op) {
    mac_cpu_set(rt->cpu, MAC_A0, PB);
    CHECK(mac_file_service(rt, op) == 1);
    int result = (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
    CHECK((int32_t)mac_cpu_get(rt->cpu, MAC_D0) == result);
    return result;
}
static void pstring(MacRuntime *rt, unsigned at, const char *s) {
    size_t n = strlen(s);
    mac_cpu_ram(rt->cpu)[at] = (uint8_t)n;
    memcpy(mac_cpu_ram(rt->cpu) + at + 1, s, n);
}
static void clear_pb(MacRuntime *rt) { memset(mac_cpu_ram(rt->cpu) + PB, 0, 128); }
static int named(MacRuntime *rt, uint16_t op, const char *name) {
    clear_pb(rt); pstring(rt, NAME, name); put(rt, 18, 4, NAME); put(rt, 22, 2, 0);
    return call(rt, op);
}
static int open_name(MacRuntime *rt, const char *name, int permission) {
    clear_pb(rt); pstring(rt, NAME, name);
    put(rt, 18, 4, NAME); put(rt, 22, 2, 0); put(rt, 27, 1, (uint32_t)permission);
    int r = call(rt, 0xa000);
    return r ? r : (int16_t)mac_cpu_read(rt->cpu, PB + 24, 2);
}
static int io(MacRuntime *rt, uint16_t op, int ref, int mode, int offset, const void *bytes, int count) {
    clear_pb(rt);
    if (bytes) memcpy(mac_cpu_ram(rt->cpu) + DATA, bytes, (size_t)count);
    put(rt, 24, 2, (uint16_t)ref); put(rt, 32, 4, DATA); put(rt, 36, 4, (uint32_t)count);
    put(rt, 44, 2, (uint16_t)mode); put(rt, 46, 4, (uint32_t)offset);
    return call(rt, op);
}
static int close_ref(MacRuntime *rt, int ref) { clear_pb(rt); put(rt, 24, 2, (uint16_t)ref); return call(rt, 0xa001); }
static __attribute__((unused)) int set_eof(MacRuntime *rt, int ref, uint32_t eof) {
    clear_pb(rt); put(rt, 24, 2, (uint16_t)ref); put(rt, 28, 4, eof); return call(rt, 0xa012);
}
static __attribute__((unused)) int rename_name(MacRuntime *rt, const char *from, const char *to) {
    clear_pb(rt); pstring(rt, NAME, from); pstring(rt, NAME2, to);
    put(rt, 18, 4, NAME); put(rt, 22, 2, 0); put(rt, 28, 4, NAME2);
    return call(rt, 0xa00b);
}
static int info(MacRuntime *rt, const char *name, int index) {
    clear_pb(rt);
    if (name) pstring(rt, NAME, name); else mac_cpu_ram(rt->cpu)[NAME] = 0;
    put(rt, 18, 4, NAME); put(rt, 22, 2, 0); put(rt, 28, 2, (uint16_t)index);
    return call(rt, 0xa00c);
}
/* PBHGetFInfo: like info() but through $A20C with a directory ID and the root volume as -1. */
static int hinfo(MacRuntime *rt, const char *name, int index, uint32_t dir, uint16_t op) {
    clear_pb(rt);
    if (name) pstring(rt, NAME, name); else mac_cpu_ram(rt->cpu)[NAME] = 0;
    put(rt, 18, 4, NAME); put(rt, 22, 2, 0xffff); put(rt, 28, 2, (uint16_t)index); put(rt, 48, 4, dir);
    return call(rt, op);
}
static long host_size(const char *dir, const char *name) {
    char path[1024]; struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return lstat(path, &st) ? -1 : (long)st.st_size;
}
static int host_exists(const char *dir, const char *name) { return host_size(dir, name) >= 0; }
static void mount(MacRuntime *rt, const char *data, const char *saves, unsigned files, uint64_t bytes) {
    CHECK(mac_files_root(rt, data) == 0);
    CHECK(mac_files_writable(rt, saves, files, bytes) == 0);
}
static void unmount(MacRuntime *rt) { mac_files_destroy(rt); mac_drive_destroy(rt); }

static uint32_t pb32(MacRuntime *rt, unsigned off) { return mac_cpu_read(rt->cpu, PB + off, 4); }
static long host_ino(const char *dir, const char *name) { char p[1024]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, name); return lstat(p, &st) ? -1 : (long)st.st_ino; }
int main(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(4096);
    rt.heap_next = 0x800; rt.heap_limit = 0xe00;
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    char data[] = "/tmp/ospi-flush-data-XXXXXX", saves[] = "/tmp/ospi-flush-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    mount(&rt, data, saves, 8, 100000);
    CHECK(named(&rt, 0xa008, "House") == 0);
    /* Finder info set on the committed file survives the commits (type 'gliH', creator 'ozm5'). */
    clear_pb(&rt); pstring(&rt, NAME, "House"); put(&rt, 18, 4, NAME); put(&rt, 22, 2, 0);
    put(&rt, 32, 4, 0x676c6948); put(&rt, 36, 4, 0x6f7a6d35); CHECK(call(&rt, 0xa00d) == 0);
    int w = open_name(&rt, "House", 2);
    CHECK(w >= 128);
    CHECK(io(&rt, 0xa003, w, 0, 0, "hello", 5) == 0);
    CHECK(host_size(saves, "House") == 0 && host_size(saves, ".stage-House") == 5);               /* nothing published yet */
    /* A lookup of the name publishes what was written: the length is 5, the committed file holds it, finder info is intact, the staging copy is a fresh copy. */
    CHECK(info(&rt, "House", 0) == 0 && pb32(&rt, 54) == 5 && pb32(&rt, 32) == 0x676c6948 && pb32(&rt, 36) == 0x6f7a6d35);
    CHECK(host_size(saves, "House") == 5 && host_size(saves, ".stage-House") == 5);
    long ino = host_ino(saves, "House");
    CHECK(info(&rt, "House", 0) == 0 && host_ino(saves, "House") == ino);                         /* nothing written since: not committed again */
    CHECK(io(&rt, 0xa003, w, 0, 0, " world", 6) == 0);                                            /* writing goes on at the mark, into the new staging copy */
    CHECK(host_size(saves, "House") == 5 && host_size(saves, ".stage-House") == 11);
    CHECK(hinfo(&rt, NULL, 1, 2, 0xa20c) == 0 && pb32(&rt, 54) == 11 && host_size(saves, "House") == 11);   /* an indexed listing publishes too */
    CHECK(io(&rt, 0xa003, w, 0, 0, "!", 1) == 0 && host_size(saves, "House") == 11);
    /* FlushVol publishes. */
    clear_pb(&rt); put(&rt, 22, 2, 0xffff); CHECK(call(&rt, 0xa013) == 0 && host_size(saves, "House") == 12);
    /* A read-only open of a file being written sees what was written. */
    CHECK(io(&rt, 0xa003, w, 0, 0, "?", 1) == 0);
    int r = open_name(&rt, "House", 1);
    CHECK(r >= 128 && host_size(saves, "House") == 13);
    {   char buf[16] = {0}; CHECK(io(&rt, 0xa002, r, 1, 0, NULL, 13) == 0); memcpy(buf, mac_cpu_ram(rt.cpu) + DATA, 13); CHECK(!memcmp(buf, "hello world!?", 13)); }
    CHECK(close_ref(&rt, r) == 0);
    /* Close commits the rest and leaves no staging copy. */
    CHECK(io(&rt, 0xa003, w, 0, 0, "#", 1) == 0);
    CHECK(close_ref(&rt, w) == 0 && host_size(saves, "House") == 14 && !host_exists(saves, ".stage-House"));
    CHECK(info(&rt, "House", 0) == 0 && pb32(&rt, 54) == 14 && pb32(&rt, 32) == 0x676c6948);
    /* Two writers at once, both published by one lookup; a vanished staging copy does not break the lookup. */
    CHECK(named(&rt, 0xa008, "A") == 0 && named(&rt, 0xa008, "B") == 0);
    int wa = open_name(&rt, "A", 2), wb = open_name(&rt, "B", 2);
    CHECK(wa >= 128 && wb >= 128 && wa != wb);
    CHECK(io(&rt, 0xa003, wa, 0, 0, "aaa", 3) == 0 && io(&rt, 0xa003, wb, 0, 0, "bbbb", 4) == 0);
    CHECK(info(&rt, "A", 0) == 0 && pb32(&rt, 54) == 3 && host_size(saves, "B") == 4);
    {   char p[1024]; snprintf(p, sizeof p, "%s/.stage-A", saves); CHECK(unlink(p) == 0); }       /* sabotage: A's staging copy disappears */
    CHECK(io(&rt, 0xa003, wa, 0, 0, "x", 1) == 0);                                                /* the open fd still writes (to an unlinked file) */
    CHECK(info(&rt, "B", 0) == 0 && pb32(&rt, 54) == 4 && host_size(saves, "A") == 3);            /* A cannot be published: its committed data stay as they were */
    CHECK(close_ref(&rt, wb) == 0);
    CHECK(close_ref(&rt, wa) != 0 || host_size(saves, "A") >= 3);                                 /* closing the sabotaged file is an error or leaves the old data, never damage */
    /* A file on the read-only data volume is never published or touched. */
    {   char p[1024]; snprintf(p, sizeof p, "%s/Game Data", data); int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(fd >= 0 && write(fd, "abcdef", 6) == 6 && close(fd) == 0); }
    CHECK(info(&rt, "Game Data", 0) == 0 && pb32(&rt, 54) == 6);
    unmount(&rt);
    mac_cpu_destroy(rt.cpu);
    if (failures) { fprintf(stderr, "%d checks failed\n", failures); return 1; }
    puts("Staged write publishing checks passed");
    return 0;
}
