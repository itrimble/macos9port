#include "runtime_audio.h"
#include "mac_sound_int.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

#define SP 0x8000u

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) {
    CHECK(mac_cpu_write(rt->cpu, p, n, v) == 0);
}

static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) {
    return mac_cpu_read(rt->cpu, p, n);
}

static uint32_t create_channel_with_sample(MacRuntime *rt) {
    wr(rt, SP, 4, 0);
    wr(rt, SP + 4, 4, 0x8c);
    wr(rt, SP + 8, 2, 5);
    wr(rt, SP + 10, 4, 0x7000);
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    CHECK(mac_sound_service(rt, 0xa807) == 1);
    uint32_t chan = rd(rt, 0x7000, 4);
    CHECK(chan != 0);

    /* Authored 8-bit sample buffer at 48 kHz containing 8 frames of 0xFF (32512). */
    wr(rt, 0x4100, 4, 0);
    wr(rt, 0x4104, 4, 8);
    wr(rt, 0x4108, 4, 48000u << 16);
    wr(rt, 0x4114, 1, 0);
    memset(mac_cpu_ram(rt->cpu) + 0x4116, 255, 8);

    /* Queue bufferCmd (81). */
    wr(rt, 0x4000, 2, 81);
    wr(rt, 0x4002, 2, 0);
    wr(rt, 0x4004, 4, 0x4100);
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    wr(rt, SP, 2, 0x01aa);
    wr(rt, SP + 2, 4, 0x4000);
    wr(rt, SP + 6, 4, chan);
    CHECK(mac_sound_service(rt, 0xa803) == 1);
    return chan;
}

int main(void) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    char tmpl[] = "/tmp/ospi-rtaudio-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (!dir) return 1;

    char wav_path[1024];
    snprintf(wav_path, sizeof wav_path, "%s/producer.wav", dir);
    CHECK(setenv("OSPI_AUDIO_WAV", wav_path, 1) == 0);
    CHECK(setenv("OSPI_AUDIO_TRACE", "1", 1) == 0);

    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(rt.cpu != NULL);
    rt.heap_next = 0x100000;
    rt.heap_limit = 0x200000;

    RuntimeAudio audio = {0};
    size_t cb_inst = 99;
    /* Without an active sound manager, pump is a no-op and does not open SDL audio. */
    CHECK(runtime_audio_pump(&audio, &rt, 10000, &cb_inst) == 0);
    CHECK(cb_inst == 0 && audio.device == 0);

    (void)create_channel_with_sample(&rt);
    CHECK(runtime_audio_pump(&audio, &rt, 10000, &cb_inst) == 0);
    CHECK(audio.device != 0);
    CHECK(audio.started == 1);
    CHECK(audio.submitted_frames == 4096);
    CHECK(audio.capture_frames == 4096);
    CHECK(audio.ring[0] == 32512 && audio.ring[1] == 32512);
    CHECK(audio.capture_pcm != NULL && audio.capture_pcm[0] == 32512);

    runtime_audio_close(&audio);
    CHECK(audio.device == 0 && audio.capture_pcm == NULL);

    /* Verify written RIFF/WAVE file header and synthesized PCM payload. */
    struct stat st;
    CHECK(stat(wav_path, &st) == 0);
    CHECK(st.st_size == 44 + 4096 * 4);
    FILE *wf = fopen(wav_path, "rb");
    CHECK(wf != NULL);
    if (wf) {
        uint8_t hdr[60] = {0};
        CHECK(fread(hdr, 1, sizeof hdr, wf) == sizeof hdr);
        fclose(wf);
        CHECK(memcmp(hdr, "RIFF", 4) == 0);
        CHECK(le32(hdr + 4) == 36 + 4096 * 4);
        CHECK(memcmp(hdr + 8, "WAVEfmt ", 8) == 0);
        CHECK(le32(hdr + 16) == 16);
        CHECK(le16(hdr + 20) == 1);
        CHECK(le16(hdr + 22) == 2);
        CHECK(le32(hdr + 24) == MAC_SOUND_RATE);
        CHECK(le16(hdr + 34) == 16);
        CHECK(memcmp(hdr + 36, "data", 4) == 0);
        CHECK(le32(hdr + 40) == 4096 * 4);
        CHECK((int16_t)le16(hdr + 44) == 32512);
        CHECK((int16_t)le16(hdr + 46) == 32512);
    }

    /* Overlong OSPI_AUDIO_WAV path disables capture cleanly without failing open. */
    char long_path[1100];
    memset(long_path, 'a', sizeof long_path - 1);
    long_path[sizeof long_path - 1] = 0;
    CHECK(setenv("OSPI_AUDIO_WAV", long_path, 1) == 0);
    CHECK(runtime_audio_open(&audio) == 0);
    CHECK(audio.capture_pcm == NULL && audio.capture_capacity_frames == 0);
    runtime_audio_close(&audio);

    unsetenv("OSPI_AUDIO_WAV");
    unsetenv("OSPI_AUDIO_TRACE");
    mac_sound_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    CHECK(unlink(wav_path) == 0);
    CHECK(rmdir(dir) == 0);

    if (fails) {
        fprintf(stderr, "runtime audio checks: %d failed\n", fails);
        return 1;
    }
    puts("runtime audio ring and WAV capture checks: ok");
    return 0;
}
