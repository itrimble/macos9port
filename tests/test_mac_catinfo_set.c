/* PBSetCatInfo (HFSDispatch selector 10): Finder info of a folder and of a file is stored in the save overlay and read back by PBGetCatInfo; the read-only
 * application data is never written. Headless; no SDL. */
#include "fsspec_test.h"

static int hfs(MacRuntime *rt, unsigned selector, uint32_t name_ptr, uint32_t dir) {
    w32(rt, PB + 18, name_ptr); w16(rt, PB + 22, 0); w16(rt, PB + 28, 0); w32(rt, PB + 48, dir);
    mac_cpu_set(rt->cpu, MAC_D0, selector);
    mac_cpu_set(rt->cpu, MAC_A0, PB);
    CHECK(mac_file_service(rt, 0xa260) == 1);
    return (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
}
static int get(MacRuntime *rt, const char *name, uint8_t out[16]) {
    memset(mac_cpu_ram(rt->cpu) + PB, 0, 128);
    pstr(rt, NAMEBUF, name);
    int e = hfs(rt, 9, NAMEBUF, 2);
    if (!e) memcpy(out, mac_cpu_ram(rt->cpu) + PB + 32, 16);
    return e;
}
static int set(MacRuntime *rt, const char *name, const uint8_t info[16]) {
    memset(mac_cpu_ram(rt->cpu) + PB, 0, 128);
    memcpy(mac_cpu_ram(rt->cpu) + PB + 32, info, 16);
    pstr(rt, NAMEBUF, name);
    return hfs(rt, 10, NAMEBUF, 2);
}

int main(void) {
    char data[] = "/tmp/ospi-setcat-data-XXXXXX", saves[] = "/tmp/ospi-setcat-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    host_write(data, "Locked File", "abc");
    char p[512]; snprintf(p, sizeof p, "%s/Data Folder", data); CHECK(mkdir(p, 0755) == 0);
    MacRuntime rt = fresh();
    CHECK(mac_files_root(&rt, data) == 0);
    uint8_t info[16] = {'T', 'E', 'X', 'T', 'q', 't', 'i', 'm', 0, 0x40, 0, 7, 0, 9, 0, 0}, back[16];

    /* No writable overlay: refused, as PBDirCreate is. */
    CHECK(set(&rt, "Data Folder", info) == -46);
    CHECK(mac_files_writable(&rt, saves, 50, 1 << 20) == 0);

    /* A folder made through the call keeps its Finder info. */
    pstr(&rt, NAMEBUF, "Made"); CHECK(hfs(&rt, 6, NAMEBUF, 2) == 0);
    CHECK(get(&rt, "Made", back) == 0 && !memcmp(back, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 15));
    CHECK(set(&rt, "Made", info) == 0);
    memset(back, 0xEE, 16);
    CHECK(get(&rt, "Made", back) == 0 && !memcmp(back, info, 16));

    /* A folder that exists only in the data volume is made in the overlay; the data one gains no metadata. */
    CHECK(set(&rt, "Data Folder", info) == 0);
    memset(back, 0xEE, 16);
    CHECK(get(&rt, "Data Folder", back) == 0 && !memcmp(back, info, 16));
    snprintf(p, sizeof p, "%s/Data Folder", saves); CHECK(access(p, F_OK) == 0);

    /* A file in the overlay takes type and creator; a file of the data volume is locked (fLckdErr); a missing item is fnfErr. */
    host_write(saves, "Plain", "xyz");
    CHECK(set(&rt, "Plain", info) == 0);
    memset(back, 0xEE, 16);
    CHECK(get(&rt, "Plain", back) == 0 && !memcmp(back, info, 16));
    CHECK(set(&rt, "Locked File", info) == -45);
    CHECK(set(&rt, "Nothing Here", info) == -43);

    mac_files_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", data, saves);
    if (system(cmd)) failures++;
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("PBSetCatInfo checks passed");
    return 0;
}
