/* Writable guest files (checklist L04): a contained save overlay on the
 * virtual data volume with staged atomic commits, quotas, symlink and
 * traversal defense, and restart recovery. Headless; no SDL. */
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
static int set_eof(MacRuntime *rt, int ref, uint32_t eof) {
    clear_pb(rt); put(rt, 24, 2, (uint16_t)ref); put(rt, 28, 4, eof); return call(rt, 0xa012);
}
static int rename_name(MacRuntime *rt, const char *from, const char *to) {
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

int main(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(4096);
    rt.heap_next = 0x800; rt.heap_limit = 0xe00;
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    char data[] = "/tmp/ospi-write-data-XXXXXX", saves[] = "/tmp/ospi-write-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    char path[1024];
    snprintf(path, sizeof path, "%s/Game Data", data);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "abcdef", 6) == 6 && close(fd) == 0);

    /* Read-only volume: write access and every mutation fail closed. */
    CHECK(mac_files_root(&rt, data) == 0);
    CHECK(open_name(&rt, "Game Data", 2) == -54);
    CHECK(open_name(&rt, "Game Data", 3) == -54);
    CHECK(named(&rt, 0xa008, "Pilot 1") == -46);
    CHECK(named(&rt, 0xa009, "Game Data") == -46);
    CHECK(rename_name(&rt, "Game Data", "Other") == -46);
    int ro = open_name(&rt, "Game Data", 1);
    CHECK(ro >= 128);
    CHECK(io(&rt, 0xa003, ro, 0, 0, "zz", 2) == -61);
    CHECK(set_eof(&rt, ro, 2) == -61);
    CHECK(host_size(data, "Game Data") == 6);
    CHECK(close_ref(&rt, ro) == 0);
    /* Saves directory must not alias the read-only data directory. */
    CHECK(mac_files_writable(&rt, data, 8, 1024) != 0);
    unmount(&rt);

    /* Restart recovery: a stale staging file from an interrupted save is discarded. */
    snprintf(path, sizeof path, "%s/.stage-Pilot 9", saves);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && close(fd) == 0);
    mount(&rt, data, saves, 3, 128);
    CHECK(!host_exists(saves, ".stage-Pilot 9"));

    /* Name policy: traversal, separators, hidden and overlong names are paramErr. */
    CHECK(named(&rt, 0xa008, "../escape") == -50);
    CHECK(named(&rt, 0xa008, "a/b") == -50);
    CHECK(named(&rt, 0xa008, ":Save1") == -50);
    CHECK(named(&rt, 0xa008, ".hidden") == -50);
    CHECK(named(&rt, 0xa008, "ThisNameIsDefinitelyOverThirtyOneChars") == -50);
    CHECK(named(&rt, 0xa008, "") == -50);
    CHECK(named(&rt, 0xa008, "Game Data") == -48); /* One namespace with the data volume. */
    CHECK(named(&rt, 0xa408, "Pilot 1") == -50); /* Async unsupported. */

    /* Create, duplicate, then an interrupted save leaves the committed file intact. */
    CHECK(named(&rt, 0xa008, "Pilot 1") == 0);
    CHECK(named(&rt, 0xa008, "Pilot 1") == -48);
    CHECK(host_size(saves, "Pilot 1") == 0);
    int w = open_name(&rt, "Pilot 1", 2);
    CHECK(w >= 128);
    CHECK(open_name(&rt, "Pilot 1", 2) == -49); /* Already open for writing. */
    int r = open_name(&rt, "Pilot 1", 1);
    CHECK(r >= 128 && r != w);
    CHECK(close_ref(&rt, r) == 0);
    CHECK(io(&rt, 0xa003, w, 0, 0, "SCORE=1000", 10) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 40, 4) == 10);
    CHECK(mac_cpu_read(rt.cpu, PB + 46, 4) == 10);
    clear_pb(&rt); put(&rt, 24, 2, (uint16_t)w); CHECK(call(&rt, 0xa011) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 28, 4) == 10); /* Staged length visible to the writer. */
    CHECK(host_size(saves, "Pilot 1") == 0);      /* Not yet committed. */
    CHECK(host_exists(saves, ".stage-Pilot 1"));
    CHECK(named(&rt, 0xa009, "Pilot 1") == -47);  /* Busy while open. */
    CHECK(rename_name(&rt, "Pilot 1", "Pilot X") == -47);
    unmount(&rt);                                  /* Simulated crash before close. */
    CHECK(host_size(saves, "Pilot 1") == 0);
    CHECK(!host_exists(saves, ".stage-Pilot 1"));

    /* Full save: write, close commits atomically, reopen reads the data back. */
    mount(&rt, data, saves, 3, 128);
    w = open_name(&rt, "Pilot 1", 3);
    CHECK(w >= 128);
    CHECK(io(&rt, 0xa003, w, 0, 0, "SCORE=1000", 10) == 0);
    CHECK(io(&rt, 0xa003, w, 1, 6, "2000", 4) == 0);
    CHECK(close_ref(&rt, w) == 0);
    CHECK(!host_exists(saves, ".stage-Pilot 1"));
    CHECK(host_size(saves, "Pilot 1") == 10);
    r = open_name(&rt, "Pilot 1", 0);
    CHECK(r >= 128);
    CHECK(io(&rt, 0xa002, r, 1, 0, NULL, 10) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + DATA, "SCORE=2000", 10));
    /* fsCurPerm on a save is writable; an edit followed by close updates it. */
    CHECK(io(&rt, 0xa003, r, 1, 0, "sc", 2) == 0);
    CHECK(close_ref(&rt, r) == 0);
    CHECK(host_size(saves, "Pilot 1") == 10);

    /* Positioning: SetFPos/GetFPos, past EOF clamps with eofErr. */
    r = open_name(&rt, "Pilot 1", 1);
    CHECK(r >= 128);
    CHECK(io(&rt, 0xa044, r, 1, 3, NULL, 0) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 46, 4) == 3);
    CHECK(io(&rt, 0xa018, r, 0, 0, NULL, 0) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 46, 4) == 3);
    CHECK(io(&rt, 0xa044, r, 3, 100, NULL, 0) == -39);
    CHECK(mac_cpu_read(rt.cpu, PB + 46, 4) == 10);
    CHECK(io(&rt, 0xa044, r, 1, -1, NULL, 0) == -40);
    CHECK(close_ref(&rt, r) == 0);

    /* Byte quota (128): oversize write and SetEOF are dskFulErr, nothing changes. */
    char big[200]; memset(big, 'X', sizeof big);
    w = open_name(&rt, "Pilot 1", 2);
    CHECK(w >= 128);
    CHECK(io(&rt, 0xa003, w, 1, 0, big, 200) == -34);
    CHECK(mac_cpu_read(rt.cpu, PB + 40, 4) == 0);
    CHECK(set_eof(&rt, w, 200) == -34);
    CHECK(set_eof(&rt, w, 64) == 0);
    CHECK(io(&rt, 0xa044, w, 1, 50, NULL, 0) == 0);
    CHECK(set_eof(&rt, w, 4) == 0);
    CHECK(io(&rt, 0xa018, w, 0, 0, NULL, 0) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 46, 4) == 4); /* Mark clamped to the new EOF. */
    CHECK(io(&rt, 0xa003, w, 1, 4, "ab", 2) == 0);
    CHECK(close_ref(&rt, w) == 0);
    CHECK(host_size(saves, "Pilot 1") == 6);

    /* File-count quota (3): the fourth save is dskFulErr. */
    CHECK(named(&rt, 0xa008, "Pilot 2") == 0);
    CHECK(named(&rt, 0xa008, "Pilot 3") == 0);
    CHECK(named(&rt, 0xa008, "Pilot 4") == -34);

    /* Symlinks and directories planted in the save folder are never followed. */
    snprintf(path, sizeof path, "%s/SymSave", saves);
    CHECK(symlink("/etc/hosts", path) == 0);
    snprintf(path, sizeof path, "%s/DirSave", saves);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(open_name(&rt, "SymSave", 1) == -54);
    CHECK(open_name(&rt, "SymSave", 2) == -54);
    CHECK(open_name(&rt, "DirSave", 2) == -54);
    CHECK(named(&rt, 0xa009, "SymSave") == -54);
    CHECK(named(&rt, 0xa008, "SymSave") == -48);
    CHECK(info(&rt, "SymSave", 0) == -43);
    CHECK(host_exists(saves, "SymSave"));

    /* Rename within the overlay only; data files are locked. */
    CHECK(rename_name(&rt, "Pilot 1", "Pilot A") == 0);
    CHECK(host_size(saves, "Pilot A") == 6 && !host_exists(saves, "Pilot 1"));
    CHECK(rename_name(&rt, "Pilot A", "Pilot 2") == -48);
    CHECK(rename_name(&rt, "Pilot A", "Game Data") == -48);
    CHECK(rename_name(&rt, "Pilot A", "../out") == -50);
    CHECK(rename_name(&rt, "Game Data", "Taken") == -45);
    CHECK(rename_name(&rt, "Missing", "Taken") == -43);
    CHECK(host_size(data, "Game Data") == 6);

    /* Finder info round-trips through SetFInfo/GetFInfo and survives a commit. */
    clear_pb(&rt); pstring(&rt, NAME, "Pilot A"); put(&rt, 18, 4, NAME); put(&rt, 22, 2, 0);
    memcpy(mac_cpu_ram(rt.cpu) + PB + 32, "SAVEMKY1\0\0\0\0\0\0\0\0", 16);
    CHECK(call(&rt, 0xa00d) == 0);
    clear_pb(&rt); pstring(&rt, NAME, "Game Data"); put(&rt, 18, 4, NAME); put(&rt, 22, 2, 0);
    CHECK(call(&rt, 0xa00d) == -45);
    w = open_name(&rt, "Pilot A", 2);
    CHECK(w >= 128 && io(&rt, 0xa003, w, 1, 0, "123456", 6) == 0 && close_ref(&rt, w) == 0);
    CHECK(info(&rt, "Pilot A", 0) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + PB + 32, "SAVEMKY1", 8));
    CHECK(mac_cpu_read(rt.cpu, PB + 30, 1) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 54, 4) == 6);
    CHECK(mac_cpu_read(rt.cpu, PB + 58, 4) == 512);
    CHECK(mac_cpu_read(rt.cpu, PB + 76, 4) != 0);
    CHECK(info(&rt, "Game Data", 0) == 0);
    CHECK(mac_cpu_read(rt.cpu, PB + 30, 1) == 0x01); /* Data files report locked. */
    CHECK(mac_cpu_read(rt.cpu, PB + 54, 4) == 6);
    CHECK(info(&rt, "Missing", 0) == -43);
    /* Indexed enumeration: sorted, data then saves, regular files only. */
    CHECK(info(&rt, NULL, 1) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + NAME, "\x09Game Data", 10));
    CHECK(info(&rt, NULL, 2) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + NAME, "\x07Pilot 2", 8));
    CHECK(info(&rt, NULL, 3) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + NAME, "\x07Pilot 3", 8));
    CHECK(info(&rt, NULL, 4) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + NAME, "\x07Pilot A", 8));
    CHECK(info(&rt, NULL, 5) == -43);

    /* Delete: open files are busy, data files locked, missing not found. */
    CHECK(named(&rt, 0xa009, "Game Data") == -45);
    CHECK(named(&rt, 0xa209, "Game Data") == -45);
    CHECK(named(&rt, 0xa009, "Missing") == -43);
    r = open_name(&rt, "Pilot 2", 1);
    CHECK(r >= 128 && named(&rt, 0xa009, "Pilot 2") == -47);
    CHECK(close_ref(&rt, r) == 0);
    CHECK(named(&rt, 0xa009, "Pilot 2") == 0);
    CHECK(!host_exists(saves, "Pilot 2"));
    clear_pb(&rt); pstring(&rt, NAME, "Pilot 3"); put(&rt, 18, 4, NAME); put(&rt, 22, 2, 0); put(&rt, 48, 4, 99);
    CHECK(call(&rt, 0xa209) == -120);
    put(&rt, 48, 4, 2); CHECK(call(&rt, 0xa209) == 0);
    CHECK(host_exists(data, "Game Data") && host_exists(saves, "Pilot A"));

    /* SetVol accepts only the mounted volume. */
    clear_pb(&rt); put(&rt, 18, 4, 0); put(&rt, 22, 2, 0); CHECK(call(&rt, 0xa015) == 0);
    put(&rt, 22, 2, 0xfffe); CHECK(call(&rt, 0xa015) == -35);
    pstring(&rt, NAME, "os pi data:"); put(&rt, 18, 4, NAME); put(&rt, 22, 2, 0xffff);
    CHECK(call(&rt, 0xa015) == 0);
    pstring(&rt, NAME, "Elsewhere:"); CHECK(call(&rt, 0xa015) == -35);

    /* Finder info and saves persist across a remount. */
    unmount(&rt);
    mount(&rt, data, saves, 3, 128);
    CHECK(info(&rt, "Pilot A", 0) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + PB + 32, "SAVEMKY1", 8));
    r = open_name(&rt, "Pilot A", 1);
    CHECK(r >= 128 && io(&rt, 0xa002, r, 1, 0, NULL, 6) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + DATA, "123456", 6));
    CHECK(close_ref(&rt, r) == 0);
    unmount(&rt);
    mac_cpu_destroy(rt.cpu);

    snprintf(path, sizeof path, "%s/Pilot A", saves); unlink(path);
    snprintf(path, sizeof path, "%s/SymSave", saves); unlink(path);
    snprintf(path, sizeof path, "%s/DirSave", saves); rmdir(path);
    CHECK(rmdir(saves) == 0);
    snprintf(path, sizeof path, "%s/Game Data", data); unlink(path);
    CHECK(rmdir(data) == 0);
    if (failures) { fprintf(stderr, "test_mac_files_write: %d failures\n", failures); return 1; }
    printf("Writable guest files: ok\n");
    return 0;
}
