#include "mac_movie_int.h"

int mac_movie_play(MacMovie *movie) {
    if (!movie) return -1;
    if (movie->state == MAC_MOVIE_ENDED || movie->time >= movie->duration) {
        movie->time = 0;
        movie->state = MAC_MOVIE_PLAYING;
        if (mac_movie_show(movie) < 0) return -1;
    }
    movie->state = MAC_MOVIE_PLAYING;
    movie->needs_anchor = 1;
    return 0;
}

int mac_movie_pause(MacMovie *movie) {
    if (!movie) return -1;
    if (movie->state != MAC_MOVIE_PLAYING) return 0;
    movie->state = MAC_MOVIE_PAUSED;
    movie->needs_anchor = 1;
    return 0;
}

int mac_movie_stop(MacMovie *movie) {
    if (!movie) return -1;
    movie->state = MAC_MOVIE_STOPPED;
    movie->time = 0;
    movie->needs_anchor = 1;
    return mac_movie_show(movie) < 0 ? -1 : 0;
}

int mac_movie_seek(MacMovie *movie, uint32_t tick) {
    if (!movie) return -1;
    if (tick > movie->duration) tick = movie->duration;
    movie->time = tick;
    movie->needs_anchor = 1;
    if (tick >= movie->duration) movie->state = MAC_MOVIE_ENDED;
    else if (movie->state == MAC_MOVIE_ENDED) movie->state = MAC_MOVIE_PAUSED;
    return mac_movie_show(movie) < 0 ? -1 : 0;
}

int mac_movie_tick(MacMovie *movie, uint32_t host_ms) {
    if (!movie) return -1;
    if (movie->state != MAC_MOVIE_PLAYING) return 0;
    if (movie->needs_anchor || host_ms < movie->origin_ms) {
        movie->origin_ms = host_ms;
        movie->origin_tick = movie->time;
        movie->needs_anchor = 0;
        return 0;
    }
    uint64_t elapsed = (uint64_t)host_ms - movie->origin_ms;
    uint64_t tick = movie->origin_tick + elapsed * movie->timescale / 1000u;
    int ended = 0;
    if (tick >= movie->duration) {
        tick = movie->duration;
        ended = 1;
    }
    movie->time = (uint32_t)tick;
    int changed = mac_movie_show(movie);
    if (changed < 0) return -1;
    if (ended) movie->state = MAC_MOVIE_ENDED;
    return changed;
}

int mac_movie_sync(MacMovie *movie, uint32_t video_ms, uint32_t audio_ms) {
    if (!movie) return -1;
    if (movie->state != MAC_MOVIE_PLAYING || movie->timescale == 0) return 0;
    int32_t drift = (int32_t)audio_ms - (int32_t)video_ms;
    uint32_t ms = drift > 20 ? audio_ms : video_ms;
    if (drift > 20) movie->dropped++;
    uint64_t tick = (uint64_t)ms * movie->timescale / 1000u;
    if (tick >= movie->duration) {
        movie->time = movie->duration;
        movie->state = MAC_MOVIE_ENDED;
    } else {
        movie->time = (uint32_t)tick;
    }
    movie->needs_anchor = 1;
    return mac_movie_show(movie) < 0 ? -1 : 0;
}
