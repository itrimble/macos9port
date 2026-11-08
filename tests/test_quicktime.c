#include "quicktime_movie.h"
#include "audio_service.h"
#include "mac_movie.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned hash(const uint32_t *pixels, int w, int h) {
    unsigned result = 2166136261u;
    for (int i = 0; i < w * h; i++) result = (result ^ pixels[i]) * 16777619u;
    return result;
}
int main(int argc, char **argv) {
    assert(argc == 5);
    char error[256]; QuickTimeMovie *movie = NULL;
    assert(NewMovieFromFile(&movie, "/nonexistent/movie.mov", error, sizeof error) < 0 && !movie);
    for (int i = 1; i < argc; i++) {
        assert(NewMovieFromFile(&movie, argv[i], error, sizeof error) == 0);
        int w, h; const uint32_t *pixels = GetMoviePixels(movie, &w, &h);
        assert(pixels && w == 64 && h == 48 && GetMovieDuration(movie) >= 1900);
        unsigned first = hash(pixels, w, h);
        StartMovie(movie); assert(GetMovieState(movie) == MAC_MOVIE_PLAYING);
        assert(!MoviesTask(movie, 1000) && !MoviesTask(movie, 1700));
        assert(GetMovieTime(movie) >= 699 && GetMovieTime(movie) <= 701);
        assert(hash(GetMoviePixels(movie, NULL, NULL), w, h) != first);
        StopMovie(movie); uint32_t paused = GetMovieTime(movie);
        assert(!MoviesTask(movie, 2000) && GetMovieTime(movie) == paused);
        assert(SetMovieTime(movie, 200) == 0 && GetMovieTime(movie) == 200);
        assert(SetMovieTime(movie, GetMovieDuration(movie) + 1) < 0);
        SetMovieVolume(movie, 0.25f); assert(GetMovieVolume(movie) == 0.25f);
        SetMovieVolume(movie, NAN); assert(GetMovieVolume(movie) == 0.25f);
        StartMovie(movie); assert(!MoviesTask(movie, 2000) && !MoviesTask(movie, 5000));
        assert(GetMovieState(movie) == MAC_MOVIE_ENDED);
        StartMovie(movie); assert(GetMovieTime(movie) == 0);
        printf("decoded %s codec=%s hardware_frames=%d\n", argv[i], GetMovieCodec(movie), MovieHardwareDecoded(movie));
        DisposeMovie(movie);
    }
    char linkpath[] = "/tmp/os999-qt-link-XXXXXX";
    int fd = mkstemp(linkpath); assert(fd >= 0); close(fd); unlink(linkpath);
    assert(!symlink(argv[1], linkpath));
    assert(NewMovieFromFile(&movie, linkpath, error, sizeof error) < 0); unlink(linkpath);
    char malformed[] = "/tmp/os999-qt-bad-XXXXXX";
    fd = mkstemp(malformed); assert(fd >= 0); assert(write(fd, "bad movie", 9) == 9); close(fd);
    assert(NewMovieFromFile(&movie, malformed, error, sizeof error) < 0); unlink(malformed);

    /* Valid container with an unavailable video sample-entry codec. */
    FILE *source = fopen(argv[1], "rb"); assert(source);
    assert(!fseek(source, 0, SEEK_END)); long size = ftell(source); rewind(source);
    assert(size > 0 && size < 1024 * 1024);
    unsigned char *data = malloc((size_t)size); assert(data);
    assert(fread(data, 1, (size_t)size, source) == (size_t)size); fclose(source);
    int changed = 0;
    for (long i = 4; i + 80 < size; i++) {
        if (!memcmp(data + i, "avc1", 4) && data[i - 4] == 0 && data[i - 3] == 0 &&
            ((unsigned)data[i - 2] * 256 + data[i - 1]) >= 86) {
            memcpy(data + i, "zzzz", 4); changed++;
        }
    }
    assert(changed == 1);
    char unknown[] = "/tmp/os999-qt-codec-XXXXXX";
    fd = mkstemp(unknown); assert(fd >= 0);
    assert(write(fd, data, (size_t)size) == size); close(fd); free(data);
    assert(NewMovieFromFile(&movie, unknown, error, sizeof error) < 0); unlink(unknown);

    setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(audio_service_init(NULL) == 0); audio_service_pause_output(1);
    assert(NewMovieFromFile(&movie, argv[1], error, sizeof error) == 0);
    StartMovie(movie); float buffer[960 * 2]; double energy = 0;
    for (int i = 0; i < 75; i++) {
        usleep(2000); audio_service_mix_frames(buffer, 960);
        for (int j = 0; j < 1920; j++) energy += fabs(buffer[j]);
        assert(!MoviesTask(movie, (uint32_t)(1000 + i * 20)));
    }
    assert(energy > 1 && GetMovieTime(movie) > 900 && GetMovieTime(movie) < 1700);
    printf("AAC mixed PCM energy=%.1f movie_ms=%u (dummy output, not listening evidence)\n", energy, GetMovieTime(movie));
    for (int i = 75; i < 125; i++) {
        usleep(2000); audio_service_mix_frames(buffer, 960);
        assert(!MoviesTask(movie, (uint32_t)(1000 + i * 20)));
    }
    assert(GetMovieState(movie) == MAC_MOVIE_ENDED);
    assert(GetMovieTime(movie) == GetMovieDuration(movie));
    DisposeMovie(movie); audio_service_shutdown();
    puts("Native movie decode, clock, seek, pause, EOF, volume and boundary checks passed");
    return 0;
}
