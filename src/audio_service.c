#include "audio_service.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_CACHED_SOUNDS = 40, MAX_SYSTEM_VOICES = 8, MAX_MEDIA_PLAYERS = 4, MAX_CACHE_FRAMES = 48000 * 10 };

typedef struct { char name[48]; float *pcm; size_t frames; } CachedSound;
typedef struct { const CachedSound *sound; size_t pos; int active; } SystemVoice;

struct AudioMediaPlayer {
    char path[1024];
    AudioDecoder *decoder;
    AudioMetadata meta;
    float *ring;
    size_t ring_cap, read_pos, write_pos, buffered;
    uint64_t played_frames;
    double base_seek_sec, seek_target_sec;
    int playing, looping, decoder_eof, stop_worker, seek_requested;
    float volume;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static struct {
    int initialized;
    AudioOutput *output;
    pthread_mutex_t lock;
    CachedSound cache[MAX_CACHED_SOUNDS];
    size_t cache_count;
    SystemVoice voices[MAX_SYSTEM_VOICES];
    AudioMediaPlayer *players[MAX_MEDIA_PLAYERS];
    float app_ring[AUDIO_APP_RING_FRAMES * 2];
    size_t app_read, app_write, app_buffered;
    _Atomic int bus_vol_milli[AUDIO_BUS_COUNT];
    _Atomic int master_muted;
} svc = { .lock = PTHREAD_MUTEX_INITIALIZER };

static float clamp_unit(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float clamp_sample(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

static void mixer_pull_cb(void *userdata, float *out_pcm_f32, size_t frames) {
    (void)userdata;
    audio_service_mix_frames(out_pcm_f32, frames);
}

static const struct { const char *name, *c9_file, *alt_file; } k_pack_events[] = {
    {"startup",          "01-startup_chord.wav",    "01-startup_chime.wav"},
    {"restart",          "02-restart_tone.wav",     "02-shutdown.wav"},
    {"alert.beep",       "03-plain_beep.wav",       "05-alert_beep.wav"},
    {"alert.chime",      "04-tin_chime.wav",        "07-alert_glass.wav"},
    {"alert.droplet",    "07-droplet.wav",          "06-alert_droplet.wav"},
    {"alert.indigo",     "08-indigo.wav",           "10-alert_marimba.wav"},
    {"alert.appear",     "34-alert_appear.wav",     "14-notification.wav"},
    {"menu.open",        "16-menu_open.wav",        "15-menu_tick.wav"},
    {"menu.select",      "17-menu_select.wav",      "16-button_click.wav"},
    {"menu.close",       "18-menu_close.wav",       "15-menu_tick.wav"},
    {"window.open",      "19-window_open_zoom.wav", "18-window_open.wav"},
    {"window.close",     "20-window_close_zoom.wav","19-window_close.wav"},
    {"window.collapse",  "21-collapse_up.wav",      "20-windowshade_roll.wav"},
    {"window.expand",    "22-expand_down.wav",      "20-windowshade_roll.wav"},
    {"window.zoom",      "23-zoom_box.wav",         "16-button_click.wav"},
    {"drag.begin",       "24-drag_begin.wav",       "21-drag_pickup.wav"},
    {"drag.end",         "25-drag_end.wav",         "22-drop_thunk.wav"},
    {"button.press",     "26-button_press.wav",     "16-button_click.wav"},
    {"button.release",   "27-button_release.wav",   "16-button_click.wav"},
    {"checkbox.toggle",  "28-checkbox.wav",         "16-button_click.wav"},
    {"scroll.tick",      "29-scroll_tick.wav",      "15-menu_tick.wav"},
    {"window.resize",    "30-resize_grow.wav",      "20-windowshade_roll.wav"},
    {"folder.open",      "31-spring_open.wav",      "25-folder_open.wav"},
    {"trash.empty",      "32-trash_crumple.wav",    "24-trash_empty.wav"},
    {"trash.drop",       "33-lid_clank.wav",        "23-trash_drop.wav"},
};

int audio_cache_system_sound(const char *name, const char *path) {
    if (!name || !*name || !path || !*path) return -1;
    AudioDecoder *dec = NULL;
    if (audio_decoder_open(&dec, path) != 0 || !dec) return -1;
    float *pcm = malloc(4096 * 2 * sizeof(float));
    if (!pcm) { audio_decoder_close(dec); return -1; }
    size_t cap = 4096, total = 0;
    int eof = 0;
    while (!eof && total < MAX_CACHE_FRAMES) {
        if (total + 2048 > cap) {
            size_t ncap = cap * 2 > MAX_CACHE_FRAMES ? MAX_CACHE_FRAMES : cap * 2;
            float *nb = realloc(pcm, ncap * 2 * sizeof(float));
            if (!nb) break;
            pcm = nb; cap = ncap;
        }
        size_t want = cap - total < 2048 ? cap - total : 2048;
        int got = audio_decoder_read_frames(dec, pcm + total * 2, want, &eof);
        if (got <= 0) break;
        total += (size_t)got;
    }
    audio_decoder_close(dec);
    if (total == 0) { free(pcm); return -1; }

    pthread_mutex_lock(&svc.lock);
    int slot = -1;
    for (size_t i = 0; i < svc.cache_count; i++)
        if (strcmp(svc.cache[i].name, name) == 0) { slot = (int)i; break; }
    if (slot < 0 && svc.cache_count < MAX_CACHED_SOUNDS) slot = (int)svc.cache_count++;
    if (slot >= 0) {
        free(svc.cache[slot].pcm);
        snprintf(svc.cache[slot].name, sizeof svc.cache[slot].name, "%s", name);
        svc.cache[slot].pcm = pcm;
        svc.cache[slot].frames = total;
    } else free(pcm);
    pthread_mutex_unlock(&svc.lock);
    return slot >= 0 ? 0 : -1;
}

int audio_service_init(const char *sound_pack_dir) {
    audio_service_shutdown();
    audio_dsp_reset();
    for (int i = 0; i < AUDIO_BUS_COUNT; i++) atomic_init(&svc.bus_vol_milli[i], 1000);
    atomic_init(&svc.master_muted, 0);
    if (audio_output_open(&svc.output, AUDIO_INTERNAL_RATE, AUDIO_INTERNAL_CHANNELS,
                          mixer_pull_cb, NULL) != 0) return -1;
    svc.initialized = 1;
    if (sound_pack_dir && *sound_pack_dir) {
        size_t count = sizeof k_pack_events / sizeof k_pack_events[0];
        for (size_t i = 0; i < count; i++) {
            char path[4096];
            snprintf(path, sizeof path, "%s/%s", sound_pack_dir, k_pack_events[i].c9_file);
            if (audio_cache_system_sound(k_pack_events[i].name, path) != 0) {
                snprintf(path, sizeof path, "%s/%s", sound_pack_dir, k_pack_events[i].alt_file);
                (void)audio_cache_system_sound(k_pack_events[i].name, path);
            }
        }
    }
    return 0;
}

void audio_service_shutdown(void) {
    for (int i = 0; i < MAX_MEDIA_PLAYERS; i++)
        if (svc.players[i]) audio_player_close(svc.players[i]);
    if (svc.output) { audio_output_close(svc.output); svc.output = NULL; }
    pthread_mutex_lock(&svc.lock);
    memset(svc.voices, 0, sizeof svc.voices);
    for (size_t i = 0; i < svc.cache_count; i++) {
        free(svc.cache[i].pcm);
        memset(&svc.cache[i], 0, sizeof svc.cache[i]);
    }
    svc.cache_count = 0;
    svc.app_read = svc.app_write = svc.app_buffered = 0;
    svc.initialized = 0;
    pthread_mutex_unlock(&svc.lock);
}

int audio_service_is_initialized(void) { return svc.initialized; }
size_t audio_cached_sound_count(void) {
    pthread_mutex_lock(&svc.lock);
    size_t n = svc.cache_count;
    pthread_mutex_unlock(&svc.lock);
    return n;
}
void audio_clear_system_voices(void) {
    pthread_mutex_lock(&svc.lock);
    memset(svc.voices, 0, sizeof svc.voices);
    pthread_mutex_unlock(&svc.lock);
}
int audio_play_system_sound(const char *name) {
    if (!svc.initialized || !name || !*name) return 0;
    if (atomic_load_explicit(&svc.master_muted, memory_order_relaxed)) return 0;
    pthread_mutex_lock(&svc.lock);
    const CachedSound *found = NULL;
    for (size_t i = 0; i < svc.cache_count; i++)
        if (strcmp(svc.cache[i].name, name) == 0) { found = &svc.cache[i]; break; }
    int ok = 0;
    if (found && found->pcm && found->frames > 0) {
        int slot = 0;
        for (int i = 0; i < MAX_SYSTEM_VOICES; i++) {
            if (!svc.voices[i].active) { slot = i; break; }
            if (svc.voices[i].pos > svc.voices[slot].pos) slot = i;
        }
        svc.voices[slot] = (SystemVoice){found, 0, 1};
        ok = 1;
    }
    pthread_mutex_unlock(&svc.lock);
    return ok;
}
size_t audio_active_system_frames(void) {
    pthread_mutex_lock(&svc.lock);
    size_t rem = 0;
    for (int i = 0; i < MAX_SYSTEM_VOICES; i++) {
        if (svc.voices[i].active && svc.voices[i].sound) {
            size_t r = svc.voices[i].sound->frames - svc.voices[i].pos;
            if (r > rem) rem = r;
        }
    }
    pthread_mutex_unlock(&svc.lock);
    return rem;
}

static void *media_worker_main(void *arg) {
    AudioMediaPlayer *p = arg;
    float chunk[1024 * 2];
    pthread_mutex_lock(&p->lock);
    while (!p->stop_worker) {
        if (p->seek_requested) {
            double target = p->seek_target_sec;
            p->seek_requested = 0;
            pthread_mutex_unlock(&p->lock);
            int rc = audio_decoder_seek(p->decoder, target);
            pthread_mutex_lock(&p->lock);
            if (rc == 0) {
                p->read_pos = p->write_pos = p->buffered = 0;
                p->played_frames = 0;
                p->base_seek_sec = target;
                p->decoder_eof = 0;
            }
            pthread_cond_broadcast(&p->cond);
            continue;
        }
        if (p->decoder_eof || p->buffered + 1024 > p->ring_cap) {
            pthread_cond_wait(&p->cond, &p->lock);
            continue;
        }
        pthread_mutex_unlock(&p->lock);
        int eof = 0, got = audio_decoder_read_frames(p->decoder, chunk, 1024, &eof);
        pthread_mutex_lock(&p->lock);
        if (p->seek_requested || p->stop_worker) continue;
        if (got > 0) {
            for (int i = 0; i < got; i++) {
                p->ring[p->write_pos * 2] = chunk[i * 2];
                p->ring[p->write_pos * 2 + 1] = chunk[i * 2 + 1];
                p->write_pos = (p->write_pos + 1) % p->ring_cap;
            }
            p->buffered += (size_t)got;
        }
        if (eof || got < 0) {
            if (p->looping && got >= 0) {
                pthread_mutex_unlock(&p->lock);
                (void)audio_decoder_seek(p->decoder, 0.0);
                pthread_mutex_lock(&p->lock);
                p->decoder_eof = 0;
            } else p->decoder_eof = 1;
        }
        pthread_cond_broadcast(&p->cond);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

AudioMediaPlayer *audio_open_media(const char *path) {
    if (!path || !*path) return NULL;
    AudioDecoder *dec = NULL;
    if (audio_decoder_open(&dec, path) != 0 || !dec) return NULL;
    AudioMediaPlayer *p = calloc(1, sizeof *p);
    if (!p) { audio_decoder_close(dec); return NULL; }
    snprintf(p->path, sizeof p->path, "%s", path);
    p->decoder = dec;
    audio_decoder_metadata(dec, &p->meta);
    p->ring_cap = AUDIO_MEDIA_RING_FRAMES;
    p->ring = calloc(p->ring_cap * 2, sizeof(float));
    if (!p->ring) { audio_decoder_close(dec); free(p); return NULL; }
    p->volume = 1.0f;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cond, NULL);

    pthread_mutex_lock(&svc.lock);
    int slot = -1;
    for (int i = 0; i < MAX_MEDIA_PLAYERS; i++)
        if (!svc.players[i]) { slot = i; svc.players[i] = p; break; }
    pthread_mutex_unlock(&svc.lock);
    if (slot < 0 || pthread_create(&p->thread, NULL, media_worker_main, p) != 0) {
        if (slot >= 0) {
            pthread_mutex_lock(&svc.lock);
            svc.players[slot] = NULL;
            pthread_mutex_unlock(&svc.lock);
        }
        pthread_mutex_destroy(&p->lock);
        pthread_cond_destroy(&p->cond);
        free(p->ring); audio_decoder_close(dec); free(p);
        return NULL;
    }
    pthread_mutex_lock(&p->lock);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 50 * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    while (p->buffered == 0 && !p->decoder_eof)
        if (pthread_cond_timedwait(&p->cond, &p->lock, &ts) != 0) break;
    pthread_mutex_unlock(&p->lock);
    return p;
}

void audio_player_play(AudioMediaPlayer *p) {
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    p->playing = 1;
    pthread_cond_signal(&p->cond);
    pthread_mutex_unlock(&p->lock);
}

void audio_player_pause(AudioMediaPlayer *p) {
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    p->playing = 0;
    pthread_mutex_unlock(&p->lock);
}

int audio_player_seek(AudioMediaPlayer *p, double seconds) {
    if (!p) return -1;
    pthread_mutex_lock(&p->lock);
    p->seek_target_sec = seconds < 0.0 ? 0.0 : seconds;
    p->seek_requested = 1;
    pthread_cond_signal(&p->cond);
    while (p->seek_requested && !p->stop_worker)
        pthread_cond_wait(&p->cond, &p->lock);
    pthread_mutex_unlock(&p->lock);
    return 0;
}

void audio_player_set_volume(AudioMediaPlayer *p, float volume) {
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    p->volume = clamp_unit(volume);
    pthread_mutex_unlock(&p->lock);
}
float audio_player_get_volume(const AudioMediaPlayer *p) { return p ? p->volume : 0.0f; }
int audio_player_get_looping(const AudioMediaPlayer *p) { return p ? p->looping : 0; }
double audio_player_duration(const AudioMediaPlayer *p) { return p ? p->meta.duration_seconds : 0.0; }
const char *audio_player_path(const AudioMediaPlayer *p) { return p ? p->path : ""; }

void audio_player_set_looping(AudioMediaPlayer *p, int looping) {
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    p->looping = !!looping;
    if (p->looping && p->decoder_eof) {
        p->seek_target_sec = 0.0;
        p->seek_requested = 1;
        pthread_cond_signal(&p->cond);
    }
    pthread_mutex_unlock(&p->lock);
}
int audio_player_is_playing(const AudioMediaPlayer *p) {
    if (!p) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&p->lock);
    int res = p->playing && (!p->decoder_eof || p->buffered > 0);
    pthread_mutex_unlock((pthread_mutex_t *)&p->lock);
    return res;
}
int audio_player_is_eof(const AudioMediaPlayer *p) {
    if (!p) return 1;
    pthread_mutex_lock((pthread_mutex_t *)&p->lock);
    int res = p->decoder_eof && p->buffered == 0 && !p->seek_requested;
    pthread_mutex_unlock((pthread_mutex_t *)&p->lock);
    return res;
}
double audio_player_position(const AudioMediaPlayer *p) {
    if (!p) return 0.0;
    pthread_mutex_lock((pthread_mutex_t *)&p->lock);
    double pos = p->base_seek_sec + (double)p->played_frames / (double)AUDIO_INTERNAL_RATE;
    if (p->meta.duration_seconds > 0.0 && p->looping)
        while (pos >= p->meta.duration_seconds) pos -= p->meta.duration_seconds;
    pthread_mutex_unlock((pthread_mutex_t *)&p->lock);
    return pos;
}
size_t audio_player_buffered_frames(const AudioMediaPlayer *p) {
    if (!p) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&p->lock);
    size_t b = p->buffered;
    pthread_mutex_unlock((pthread_mutex_t *)&p->lock);
    return b;
}
int audio_player_metadata(const AudioMediaPlayer *p, AudioMetadata *out_meta) {
    if (!p || !out_meta) return -1;
    *out_meta = p->meta;
    return 0;
}
void audio_player_close(AudioMediaPlayer *p) {
    if (!p) return;
    pthread_mutex_lock(&svc.lock);
    for (int i = 0; i < MAX_MEDIA_PLAYERS; i++)
        if (svc.players[i] == p) svc.players[i] = NULL;
    pthread_mutex_unlock(&svc.lock);
    pthread_mutex_lock(&p->lock);
    p->stop_worker = 1; p->playing = 0;
    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->lock);
    pthread_join(p->thread, NULL);
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->cond);
    audio_decoder_close(p->decoder);
    free(p->ring); free(p);
}

