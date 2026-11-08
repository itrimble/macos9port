#include "mac_movie.h"
#include "movie_fixture.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                         \
    do {                                                                                                 \
        if (!(c)) {                                                                                      \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c);                                              \
            failures++;                                                                                  \
        }                                                                                                \
    } while (0)

static void poke32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static MacMovie *open_fix(const MovieFixture *f, char *err, size_t n) {
    MacMovie *m = mac_movie_open(f->data, f->len, err, n);
    if (!m) fprintf(stderr, "open failed: %s\n", err);
    return m;
}

static void test_raw(void) {
    MovieFixture f;
    char err[160];
    CHECK(movie_fixture_build(&f, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    MacMovie *m = open_fix(&f, err, sizeof err);
    CHECK(m != NULL);
    if (!m) return;
    int w = 0, h = 0;
    const uint32_t *px = mac_movie_pixels(m, &w, &h);
    CHECK(mac_movie_can_draw(m));
    CHECK(mac_movie_timescale(m) == 600);
    CHECK(mac_movie_duration(m) == 1800);
    CHECK(mac_movie_sample_count(m) == 3);
    CHECK(mac_movie_width(m) == 2 && mac_movie_height(m) == 2);
    CHECK(mac_movie_depth(m) == 32);
    CHECK(mac_movie_state(m) == MAC_MOVIE_STOPPED);
    CHECK(px && w == 2 && h == 2 && px[0] == 0xFFFF0000u);
    uint32_t idx = 9, off = 0, sz = 0;
    CHECK(mac_movie_sample_at(m, 0, &idx, &off, &sz) == 0 && idx == 0 && sz == 16);
    CHECK(f.data[off + 1] == 255 && f.data[off + 2] == 0 && f.data[off + 3] == 0);
    CHECK(mac_movie_sample_at(m, 599, &idx, &off, &sz) == 0 && idx == 0);
    CHECK(mac_movie_sample_at(m, 600, &idx, &off, &sz) == 0 && idx == 1);
    CHECK(f.data[off + 2] == 255);
    CHECK(mac_movie_sample_at(m, 1200, &idx, &off, &sz) == 0 && idx == 2);
    CHECK(f.data[off + 3] == 255);
    CHECK(mac_movie_sample_at(m, 1800, &idx, &off, &sz) != 0);
    CHECK(mac_movie_seek(m, 1800) == 0);
    CHECK(mac_movie_state(m) == MAC_MOVIE_ENDED);
    px = mac_movie_pixels(m, &w, &h);
    CHECK(px && px[0] == 0xFF0000FFu);
    mac_movie_close(m);
}

static void test_two_chunk(void) {
    MovieFixture f;
    char err[160];
    CHECK(movie_fixture_build(&f, 1, MAC_MOVIE_CODEC_RAW, 32) == 0);
    CHECK(f.stco2_at != 0);
    MacMovie *m = open_fix(&f, err, sizeof err);
    CHECK(m != NULL);
    if (!m) return;
    uint32_t i0, o0, s0, i1, o1, s1, i2, o2, s2;
    CHECK(mac_movie_sample_at(m, 0, &i0, &o0, &s0) == 0);
    CHECK(mac_movie_sample_at(m, 600, &i1, &o1, &s1) == 0);
    CHECK(mac_movie_sample_at(m, 1200, &i2, &o2, &s2) == 0);
    CHECK(i0 == 0 && i1 == 1 && i2 == 2);
    CHECK(o1 == o0 + 16);
    CHECK(o2 == o0 + 40);
    CHECK(s0 == 16 && s1 == 16 && s2 == 16);
    mac_movie_close(m);
}

static void test_clock(void) {
    MovieFixture f;
    char err[160];
    CHECK(movie_fixture_build(&f, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    MacMovie *m = open_fix(&f, err, sizeof err);
    CHECK(m != NULL);
    if (!m) return;
    int w = 0, h = 0;
    CHECK(mac_movie_play(m) == 0);
    CHECK(mac_movie_state(m) == MAC_MOVIE_PLAYING);
    CHECK(mac_movie_tick(m, 5000) == 0);
    CHECK(mac_movie_time(m) == 0);
    CHECK(mac_movie_pixels(m, &w, &h)[0] == 0xFFFF0000u);
    CHECK(mac_movie_tick(m, 6000) == 1);
    CHECK(mac_movie_time(m) == 600);
    CHECK(mac_movie_pixels(m, &w, &h)[0] == 0xFF00FF00u);
    CHECK(mac_movie_pause(m) == 0);
    CHECK(mac_movie_state(m) == MAC_MOVIE_PAUSED);
    CHECK(mac_movie_tick(m, 9000) == 0);
    CHECK(mac_movie_time(m) == 600);
    CHECK(mac_movie_play(m) == 0);
    CHECK(mac_movie_tick(m, 1000) == 0);
    CHECK(mac_movie_time(m) == 600);
    CHECK(mac_movie_tick(m, 2000) == 1);
    CHECK(mac_movie_time(m) == 1200);
    CHECK(mac_movie_pixels(m, &w, &h)[0] == 0xFF0000FFu);
    CHECK(mac_movie_tick(m, 500) == 0);
    CHECK(mac_movie_time(m) == 1200);
    CHECK(mac_movie_tick(m, 1500) == 0);
    CHECK(mac_movie_time(m) == 1800);
    CHECK(mac_movie_state(m) == MAC_MOVIE_ENDED);
    CHECK(mac_movie_play(m) == 0);
    CHECK(mac_movie_state(m) == MAC_MOVIE_PLAYING);
    CHECK(mac_movie_time(m) == 0);
    CHECK(mac_movie_pixels(m, &w, &h)[0] == 0xFFFF0000u);
    CHECK(mac_movie_stop(m) == 0);
    CHECK(mac_movie_state(m) == MAC_MOVIE_STOPPED);
    CHECK(mac_movie_time(m) == 0);
    mac_movie_close(m);
}

static void test_sync(void) {
    MovieFixture f;
    char err[160];
    CHECK(movie_fixture_build(&f, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    MacMovie *m = open_fix(&f, err, sizeof err);
    CHECK(m != NULL);
    if (!m) return;
    CHECK(mac_movie_play(m) == 0);
    CHECK(mac_movie_sync(m, 1000, 1010) == 0);
    CHECK(mac_movie_time(m) == 600);
    CHECK(mac_movie_dropped(m) == 0);
    CHECK(mac_movie_state(m) == MAC_MOVIE_PLAYING);
    CHECK(mac_movie_sync(m, 1050, 1100) == 0);
    CHECK(mac_movie_time(m) == 660);
    CHECK(mac_movie_dropped(m) == 1);
    CHECK(mac_movie_pause(m) == 0);
    CHECK(mac_movie_sync(m, 0, 5000) == 0);
    CHECK(mac_movie_time(m) == 660);
    CHECK(mac_movie_dropped(m) == 1);
    CHECK(mac_movie_state(m) == MAC_MOVIE_PAUSED);
    CHECK(mac_movie_play(m) == 0);
    CHECK(mac_movie_sync(m, 3000, 3000) == 0);
    CHECK(mac_movie_time(m) == 1800);
    CHECK(mac_movie_state(m) == MAC_MOVIE_ENDED);
    int w = 0, h = 0;
    CHECK(mac_movie_pixels(m, &w, &h)[0] == 0xFF0000FFu);
    mac_movie_close(m);
}

static void expect_fail(const uint8_t *bytes, size_t len, const char *part) {
    char err[160];
    MacMovie *m = mac_movie_open(bytes, len, err, sizeof err);
    if (m) fprintf(stderr, "opened a movie that should fail (%s)\n", part);
    CHECK(m == NULL);
    if (!strstr(err, part)) fprintf(stderr, "error %s, wanted %s\n", err, part);
    CHECK(strstr(err, part) != NULL);
    mac_movie_close(m);
}

static void test_reject(void) {
    MovieFixture f;
    char err[160];
    CHECK(movie_fixture_build(&f, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    expect_fail(NULL, 0, "cut off");
    uint8_t shortb[4] = {0, 0, 0, 1};
    expect_fail(shortb, sizeof shortb, "cut off");
    uint8_t garbage[8];
    memset(garbage, 0xff, sizeof garbage);
    expect_fail(garbage, sizeof garbage, "header is damaged");
    uint8_t sized[8] = {0, 0, 0, 4, 'm', 'o', 'o', 'v'};
    expect_fail(sized, sizeof sized, "header is damaged");
    uint8_t dummy[8];
    expect_fail(dummy, (size_t)MAC_MOVIE_MAX_BYTES + 1, "larger than 8 MB");
    poke32(f.data + f.timescale_at, 0);
    expect_fail(f.data, f.len, "no timescale");
    CHECK(movie_fixture_build(&f, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    f.data[f.timescale_at - 12] = 1;
    expect_fail(f.data, f.len, "version 0");
    CHECK(movie_fixture_build(&f, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    poke32(f.data + f.stco_at, 0);
    expect_fail(f.data, f.len, "outside its media block");
    CHECK(movie_fixture_build(&f, 0, 0x72707A61u, 16) == 0);
    MacMovie *m = open_fix(&f, err, sizeof err);
    CHECK(m != NULL);
    if (!m) return;
    char fcc[5];
    mac_movie_codec_chars(m, fcc);
    CHECK(!mac_movie_can_draw(m));
    CHECK(strcmp(fcc, "rpza") == 0);
    CHECK(mac_movie_pixels(m, NULL, NULL) == NULL);
    mac_movie_close(m);
}

int main(void) {
    test_raw();
    test_two_chunk();
    test_clock();
    test_sync();
    test_reject();
    if (failures) fprintf(stderr, "%d movie checks failed\n", failures);
    else printf("ok\n");
    return failures ? 1 : 0;
}
