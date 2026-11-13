/* Mac Roman file names through the guest File Manager: lookup, create, rename, enumerate, FSSpec, case-insensitive matching, NFC and NFD host names,
 * hidden host names, refusals, and the 31-byte limit counted in Mac Roman. Headless; no SDL. */
#include "fsspec_test.h"
#include <dirent.h>

static const char TM_FOLDER[] = "QuickTime\252 Folder";                      /* guest bytes; $AA is the trade mark sign */
static const char TM_FOLDER_UTF8[] = "QuickTime\xE2\x84\xA2 Folder";           /* host spelling */

static int hfs(MacRuntime *rt, unsigned selector, uint32_t name_ptr, uint32_t dir, int index) {
    memset(mac_cpu_ram(rt->cpu) + PB, 0, 128);
    w32(rt, PB + 18, name_ptr); w16(rt, PB + 22, 0); w16(rt, PB + 28, (uint16_t)index); w32(rt, PB + 48, dir);
    mac_cpu_set(rt->cpu, MAC_D0, selector);
    mac_cpu_set(rt->cpu, MAC_A0, PB);
    CHECK(mac_file_service(rt, 0xa260) == 1);
    return (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
}
static int catinfo(MacRuntime *rt, const char *name, uint32_t dir) { pstr(rt, NAMEBUF, name); return hfs(rt, 9, NAMEBUF, dir, 0); }
static int dircreate(MacRuntime *rt, const char *name, uint32_t dir) { pstr(rt, NAMEBUF, name); return hfs(rt, 6, NAMEBUF, dir, 0); }
static int is_dir(MacRuntime *rt) { return (mac_cpu_read(rt->cpu, PB + 30, 1) & 0x10) != 0; }
/* The index-th entry of the directory; its name comes back as Mac Roman bytes in NAME2. */
static int nth(MacRuntime *rt, uint32_t dir, int index, char *out) {
    mac_cpu_ram(rt->cpu)[NAME2] = 0xEE;
    int e = hfs(rt, 9, NAME2, dir, index);
    if (e) return e;
    size_t n = mac_cpu_ram(rt->cpu)[NAME2];
    memcpy(out, mac_cpu_ram(rt->cpu) + NAME2 + 1, n); out[n] = 0;
    return 0;
}
static int host_has(const char *dir, const char *name) { return host_size(dir, name) >= 0; }
static int dir_entries(const char *dir, char names[][128], int max) {
    DIR *d = opendir(dir); int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) if (e->d_name[0] != '.' && n < max) snprintf(names[n++], 128, "%s", e->d_name);
    if (d) closedir(d);
    return n;
}
static int in_list(char names[][128], int n, const char *want) { for (int i = 0; i < n; i++) if (!strcmp(names[i], want)) return 1; return 0; }

