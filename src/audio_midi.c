#include "audio_midi.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { MAX_MIDI_FILE = 4 * 1024 * 1024, MAX_MIDI_EVENTS = 65536, MIDI_VOICES = 24 };

typedef struct {
    double time_sec;
    uint8_t status, data1, data2;
} MidiEvent;

typedef struct {
    int active;
    uint8_t note, channel;
    float freq, velocity, phase, env;
    int releasing;
} MidiVoice;

struct AudioMidiSynth {
    MidiEvent *events;
    size_t event_count;
    size_t next_event;
    double duration_sec;
    uint64_t current_frame;
    uint64_t total_frames;
    MidiVoice voices[MIDI_VOICES];
};

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int read_vlq(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*pos >= len) return -1;
        uint8_t b = buf[(*pos)++];
        v = (v << 7) | (b & 0x7Fu);
        if (!(b & 0x80u)) { *out = v; return 0; }
    }
    return -1;
}

static int cmp_event(const void *a, const void *b) {
    const MidiEvent *ea = a, *eb = b;
    return (ea->time_sec > eb->time_sec) - (ea->time_sec < eb->time_sec);
}

static int parse_smf(const char *path, MidiEvent **out_events, size_t *out_count, double *out_dur) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 18 || st.st_size > MAX_MIDI_FILE) {
        fclose(f); return -1;
    }
    size_t sz = (size_t)st.st_size;
    uint8_t *buf = malloc(sz);
    if (!buf || fread(buf, 1, sz, f) != sz) { fclose(f); free(buf); return -1; }
    fclose(f);
    if (memcmp(buf, "MThd", 4) != 0 || be32(buf + 4) < 6) { free(buf); return -1; }
    uint16_t fmt = be16(buf + 8), ntrks = be16(buf + 10), div = be16(buf + 12);
    if (fmt > 2 || ntrks == 0 || (div & 0x8000u) || div == 0) { free(buf); return -1; }

    MidiEvent *events = NULL;
    size_t count = 0, cap = 0;
    size_t at = 8 + be32(buf + 4);
    double tempo_us = 500000.0; /* 120 BPM default */
    double max_sec = 0.0;

    for (uint16_t t = 0; t < ntrks && at + 8 <= sz; t++) {
        uint32_t clen = be32(buf + at + 4);
        size_t trk_start = at + 8, trk_end = trk_start + clen;
        if (trk_end > sz) { free(events); free(buf); return -1; }
        if (memcmp(buf + at, "MTrk", 4) != 0) { at = trk_end; continue; }
        size_t pos = trk_start;
        uint8_t running = 0;
        double cur_sec = 0.0;
        while (pos < trk_end) {
            uint32_t delta = 0;
            if (read_vlq(buf, trk_end, &pos, &delta) != 0 || pos >= trk_end) break;
            cur_sec += ((double)delta * (tempo_us / 1000000.0)) / (double)div;
            if (cur_sec > max_sec) max_sec = cur_sec;
            uint8_t b = buf[pos];
            if (b == 0xFFu) {
                pos++;
                if (pos >= trk_end) break;
                uint8_t meta_type = buf[pos++];
                uint32_t mlen = 0;
                if (read_vlq(buf, trk_end, &pos, &mlen) != 0 || pos + mlen > trk_end) break;
                if (meta_type == 0x51u && mlen == 3)
                    tempo_us = (double)(((uint32_t)buf[pos] << 16) | ((uint32_t)buf[pos + 1] << 8) | buf[pos + 2]);
                pos += mlen;
            } else if (b == 0xF0u || b == 0xF7u) {
                pos++;
                uint32_t slen = 0;
                if (read_vlq(buf, trk_end, &pos, &slen) != 0 || pos + slen > trk_end) break;
                pos += slen;
            } else {
                uint8_t status = running;
                if (b & 0x80u) { status = b; pos++; running = status; }
                if (!(status & 0x80u) || pos >= trk_end) break;
                uint8_t hi = status & 0xF0u;
                uint8_t d1 = buf[pos++], d2 = 0;
                if (hi != 0xC0u && hi != 0xD0u) {
                    if (pos >= trk_end) break;
                    d2 = buf[pos++];
                }
                if ((hi == 0x80u || hi == 0x90u) && count < MAX_MIDI_EVENTS) {
                    if (count == cap) {
                        size_t ncap = cap ? cap * 2 : 256;
                        MidiEvent *ne = realloc(events, ncap * sizeof *events);
                        if (!ne) break;
                        events = ne; cap = ncap;
                    }
                    events[count++] = (MidiEvent){cur_sec, status, d1, d2};
                }
            }
        }
        at = trk_end;
    }
    free(buf);
    if (count == 0) { free(events); return -1; }
    qsort(events, count, sizeof *events, cmp_event);
    if (events[count - 1].time_sec + 0.5 > max_sec) max_sec = events[count - 1].time_sec + 0.5;
    if (out_events) *out_events = events; else free(events);
    if (out_count) *out_count = count;
    if (out_dur) *out_dur = max_sec;
    return 0;
}

