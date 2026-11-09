/* FSSpec File Manager calls with a writable save overlay: create, open, rename, delete, Finder info, quotas, one
 * writer per file, name rules, and the selectors that still halt. Headless; no SDL. */
#include "fsspec_test.h"

enum { TEXT = 0x54455854, TTXT = 0x74747874, ROBO = 0x524f424f };

static int exists_in(const char *dir, const char *name) { return host_size(dir, name) >= 0; }
static int has_finfo(MacRuntime *rt, uint32_t type, uint32_t creator) {
    return mac_cpu_read(rt->cpu, INFO, 4) == type && mac_cpu_read(rt->cpu, INFO + 4, 4) == creator;
}
static int save_bytes(MacRuntime *rt, int ref, const char *bytes, uint32_t count) {
    memcpy(mac_cpu_ram(rt->cpu) + DATA, bytes, count);
    return pb_call(rt, 0xa003, ref, 1, 0, count);
}
static int close_ref(MacRuntime *rt, int ref) { return pb_call(rt, 0xa001, ref, 0, 0, 0); }

static void trap_path(MacRuntime *rt) {
    /* End to end through the trap dispatcher: $AA52 reaches the service and pops its Pascal frame. */
    mac_traps_init(rt);
    spec_of(rt, SPEC, -1, 2, "Game Data");
    w32(rt, S, SPEC);
    mac_cpu_set(rt->cpu, MAC_D0, 6); /* FSpDelete of a data file: fLckdErr with a save overlay */
    mac_cpu_set(rt->cpu, MAC_A7, S);
    CHECK(mac_runtime_trap(rt->cpu, 0xaa52, 0x400, rt) == 0);
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == S + 4);
    CHECK((int16_t)mac_cpu_read(rt->cpu, S + 4, 2) == -45);
}

static void halting_selectors(const char *data, const char *saves) {
    static const struct { unsigned selector; const char *name; } cases[] = {
        {9, "FSpSetFLock"}, {10, "FSpRstFLock"}, {12, "FSpCatMove"}, {15, "FSpExchangeFiles"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        MacRuntime rt = fresh();
        CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves, 8, 4096) == 0);
        spec_of(&rt, SPEC, -1, 2, "Game Data");
        spec_of(&rt, SPEC2, -1, 2, "Game Data");
        w32(&rt, S, SPEC); w32(&rt, S + 4, SPEC2);
        expect_halt(&rt, cases[i].selector, cases[i].name);
        mac_files_destroy(&rt);
        mac_cpu_destroy(rt.cpu);
    }
}