int main(void) {
    char data[] = "/tmp/ospi-names-data-XXXXXX", saves[] = "/tmp/ospi-names-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    char big[128];
    host_write(data, "Game Data", "abcdef");
    host_write(data, "Cafe\xCC\x81 NFD", "decomposed");                       /* a host that stores NFD (HFS+) */
    host_write(data, "\xC3\x89TUDE", "precomposed upper");
    host_write(data, "\xE6\x97\xA5\xE6\x9C\xAC", "cjk");                         /* hidden: not Mac Roman */
    host_write(data, "smile \xF0\x9F\x99\x82", "emoji");                         /* hidden */
    int colon_ok = 1;
    { char p[512]; snprintf(p, sizeof p, "%s/bad:name", data); int fd = open(p, O_WRONLY | O_CREAT | O_EXCL, 0600); if (fd < 0) colon_ok = 0; else close(fd); }
    memset(big, 0, sizeof big);
    for (int i = 0; i < 32; i++) memcpy(big + 3 * i, "\xE2\x84\xA2", 3);
    host_write(data, big, "x");                                                  /* hidden: 32 Mac Roman bytes (96 UTF-8) */
    { char p[512], t[512]; snprintf(p, sizeof p, "%s/Link\xE2\x84\xA2", data); snprintf(t, sizeof t, "%s/Game Data", data); CHECK(symlink(t, p) == 0); }

    MacRuntime rt = fresh();
    CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves, 50, 1 << 20) == 0);

    /* The QuickTime installer's question: PBGetCatInfo of "QuickTime<tm> Folder" at the root is fnfErr, not paramErr; then PBDirCreate makes it. */
    CHECK(catinfo(&rt, TM_FOLDER, 2) == -43);
    CHECK(dircreate(&rt, TM_FOLDER, 2) == 0);
    CHECK(host_has(saves, TM_FOLDER_UTF8) && !host_has(data, TM_FOLDER_UTF8));   /* written to the overlay only, as UTF-8 */
    CHECK(catinfo(&rt, TM_FOLDER, 2) == 0 && is_dir(&rt));
    CHECK(catinfo(&rt, "QUICKTIME\252 FOLDER", 2) == 0 && is_dir(&rt));          /* case-insensitive */
    CHECK(dircreate(&rt, "quicktime\252 folder", 2) == -48);                     /* the same name in another case is a duplicate */
    CHECK(dircreate(&rt, "QuickTime\252 Folder:Sub\216", 2) == 0);               /* ':' separates guest components; e-acute in the leaf */
    CHECK(host_has(saves, "QuickTime\xE2\x84\xA2 Folder/Sub\xC3\xA9"));
    CHECK(catinfo(&rt, ":quicktime\252 folder:SUB\203", 2) == 0 && is_dir(&rt));
    CHECK(catinfo(&rt, "OS999 HD:QuickTime\252 Folder:Sub\216", 0) == 0);
    CHECK(dircreate(&rt, "a:b", 2) < 0);                                         /* a missing parent is an error, nothing is made */
    CHECK(!host_has(saves, "a"));

    /* Enumeration: Mac Roman names in the guest's buffer; names it cannot see are absent; links and the saves overlay are merged. */
    char name[300], seen[16][128]; int count = 0;
    for (int i = 1; i < 16; i++) {
        int e = nth(&rt, 2, i, name);
        if (e) { CHECK(e == -43); break; }
        snprintf(seen[count++], 128, "%s", name);
    }
    CHECK(in_list(seen, count, "Game Data") && in_list(seen, count, TM_FOLDER));
    CHECK(in_list(seen, count, "Caf\216 NFD"));                                  /* NFD on the host, e-acute for the guest */
    CHECK(in_list(seen, count, "\203TUDE"));
    CHECK(count == 4);                                                           /* no CJK, emoji, 32-byte, "bad:name" or symlink entries */

    /* NFD host file found by NFC, by another case, and by a partial path. */
    CHECK(make(&rt, -1, 2, "Caf\216 NFD", SPEC) == 0 && spec_is(&rt, SPEC, -1, 2, "Caf\216 NFD"));
    CHECK(make(&rt, -1, 2, "CAF\203 nfd", SPEC) == 0 && spec_is(&rt, SPEC, -1, 2, "Caf\216 NFD"));   /* the host's spelling, back in Mac Roman */
    CHECK(make(&rt, -1, 2, "\216tude", SPEC) == 0 && spec_is(&rt, SPEC, -1, 2, "\203TUDE"));
    { int ref = open_df(&rt, SPEC, 1); CHECK(ref >= 0 && pb_call(&rt, 0xa001, ref, 0, 0, 0) == 0); }
    CHECK(make(&rt, -1, 2, "Missing\252", SPEC) == -43 && spec_is(&rt, SPEC, -1, 2, "Missing\252"));

    /* Create, open, rename with Mac Roman names. */
    spec_of(&rt, SPEC, -1, 2, "New\252 File");
    CHECK(create(&rt, SPEC, 0x74747874, 0x54455854) == 0 && host_has(saves, "New\xE2\x84\xA2 File"));
    CHECK(create(&rt, SPEC, 0, 0) == -48);
    spec_of(&rt, SPEC2, -1, 2, "NEW\252 FILE");
    CHECK(create(&rt, SPEC2, 0, 0) == -48);                                      /* case-only difference is the same file */
    { int ref = open_df(&rt, SPEC2, 1); CHECK(ref >= 0 && pb_call(&rt, 0xa001, ref, 0, 0, 0) == 0); }
    pstr(&rt, NAME2, "R\216sum\216");
    spec_of(&rt, SPEC, -1, 2, "new\252 file");
    CHECK(rename_to(&rt, SPEC, NAME2) == 0 && host_has(saves, "R\xC3\xA9sum\xC3\xA9") && !host_has(saves, "New\xE2\x84\xA2 File"));
    pstr(&rt, NAME2, "r\216SUM\216");                                            /* a case-only rename keeps the new spelling */
    spec_of(&rt, SPEC, -1, 2, "R\216sum\216");
    CHECK(rename_to(&rt, SPEC, NAME2) == 0 && host_has(saves, "r\xC3\xA9SUM\xC3\xA9"));
    pstr(&rt, NAME2, "bad:name");
    CHECK(rename_to(&rt, SPEC, NAME2) == -37);
    pstr(&rt, NAME2, "bad/name");
    CHECK(rename_to(&rt, SPEC, NAME2) == -37);
    CHECK(!host_has(saves, "bad"));

    /* The same through the PB (non-FSSpec) calls: PBCreate and PBGetFInfo. */
    memset(mac_cpu_ram(rt.cpu) + PB, 0, 128);
    pstr(&rt, NAMEBUF, "Old\252 Style"); w32(&rt, PB + 18, NAMEBUF); w16(&rt, PB + 22, 0);
    mac_cpu_set(rt.cpu, MAC_A0, PB); CHECK(mac_file_service(&rt, 0xa008) == 1);
    CHECK((int16_t)mac_cpu_read(rt.cpu, PB + 16, 2) == 0 && host_has(saves, "Old\xE2\x84\xA2 Style"));
    memset(mac_cpu_ram(rt.cpu) + PB, 0, 128);
    pstr(&rt, NAMEBUF, "OLD\252 STYLE"); w32(&rt, PB + 18, NAMEBUF); w16(&rt, PB + 22, 0);
    mac_cpu_set(rt.cpu, MAC_A0, PB); CHECK(mac_file_service(&rt, 0xa00c) == 1);
    CHECK((int16_t)mac_cpu_read(rt.cpu, PB + 16, 2) == 0);

    /* Refusals: containment and traversal stay intact. */
    const char *bad[] = { "a/b", "a\\b", ".x", "..", ".", "a\001b", "a\177", "x/../../y", "" };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        spec_of(&rt, SPEC, -1, 2, bad[i]);
        CHECK(create(&rt, SPEC, 0, 0) < 0);
        pstr(&rt, NAMEBUF, bad[i]);
        memset(mac_cpu_ram(rt.cpu) + PB, 0, 128);
        w32(&rt, PB + 18, NAMEBUF);
        mac_cpu_set(rt.cpu, MAC_A0, PB); CHECK(mac_file_service(&rt, 0xa008) == 1);
        CHECK((int16_t)mac_cpu_read(rt.cpu, PB + 16, 2) < 0);
    }
    CHECK(make(&rt, -1, 2, "..:x", SPEC) < 0 && make(&rt, -1, 2, "a/b", SPEC) < 0);
    { uint8_t zero[] = { 3, 'a', 0, 'b' }; memcpy(mac_cpu_ram(rt.cpu) + NAMEBUF, zero, 4);
      memset(mac_cpu_ram(rt.cpu) + PB, 0, 128); w32(&rt, PB + 18, NAMEBUF); mac_cpu_set(rt.cpu, MAC_A0, PB);
      CHECK(mac_file_service(&rt, 0xa008) == 1 && (int16_t)mac_cpu_read(rt.cpu, PB + 16, 2) < 0); }   /* NUL inside a name */
    CHECK(make(&rt, -1, 2, "Link\252", SPEC) != 0 || open_df(&rt, SPEC, 1) < 0);    /* a symbolic link is never a file */
    CHECK(!host_has(saves, "Link\xE2\x84\xA2") && !host_has(saves, "x") && !host_has(saves, "y"));

    /* The limit is 31 Mac Roman bytes: 31 trade marks (93 UTF-8 bytes) are fine, 32 are not. */
    char thirty1[40], thirty2[40], host31[128];
    memset(thirty1, 0xAA, 31); thirty1[31] = 0;
    memset(thirty2, 0xAA, 32); thirty2[32] = 0;
    memset(host31, 0, sizeof host31);
    for (int i = 0; i < 31; i++) memcpy(host31 + 3 * i, "\xE2\x84\xA2", 3);
    spec_of(&rt, SPEC, -1, 2, thirty1);
    CHECK(create(&rt, SPEC, 0, 0) == 0 && host_has(saves, host31) && strlen(host31) == 93);
    CHECK(make(&rt, -1, 2, thirty1, SPEC) == 0);
    spec_of(&rt, SPEC2, -1, 2, thirty2);
    CHECK(create(&rt, SPEC2, 0, 0) == -37 && make(&rt, -1, 2, thirty2, SPEC) < 0);
    CHECK(dircreate(&rt, thirty2, 2) < 0);
    CHECK(dircreate(&rt, thirty1, 2) == -48);

    /* Nothing leaked out of the overlay, and the data folder gained nothing. */
    char listed[64][128];
    int n = dir_entries(data, listed, 64);
    CHECK(n == 7 + (colon_ok ? 1 : 0));   /* the host files made above, untouched */
    CHECK(!host_has(data, TM_FOLDER_UTF8) && !host_has(data, "R\xC3\xA9sum\xC3\xA9") && !host_has(data, host31));
    n = dir_entries(saves, listed, 64);
    CHECK(in_list(listed, n, TM_FOLDER_UTF8) && in_list(listed, n, "r\xC3\xA9SUM\xC3\xA9") && in_list(listed, n, "Old\xE2\x84\xA2 Style") && in_list(listed, n, host31));
    CHECK(!in_list(listed, n, "bad") && !in_list(listed, n, "a"));

    mac_files_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", data, saves);
    if (system(cmd)) failures++;
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("Mac Roman File Manager name checks passed");
    return 0;
}
