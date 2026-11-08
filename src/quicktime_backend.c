#include "quicktime_movie.h"
#include "movie_host.h"

static void *open_host(const char *path, char *error, size_t n) {
    QuickTimeMovie *m = NULL; (void)NewMovieFromFile(&m, path, error, n); return m;
}
static void close_host(void *m) { DisposeMovie(m); }
static void play_host(void *m) { StartMovie(m); }
static void pause_host(void *m) { StopMovie(m); }
static int seek_host(void *m, uint32_t t) { return SetMovieTime(m, t); }
static int tick_host(void *m, uint32_t t) { return MoviesTask(m, t); }
static int state_host(const void *m) { return GetMovieState(m); }
static uint32_t time_host(const void *m) { return GetMovieTime(m); }
static uint32_t duration_host(const void *m) { return GetMovieDuration(m); }
static const uint32_t *pixels_host(const void *m, int *w, int *h) { return GetMoviePixels(m, w, h); }
static void volume_host(void *m, float v) { SetMovieVolume(m, v); }
static float get_volume_host(const void *m) { return GetMovieVolume(m); }
void quicktime_install_backend(void) {
    static const MovieHostBackend backend = {open_host, close_host, play_host, pause_host,
        seek_host, tick_host, state_host, time_host, duration_host, pixels_host, volume_host, get_volume_host};
    movie_host_install(&backend);
}
