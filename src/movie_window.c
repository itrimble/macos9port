#include "movie_window.h"
#include "native_apps.h"

#include "mac_movie.h"
#include "movie_host.h"
#include "movie_picker.h"
#include "finder_edit.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const MovieHostBackend *host;
void movie_host_install(const MovieHostBackend *backend) { host = backend; }
const MovieHostBackend *movie_host_backend(void) { return host; }
static const uint32_t *pixels_of(const Window *w, int *width, int *height) {
    return w->movie_modern && host ? host->pixels(w->movie, width, height) : mac_movie_pixels(w->movie, width, height);
}
static uint32_t duration_of(const Window *w) {
    return w->movie_modern && host ? host->duration(w->movie) : mac_movie_duration(w->movie);
}
static uint32_t milliseconds_of(const Window *w) {
    uint32_t time = movie_window_time(w);
    return w->movie_modern ? time : (uint32_t)((uint64_t)time * 1000 / mac_movie_timescale(w->movie));
}

static MacMovie *movie_of(const Window *w) {
    if (!w || w->kind != WIN_MOVIE) return NULL;
    return w->movie;
}

int movie_is_name(const char *name) {
    const char *dot = name ? strrchr(name, '.') : NULL;
    if (!dot) return 0;
    return strcasecmp(dot, ".mov") == 0 || strcasecmp(dot, ".qt") == 0 || strcasecmp(dot, ".mp4") == 0 ||
           strcasecmp(dot, ".m4v") == 0;
}

void movie_window_release(Window *w) {
    if (!w || w->kind != WIN_MOVIE || !w->movie) return;
    if (w->movie_modern && host) host->close(w->movie);
    else mac_movie_close(w->movie);
    w->movie = NULL;
}

