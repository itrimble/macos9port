/* Synthetic bridge checks: no proprietary data, display or engine required. */
#include "../src/quake_session.c"
#include <assert.h>
#include <sys/stat.h>
static char last_dialog[512];
void dialog_info(Session *s, const char *title, const char *message) {
    (void)s; snprintf(last_dialog, sizeof last_dialog, "%s|%s", title, message);
}
static void write_junk(const char *path) {
    FILE *f = fopen(path, "wb"); assert(f); fputs("not a verified pak", f); fclose(f);
}
/* The menu and icon paths launch only verified data: retail, else the demo, else the baseq3 dialog. */
static void check_open_paths(void) {
    char tmp[1024], vol[1100], exe[1200], dir[1400], file[1500];
    const char *t = getenv("TMPDIR");
    snprintf(tmp, sizeof tmp, "%s/os999-qopen-XXXXXX", t && *t ? t : "/tmp");
    assert(mkdtemp(tmp));
    snprintf(vol, sizeof vol, "%s/vol", tmp); assert(!mkdir(vol, 0755));
    snprintf(exe, sizeof exe, "%s/fake-quake3", tmp); write_junk(exe); assert(!chmod(exe, 0755));
    setenv("HOME", tmp, 1); /* the demo carry-over looks in the Mac's Library: keep the host out of this test */
    setenv("OS999_QUAKE3", exe, 1); unsetenv("OS999_QUAKE3_DATA"); unsetenv("OS999_QUAKE3_IMPORT");
    os999_private_set_root(vol);
    Session *s = calloc(1, sizeof *s); assert(s);
    snprintf(s->root, sizeof s->root, "%s", vol);
    for (int pass = 0; pass < 2; pass++) {
        for (int mode = 0; mode < 3; mode++) {
            last_dialog[0] = 0;
            assert(open_quake(s, mode) == -1);
            assert(surface == NULL && child == 0);              /* nothing was started */
            if (mode == 1) assert(strstr(last_dialog, "Arena"));
            else { if (!strstr(last_dialog, "baseq3")) fprintf(stderr, "pass %d mode %d: %s\n", pass, mode, last_dialog); assert(strstr(last_dialog, "baseq3")); }          /* menu and icon: ask for the original folder */
        }
        /* Unverified files under every name the resolvers look for must still never launch. */
        const char *base = "Library/Application Support/OS999";
        const char *sets[] = {"Quake3-Full/baseq3", "Quake3/baseq3", "Quake3/demoq3"};
        snprintf(dir, sizeof dir, "%s/.os999/%s", vol, base); (void)dir;
        for (int i = 0; i < 3; i++) {
            snprintf(dir, sizeof dir, "%s/.os999/Library", vol); mkdir(dir, 0755);
            snprintf(dir, sizeof dir, "%s/.os999/Library/Application Support", vol); mkdir(dir, 0755);
            snprintf(dir, sizeof dir, "%s/.os999/%s", vol, base); mkdir(dir, 0755);
            char sub[1400]; snprintf(sub, sizeof sub, "%s/%.*s", dir, (int)(strchr(sets[i], '/') - sets[i]), sets[i]);
            mkdir(sub, 0755);
            snprintf(sub, sizeof sub, "%s/%s", dir, sets[i]); mkdir(sub, 0755);
            snprintf(file, sizeof file, "%s/pak0.pk3", sub); write_junk(file);
        }
    }
    free(s);
    unsetenv("OS999_QUAKE3");
    char rm[1100]; snprintf(rm, sizeof rm, "rm -rf '%s'", tmp); (void)!system(rm);
}
int main(void) {
    check_open_paths();
    surface = calloc(1, sizeof *surface);
    uint32_t *out = calloc(OS999Q_WIDTH * OS999Q_HEIGHT, sizeof *out);
    assert(surface && out);
    assert(native_surface_attach(&backend) == 0);
    assert(native_surface_attach(&backend) == -1);
    assert(native_surface_active());
    assert(!strcmp(native_surface_title(), "Quake III Arena"));
    assert(!native_surface_read(out, OS999Q_WIDTH * OS999Q_HEIGHT, NULL, NULL, NULL));
    surface->pixels[0] = 0x123456;
    surface->pixels[(OS999Q_HEIGHT - 1) * OS999Q_WIDTH] = 0x654321;
    atomic_store(&surface->ready, 1);
    atomic_store(&surface->frames, 7);
    int w, h; uint64_t sequence;
    assert(!native_surface_read(out, 1, &w, &h, &sequence));
    assert(native_surface_read(out, OS999Q_WIDTH * OS999Q_HEIGHT, &w, &h, &sequence));
    assert(w == 640 && h == 480 && sequence == 7);
    assert(out[0] == 0xff654321 && out[(h-1)*w] == 0xff123456);
    atomic_store(&surface->frame_lock, 1);
    assert(!native_surface_read(out, OS999Q_WIDTH * OS999Q_HEIGHT, NULL, NULL, NULL));
    atomic_store(&surface->frame_lock, 0);
    assert(native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_KEY_DOWN,.message='w'}));
    assert(surface->events[0].kind == OS999Q_KEY_SYMBOL && surface->events[0].a == 'w');
    /* Motion sends the pointer's absolute position (for the menu cursor), then the relative step. */
    unsigned at = atomic_load(&surface->input_write);
    assert(native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_MOTION,.x=100,.y=50}));
    assert(atomic_load(&surface->input_write) == at + 2);
    assert(surface->events[at % OS999Q_EVENTS].kind == OS999Q_MOUSE_ABS &&
           surface->events[at % OS999Q_EVENTS].a == 100 && surface->events[at % OS999Q_EVENTS].b == 50);
    assert(surface->events[(at + 1) % OS999Q_EVENTS].kind == OS999Q_MOUSE);
    /* A click lands where the pointer is. */
    at = atomic_load(&surface->input_write);
    assert(native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_BUTTON_DOWN,.x=300,.y=200}));
    assert(surface->events[at % OS999Q_EVENTS].kind == OS999Q_MOUSE_ABS &&
           surface->events[at % OS999Q_EVENTS].a == 300 && surface->events[at % OS999Q_EVENTS].b == 200);
    assert(surface->events[(at + 1) % OS999Q_EVENTS].kind == OS999Q_BUTTON);
    /* Outside the frame there is no absolute position to send. */
    at = atomic_load(&surface->input_write);
    assert(native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_MOTION,.x=700,.y=50}));
    assert(atomic_load(&surface->input_write) == at + 1 && surface->events[at % OS999Q_EVENTS].kind == OS999Q_MOUSE);
    assert(native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_FOCUS_LOST}));
    assert(!atomic_load(&surface->focus) && atomic_load(&surface->reset_input));
    free(surface); surface = NULL;
    native_surface_close();
    assert(!native_surface_active());
    free(out);
    puts("Quake surface: attach, bounds, row orientation, lock, input and focus checks passed");
    return 0;
}
