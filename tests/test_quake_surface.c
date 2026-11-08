/* Synthetic bridge checks: no proprietary data, display or engine required. */
#include "../src/quake_session.c"
#include <assert.h>
void dialog_info(Session *s, const char *title, const char *message) {
    (void)s; (void)title; (void)message;
}
int main(void) {
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
    assert(native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_FOCUS_LOST}));
    assert(!atomic_load(&surface->focus) && atomic_load(&surface->reset_input));
    free(surface); surface = NULL;
    native_surface_close();
    assert(!native_surface_active());
    free(out);
    puts("Quake surface: attach, bounds, row orientation, lock, input and focus checks passed");
    return 0;
}
