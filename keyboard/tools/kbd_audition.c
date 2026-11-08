/* Type a sentence on every switch family into WAV files: kbd_audition OUTDIR */
#include "kbd.h"
#include "kbd_audio.h"
#include "../src/kbd_skin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

static void wav(const char *path, const int16_t *pcm, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t bytes = (uint32_t)frames * 4;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, KBD_SYNTH_RATE); put32(f, KBD_SYNTH_RATE * 4);
    put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(pcm, 4, (size_t)frames, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    static const char *names[] = { "dome", "alps", "mitsumi", "scissor" };
    const char *text = "The quick brown fox jumps over the lazy dog.\n";
    KbdSoundBank *bank = kbd_bank_create();
    int total = KBD_SYNTH_RATE * 12;
    int16_t *pcm = malloc((size_t)total * 4);
    for (int fam = 0; fam < KBD_SW_COUNT; fam++) {
        KbdMixer m;
        kbd_mixer_init(&m);
        uint32_t seed = 12345;
        int at = KBD_SYNTH_RATE / 4, done = 0, n = (int)strlen(text);
        int events[256][4], ne = 0;   /* frame, sound, variant, key */
        for (int i = 0; i < n && ne < 250; i++) {
            seed = seed * 1103515245u + 12345u;
            int wide = text[i] == ' ' || text[i] == '\n';
            int hold = KBD_SYNTH_RATE * (70 + (int)(seed >> 24) % 40) / 1000;
            events[ne][0] = at; events[ne][1] = wide ? KBD_SND_WIDE_PRESS : KBD_SND_PRESS;
            events[ne][2] = (int)(seed >> 16) & 3; events[ne++][3] = text[i];
            events[ne][0] = at + hold; events[ne][1] = wide ? KBD_SND_WIDE_RELEASE : KBD_SND_RELEASE;
            events[ne][2] = (int)(seed >> 20) & 3; events[ne++][3] = text[i];
            at += KBD_SYNTH_RATE * (95 + (int)(seed >> 26) % 70) / 1000;
        }
        /* trackpad click pair at the end */
        events[ne][0] = at + KBD_SYNTH_RATE / 2; events[ne][1] = KBD_SND_PAD_CLICK; events[ne][2] = 0; events[ne++][3] = 'm';
        events[ne][0] = at + KBD_SYNTH_RATE / 2 + 5000; events[ne][1] = KBD_SND_PAD_RELEASE; events[ne][2] = 1; events[ne++][3] = 'm';
        int e = 0;
        while (done < total) {
            int chunk = 256;
            while (e < ne && events[e][0] <= done) {
                int key = events[e][3];
                float pan = key >= 'a' && key <= 'z' ? ((float)(key - 'a') / 25.0f - 0.5f) * 0.8f : 0;
                kbd_mixer_trigger(&m, kbd_bank_get(bank, fam, events[e][1], events[e][2]),
                                  1.0f + (float)((e * 37) % 11 - 5) * 0.005f, 0.9f, pan);
                e++;
            }
            if (done + chunk > total) chunk = total - done;
            kbd_mixer_render(&m, pcm + 2 * done, chunk);
            done += chunk;
        }
        char path[512];
        snprintf(path, sizeof path, "%s/typing-%s.wav", dir, names[fam]);
        wav(path, pcm, total);
    }
    free(pcm);
    kbd_bank_destroy(bank);
    return 0;
}
