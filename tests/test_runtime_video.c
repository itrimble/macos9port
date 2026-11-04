#include "mac_runtime.h"

/* Pure pixel/input tests: no SDL initialization or window creation. */
struct MacCpu { uint8_t ram[4096]; };
uint8_t *mac_cpu_ram(MacCpu *cpu) { return cpu->ram; }
size_t mac_cpu_ram_size(const MacCpu *cpu) { (void)cpu; return 4096; }
int mac_cpu_halted(const MacCpu *cpu) { (void)cpu; return 1; }
const char *mac_cpu_error(const MacCpu *cpu) { (void)cpu; return "test halted"; }
size_t mac_runtime_run(MacRuntime *rt, size_t count) { rt->instructions += count; return count; }
void mac_runtime_post_event(MacRuntime *rt, uint16_t what, uint32_t message,
                            uint16_t modifiers, int16_t x, int16_t y) {
    (void)rt; (void)what; (void)message; (void)modifiers; (void)x; (void)y;
}

#define OSPI_VIDEO_CONVERSION_TEST
#include "../src/runtime_video.c"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void) {
    uint64_t us = 0;
    CHECK(elapsed_microseconds(17000000, 1000000000, 12, &us) == 0 && us == 17012);
    CHECK(elapsed_microseconds(UINT64_C(18000000000000), 1000000000, 0, &us) == 0 && us == UINT64_C(18000000000));
    CHECK(elapsed_microseconds(1, 0, 0, &us) == -1);
    CHECK(elapsed_microseconds(1000, 1000, UINT64_MAX, &us) == -1);
    MacCpu cpu = {{0}};
    MacRuntime rt = {0};
    uint32_t pixels[16];
    rt.cpu = &cpu; rt.screen_base = 100; rt.screen_width = 8; rt.screen_height = 2;
    rt.screen_row_bytes = 2; rt.screen_depth = 1;
    cpu.ram[100] = 0x81; cpu.ram[101] = 0xff; cpu.ram[102] = 0x42;
    CHECK(copy_frame(&rt, pixels, 16) == 0);
    CHECK(pixels[0] == 0xff000000 && pixels[7] == 0xff000000);
    CHECK(pixels[1] == 0xffffffff && pixels[8] == 0xffffffff);
    CHECK(pixels[9] == 0xff000000 && pixels[14] == 0xff000000);
    CHECK(copy_frame(&rt, pixels, 15) == -1);
    rt.screen_width = 4; rt.screen_height = 1; rt.screen_row_bytes = 4; rt.screen_depth = 8;
    cpu.ram[100] = 0; cpu.ram[101] = 1; cpu.ram[102] = 2; cpu.ram[103] = 255;
    rt.palette[0] = 0xffffff; rt.palette[1] = 0xff0000;
    rt.palette[2] = 0x00ff00; rt.palette[255] = 0x120000ff;
    CHECK(copy_frame(&rt, pixels, 16) == 0);
    CHECK(pixels[0] == 0xffffffff && pixels[1] == 0xffff0000);
    CHECK(pixels[2] == 0xff00ff00 && pixels[3] == 0xff0000ff);
    rt.palette[1] = 0x123456;
    CHECK(copy_frame(&rt, pixels, 16) == 0 && pixels[1] == 0xff123456);
    rt.screen_row_bytes = 3;
    CHECK(copy_frame(&rt, pixels, 16) == -1);
    rt.screen_row_bytes = 4; rt.screen_height = 2; rt.screen_base = 4090;
    CHECK(copy_frame(&rt, pixels, 16) == -1);
    rt.screen_base = UINT32_MAX;
    CHECK(copy_frame(&rt, pixels, 16) == -1);
    rt.screen_depth = 16;
    CHECK(copy_frame(&rt, pixels, 16) == -1);
    SDL_Rect r = viewport(1280, 720, 640, 480);
    CHECK(r.x == 160 && r.y == 0 && r.w == 960 && r.h == 720);
    r = viewport(640, 800, 640, 480);
    CHECK(r.x == 0 && r.y == 160 && r.w == 640 && r.h == 480);
    SDL_Keysym key = {0};
    uint32_t message = 0;
    key.sym = SDLK_LEFT;
    CHECK(key_message(&key, &message) && message == ((123u << 8) | 28));
    key.sym = SDLK_RETURN;
    CHECK(key_message(&key, &message) && message == ((36u << 8) | 13));
    key.sym = SDLK_SPACE;
    CHECK(key_message(&key, &message) && message == ((49u << 8) | 32));
    key.sym = SDLK_ESCAPE;
    CHECK(key_message(&key, &message) && message == ((53u << 8) | 27));
    key.sym = SDLK_a; key.mod = KMOD_SHIFT;
    CHECK(key_message(&key, &message) && message == 'A');
    key.mod |= KMOD_CAPS;
    CHECK(key_message(&key, &message) && message == 'a');
    key.sym = SDLK_1; key.mod = KMOD_SHIFT;
    CHECK(key_message(&key, &message) && message == ((18u << 8) | '!'));
    key.sym = SDLK_F1;
    CHECK(!key_message(&key, &message));
    CHECK(key_modifiers(KMOD_NONE, 0) == 0x80);
    CHECK(key_modifiers(KMOD_GUI | KMOD_SHIFT, 1) == 0x300);
    printf("Runtime video conversion checks: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