static int geometry(const Window *w, Rect *picture, Rect *bar) {
    MacMovie *movie = movie_of(w);
    int fw = 0, fh = 0;
    if (!movie || w->collapsed || !pixels_of(w, &fw, &fh) || fw < 1 || fh < 1) return -1;
    Layout lay;
    window_layout(w, &lay);
    if (lay.content.w < 8 || lay.content.h < 48) return -1;
    int view_w = lay.content.w;
    int view_h = lay.content.h - 48;
    if (view_h < 1) return -1;
    int dw, dh;
    if ((long long)view_w * fh <= (long long)view_h * fw) {
        dw = view_w;
        dh = (int)((long long)view_w * fh / fw);
    } else {
        dh = view_h;
        dw = (int)((long long)view_h * fw / fh);
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (picture) {
        picture->x = lay.content.x + (view_w - dw) / 2;
        picture->y = lay.content.y + (view_h - dh) / 2;
        picture->w = dw;
        picture->h = dh;
    }
    if (bar) {
        bar->x = lay.content.x + 8;
        bar->y = lay.content.y + lay.content.h - 12;
        bar->w = lay.content.w > 16 ? lay.content.w - 16 : 0;
        bar->h = 8;
    }
    return 0;
}

int movie_window_picture_rect(const Window *w, Rect *out) {
    if (!out) return -1;
    return geometry(w, out, NULL);
}

int movie_window_state(const Window *w) {
    MacMovie *movie = movie_of(w);
    return movie ? (w->movie_modern && host ? host->state(movie) : mac_movie_state(movie)) : -1;
}

uint32_t movie_window_time(const Window *w) {
    MacMovie *movie = movie_of(w);
    return movie ? (w->movie_modern && host ? host->time(movie) : mac_movie_time(movie)) : 0;
}

void movie_window_toggle(Window *w) {
    MacMovie *movie = movie_of(w);
    if (!movie) return;
    if (w->movie_modern && host) {
        if (host->state(movie) == MAC_MOVIE_PLAYING) host->pause(movie); else host->play(movie);
    } else if (mac_movie_state(movie) == MAC_MOVIE_PLAYING) mac_movie_pause(movie);
    else mac_movie_play(movie);
}

void movie_windows_tick(Session *s, uint32_t now_ms) {
    if (!s) return;
    for (int i = 0; i < s->nwin; i++) {
        MacMovie *movie = movie_of(&s->win[i]);
        if (movie) {
            if (s->win[i].movie_modern && host) host->tick(movie, now_ms);
            else mac_movie_tick(movie, now_ms);
        }
    }
}

static void put_px(Fb *fb, int x, int y, uint32_t color) {
    if (!fb || !fb->px || x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    if (fb->clip && (x < fb->cx || y < fb->cy || x >= fb->cx + fb->cw || y >= fb->cy + fb->ch)) return;
    fb->px[y * fb->w + x] = color;
}

void movie_window_paint(Fb *fb, const Window *w) {
    MacMovie *movie = movie_of(w);
    if (!fb || !w || w->kind != WIN_MOVIE) return;
    Layout lay;
    window_layout(w, &lay);
    fb_fill(fb, lay.content.x, lay.content.y, lay.content.w, lay.content.h, C_BLACK);
    if (!movie) {
        movie_picker_paint(fb, &lay);
        return;
    }
    int fw = 0, fh = 0;
    const uint32_t *px = pixels_of(w, &fw, &fh);
    Rect picture, bar;
    if (px && geometry(w, &picture, &bar) == 0) {
        for (int y = 0; y < picture.h; y++) {
            int sy = (int)((long long)y * fh / picture.h);
            for (int x = 0; x < picture.w; x++) {
                int sx = (int)((long long)x * fw / picture.w);
                put_px(fb, picture.x + x, picture.y + y, px[sy * fw + sx]);
            }
        }
        fb_fill(fb, bar.x, bar.y, bar.w, bar.h, C_TRACK);
        uint32_t dur = duration_of(w);
        uint32_t t = movie_window_time(w);
        int fill = dur ? (int)((unsigned long long)bar.w * t / dur) : 0;
        if (fill > bar.w) fill = bar.w;
        fb_fill(fb, bar.x, bar.y, fill, bar.h, C_BLACK);
    }
    const char *label = "Stopped";
    int state = movie_window_state(w);
    if (state == MAC_MOVIE_PLAYING) label = "Playing";
    else if (state == MAC_MOVIE_PAUSED) label = "Paused";
    else if (state == MAC_MOVIE_ENDED) label = "Ended";
    int cy = lay.content.y + lay.content.h - 38;
    fb_fill(fb, lay.content.x, cy - 4, lay.content.w, 24, C_MENU);
    fb_text(fb, lay.content.x + 8, cy, C_BLACK, state == MAC_MOVIE_PLAYING ? "Pause" : "Play");
    char clock[32]; uint32_t ms = milliseconds_of(w);
    snprintf(clock, sizeof clock, "%02u:%02u %s", ms / 60000, (ms / 1000) % 60, label);
    fb_text(fb, lay.content.x + 64, cy, C_BLACK, clock);
    if (w->movie_modern && host && lay.content.w > 240) {
        int vx = lay.content.x + lay.content.w - 66;
        fb_fill(fb, vx, cy + 4, 56, 8, C_TRACK);
        fb_fill(fb, vx, cy + 4, (int)(host->get_volume(movie) * 56), 8, C_BLACK);
    }
}

static void explain(MacMovie *movie, char *err, size_t n) {
    char fcc[5];
    mac_movie_codec_chars(movie, fcc);
    if (mac_movie_codec(movie) == MAC_MOVIE_CODEC_RAW)
        snprintf(err, n, "OS999 plays 32-bit raw video up to 640 by 480.\n\nThis raw movie is %d by %d, %u-bit.",
                 mac_movie_width(movie), mac_movie_height(movie), mac_movie_depth(movie));
    else
        snprintf(err, n, "OS999 plays uncompressed 32-bit raw video.\n\nThis movie uses '%s'.", fcc);
}

int movie_window_open(Session *s, const char *title, const char *rel) {
    if (!s || !rel) return -1;
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_MOVIE && strcmp(s->win[i].rel, rel) == 0) {
            bring_front(s, i);
            return 0;
        }
    }
    char full[OSPI_PATH];
    if (volume_join(full, sizeof full, s->root, rel) != 0) {
        dialog_info(s, "Open", "OS999 will not open a movie outside this disk.");
        return 0;
    }
    struct stat st;
    if (lstat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        dialog_info(s, "Open", "That movie is not a file OS999 can play.");
        return 0;
    }
    if (s->nwin >= OSPI_MAX_WIN) return -1;
    char err[240] = "";
    void *movie = NULL;
    int modern = host != NULL;
    if (modern) {
        movie = host->open(full, err, sizeof err);
        if (!movie) { dialog_info(s, "Movie Player 999", err[0] ? err : "Video decoder unavailable."); return 0; }
    } else {
        if (st.st_size <= 0 || (unsigned long long)st.st_size > MAC_MOVIE_MAX_BYTES) {
            dialog_info(s, "Open", "That movie is larger than 8 MB.");
            return 0;
        }
        FILE *f = fopen(full, "rb");
        if (!f) {
            dialog_info(s, "Open", "Could not read that movie.");
            return 0;
        }
        uint8_t *buf = malloc((size_t)st.st_size);
        size_t got = buf ? fread(buf, 1, (size_t)st.st_size, f) : 0;
        fclose(f);
        if (!buf || got != (size_t)st.st_size) {
            free(buf);
            dialog_info(s, "Open", "Could not read that movie.");
            return 0;
        }
        movie = mac_movie_open(buf, got, err, sizeof err);
        free(buf);
        if (!movie) {
            dialog_info(s, "Open", err[0] ? err : "Could not read that movie.");
            return 0;
        }
        if (!mac_movie_can_draw(movie)) {
            explain(movie, err, sizeof err);
            mac_movie_close(movie);
            dialog_info(s, "Open", err);
            return 0;
        }
    }
    for (int i = 0; i < s->nwin; i++)
        if (s->win[i].kind == WIN_MOVIE && !s->win[i].movie) { close_index(s, i); break; }
    Window *w = &s->win[s->nwin];
    memset(w, 0, sizeof *w);
    w->kind = WIN_MOVIE;
    w->movie = movie;
    w->movie_modern = modern;
    snprintf(w->title, sizeof w->title, modern ? "QuickTime Player 999: %s" : "%s", title && title[0] ? title : "Movie");
    snprintf(w->rel, sizeof w->rel, "%s", rel);
    w->x = 36 + (s->nwin % 5) * 22;
    w->y = 42 + (s->nwin % 5) * 22;
    w->w = 280;
    w->h = 220;
    w->shade_h = w->h;
    s->nwin++;
    native_app_note(s, NATIVE_MOVIE);
    if (modern) host->play(movie);
    if (!modern && mac_movie_play(movie) != 0) {
        movie_window_release(w);
        s->nwin--;
        dialog_info(s, "Open", "Could not play that movie.");
        return 0;
    }
    return 0;
}