int audio_submit_app_pcm_f32(const float *pcm_f32, size_t frames) {
    if (!pcm_f32 || frames == 0) return 0;
    pthread_mutex_lock(&svc.lock);
    size_t space = AUDIO_APP_RING_FRAMES - svc.app_buffered;
    size_t n = frames < space ? frames : space;
    for (size_t i = 0; i < n; i++) {
        svc.app_ring[svc.app_write * 2] = pcm_f32[i * 2];
        svc.app_ring[svc.app_write * 2 + 1] = pcm_f32[i * 2 + 1];
        svc.app_write = (svc.app_write + 1) % AUDIO_APP_RING_FRAMES;
    }
    svc.app_buffered += n;
    pthread_mutex_unlock(&svc.lock);
    return (int)n;
}

int audio_submit_app_pcm_s16(const int16_t *pcm_s16, size_t frames) {
    if (!pcm_s16 || frames == 0) return 0;
    float tmp[512 * 2];
    size_t total = 0;
    while (total < frames) {
        size_t chunk = (frames - total) < 512 ? (frames - total) : 512;
        for (size_t i = 0; i < chunk * 2; i++)
            tmp[i] = (float)pcm_s16[total * 2 + i] / 32768.0f;
        int w = audio_submit_app_pcm_f32(tmp, chunk);
        total += (size_t)w;
        if ((size_t)w < chunk) break;
    }
    return (int)total;
}

