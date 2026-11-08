#include "desktop_sound.h"
#include "audio_service.h"
#include <math.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); failures++; } } while (0)
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    char pref[] = "/tmp/os999-sound-test.XXXXXX";
    int fd = mkstemp(pref);
    CHECK(fd >= 0);
    if (fd < 0) return 1;
    close(fd);
    CHECK(desktop_sound_init(argv[1], pref) == DESKTOP_SOUND_COUNT);
    CHECK(desktop_sound_enabled());
    audio_service_pause_output(1); /* Deterministic mixer reads below. */
    CHECK(desktop_sound_play(DESKTOP_SOUND_STARTUP));
    CHECK(desktop_sound_queued_bytes() > 0);
    CHECK(!desktop_sound_play(DESKTOP_SOUND_MENU_OPEN));
    desktop_sound_guest_active(1);
    CHECK(desktop_sound_queued_bytes() == 0);
    CHECK(desktop_sound_effects_suppressed());
    /* Guest activation clears effects without muting other shared buses. */
    float app_pcm[2] = {0.25f, -0.25f}, mixed[2] = {0};
    CHECK(audio_submit_app_pcm_f32(app_pcm, 1) == 1);
    audio_service_mix_frames(mixed, 1);
    CHECK(fabsf(mixed[0] - 0.25f) < 0.01f && fabsf(mixed[1] + 0.25f) < 0.01f);
    CHECK(!desktop_sound_play(DESKTOP_SOUND_ALERT));
    desktop_sound_guest_active(0);
    CHECK(!desktop_sound_effects_suppressed());
    CHECK(desktop_sound_set_enabled(0));
    CHECK(!desktop_sound_enabled());
    CHECK(desktop_sound_effects_suppressed());
    CHECK(!desktop_sound_play(DESKTOP_SOUND_STARTUP));
    desktop_sound_shutdown();
    CHECK(desktop_sound_init(argv[1], pref) == DESKTOP_SOUND_COUNT);
    CHECK(!desktop_sound_enabled());
    CHECK(desktop_sound_effects_suppressed());
    CHECK(desktop_sound_set_enabled(1));
    CHECK(desktop_sound_play(DESKTOP_SOUND_MENU_SELECT));
    CHECK(!desktop_sound_play(-1));
    CHECK(!desktop_sound_play(DESKTOP_SOUND_COUNT));
    desktop_sound_shutdown();
    CHECK(!desktop_sound_init("/nonexistent/sound-pack", pref));
    CHECK(!desktop_sound_play(DESKTOP_SOUND_STARTUP));
    desktop_sound_shutdown();
    char dir[] = "/tmp/os999-wav-test.XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char bad[4096];
    snprintf(bad, sizeof bad, "%s/01-startup_chord.wav", dir);
    FILE *f = fopen(bad, "wb");
    CHECK(f != NULL);
    if (f) {
        unsigned char bytes[44] = {0};
        memcpy(bytes, "RIFF", 4); bytes[4] = 36;
        memcpy(bytes + 8, "WAVEdata", 8);
        memset(bytes + 16, 255, 4); /* chunk points beyond bounded file */
        CHECK(fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes);
        fclose(f);
        CHECK(!desktop_sound_init(dir, pref));
        CHECK(!desktop_sound_play(DESKTOP_SOUND_STARTUP));
        desktop_sound_shutdown();
    }
    unlink(bad);
    rmdir(dir);
    unlink(pref);
    printf("desktop sound: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
