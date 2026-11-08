#ifndef MAC_MOVIE_H
#define MAC_MOVIE_H

#include <stddef.h>
#include <stdint.h>

/* Host player for a classic movie container. It draws uncompressed 32-bit
 * raw samples ('raw ', byte order A,R,G,B) up to 640 by 480. Compressed
 * guest codecs are identified and refused. This is not QuickTime. */
enum { MAC_MOVIE_MAX_BYTES = 8 * 1024 * 1024 };
enum { MAC_MOVIE_CODEC_RAW = 0x72617720u };

enum {
    MAC_MOVIE_STOPPED = 0,
    MAC_MOVIE_PLAYING = 1,
    MAC_MOVIE_PAUSED = 2,
    MAC_MOVIE_ENDED = 3
};

typedef struct MacMovie MacMovie;

MacMovie *mac_movie_open(const uint8_t *bytes, size_t len, char *err, size_t err_n);
void mac_movie_close(MacMovie *movie);

int mac_movie_can_draw(const MacMovie *movie);
uint32_t mac_movie_codec(const MacMovie *movie);
void mac_movie_codec_chars(const MacMovie *movie, char out[5]);
int mac_movie_width(const MacMovie *movie);
int mac_movie_height(const MacMovie *movie);
uint16_t mac_movie_depth(const MacMovie *movie);
uint32_t mac_movie_timescale(const MacMovie *movie);
uint32_t mac_movie_duration(const MacMovie *movie);
uint32_t mac_movie_sample_count(const MacMovie *movie);
uint32_t mac_movie_time(const MacMovie *movie);
int mac_movie_state(const MacMovie *movie);
int mac_movie_dropped(const MacMovie *movie);
const uint32_t *mac_movie_pixels(const MacMovie *movie, int *width, int *height);

/* tick is movie-timescale units. At duration the lookup fails; the picture
 * stays on the last frame. */
int mac_movie_sample_at(const MacMovie *movie, uint32_t tick, uint32_t *index,
                        uint32_t *offset, uint32_t *size);

int mac_movie_play(MacMovie *movie);
int mac_movie_pause(MacMovie *movie);
int mac_movie_stop(MacMovie *movie);
int mac_movie_seek(MacMovie *movie, uint32_t tick);
/* The first tick after play anchors the clock and does not advance. */
int mac_movie_tick(MacMovie *movie, uint32_t host_ms);
/* While playing, drift above 20 ms follows the audio clock and counts a drop. */
int mac_movie_sync(MacMovie *movie, uint32_t video_ms, uint32_t audio_ms);

#endif