int movie_window_seek(Window *w, uint32_t milliseconds) {
    if (!movie_of(w)) return -1;
    if (w->movie_modern && host) return host->seek(w->movie, milliseconds);
    uint64_t ticks = (uint64_t)milliseconds * mac_movie_timescale(w->movie) / 1000;
    return ticks > UINT32_MAX ? -1 : mac_movie_seek(w->movie, (uint32_t)ticks);
}
void movie_window_click(Window *w, int x, int y) {
    Rect picture, bar; Layout lay;
    if (geometry(w, &picture, &bar)) return;
    window_layout(w, &lay);
    if (rect_has(bar, x, y) && bar.w > 0) {
        uint64_t duration = duration_of(w);
        if (!w->movie_modern) duration = duration * 1000 / mac_movie_timescale(w->movie);
        (void)movie_window_seek(w, (uint32_t)(duration * (x - bar.x) / bar.w));
    } else if (w->movie_modern && host && lay.content.w > 240 &&
               x >= lay.content.x + lay.content.w - 66 && y >= lay.content.y + lay.content.h - 42) {
        host->volume(w->movie, (float)(x - (lay.content.x + lay.content.w - 66)) / 56);
    } else movie_window_toggle(w);
}
int movie_window_key(Window *w, int key) {
    if (!movie_of(w)) return 0;
    if (key == ' ') { movie_window_toggle(w); return 1; }
    if (key == FINDER_KEY_LEFT || key == FINDER_KEY_RIGHT) {
        uint32_t now = milliseconds_of(w);
        uint64_t duration = duration_of(w);
        if (!w->movie_modern) duration = duration * 1000 / mac_movie_timescale(w->movie);
        uint32_t next = key == FINDER_KEY_LEFT ? (now > 5000 ? now - 5000 : 0) : now + 5000;
        (void)movie_window_seek(w, next > duration ? (uint32_t)duration : next);
        return 1;
    }
    return 0;
}

int movie_window_launcher(Session *s) {
    if (!s) return -1;
    for (int i = 0; i < s->nwin; i++) if (s->win[i].kind == WIN_MOVIE) {
        bring_front(s, i); native_app_note(s, NATIVE_MOVIE); return 0;
    }
    if (s->nwin >= OSPI_MAX_WIN) { dialog_info(s, "Movie Player", "Close a window first."); return -1; }
    movie_picker_scan(s);
    Window *w = &s->win[s->nwin++]; memset(w, 0, sizeof *w);
    w->kind = WIN_MOVIE; w->x = 84; w->y = 64; w->w = 360; w->h = w->shade_h = 240;
    snprintf(w->title, sizeof w->title, "Movie Player");
    bring_front(s, s->nwin - 1); native_app_note(s, NATIVE_MOVIE); return 0;
}