int audio_midi_probe(const char *path, AudioMetadata *out_meta) {
    double dur = 0.0;
    size_t count = 0;
    if (!path || parse_smf(path, NULL, &count, &dur) != 0) return -1;
    if (out_meta) {
        memset(out_meta, 0, sizeof *out_meta);
        snprintf(out_meta->container_name, sizeof out_meta->container_name, "smf");
        snprintf(out_meta->codec_name, sizeof out_meta->codec_name, "midi_synth");
        snprintf(out_meta->codec_long_name, sizeof out_meta->codec_long_name, "OS999 Built-in Polyphonic MIDI Synthesizer");
        snprintf(out_meta->channel_layout_desc, sizeof out_meta->channel_layout_desc, "stereo");
        const char *slash = strrchr(path, '/');
        snprintf(out_meta->title, sizeof out_meta->title, "%s", slash ? slash + 1 : path);
        out_meta->sample_rate = AUDIO_INTERNAL_RATE;
        out_meta->channels = AUDIO_INTERNAL_CHANNELS;
        out_meta->duration_seconds = dur;
        out_meta->seekable = 1;
    }
    return 0;
}

int audio_midi_open(AudioMidiSynth **out, const char *path, AudioMetadata *out_meta) {
    if (!out || !path) return -1;
    *out = NULL;
    MidiEvent *events = NULL;
    size_t count = 0;
    double dur = 0.0;
    if (parse_smf(path, &events, &count, &dur) != 0) return -1;
    AudioMidiSynth *m = calloc(1, sizeof *m);
    if (!m) { free(events); return -1; }
    m->events = events;
    m->event_count = count;
    m->duration_sec = dur;
    m->total_frames = (uint64_t)(dur * (double)AUDIO_INTERNAL_RATE);
    if (m->total_frames == 0) m->total_frames = AUDIO_INTERNAL_RATE;
    if (out_meta) (void)audio_midi_probe(path, out_meta);
    *out = m;
    return 0;
}

static void trigger_midi(AudioMidiSynth *m, const MidiEvent *ev) {
    uint8_t hi = ev->status & 0xF0u, ch = ev->status & 0x0Fu;
    if (hi == 0x90u && ev->data2 > 0) {
        int slot = 0;
        for (int i = 0; i < MIDI_VOICES; i++) {
            if (!m->voices[i].active) { slot = i; break; }
            if (m->voices[i].env < m->voices[slot].env) slot = i;
        }
        m->voices[slot].active = 1;
        m->voices[slot].releasing = 0;
        m->voices[slot].note = ev->data1;
        m->voices[slot].channel = ch;
        m->voices[slot].freq = 440.0f * powf(2.0f, ((float)ev->data1 - 69.0f) / 12.0f);
        m->voices[slot].velocity = (float)ev->data2 / 127.0f;
        m->voices[slot].phase = 0.0f;
        m->voices[slot].env = 1.0f;
    } else if (hi == 0x80u || (hi == 0x90u && ev->data2 == 0)) {
        for (int i = 0; i < MIDI_VOICES; i++) {
            if (m->voices[i].active && m->voices[i].note == ev->data1 && m->voices[i].channel == ch)
                m->voices[i].releasing = 1;
        }
    }
}

int audio_midi_read_frames(AudioMidiSynth *m, float *pcm_f32, size_t max_frames, int *out_eof) {
    if (!m || !pcm_f32) return -1;
    if (out_eof) *out_eof = 0;
    if (m->current_frame >= m->total_frames) {
        if (out_eof) *out_eof = 1;
        return 0;
    }
    size_t remain = (size_t)(m->total_frames - m->current_frame);
    size_t n = max_frames < remain ? max_frames : remain;
    for (size_t i = 0; i < n; i++) {
        double t = (double)(m->current_frame + i) / (double)AUDIO_INTERNAL_RATE;
        while (m->next_event < m->event_count && m->events[m->next_event].time_sec <= t)
            trigger_midi(m, &m->events[m->next_event++]);
        float left = 0.0f, right = 0.0f;
        for (int v = 0; v < MIDI_VOICES; v++) {
            MidiVoice *vc = &m->voices[v];
            if (!vc->active) continue;
            float s = sinf(vc->phase * 6.2831853f) * 0.7f + sinf(vc->phase * 12.5663706f) * 0.3f;
            float sample = s * vc->velocity * vc->env * 0.18f;
            float pan = 0.35f + 0.3f * ((float)(vc->note & 7) / 7.0f);
            left += sample * (1.0f - pan);
            right += sample * pan;
            vc->phase += vc->freq / (float)AUDIO_INTERNAL_RATE;
            if (vc->phase >= 1.0f) vc->phase -= 1.0f;
            vc->env *= vc->releasing ? 0.9985f : 0.99992f;
            if (vc->env < 0.001f) vc->active = 0;
        }
        pcm_f32[i * 2] = left;
        pcm_f32[i * 2 + 1] = right;
    }
    m->current_frame += n;
    if (m->current_frame >= m->total_frames && out_eof) *out_eof = 1;
    return (int)n;
}

int audio_midi_seek(AudioMidiSynth *m, double seconds) {
    if (!m) return -1;
    if (seconds < 0.0) seconds = 0.0;
    if (seconds > m->duration_sec) seconds = m->duration_sec;
    m->current_frame = (uint64_t)(seconds * (double)AUDIO_INTERNAL_RATE);
    memset(m->voices, 0, sizeof m->voices);
    m->next_event = 0;
    while (m->next_event < m->event_count && m->events[m->next_event].time_sec < seconds)
        m->next_event++;
    return 0;
}

void audio_midi_close(AudioMidiSynth *m) {
    if (!m) return;
    free(m->events);
    free(m);
}
