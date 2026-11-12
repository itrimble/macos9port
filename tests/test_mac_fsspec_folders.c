/* Folders in the FSSpec calls: vRefNum -2 (the System volume), Mac Roman folder names, FSpDirCreate handing out a folder ID derived from the name, and files whose
 * parent is such an ID living under a "d<id>-" prefix in the one flat save namespace, apart from a root file of the same name. Headless; no SDL. */
#include "fsspec_test.h"
#include "mac_files_int.h"

static int dir_create(MacRuntime *rt, uint32_t spec, uint32_t out) { w32(rt, S, out); w16(rt, S + 4, 0); w32(rt, S + 6, spec); return dispatch(rt, 5, 10); }
static int save_bytes(MacRuntime *rt, int ref, const char *bytes, uint32_t count) { memcpy(mac_cpu_ram(rt->cpu) + DATA, bytes, count); return pb_call(rt, 0xa003, ref, 1, 0, count); }

int main(void) {
    char data[] = "/tmp/ospi-fold-data-XXXXXX", saves[] = "/tmp/ospi-fold-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    host_write(data, "Game Data", "abcdef");
    int sysvol;
    MacRuntime rt = fresh(); rt.system_volume = (struct MacSystemVolume *)&sysvol;
    CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves, 8, 65536) == 0);
    /* vRefNum -2 names the same flat namespace; without a System volume it is still nsvErr. */
    CHECK(make(&rt, -2, 2, "Game Data", SPEC) == 0 && spec_is(&rt, SPEC, -1, 2, "Game Data"));
    CHECK(make(&rt, -2, 2, "Missing", SPEC) == -43);
    { MacRuntime bare = fresh(); CHECK(mac_files_root(&bare, data) == 0); CHECK(make(&bare, -2, 2, "Game Data", SPEC) == -35); mac_cpu_destroy(bare.cpu); }
    /* A folder name with a Mac Roman character ("G-PRO Scores " + 0xC4) is carried through a spec but never a host name. */
    const char folder[] = "G-PRO Scores \xc4";
    CHECK(make(&rt, -2, 2, folder, SPEC) == -43 && spec_is(&rt, SPEC, -1, 2, folder));
    CHECK(make(&rt, -2, 2, "bad\xff", SPEC) == -37 && make(&rt, -2, 2, "bad\x01", SPEC) == -37 && make(&rt, -2, 2, "bad\x7f", SPEC) == -37);
    spec_of(&rt, SPEC, -1, 2, folder);
    CHECK(open_df(&rt, SPEC, 1) == -37);                                     /* anything that would touch the host refuses such a name */
    /* FSpDirCreate: a folder ID in 3..66, the same every time for a name, different names mostly different, never the root, not over a file, not nested. */
    w32(&rt, REFBUF, 0xdeadbeef);
    CHECK(dir_create(&rt, SPEC, REFBUF) == 0);
    uint32_t id = mac_cpu_read(rt.cpu, REFBUF, 4); CHECK(id >= 3 && id <= 66);
    w32(&rt, REFBUF, 0); CHECK(dir_create(&rt, SPEC, REFBUF) == 0 && mac_cpu_read(rt.cpu, REFBUF, 4) == id);
    spec_of(&rt, SPEC2, -1, 2, "Another folder"); CHECK(dir_create(&rt, SPEC2, REFBUF) == 0 && mac_cpu_read(rt.cpu, REFBUF, 4) >= 3 && mac_cpu_read(rt.cpu, REFBUF, 4) != id);   /* a different name, a different ID */
    spec_of(&rt, SPEC2, -1, 2, "Game Data"); w32(&rt, REFBUF, 0x77); CHECK(dir_create(&rt, SPEC2, REFBUF) == -48 && mac_cpu_read(rt.cpu, REFBUF, 4) == 0x77);   /* a file has the name */
    spec_of(&rt, SPEC2, -1, 1, "OS999 HD"); CHECK(dir_create(&rt, SPEC2, REFBUF) == -48);                                                                     /* the volume */
    spec_of(&rt, SPEC2, -1, id, "inner"); CHECK(dir_create(&rt, SPEC2, REFBUF) == -120);                                                                         /* no nesting */
    spec_of(&rt, SPEC2, -1, 2, "x"); CHECK(dir_create(&rt, SPEC2, 0x1000000 - 2) == -50 && dir_create(&rt, SPEC2, REFBUF + 1) == -50);                         /* a bad result pointer */
    /* Files in the folder: separate from a root file of the same name, listed by the host with the prefix, found again through FSMakeFSSpec. */
    spec_of(&rt, SPEC, -1, id, "Slumberland"); spec_of(&rt, SPEC2, -1, 2, "Slumberland");
    CHECK(create(&rt, SPEC, 0x6f7a6d35, 0x676c6948) == 0 && create(&rt, SPEC2, 0x6f7a6d35, 0x676c6948) == 0);   /* two files, one name, two parents */
    char host[32]; snprintf(host, sizeof host, "d%u-Slumberland", id);
    CHECK(host_size(saves, host) == 0 && host_size(saves, "Slumberland") == 0);
    int in = open_df(&rt, SPEC, 2), out = open_df(&rt, SPEC2, 2); CHECK(in >= 128 && out >= 128 && in != out);
    CHECK(save_bytes(&rt, in, "inside", 6) == 0 && save_bytes(&rt, out, "root!", 5) == 0);
    CHECK(pb_call(&rt, 0xa001, in, 0, 0, 0) == 0 && pb_call(&rt, 0xa001, out, 0, 0, 0) == 0);
    CHECK(host_size(saves, host) == 6 && host_size(saves, "Slumberland") == 5);
    CHECK(make(&rt, -2, id, "Slumberland", SPEC) == 0 && spec_is(&rt, SPEC, -1, id, "Slumberland"));            /* the spec keeps the folder */
    CHECK(make(&rt, -2, id, "Nothing", SPEC) == -43 && spec_is(&rt, SPEC, -1, id, "Nothing"));
    CHECK(make(&rt, -2, 100, "Slumberland", SPEC) == -120 && make(&rt, -2, 67, "Slumberland", SPEC) == -120);   /* IDs outside 3..66 are no directory */
    CHECK(make(&rt, -1, 2, "d3-x", SPEC) == -43);                                                              /* the prefix is only ever added, never read from a name */
    /* Rename and delete act inside the folder. */
    spec_of(&rt, SPEC, -1, id, "Slumberland"); pstr(&rt, NAME2, "Renamed");
    CHECK(rename_to(&rt, SPEC, NAME2) == 0);
    snprintf(host, sizeof host, "d%u-Renamed", id); CHECK(host_size(saves, host) == 6 && host_size(saves, "Slumberland") == 5);
    spec_of(&rt, SPEC, -1, id, "Renamed"); CHECK(one_spec(&rt, 6, SPEC) == 0 && host_size(saves, host) < 0 && host_size(saves, "Slumberland") == 5);
    /* The 31 character limit counts the host name, prefix included: "d<id>-" leaves less room in a folder. */
    size_t room = 31 - strlen(host) + strlen("Renamed");   /* host is "d<id>-Renamed": the prefix is its length minus 7 */
    char longname[40]; memset(longname, 'n', room); longname[room] = 0;
    spec_of(&rt, SPEC, -1, id, longname); CHECK(create(&rt, SPEC, 0x74747874, 0x54455854) == 0);
    memset(longname, 'n', room + 1); longname[room + 1] = 0; spec_of(&rt, SPEC, -1, id, longname); CHECK(create(&rt, SPEC, 0x74747874, 0x54455854) == -37);
    /* Folder files are not listed as root files: the indexed catalog lists the two root files and nothing else, however many folder files exist. */
    {   unsigned seen = 0; int found_root = 0, found_folder = 0;
        for (int index = 1; index < 20; index++) {
            memset(mac_cpu_ram(rt.cpu) + PB, 0, 128); w32(&rt, PB + 18, NAMEBUF); w16(&rt, PB + 22, 0xffff); w16(&rt, PB + 28, (uint16_t)index);
            mac_cpu_set(rt.cpu, MAC_A0, PB); mac_cpu_set(rt.cpu, MAC_D0, 9); CHECK(mac_file_service(&rt, 0xa260) == 1);
            if ((int16_t)mac_cpu_read(rt.cpu, PB + 16, 2) != 0) break;
            const uint8_t *nm = mac_cpu_ram(rt.cpu) + NAMEBUF; seen++;
            if (nm[0] == 11 && !memcmp(nm + 1, "Slumberland", 11)) found_root = 1;
            if (nm[1] == 'd' && nm[2] >= '0' && nm[2] <= '9') found_folder = 1;
        }
        CHECK(seen >= 2 && found_root && !found_folder); }
    CHECK(mac_files_in_folder("d3-x") && mac_files_in_folder("d66-x") && !mac_files_in_folder("d-x") && !mac_files_in_folder("d123-x") && !mac_files_in_folder("dx3-x") && !mac_files_in_folder("data"));
    snprintf(host, sizeof host, "d%u-%s", id, "Slumberland"); CHECK(host_size(saves, host) < 0);
    /* PBDirCreate (HFSDispatch 6): the same folders. ioNamePtr +18, ioVRefNum +22, ioDirID +48 in (parent) and out (the new folder's ID). */
    {   const char *nm = "Preferences"; pstr(&rt, NAMEBUF, nm);
        #define DIRCREATE(vref, parent, ptr) (memset(mac_cpu_ram(rt.cpu) + PB, 0, 128), w32(&rt, PB + 18, (ptr)), w16(&rt, PB + 22, (uint16_t)(vref)), w32(&rt, PB + 48, (parent)), \
            mac_cpu_set(rt.cpu, MAC_A0, PB), mac_cpu_set(rt.cpu, MAC_D0, 6), mac_file_service(&rt, 0xa260), (int16_t)mac_cpu_read(rt.cpu, PB + 16, 2))
        CHECK(DIRCREATE(-2, 2, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == mac_files_folder_id("Preferences") && mac_cpu_read(rt.cpu, PB + 48, 4) >= 3 && mac_cpu_read(rt.cpu, PB + 48, 4) <= 66);
        uint32_t pid = mac_cpu_read(rt.cpu, PB + 48, 4);
        CHECK(DIRCREATE(0, 0, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == pid);                       /* default volume, parent 0 = root; same ID again */
        spec_of(&rt, SPEC, -1, pid, "Prefs file"); CHECK(create(&rt, SPEC, 0x74747874, 0x54455854) == 0 && make(&rt, -1, pid, "Prefs file", SPEC2) == 0);   /* the ID is usable as a parent */
        CHECK(DIRCREATE(5, 2, NAMEBUF) == -35 && DIRCREATE(-2, 3, NAMEBUF) == -120 && DIRCREATE(-2, 99, NAMEBUF) == -120);
        pstr(&rt, NAMEBUF, "Game Data"); CHECK(DIRCREATE(-1, 2, NAMEBUF) == -48);                                  /* a file holds the name */
        pstr(&rt, NAMEBUF, "bad:name"); CHECK(DIRCREATE(-1, 2, NAMEBUF) == -37);
        pstr(&rt, NAMEBUF, "a/b"); CHECK(DIRCREATE(-1, 2, NAMEBUF) == -37);
        pstr(&rt, NAMEBUF, ""); CHECK(DIRCREATE(-1, 2, NAMEBUF) == -37);
        pstr(&rt, NAMEBUF, "this folder name is far too long!"); CHECK(DIRCREATE(-1, 2, NAMEBUF) == -37);
        pstr(&rt, NAMEBUF, "G-PRO Scores \xc4"); CHECK(DIRCREATE(-2, 2, NAMEBUF) == 0);                          /* Mac Roman characters are fine in a folder name */
        CHECK(DIRCREATE(-1, 2, 0) == -50 && DIRCREATE(-1, 2, 0x1000000 - 2) == -50);                              /* bad name pointers */
        mac_cpu_ram(rt.cpu)[NAMEBUF] = 200; CHECK(DIRCREATE(-1, 2, NAMEBUF) == -37 || DIRCREATE(-1, 2, NAMEBUF) == -50);   /* a length byte that is no name */
        MacRuntime bare = fresh(); bare.system_volume = NULL; CHECK(mac_files_root(&bare, data) == 0); pstr(&bare, NAMEBUF, "x"); w32(&bare, PB + 18, NAMEBUF);
        mac_cpu_set(bare.cpu, MAC_A0, PB); mac_cpu_set(bare.cpu, MAC_D0, 6); w16(&bare, PB + 22, 0xfffe); CHECK(mac_file_service(&bare, 0xa260) == 1 && (int16_t)mac_cpu_read(bare.cpu, PB + 16, 2) == -46);   /* no overlay */
        mac_cpu_destroy(bare.cpu);
        /* PBGetCatInfo by name of a made folder: a directory record; a file of the name wins; an unmade folder is not found. */
        #define PBCALL(op, sel, vref, parent, ptr) (memset(mac_cpu_ram(rt.cpu) + PB, 0, 128), w32(&rt, PB + 18, (ptr)), w16(&rt, PB + 22, (uint16_t)(vref)), w32(&rt, PB + 48, (parent)), \
            mac_cpu_set(rt.cpu, MAC_A0, PB), mac_cpu_set(rt.cpu, MAC_D0, (sel)), mac_file_service(&rt, (op)), (int16_t)mac_cpu_read(rt.cpu, PB + 16, 2))
        pstr(&rt, NAMEBUF, "Preferences");
        CHECK(PBCALL(0xa260, 9, -2, 0, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 30, 1) == 0x10 && mac_cpu_read(rt.cpu, PB + 48, 4) == pid && mac_cpu_read(rt.cpu, PB + 100, 4) == 2);
        CHECK(PBCALL(0xa260, 9, -1, 2, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 30, 1) == 0x10);
        pstr(&rt, NAMEBUF, "Never made"); CHECK(PBCALL(0xa260, 9, -2, 0, NAMEBUF) == -43);
        pstr(&rt, NAMEBUF, "Game Data"); CHECK(PBCALL(0xa260, 9, -2, 0, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 30, 1) != 0x10);   /* a file */
        pstr(&rt, NAMEBUF, "Preferences"); CHECK(PBCALL(0xa260, 9, -2, 33, NAMEBUF) != 0 || mac_cpu_read(rt.cpu, PB + 30, 1) != 0x10);   /* another parent: not this folder */
        pstr(&rt, NAMEBUF, "Both"); CHECK(DIRCREATE(-2, 2, NAMEBUF) == 0 && PBCALL(0xa260, 9, -2, 0, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 30, 1) == 0x10);
        CHECK(PBCALL(0xa008, 0, 0, 0, NAMEBUF) == 0 && PBCALL(0xa260, 9, -2, 0, NAMEBUF) == 0 && mac_cpu_read(rt.cpu, PB + 30, 1) != 0x10);   /* a file made later with the folder's name wins the lookup */
        /* Working directories: PBOpenWD (1) gives -(100 + id), PBGetWDInfo (7) and PBCloseWD (2) take it back; unmade folders and bad IDs are refused. */
        w32(&rt, PB + 28, 0);
        CHECK(PBCALL(0xa260, 1, -2, pid, 0) == 0 && (int16_t)mac_cpu_read(rt.cpu, PB + 22, 2) == -(int)(100 + pid));
        int wdref = (int16_t)mac_cpu_read(rt.cpu, PB + 22, 2);
        CHECK(PBCALL(0xa260, 7, wdref, 0, 0) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == pid);
        CHECK(PBCALL(0xa260, 2, wdref, 0, 0) == 0);
        { uint32_t other = pid == 3 ? 4 : 3; CHECK(PBCALL(0xa260, 1, -2, other, 0) == -120 && PBCALL(0xa260, 7, -(int)(100 + other), 0, 0) == -51); }
                CHECK(PBCALL(0xa260, 1, 5, pid, 0) == -35);
        PBCALL(0xa260, 1, -2, 100, 0); mac_cpu_set(rt.cpu, MAC_A0, PB); mac_cpu_set(rt.cpu, MAC_D0, 1); CHECK(mac_file_service(&rt, 0xa260) == 0);   /* an ID that is no folder is not ours: another service answers */
        /* Files through the WD reference: created, found, opened in the folder (host name d<id>-name), not in the root; an unmade WD is an unknown volume. */
        pstr(&rt, NAMEBUF, "Glider Prefs");
        CHECK(PBCALL(0xa008, 0, wdref, 0, NAMEBUF) == 0);
        char hn[48]; snprintf(hn, sizeof hn, "d%u-Glider Prefs", pid);
        CHECK(host_size(saves, hn) == 0 && host_size(saves, "Glider Prefs") < 0);
        CHECK(PBCALL(0xa000, 0, wdref, 0, NAMEBUF) == 0 && (int16_t)mac_cpu_read(rt.cpu, PB + 24, 2) >= 128);
        int wref = (int16_t)mac_cpu_read(rt.cpu, PB + 24, 2); CHECK(pb_call(&rt, 0xa001, wref, 0, 0, 0) == 0);
        CHECK(PBCALL(0xa000, 0, 0, 0, NAMEBUF) == -43);                                                         /* not in the root */
        CHECK(PBCALL(0xa008, 0, -(int)(100 + (pid == 3 ? 4 : 3)), 0, NAMEBUF) == -35);                           /* a WD that was never opened */
        CHECK(make(&rt, wdref, 0, "Glider Prefs", SPEC) == 0 && spec_is(&rt, SPEC, -1, pid, "Glider Prefs"));       /* FSMakeFSSpec with the WD reference: the name is in the folder */
        CHECK(make(&rt, -(int)(100 + (pid == 3 ? 4 : 3)), 0, "Glider Prefs", SPEC) == -43);   /* a WD range value of an unmade folder: just not found */
        CHECK(PBCALL(0xa009, 0, wdref, 0, NAMEBUF) == 0 && host_size(saves, hn) < 0);                           /* delete inside the folder */
        #undef PBCALL
        #undef DIRCREATE
    }
    mac_cpu_destroy(rt.cpu);
    printf("FSSpec folders: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