int main(void) {
    char data[] = "/tmp/ospi-fsspecw-data-XXXXXX", saves[] = "/tmp/ospi-fsspecw-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    host_write(data, "Game Data", "abcdef");
    MacRuntime rt = fresh();
    CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves, 3, 128) == 0);

    /* Create: name rules first, then duplicates; nothing may escape the overlay. */
    spec_of(&rt, SPEC, -1, 2, "Pilot 1");
    CHECK(create(&rt, SPEC, TTXT, TEXT) == 0);
    CHECK(host_size(saves, "Pilot 1") == 0);
    CHECK(create(&rt, SPEC, TTXT, TEXT) == -48);
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    CHECK(create(&rt, SPEC, 0, 0) == -48);                           /* one namespace with the data volume */
    spec_of(&rt, SPEC, -1, 1, "OS Pi Data");
    CHECK(create(&rt, SPEC, 0, 0) == -48);
    static const char *bad[] = {"../escape", "a/b", "a:b", ".hidden", "..", "", "ThisNameIsDefinitelyOverThirtyOneChars", "a\\b"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        spec_of(&rt, SPEC, -1, 2, bad[i]);
        CHECK(create(&rt, SPEC, 0, 0) == -37);
    }
    spec_of(&rt, SPEC, -1, 100, "Pilot 9");
    CHECK(create(&rt, SPEC, 0, 0) == -120);
    spec_of(&rt, SPEC, 3, 2, "Pilot 9");
    CHECK(create(&rt, SPEC, 0, 0) == -35);
    CHECK(create(&rt, 0, 0, 0) == -50 && create(&rt, RAM - 69, 0, 0) == -50 && create(&rt, SPEC + 1, 0, 0) == -50);
    CHECK(!exists_in(saves, "escape") && !exists_in("/tmp", "escape") && !exists_in(saves, "Pilot 9"));

    /* Finder info round trip (type and creator come from FSpCreate). */
    spec_of(&rt, SPEC, -1, 2, "Pilot 1");
    CHECK(finfo(&rt, 7, SPEC, INFO) == 0 && has_finfo(&rt, TEXT, TTXT));
    w32(&rt, INFO + 4, ROBO);
    CHECK(finfo(&rt, 8, SPEC, INFO) == 0);
    memset(mac_cpu_ram(rt.cpu) + INFO, 0, 16);
    CHECK(finfo(&rt, 7, SPEC, INFO) == 0 && has_finfo(&rt, TEXT, ROBO));
    spec_of(&rt, SPEC2, -1, 2, "Game Data");
    CHECK(finfo(&rt, 8, SPEC2, INFO) == -45);                        /* application data is locked */
    spec_of(&rt, SPEC2, -1, 2, "Missing");
    CHECK(finfo(&rt, 8, SPEC2, INFO) == -43);

    /* One writer per file; readers may share; commit happens at close. */
    int w = open_df(&rt, SPEC, 2);
    CHECK(w >= 128);
    CHECK(open_df(&rt, SPEC, 2) == -49 && open_df(&rt, SPEC, 3) == -49);
    int r = open_df(&rt, SPEC, 1);
    CHECK(r >= 128 && r != w);
    CHECK(save_bytes(&rt, w, "hello", 5) == 0);
    CHECK(host_size(saves, "Pilot 1") == 0);                         /* staged, not yet committed */
    CHECK(one_spec(&rt, 6, SPEC) == -47);                            /* busy */
    pstr(&rt, NAME2, "Pilot 2");
    CHECK(rename_to(&rt, SPEC, NAME2) == -47);
    CHECK(close_ref(&rt, w) == 0 && close_ref(&rt, r) == 0);
    CHECK(host_size(saves, "Pilot 1") == 5);
    CHECK(finfo(&rt, 7, SPEC, INFO) == 0 && has_finfo(&rt, TEXT, ROBO)); /* Finder info survives the commit */

    /* Byte quota (128): a write that would exceed it is rejected whole. */
    w = open_df(&rt, SPEC, 2);
    CHECK(w >= 128);
    char big[200]; memset(big, 'z', sizeof big);
    CHECK(save_bytes(&rt, w, big, 200) == -34);
    CHECK(close_ref(&rt, w) == 0 && host_size(saves, "Pilot 1") == 5);

    /* Rename: bad new names are bdNamErr, duplicates dupFNErr, data files and the volume are locked. */
    static const char *badnew[] = {"a:b", ".x", "a/b", "ThisNameIsDefinitelyOverThirtyOneChars", "", ".."};
    for (unsigned i = 0; i < sizeof badnew / sizeof *badnew; i++) {
        pstr(&rt, NAME2, badnew[i]);
        CHECK(rename_to(&rt, SPEC, NAME2) == -37);
    }
    pstr(&rt, NAME2, "Game Data");
    CHECK(rename_to(&rt, SPEC, NAME2) == -48);
    pstr(&rt, NAME2, "Pilot 2");
    CHECK(rename_to(&rt, SPEC, 0) == -50 && rename_to(&rt, SPEC, RAM) == -50);
    mac_cpu_ram(rt.cpu)[RAM - 1] = 7;
    CHECK(rename_to(&rt, SPEC, RAM - 1) == -50);
    spec_of(&rt, SPEC2, -1, 2, "Game Data");
    CHECK(rename_to(&rt, SPEC2, NAME2) == -45);
    spec_of(&rt, SPEC2, -1, 2, "Missing");
    CHECK(rename_to(&rt, SPEC2, NAME2) == -43);
    spec_of(&rt, SPEC2, -1, 1, "OS Pi Data");
    CHECK(rename_to(&rt, SPEC2, NAME2) == -46);
    CHECK(rename_to(&rt, SPEC, NAME2) == 0);
    CHECK(!exists_in(saves, "Pilot 1") && host_size(saves, "Pilot 2") == 5);
    CHECK(make(&rt, -1, 2, "Pilot 1", SPEC2) == -43 && make(&rt, -1, 2, "Pilot 2", SPEC2) == 0 && spec_is(&rt, SPEC2, -1, 2, "Pilot 2"));

    /* File-count quota (3): Pilot 2 plus two more, the fourth is dskFulErr; deleting frees a slot. */
    spec_of(&rt, SPEC, -1, 2, "Pilot 3");
    CHECK(create(&rt, SPEC, 0, 0) == 0);
    spec_of(&rt, SPEC, -1, 2, "Pilot 4");
    CHECK(create(&rt, SPEC, 0, 0) == 0);
    spec_of(&rt, SPEC, -1, 2, "Pilot 5");
    CHECK(create(&rt, SPEC, 0, 0) == -34 && !exists_in(saves, "Pilot 5"));
    spec_of(&rt, SPEC, -1, 2, "Pilot 3");
    CHECK(one_spec(&rt, 6, SPEC) == 0 && !exists_in(saves, "Pilot 3"));
    CHECK(one_spec(&rt, 6, SPEC) == -43);
    spec_of(&rt, SPEC, -1, 2, "Pilot 5");
    CHECK(create(&rt, SPEC, 0, 0) == 0);

    /* Delete: data files and the volume root cannot go. */
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    CHECK(one_spec(&rt, 6, SPEC) == -45 && host_size(data, "Game Data") == 6);
    spec_of(&rt, SPEC, -1, 1, "OS Pi Data");
    CHECK(one_spec(&rt, 6, SPEC) == -47);
    spec_of(&rt, SPEC, -1, 2, "../Game Data");
    CHECK(one_spec(&rt, 6, SPEC) == -37 && host_size(data, "Game Data") == 6);

    /* The same spec reaches the same file through FSMakeFSSpec and opens it. */
    CHECK(make(&rt, -1, 2, "Pilot 2", SPEC) == 0);
    w = open_df(&rt, SPEC, 0);                                       /* fsCurPerm on a save: read/write */
    CHECK(w >= 128 && pb_call(&rt, 0xa002, w, 1, 0, 5) == 0 && !memcmp(mac_cpu_ram(rt.cpu) + DATA, "hello", 5));
    CHECK(close_ref(&rt, w) == 0);
    trap_path(&rt);

    /* Out of file references. */
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    int refs[40], n = 0, last = 0;
    for (; n < 40; n++) { last = open_df(&rt, SPEC, 1); if (last < 128) break; refs[n] = last; }
    CHECK(n == 32 && last == -42);                                   /* tmfoErr */
    for (int i = 0; i < n; i++) CHECK(close_ref(&rt, refs[i]) == 0);

    /* The data directory is untouched; only whole, expected names live in the overlay. */
    CHECK(host_size(data, "Game Data") == 6 && !exists_in(data, "Pilot 1") && !exists_in(data, "Pilot 2"));
    mac_files_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    halting_selectors(data, saves);
    printf("FSSpec overlay: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
