/* The hierarchical File Manager over real host directories: directory IDs, PBGetCatInfo enumeration, DirCreate, partial paths, working directories, HFS file
 * calls with directory IDs, moves and renames, sidecars per directory, containment (traversal, symbolic links) and the overlay quotas. Headless; no SDL.
 * Runs against a data tree (read-only, "Textures/Wood", "Textures/Stone", ...) and a save overlay in temporary directories. */
#include "fsspec_test.h"
#include "mac_files_int.h"

enum { NAMEA = 0x1a00, NAMEB = 0x1b00 };
static void clear_pb(MacRuntime *rt) { memset(mac_cpu_ram(rt->cpu) + PB, 0, 128); mac_cpu_set(rt->cpu, MAC_A0, PB); }
static int pbcall(MacRuntime *rt, uint16_t op, unsigned selector) {
    mac_cpu_set(rt->cpu, MAC_A0, PB); mac_cpu_set(rt->cpu, MAC_D0, selector);
    CHECK(mac_file_service(rt, op) == 1);
    return (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
}
/* An HFS call with a name, volume and directory ID. */
static int hcall(MacRuntime *rt, uint16_t op, unsigned selector, const char *name, int vref, uint32_t dir) {
    clear_pb(rt);
    if (name) { pstr(rt, NAMEA, name); w32(rt, PB + 18, NAMEA); }
    w16(rt, PB + 22, (uint16_t)vref); w32(rt, PB + 48, dir);
    return pbcall(rt, op, selector);
}
static int catinfo(MacRuntime *rt, const char *name, int vref, uint32_t dir, int index) {
    clear_pb(rt);
    memset(mac_cpu_ram(rt->cpu) + NAMEA, 0, 48);
    if (name) pstr(rt, NAMEA, name);
    w32(rt, PB + 18, NAMEA); w16(rt, PB + 22, (uint16_t)vref); w16(rt, PB + 28, (uint16_t)index); w32(rt, PB + 48, dir);
    int e_ = pbcall(rt, 0xa260, 9); if (getenv("DBG")) fprintf(stderr, "catinfo(%s,%d,%u,%d)=%d\n", name ? name : "(nil)", vref, dir, index, e_); return e_;
}
static int is_dir_record(MacRuntime *rt) { return (mac_cpu_ram(rt->cpu)[PB + 30] & 0x10) != 0; }
static int out_name_is(MacRuntime *rt, const char *name) {
    const uint8_t *p = mac_cpu_ram(rt->cpu) + NAMEA;
    return p[0] == strlen(name) && !memcmp(p + 1, name, strlen(name));
}
static int dir_create(MacRuntime *rt, const char *name, int vref, uint32_t parent, uint32_t *id) {
    int e = hcall(rt, 0xa260, 6, name, vref, parent);
    *id = mac_cpu_read(rt->cpu, PB + 48, 4);
    return e;
}
static int hopen(MacRuntime *rt, const char *name, uint32_t dir, int perm) {
    clear_pb(rt); pstr(rt, NAMEA, name); w32(rt, PB + 18, NAMEA); w16(rt, PB + 22, 0); mac_cpu_ram(rt->cpu)[PB + 27] = (uint8_t)perm; w32(rt, PB + 48, dir);
    int e = pbcall(rt, 0xa200, 0);
    return e ? e : (int16_t)mac_cpu_read(rt->cpu, PB + 24, 2);
}
static int write_close(MacRuntime *rt, int ref, const char *bytes) {
    memcpy(mac_cpu_ram(rt->cpu) + DATA, bytes, strlen(bytes));
    int e = pb_call(rt, 0xa003, ref, 1, 0, (uint32_t)strlen(bytes));
    int c = pb_call(rt, 0xa001, ref, 0, 0, 0);
    return e ? e : c;
}
static void mkdirs(const char *root, const char *rel) {
    char path[1024], cmd[1100];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
    CHECK(system(cmd) == 0);
}
static int cat_find(MacRuntime *rt, uint32_t dir, const char *wanted) {   /* index of `wanted` in the directory's enumeration, or 0 */
    for (int i = 1; i < 200; i++) {
        if (catinfo(rt, NULL, 0, dir, i) != 0) return 0;
        if (out_name_is(rt, wanted)) return i;
    }
    return 0;
}

static char g_outside[1024];
static void session(const char *data, const char *saves, uint32_t *tex_id, uint32_t *wood_id) {
    MacRuntime rt = fresh();
    CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves, 6, 4096) == 0);

    /* The root: directory 2, parent 1, volume name; index < 0 names the directory itself. */
    CHECK(catinfo(&rt, NULL, 0, 2, -1) == 0 && is_dir_record(&rt) && mac_cpu_read(rt.cpu, PB + 48, 4) == 2 && mac_cpu_read(rt.cpu, PB + 100, 4) == 1 && out_name_is(&rt, "OS999 HD"));
    CHECK(mac_cpu_read(rt.cpu, PB + 52, 2) == 4);   /* Textures, Hostile, readme, Game Data: sidecar readme.rsrc is a fork, not an entry */

    /* A subfolder beside the application is visible: by name, with its directory ID, valence and parent. */
    CHECK(catinfo(&rt, "Textures", 0, 2, 0) == 0 && is_dir_record(&rt));
    *tex_id = mac_cpu_read(rt.cpu, PB + 48, 4);
    CHECK(*tex_id > 2 && mac_cpu_read(rt.cpu, PB + 100, 4) == 2 && mac_cpu_read(rt.cpu, PB + 52, 2) == 4);
    CHECK(catinfo(&rt, "textures", 0, 2, 0) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == *tex_id);   /* HFS names ignore case; the host spelling wins */
    CHECK(catinfo(&rt, NULL, 0, *tex_id, -1) == 0 && out_name_is(&rt, "Textures") && mac_cpu_read(rt.cpu, PB + 100, 4) == 2);

    /* Index enumeration, files and directories alike, in name order; past the end is fnfErr. */
    CHECK(catinfo(&rt, NULL, 0, *tex_id, 1) == 0 && out_name_is(&rt, "Marble.pct") && !is_dir_record(&rt) && mac_cpu_read(rt.cpu, PB + 54, 4) == 6);
    CHECK(mac_cpu_read(rt.cpu, PB + 100, 4) == *tex_id);   /* a file's ioFlParID is its directory */
    CHECK(catinfo(&rt, NULL, 0, *tex_id, 2) == 0 && out_name_is(&rt, "Oak.pct"));
    CHECK(catinfo(&rt, NULL, 0, *tex_id, 3) == 0 && out_name_is(&rt, "Stone") && is_dir_record(&rt));
    CHECK(catinfo(&rt, NULL, 0, *tex_id, 4) == 0 && out_name_is(&rt, "Wood") && is_dir_record(&rt));
    *wood_id = mac_cpu_read(rt.cpu, PB + 48, 4);
    CHECK(catinfo(&rt, NULL, 0, *tex_id, 5) == -43);
    CHECK(catinfo(&rt, NULL, 0, 99999, 1) == -120);                     /* an ID never handed out */
    CHECK(cat_find(&rt, 2, "readme.rsrc") == 0 && cat_find(&rt, 2, "readme") != 0);   /* the sidecar is not an entry of its own */

    /* Nested: a second level. Partial paths with colons, from the root and from a directory; "::" goes up. */
    CHECK(catinfo(&rt, ":Textures:Wood:Birch.pct", 0, 2, 0) == 0 && !is_dir_record(&rt) && mac_cpu_read(rt.cpu, PB + 54, 4) == 5 && mac_cpu_read(rt.cpu, PB + 100, 4) == *wood_id);
    CHECK(catinfo(&rt, "Wood:Birch.pct", 0, *tex_id, 0) == 0 && mac_cpu_read(rt.cpu, PB + 54, 4) == 5);
    CHECK(catinfo(&rt, "OS999 HD:Textures:Wood", 77, 2, 0) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == *wood_id);
    CHECK(catinfo(&rt, "::Oak.pct", 0, *wood_id, 0) == 0 && mac_cpu_read(rt.cpu, PB + 54, 4) == 3);
    CHECK(catinfo(&rt, ":Nope:x", 0, 2, 0) == -120 && catinfo(&rt, ":Textures:Nope", 0, 2, 0) == -43);
    CHECK(catinfo(&rt, "Elsewhere:x", 0, 2, 0) == -35);               /* neither this volume nor a folder here */
    CHECK(catinfo(&rt, ":Textures:Oak.pct:", 0, 2, 0) == -120);       /* a trailing colon asks for a directory */

    /* HGetFInfo and HOpen take the directory ID; plain GetFInfo index counts files only. */
    CHECK(hcall(&rt, 0xa20c, 0, "Birch.pct", 0, *wood_id) == 0 && mac_cpu_read(rt.cpu, PB + 54, 4) == 5);
    CHECK(hcall(&rt, 0xa20c, 0, "Birch.pct", 0, *tex_id) == -43);
    clear_pb(&rt); w32(&rt, PB + 18, NAMEA); mac_cpu_ram(rt.cpu)[NAMEA] = 0; w16(&rt, PB + 28, 2); w32(&rt, PB + 48, *tex_id);
    CHECK(pbcall(&rt, 0xa20c, 0) == 0 && out_name_is(&rt, "Oak.pct"));
    clear_pb(&rt); w16(&rt, PB + 28, 3); w32(&rt, PB + 18, NAMEA); w32(&rt, PB + 48, *tex_id);
    CHECK(pbcall(&rt, 0xa20c, 0) == -43);                              /* only two files here: Stone and Wood are directories */
    int r = hopen(&rt, "Birch.pct", *wood_id, 0);
    CHECK(r >= 128);
    /* PBGetFCBInfo: the name and the parent directory ID of an open file, by reference and by index; the application's own resource file is the file it started from. */
    clear_pb(&rt); memset(mac_cpu_ram(rt.cpu) + NAMEA, 0, 48); w32(&rt, PB + 18, NAMEA); w16(&rt, PB + 24, (uint16_t)r);
    CHECK(pbcall(&rt, 0xa260, 8) == 0 && out_name_is(&rt, "Birch.pct") && mac_cpu_read(rt.cpu, PB + 58, 4) == *wood_id && mac_cpu_read(rt.cpu, PB + 40, 4) == 5);
    clear_pb(&rt); memset(mac_cpu_ram(rt.cpu) + NAMEA, 0, 48); w32(&rt, PB + 18, NAMEA); w16(&rt, PB + 28, 1);
    CHECK(pbcall(&rt, 0xa260, 8) == 0 && (int16_t)mac_cpu_read(rt.cpu, PB + 24, 2) == r && out_name_is(&rt, "Birch.pct"));
    mac_files_set_app(&rt, "Some App");
    CHECK(mac_cpu_read(rt.cpu, 0x910, 1) == 8 && !memcmp(mac_cpu_ram(rt.cpu) + 0x911, "Some App", 8));   /* ApName, the Str31 an installer builds its own FSSpec from */
    mac_files_set_app_path(&rt, "/x/y/Other.rsrc");
    CHECK(mac_cpu_read(rt.cpu, 0x910, 1) == 5 && !memcmp(mac_cpu_ram(rt.cpu) + 0x911, "Other", 5));
    mac_files_set_app(&rt, "Some App");
    clear_pb(&rt); memset(mac_cpu_ram(rt.cpu) + NAMEA, 0, 48); w32(&rt, PB + 18, NAMEA); w16(&rt, PB + 24, 1);
    CHECK(pbcall(&rt, 0xa260, 8) == 0 && out_name_is(&rt, "Some App") && mac_cpu_read(rt.cpu, PB + 58, 4) == 2);
    memset(mac_cpu_ram(rt.cpu) + DATA, 0, 8);
    CHECK(pb_call(&rt, 0xa002, r, 1, 0, 5) == 0 && !memcmp(mac_cpu_ram(rt.cpu) + DATA, "birch", 5));
    CHECK(pb_call(&rt, 0xa001, r, 0, 0, 0) == 0);
    CHECK(hopen(&rt, "Birch.pct", *tex_id, 0) == -43);
    CHECK(hopen(&rt, "Wood", *tex_id, 0) == -50);                      /* a directory is not a file */
    CHECK(hopen(&rt, "Birch.pct", 424242, 0) == -120);

    /* Per-directory sidecars: HOpenRF opens Name.rsrc beside the file, here in a subfolder. */
    clear_pb(&rt); pstr(&rt, NAMEA, "Oak.pct"); w32(&rt, PB + 18, NAMEA); w32(&rt, PB + 48, *tex_id);
    CHECK(pbcall(&rt, 0xa20a, 0) == 0);
    int fork = (int16_t)mac_cpu_read(rt.cpu, PB + 24, 2);
    memset(mac_cpu_ram(rt.cpu) + DATA, 0, 4);
    CHECK(pb_call(&rt, 0xa002, fork, 1, 0, 4) == 0 && !memcmp(mac_cpu_ram(rt.cpu) + DATA, "FORK", 4));
    CHECK(pb_call(&rt, 0xa001, fork, 0, 0, 0) == 0);

    /* Create, write, delete in a data-backed subfolder: the overlay grows the folder next to the read-only one. */
    CHECK(hcall(&rt, 0xa208, 0, "New.txt", 0, *wood_id) == 0);
    r = hopen(&rt, "New.txt", *wood_id, 2);
    CHECK(r >= 128 && write_close(&rt, r, "hello") == 0);
    CHECK(host_size(saves, "Textures/Wood/New.txt") == 5 && host_size(data, "Textures/Wood/New.txt") < 0);
    CHECK(cat_find(&rt, *wood_id, "Birch.pct") == 1 && cat_find(&rt, *wood_id, "New.txt") == 2);   /* merged view of data and overlay */
    CHECK(hcall(&rt, 0xa208, 0, "New.txt", 0, *wood_id) == -48);
    CHECK(hcall(&rt, 0xa209, 0, "Birch.pct", 0, *wood_id) == -45);      /* application data is locked */
    CHECK(hcall(&rt, 0xa209, 0, "New.txt", 0, *wood_id) == 0 && host_size(saves, "Textures/Wood/New.txt") < 0);

    /* DirCreate: nested, with IDs; duplicates; a missing parent; a name taken by a file. */
    uint32_t mine, inner;
    CHECK(dir_create(&rt, "Mine", 0, 2, &mine) == 0 && mine > 2);
    CHECK(dir_create(&rt, "Inner", 0, mine, &inner) == 0 && inner != mine && host_size(saves, "Mine/Inner") >= 0);
    CHECK(dir_create(&rt, "Mine", 0, 2, &inner) == -48 && dir_create(&rt, "readme", 0, 2, &inner) == -48);
    CHECK(dir_create(&rt, "X", 0, 31337, &inner) == -120);
    CHECK(dir_create(&rt, ":Mine:Inner:Deep", 0, 2, &inner) == 0);      /* a partial path as the name */
    CHECK(dir_create(&rt, ":Textures:Wood:Burl", 0, 2, &inner) == 0 && host_size(saves, "Textures/Wood/Burl") >= 0);   /* a folder that exists only in the data */
    CHECK(hcall(&rt, 0xa208, 0, "Deep.txt", 0, inner) == 0 && host_size(saves, "Textures/Wood/Burl/Deep.txt") == 0);
    CHECK(hcall(&rt, 0xa209, 0, ":Textures:Wood:Burl", 0, 2) == -47);   /* not empty */
    CHECK(hcall(&rt, 0xa209, 0, ":Textures:Wood:Burl:Deep.txt", 0, 2) == 0);
    CHECK(hcall(&rt, 0xa209, 0, ":Textures:Wood:Burl", 0, 2) == 0 && host_size(saves, "Textures/Wood/Burl") < 0);
    CHECK(hcall(&rt, 0xa209, 0, "Textures", 0, 2) == -47);              /* a folder that holds anything cannot go */

    /* Working directories and the default directory. */
    CHECK(hcall(&rt, 0xa260, 1, NULL, 0, *wood_id) == 0);
    int wd = (int16_t)mac_cpu_read(rt.cpu, PB + 22, 2);
    CHECK(wd <= -100 && wd > -200);
    CHECK(catinfo(&rt, "Birch.pct", wd, 0, 0) == 0 && mac_cpu_read(rt.cpu, PB + 54, 4) == 5);   /* a bare name in the working directory */
    clear_pb(&rt); w16(&rt, PB + 22, (uint16_t)wd); CHECK(pbcall(&rt, 0xa260, 7) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == *wood_id);
    CHECK(hcall(&rt, 0xa260, 2, NULL, wd, 0) == 0);
    CHECK(catinfo(&rt, "Birch.pct", wd, 0, 0) == -35);                   /* closed */
    CHECK(hcall(&rt, 0xa260, 1, NULL, 0, 31337) == -120);
    CHECK(hcall(&rt, 0xa215, 0, NULL, 0, *tex_id) == 0);                 /* PBHSetVol: bare names now mean the Textures folder */
    clear_pb(&rt); CHECK(pbcall(&rt, 0xa214, 0) == 0 && mac_cpu_read(rt.cpu, PB + 48, 4) == *tex_id);
    clear_pb(&rt); pstr(&rt, NAMEA, "Oak.pct"); w32(&rt, PB + 18, NAMEA); w16(&rt, PB + 28, 0);
    CHECK(pbcall(&rt, 0xa00c, 0) == 0 && mac_cpu_read(rt.cpu, PB + 54, 4) == 3);   /* plain PBGetFInfo uses the default directory */
    CHECK(hcall(&rt, 0xa215, 0, NULL, 0, 2) == 0);

    /* Rename and move inside the overlay; the application data cannot move. */
    CHECK(hcall(&rt, 0xa208, 0, "Note", 0, mine) == 0);
    clear_pb(&rt); pstr(&rt, NAMEA, "Note"); pstr(&rt, NAMEB, "Memo"); w32(&rt, PB + 18, NAMEA); w32(&rt, PB + 28, NAMEB); w32(&rt, PB + 48, mine);
    CHECK(pbcall(&rt, 0xa20b, 0) == 0 && host_size(saves, "Mine/Memo") == 0 && host_size(saves, "Mine/Note") < 0);
    clear_pb(&rt); pstr(&rt, NAMEA, "Memo"); pstr(&rt, NAMEB, "Inner"); w32(&rt, PB + 18, NAMEA); w32(&rt, PB + 28, NAMEB); w32(&rt, PB + 36, mine); w32(&rt, PB + 48, mine);
    CHECK(pbcall(&rt, 0xa260, 5) == 0 && host_size(saves, "Mine/Inner/Memo") == 0 && host_size(saves, "Mine/Memo") < 0);   /* CatMove into a sub-directory by name */
    clear_pb(&rt); pstr(&rt, NAMEA, "Oak.pct"); pstr(&rt, NAMEB, "Mine"); w32(&rt, PB + 18, NAMEA); w32(&rt, PB + 28, NAMEB); w32(&rt, PB + 36, 2); w32(&rt, PB + 48, *tex_id);
    CHECK(pbcall(&rt, 0xa260, 5) == -45);
    clear_pb(&rt); pstr(&rt, NAMEA, "Mine"); pstr(&rt, NAMEB, "Inner"); w32(&rt, PB + 18, NAMEA); w32(&rt, PB + 28, NAMEB); w32(&rt, PB + 36, mine); w32(&rt, PB + 48, 2);
    CHECK(pbcall(&rt, 0xa260, 5) == -48);                                // a directory cannot move into itself

    /* Containment: no spelling gets out of the volume. */
    static const char *hostile[] = {":..:x", "::..:x", "..", "../x", ":Textures:..:..:x", "a/b", ":Textures/Wood:x", "\\x", ":Textures:Wood:.hidden", ":.:x"};
    for (unsigned i = 0; i < sizeof hostile / sizeof *hostile; i++) {
        int e = catinfo(&rt, hostile[i], 0, 2, 0);
        CHECK(e == -50 || e == -37 || e == -120 || e == -43 || e == -35);
        e = hcall(&rt, 0xa208, 0, hostile[i], 0, 2);
        CHECK(e != 0);
        e = hcall(&rt, 0xa260, 6, hostile[i], 0, 2);
        CHECK(e != 0);
    }
    CHECK(catinfo(&rt, "::x", 0, 2, 0) == -120);                       /* above the root */

    /* Symbolic links never lead out: not as a middle component, not as the last one, in the data or in the overlay. */
    char outside_file[1100];
    CHECK(catinfo(&rt, ":Hostile:EscDir:secret", 0, 2, 0) != 0);
    CHECK(catinfo(&rt, ":Hostile:EscDir", 0, 2, 0) == -43);           // a link is neither file nor directory
    CHECK(catinfo(&rt, ":Hostile:EscFile", 0, 2, 0) == -43);
    CHECK(hopen(&rt, ":Hostile:EscFile", 2, 0) < 0 && hopen(&rt, ":Hostile:EscDir:secret", 2, 0) < 0);
    CHECK(hcall(&rt, 0xa208, 0, ":Hostile:EscDir:evil", 0, 2) != 0);
    CHECK(hcall(&rt, 0xa260, 6, ":Hostile:EscDir:evil", 0, 2) != 0);
    CHECK(hcall(&rt, 0xa209, 0, ":Hostile:EscDir:secret", 0, 2) != 0);
    CHECK(hcall(&rt, 0xa209, 0, ":Hostile:EscFile", 0, 2) != 0);
    snprintf(outside_file, sizeof outside_file, "%s/secret", g_outside);
    CHECK(host_size(g_outside, "secret") == 6 && host_size(g_outside, "evil") < 0);
    {   char link[1100]; snprintf(link, sizeof link, "%s/OverlayEsc", saves); CHECK(symlink(g_outside, link) == 0);
        CHECK(catinfo(&rt, ":OverlayEsc:secret", 0, 2, 0) != 0);
        CHECK(hcall(&rt, 0xa208, 0, ":OverlayEsc:evil", 0, 2) != 0 && hcall(&rt, 0xa260, 6, ":OverlayEsc:evil", 0, 2) != 0);
        CHECK(hcall(&rt, 0xa209, 0, ":OverlayEsc:secret", 0, 2) != 0);
        CHECK(host_size(g_outside, "evil") < 0 && host_size(g_outside, "secret") == 6);
        CHECK(hcall(&rt, 0xa208, 0, "Fresh", 0, 2) == 0);   // the overlay itself still works
        unlink(link); }
    CHECK(host_size(data, "../escape") < 0);
    mac_files_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
}