void audio_set_bus_volume(AudioBus bus, float volume) {
    if (bus < 0 || bus >= AUDIO_BUS_COUNT) return;
    volume = clamp_unit(volume);
    atomic_store_explicit(&svc.bus_vol_milli[bus], (int)(volume * 1000.0f + 0.5f), memory_order_relaxed);
}
float audio_get_bus_volume(AudioBus bus) {
    if (bus < 0 || bus >= AUDIO_BUS_COUNT) return 0.0f;
    return (float)atomic_load_explicit(&svc.bus_vol_milli[bus], memory_order_relaxed) / 1000.0f;
}
void audio_set_master_muted(int muted) {
    atomic_store_explicit(&svc.master_muted, muted ? 1 : 0, memory_order_relaxed);
    if (muted) audio_clear_system_voices();
}
int audio_get_master_muted(void) {
    return atomic_load_explicit(&svc.master_muted, memory_order_relaxed);
}
void audio_service_pause_output(int paused) {
    if (svc.output) audio_output_pause(svc.output, paused);
}
void audio_service_mix_frames(float *out_pcm_f32, size_t frames) {
    if (!out_pcm_f32 || frames == 0) return;
    memset(out_pcm_f32, 0, frames * 2 * sizeof(float));
    if (atomic_load_explicit(&svc.master_muted, memory_order_relaxed)) return;
    float sys_vol = audio_get_bus_volume(AUDIO_BUS_SYSTEM);
    float med_vol = audio_get_bus_volume(AUDIO_BUS_MEDIA);
    float app_vol = audio_get_bus_volume(AUDIO_BUS_APP);
    pthread_mutex_lock(&svc.lock);
    for (int v = 0; v < MAX_SYSTEM_VOICES; v++) {
        SystemVoice *vc = &svc.voices[v];
        if (!vc->active || !vc->sound) continue;
        size_t avail = vc->sound->frames - vc->pos;
        size_t take = frames < avail ? frames : avail;
        const float *src = vc->sound->pcm + vc->pos * 2;
        for (size_t i = 0; i < take * 2; i++) out_pcm_f32[i] += src[i] * sys_vol;
        vc->pos += take;
        if (vc->pos >= vc->sound->frames) vc->active = 0;
    }

    size_t app_take = frames < svc.app_buffered ? frames : svc.app_buffered;
    for (size_t i = 0; i < app_take; i++) {
        out_pcm_f32[i * 2] += svc.app_ring[svc.app_read * 2] * app_vol;
        out_pcm_f32[i * 2 + 1] += svc.app_ring[svc.app_read * 2 + 1] * app_vol;
        svc.app_read = (svc.app_read + 1) % AUDIO_APP_RING_FRAMES;
    }
    svc.app_buffered -= app_take;

    for (int m = 0; m < MAX_MEDIA_PLAYERS; m++) {
        AudioMediaPlayer *p = svc.players[m];
        if (!p) continue;
        pthread_mutex_lock(&p->lock);
        if (p->playing && p->buffered > 0) {
            size_t take = frames < p->buffered ? frames : p->buffered, done = 0;
            float gain = med_vol * p->volume, blk[512 * 2];
            while (done < take) {
                size_t n = (take - done) < 512 ? (take - done) : 512;
                for (size_t i = 0; i < n; i++) {
                    blk[i * 2] = p->ring[p->read_pos * 2] * gain;
                    blk[i * 2 + 1] = p->ring[p->read_pos * 2 + 1] * gain;
                    p->read_pos = (p->read_pos + 1) % p->ring_cap;
                }
                audio_dsp_process_media(blk, n, AUDIO_INTERNAL_RATE);
                for (size_t i = 0; i < n * 2; i++) out_pcm_f32[done * 2 + i] += blk[i];
                done += n;
            }
            p->buffered -= take; p->played_frames += take;
            if (p->buffered == 0 && p->decoder_eof && !p->looping) p->playing = 0;
            pthread_cond_signal(&p->cond);
        }
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&svc.lock);

    for (size_t i = 0; i < frames * 2; i++) out_pcm_f32[i] = clamp_sample(out_pcm_f32[i]);
}
