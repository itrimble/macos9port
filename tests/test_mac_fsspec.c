/* FSSpec File Manager calls (HighLevelFSDispatch, trap $AA52) and Gestalt 'fs  ': Gestalt answer, honest halts,
 * and the read-only volume (no --saves) including hostile names and pointers. Headless; no SDL. */
#include "fsspec_test.h"

static void gestalt(void) {
    MacRuntime rt = fresh();
    mac_cpu_set(rt.cpu, MAC_D0, 0x66732020);
    CHECK(mac_system_service(&rt, 0xa1ad) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 0);
    CHECK(mac_cpu_get(rt.cpu, MAC_A0) == 2); /* exactly gestaltHasFSSpecCalls (bit 1) */
    mac_cpu_destroy(rt.cpu);
}

static void unimplemented_halts(void) {
    static const struct { unsigned selector; const char *name; } cases[] = {
        {14, "FSpCreateResFile"}, {0, "selector 0"}, {16, "selector 16"}, {0x7fff, "unknown"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        MacRuntime rt = fresh();
        expect_halt(&rt, cases[i].selector, cases[i].name);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt = fresh();                       /* a stack that cannot hold the frame */
    mac_cpu_set(rt.cpu, MAC_D0, 1); mac_cpu_set(rt.cpu, MAC_A7, RAM - 8);
    CHECK(mac_fsspec_service(&rt, 0xaa52) == -1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
}

static void read_only_volume(const char *data) {
    MacRuntime rt = fresh();
    CHECK(make(&rt, -1, 2, "Game Data", SPEC) == -35);          /* no volume mounted */
    CHECK(mac_files_root(&rt, data) == 0);

    /* FSMakeFSSpec: forms of name, volume and directory. */
    CHECK(make(&rt, -1, 2, "Game Data", SPEC) == 0 && spec_is(&rt, SPEC, -1, 2, "Game Data"));
    CHECK(make(&rt, 0, 0, "Game Data", SPEC) == 0 && spec_is(&rt, SPEC, -1, 2, "Game Data"));
    CHECK(make(&rt, 99, 0, "OS999 HD:Game Data", SPEC) == 0);  /* absolute path ignores vRefNum */
    CHECK(make(&rt, -1, 2, "os999 hd:game data", SPEC) == 0);
    CHECK(make(&rt, -1, 2, ":Game Data", SPEC) == 0);
    CHECK(make(&rt, -1, 2, "Missing", SPEC) == -43 && spec_is(&rt, SPEC, -1, 2, "Missing")); /* usable spec */
    CHECK(make(&rt, -1, 2, "sub", SPEC) == 0);                  /* a host directory is a directory entry */
    CHECK(make(&rt, -1, 2, "link", SPEC) == -43);                /* links are never followed */
    CHECK(make(&rt, -1, 2, "", SPEC) == 0 && spec_is(&rt, SPEC, -1, 1, "OS999 HD"));
    CHECK(make(&rt, 0, 0, "", SPEC) == 0 && spec_is(&rt, SPEC, -1, 1, "OS999 HD"));
    CHECK(make(&rt, -1, 2, "OS999 HD:", SPEC) == 0 && spec_is(&rt, SPEC, -1, 1, "OS999 HD"));
    CHECK(make_at(&rt, -1, 2, 0, SPEC) == 0 && spec_is(&rt, SPEC, -1, 1, "OS999 HD")); /* NULL fileName */
    CHECK(make(&rt, -1, 2, "Other:Game Data", SPEC) == -35);
    CHECK(make(&rt, 7, 2, "Game Data", SPEC) == -35);
    CHECK(make(&rt, -2, 2, "Game Data", SPEC) == -35);              /* the System volume exists only when the runtime has one */
    CHECK(make(&rt, -1, 100, "Game Data", SPEC) == -120);          /* 3 to 66 are folder IDs (see the folders test); 100 is none */
    CHECK(make(&rt, -1, 1, "Game Data", SPEC) == -120);
    CHECK(make(&rt, -1, 2, "sub:file", SPEC) == -43);           /* "sub" is no volume but a folder here, so a partial path; the file is not there */
    CHECK(make(&rt, -1, 2, "Game Data:", SPEC) == -35);
    CHECK(make(&rt, -1, 2, ":sub:file", SPEC) == -43);          /* a missing last component is fnfErr */
    CHECK(make(&rt, -1, 2, ":Game Data:", SPEC) == -120);
    CHECK(make(&rt, -1, 2, "::Game Data", SPEC) == -120);
    CHECK(make(&rt, -1, 2, "OS999 HD:sub:x", SPEC) == -43);
    CHECK(make(&rt, -1, 2, "OS999 HD:nosuch:x", SPEC) == -120);   /* a missing earlier component is dirNFErr */
    CHECK(make(&rt, -1, 2, "../escape", SPEC) == -37);
    CHECK(make(&rt, -1, 2, "a/b", SPEC) == -37);
    CHECK(make(&rt, -1, 2, "a\\b", SPEC) == -37);
    CHECK(make(&rt, -1, 2, ".secret", SPEC) == -37);
    CHECK(make(&rt, -1, 2, "..", SPEC) == -37);
    CHECK(make(&rt, -1, 2, "bad\001name", SPEC) == -37);
    CHECK(make(&rt, -1, 2, "0123456789012345678901234567890123456789012345678901234567890123", SPEC) == -37); /* 64 */
    CHECK(make(&rt, -1, 2, "012345678901234567890123456789012345678901234567890123456789012", SPEC) == -37);  /* more than 31 characters is bdNamErr: HFS names stop at 31 */
    /* On failure the spec is cleared, not left stale. */
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    CHECK(make(&rt, -1, 2, "../x", SPEC) == -37 && mac_cpu_read(rt.cpu, SPEC + 2, 4) == 0);

    /* Hostile pointers: nothing is read or written outside RAM, the guard bytes stay intact. */
    memset(mac_cpu_ram(rt.cpu) + GUARD, 0x5a, 64);
    pstr(&rt, NAMEBUF, "Game Data");
    CHECK(make_at(&rt, -1, 2, NAMEBUF, 0) == -50);
    CHECK(make_at(&rt, -1, 2, NAMEBUF, SPEC + 1) == -50);            /* odd */
    CHECK(make_at(&rt, -1, 2, NAMEBUF, RAM - 69) == -50);            /* last byte out of RAM */
    CHECK(make_at(&rt, -1, 2, NAMEBUF, 0xfffffffe) == -50);
    CHECK(make_at(&rt, -1, 2, RAM, SPEC) == -50);
    CHECK(make_at(&rt, -1, 2, 0xffffffff, SPEC) == -50);
    mac_cpu_ram(rt.cpu)[RAM - 1] = 9;                               /* length byte promises 9, RAM ends */
    CHECK(make_at(&rt, -1, 2, RAM - 1, SPEC) == -50);
    {   /* The final spec that fits exactly is accepted. */
        pstr(&rt, NAMEBUF, "Game Data");
        CHECK(make_at(&rt, -1, 2, NAMEBUF, RAM - 70) == 0);
        CHECK(spec_is(&rt, RAM - 70, -1, 2, "Game Data"));
    }
    for (int i = 0; i < 64; i++) CHECK(mac_cpu_ram(rt.cpu)[GUARD + i] == 0x5a);

    /* Open, read through the ordinary PB path, close. */
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    CHECK(open_df(&rt, SPEC, 2) == -54);                           /* application data is read-only */
    CHECK(open_df(&rt, SPEC, 3) == -54);
    CHECK(open_df(&rt, SPEC, 9) == -50);
    int ref = open_df(&rt, SPEC, 1);
    CHECK(ref >= 128);
    CHECK(pb_call(&rt, 0xa002, ref, 1, 0, 6) == 0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + DATA, "abcdef", 6));
    CHECK(pb_call(&rt, 0xa003, ref, 1, 0, 2) == -61);              /* read-only access path */
    CHECK(pb_call(&rt, 0xa001, ref, 0, 0, 0) == 0);
    CHECK(open_df(&rt, SPEC, 0) >= 128);                            /* fsCurPerm on data: read */
    spec_of(&rt, SPEC, -1, 2, "Missing");
    CHECK(open_df(&rt, SPEC, 1) == -43);
    spec_of(&rt, SPEC, -1, 1, "OS999 HD");                         /* the root directory is not a file */
    CHECK(open_df(&rt, SPEC, 1) == -43);
    spec_of(&rt, SPEC, -1, 2, "sub");
    CHECK(open_df(&rt, SPEC, 1) == -43);
    spec_of(&rt, SPEC, -1, 100, "Game Data");                        /* unknown parent directory */
    CHECK(open_df(&rt, SPEC, 1) == -120);
    spec_of(&rt, SPEC, 5, 2, "Game Data");                           /* unknown volume */
    CHECK(open_df(&rt, SPEC, 1) == -35);
    spec_of(&rt, SPEC, -1, 2, "../escape");
    CHECK(open_df(&rt, SPEC, 1) == -37);
    spec_of(&rt, SPEC, -1, 2, "a:b");
    CHECK(open_df(&rt, SPEC, 1) == -37);
    spec_of(&rt, SPEC, -1, 2, "");
    CHECK(open_df(&rt, SPEC, 1) == -37);
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    mac_cpu_ram(rt.cpu)[SPEC + 6] = 64;                             /* Str63 length byte too large */
    CHECK(open_df(&rt, SPEC, 1) == -37);
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    w32(&rt, S, 0); mac_cpu_ram(rt.cpu)[S + 4] = 1; w32(&rt, S + 6, SPEC);   /* NULL refNum pointer */
    CHECK(dispatch(&rt, 2, 10) == -50);
    w32(&rt, S, RAM - 1); mac_cpu_ram(rt.cpu)[S + 4] = 1; w32(&rt, S + 6, SPEC); /* odd, partly outside RAM */
    CHECK(dispatch(&rt, 2, 10) == -50);
    w32(&rt, S, REFBUF); mac_cpu_ram(rt.cpu)[S + 4] = 1; w32(&rt, S + 6, RAM - 20); /* spec runs off RAM */
    CHECK(dispatch(&rt, 2, 10) == -50);

    /* Every mutation on the read-only volume fails closed with the documented error, after validation. */
    spec_of(&rt, SPEC, -1, 2, "Pilot 1");
    CHECK(create(&rt, SPEC, 0x74747874, 0x54455854) == -46);
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    CHECK(create(&rt, SPEC, 0, 0) == -46);
    CHECK(one_spec(&rt, 6, SPEC) == -46);
    spec_of(&rt, SPEC, -1, 2, "Missing");
    CHECK(one_spec(&rt, 6, SPEC) == -43);
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    pstr(&rt, NAME2, "Other");
    CHECK(rename_to(&rt, SPEC, NAME2) == -46);
    memset(mac_cpu_ram(rt.cpu) + INFO, 0, 16);
    CHECK(finfo(&rt, 8, SPEC, INFO) == -46);
    CHECK(one_spec(&rt, 9, SPEC) == -46 && one_spec(&rt, 10, SPEC) == -46);
    spec_of(&rt, SPEC2, -1, 2, "Elsewhere");
    w32(&rt, S, SPEC2); w32(&rt, S + 4, SPEC);
    CHECK(dispatch(&rt, 12, 8) == -46);                              /* FSpCatMove */
    CHECK(dispatch(&rt, 15, 8) == -46);                              /* FSpExchangeFiles */
    w32(&rt, S, REFBUF); w16(&rt, S + 4, 0); w32(&rt, S + 6, SPEC2);
    CHECK(dispatch(&rt, 5, 10) == -46);                              /* FSpDirCreate */
    spec_of(&rt, SPEC, -1, 1, "OS999 HD");
    CHECK(one_spec(&rt, 6, SPEC) == -46);
    CHECK(host_size(data, "Game Data") == 6);

    /* FSpGetFInfo on data files: allowed, a directory is not a file. */
    spec_of(&rt, SPEC, -1, 2, "Game Data");
    memset(mac_cpu_ram(rt.cpu) + INFO, 0xee, 16);
    CHECK(finfo(&rt, 7, SPEC, INFO) == 0);
    CHECK(finfo(&rt, 7, SPEC, 0) == -50);
    CHECK(finfo(&rt, 7, SPEC, RAM - 8) == -50);
    spec_of(&rt, SPEC, -1, 1, "OS999 HD");
    CHECK(finfo(&rt, 7, SPEC, INFO) == -43);
    spec_of(&rt, SPEC, -1, 2, "Missing");
    CHECK(finfo(&rt, 7, SPEC, INFO) == -43);
    mac_cpu_destroy(rt.cpu);
}

int main(void) {
    gestalt();
    unimplemented_halts();
    char data[] = "/tmp/ospi-fsspec-data-XXXXXX", saves[] = "/tmp/ospi-fsspec-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    char path[1024];
    host_write(data, "Game Data", "abcdef");
    host_write(data, ".secret", "x");
    snprintf(path, sizeof path, "%s/sub", data);
    CHECK(mkdir(path, 0700) == 0);
    snprintf(path, sizeof path, "%s/link", data);
    CHECK(symlink("/etc/hosts", path) == 0);
    read_only_volume(data);
    printf("FSSpec: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