/* Capacity: the file quota and the byte quota still answer dskFulErr (-34) inside subfolders. */
static void quotas(const char *data) {
    char saves[] = "/tmp/ospi-fold-quota-XXXXXX";
    CHECK(mkdtemp(saves) != NULL);
    MacRuntime rt = fresh();
    CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves, 3, 64) == 0);
    uint32_t sub, inner;
    CHECK(dir_create(&rt, "Sub", 0, 2, &sub) == 0 && dir_create(&rt, "Deeper", 0, sub, &inner) == 0);
    CHECK(hcall(&rt, 0xa208, 0, "a", 0, sub) == 0 && hcall(&rt, 0xa208, 0, "b", 0, inner) == 0 && hcall(&rt, 0xa208, 0, "c", 0, sub) == 0);
    CHECK(hcall(&rt, 0xa208, 0, "d", 0, inner) == -34);                  // three files in the whole tree
    int r = hopen(&rt, "a", sub, 2);
    char big[100]; memset(big, 'z', sizeof big);
    memcpy(mac_cpu_ram(rt.cpu) + DATA, big, sizeof big);
    CHECK(r >= 128 && pb_call(&rt, 0xa003, r, 1, 0, 100) == -34);        // 64 bytes in total
    CHECK(pb_call(&rt, 0xa003, r, 1, 0, 40) == 0 && pb_call(&rt, 0xa001, r, 0, 0, 0) == 0);
    int r2 = hopen(&rt, "b", inner, 2);
    CHECK(r2 >= 128 && pb_call(&rt, 0xa003, r2, 1, 0, 30) == -34 && pb_call(&rt, 0xa001, r2, 0, 0, 0) == 0);   // 40 + 30 > 64, across directories
    CHECK(host_size(saves, "Sub/a") == 40 && host_size(saves, "Sub/Deeper/b") == 0);
    mac_files_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    char cmd[1100]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", saves); CHECK(system(cmd) == 0);
}

