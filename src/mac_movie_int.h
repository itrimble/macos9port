#ifndef MAC_MOVIE_INT_H
#define MAC_MOVIE_INT_H

#include "mac_movie.h"

struct MacMovie {
    uint8_t *bytes;
    size_t len;
    uint32_t timescale;
    uint32_t duration;
    uint32_t time;
    uint16_t width;
    uint16_t height;
    uint16_t depth;
    uint32_t codec;
    uint32_t sample_count;
    uint32_t *sizes;
    uint32_t *offs;
    uint32_t *dts;
    uint32_t *pixels;
    int can_draw;
    int have_frame;
    uint32_t shown;
    int state;
    int needs_anchor;
    uint32_t origin_ms;
    uint32_t origin_tick;
    int dropped;
};

int mac_movie_show(MacMovie *movie);

#endif
