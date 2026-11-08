#include "desktop_host.h"
#include "desktop_sound.h"
#include "control_panels.h"
#include "audio_service.h"
#include "movie_window.h"
#include "quicktime_movie.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    char root[] = "/tmp/os999-movie-lifecycle-XXXXXX";
    assert(mkdtemp(root) && !volume_seed(root));
    char path[OSPI_PATH];
    snprintf(path, sizeof path, "%s/Documents/audio.mp4", root);
    FILE *in = fopen(argv[1], "rb"), *out = fopen(path, "wb");
    assert(in && out);
    char bytes[4096]; size_t n;
    while ((n = fread(bytes, 1, sizeof bytes, in))) assert(fwrite(bytes, 1, n, out) == n);
    assert(!ferror(in)); fclose(in); assert(!fclose(out));
    for (int round = 0; round < 2; round++) {
        desktop_host_init(root);
        assert(audio_service_is_initialized());
        assert(desktop_sound_queued_bytes() == 0); /* No default startup chime. */
        audio_service_pause_output(1);
        audio_clear_system_voices();
        Session *s = session_create(root); assert(s);
        assert(!desktop_sound_enabled() && s->sound_muted && !s->startup_chime_enabled);
        assert(s->system_volume == 20 && !audio_get_master_muted());
        assert(!movie_window_open(s, "audio.mp4", "Documents/audio.mp4"));
        Window *w = &s->win[s->nwin - 1];
        assert(w->kind == WIN_MOVIE && w->movie_modern && w->movie);
        QuickTimeMovie *movie = w->movie;
        StartMovie(movie);
        float pcm[960 * 2]; double energy = 0;
        for (int i = 0; i < 10; i++) {
            usleep(2000); audio_service_mix_frames(pcm, 960);
            for (int j = 0; j < 1920; j++) energy += fabs(pcm[j]);
            assert(!MoviesTask(movie, (uint32_t)(i * 20)));
        }
        assert(energy > 1); /* Confirm a real audio player is owned by the movie. */
        /* The production helper must destroy movie owners BEFORE audio shutdown.
         * Reversing those calls triggers a use-after-free under ASan. */
        desktop_host_end_session(s);
        assert(!audio_service_is_initialized());
    }
    /* Interface effects persist independently of media's master output. */
    desktop_host_init(root);
    Session *s = session_create(root); assert(s);
    assert(!desktop_sound_enabled());
    session_command(s, CMD_SOUND_TOGGLE);
    desktop_host_tick(s, 0);
    assert(desktop_sound_enabled() && !s->sound_muted && !audio_get_master_muted());
    assert(!os999_cp_store_get_bool(control_panels_store(s), "sound", "muted", 1));
    session_command(s, CMD_CP_SOUND);
    assert(!s->sound_muted);
    desktop_host_end_session(s);
    desktop_host_init(root);
    s = session_create(root); assert(s);
    assert(desktop_sound_enabled() && !s->sound_muted);
    session_command(s, CMD_SOUND_TOGGLE);
    desktop_host_tick(s, 0);
    session_command(s, CMD_CP_SOUND);
    assert(!desktop_sound_enabled() && s->sound_muted && !audio_get_master_muted());
    desktop_host_end_session(s);
    char legacy[] = "/tmp/os999-sound-legacy-XXXXXX";
    assert(mkdtemp(legacy) && !volume_seed(legacy));
    snprintf(path, sizeof path, "%s/.os999-sound", legacy);
    out = fopen(path, "wb"); assert(out);
    assert(fputs("1\n", out) >= 0 && !fclose(out));
    desktop_host_init(legacy);
    s = session_create(legacy); assert(s);
    assert(desktop_sound_enabled() && !s->sound_muted && !s->startup_chime_enabled);
    desktop_host_end_session(s);
    char cp_only[] = "/tmp/os999-sound-store-XXXXXX";
    assert(mkdtemp(cp_only) && !volume_seed(cp_only));
    snprintf(path, sizeof path, "%s/.os999-sound", cp_only);
    assert(!mkdir(path, 0700)); /* Invalid legacy preference; never replace user data. */
    os999_cp_store_t store;
    assert(!os999_cp_store_init(&store, cp_only));
    assert(!os999_cp_store_set_str(&store, "sound", "muted", "invalid"));
    assert(!os999_cp_store_save(&store));
    desktop_host_init(cp_only);
    s = session_create(cp_only); assert(s);
    assert(!desktop_sound_enabled() && s->sound_muted);
    desktop_host_end_session(s);
    assert(!os999_cp_store_set_bool(&store, "sound", "muted", 0));
    assert(!os999_cp_store_save(&store));
    os999_cp_store_destroy(&store);
    desktop_host_init(cp_only);
    s = session_create(cp_only); assert(s);
    assert(desktop_sound_enabled() && !s->sound_muted);
    struct stat st; assert(!stat(path, &st) && S_ISDIR(st.st_mode));
    session_command(s, CMD_CP_SOUND);
    session_key(s, 'm', 0);
    assert(!desktop_sound_enabled() && s->sound_muted && !audio_get_master_muted());
    desktop_host_end_session(s);
    desktop_host_init(cp_only);
    s = session_create(cp_only); assert(s);
    assert(!desktop_sound_enabled() && s->sound_muted);
    desktop_host_end_session(s);
    desktop_host_end_session(NULL); /* partial-startup cleanup */
    printf("Movie/session/audio shutdown and reinitialization passed; volume %s\n", root);
    return 0;
}