int main(void) {
    char data[] = "/tmp/ospi-fold-data-XXXXXX", saves[] = "/tmp/ospi-fold-saves-XXXXXX", saves2[] = "/tmp/ospi-fold-saves2-XXXXXX", outside[] = "/tmp/ospi-fold-out-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves) && mkdtemp(saves2) && mkdtemp(outside));
    snprintf(g_outside, sizeof g_outside, "%s", outside);
    host_write(outside, "secret", "secret");
    host_write(data, "Game Data", "abcdef");
    host_write(data, "readme", "hello");
    host_write(data, "readme.rsrc", "FORKFORKFORKFORKFORK");
    mkdirs(data, "Textures/Wood"); mkdirs(data, "Textures/Stone"); mkdirs(data, "Hostile");
    host_write(data, "Textures/Marble.pct", "marble");
    host_write(data, "Textures/Oak.pct", "oak");
    host_write(data, "Textures/Oak.pct.rsrc", "FORKFORKFORKFORKFORK");
    host_write(data, "Textures/Wood/Birch.pct", "birch");
    host_write(data, "Textures/Stone/Slate.pct", "slate");
    char path[1200], link[1200];
    snprintf(path, sizeof path, "%s/Hostile/EscDir", data); CHECK(symlink(outside, path) == 0);
    snprintf(path, sizeof path, "%s/Hostile/EscFile", data); snprintf(link, sizeof link, "%s/secret", outside); CHECK(symlink(link, path) == 0);

    uint32_t tex1 = 0, wood1 = 0, tex2 = 0, wood2 = 0;
    session(data, saves, &tex1, &wood1);
    /* Directory IDs are stable: a new session on a fresh overlay looks the same directories up and gets the same numbers. */
    {   MacRuntime rt = fresh();
        CHECK(mac_files_root(&rt, data) == 0 && mac_files_writable(&rt, saves2, 8, 65536) == 0);
        CHECK(catinfo(&rt, ":Textures", 0, 2, 0) == 0); tex2 = mac_cpu_read(rt.cpu, PB + 48, 4);
        CHECK(catinfo(&rt, ":Textures:Wood", 0, 2, 0) == 0); wood2 = mac_cpu_read(rt.cpu, PB + 48, 4);
        CHECK(tex2 == tex1 && wood2 == wood1 && tex2 != wood2);
        CHECK(catinfo(&rt, NULL, 0, tex2 + 1, 1) == -120);        // an ID nobody handed out
        /* Read-only volume semantics survive: no overlay, no writes. */
        mac_files_destroy(&rt); mac_cpu_destroy(rt.cpu); }
    {   MacRuntime rt = fresh();
        uint32_t id;
        CHECK(mac_files_root(&rt, data) == 0);
        CHECK(catinfo(&rt, ":Textures:Wood", 0, 2, 0) == 0);
        CHECK(dir_create(&rt, "X", 0, 2, &id) == -46 && hcall(&rt, 0xa208, 0, ":Textures:New", 0, 2) == -46);
        mac_files_destroy(&rt); mac_cpu_destroy(rt.cpu); }
    quotas(data);

    char cmd[1200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s' '%s' '%s'", data, saves, saves2, outside);
    CHECK(system(cmd) == 0);
    printf("Hierarchical File Manager: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
