#include "desktop_host.h"
#include "audio_service.h"
#include "movie_window.h"
#include "quicktime_movie.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
        audio_service_pause_output(1);
        audio_clear_system_voices();
        Session *s = session_create(root); assert(s);
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
    desktop_host_end_session(NULL); /* partial-startup cleanup */
    printf("Movie/session/audio shutdown and reinitialization passed; volume %s\n", root);
    return 0;
}
