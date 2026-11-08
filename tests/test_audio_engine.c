#include "audio_decoder.h"
#include "audio_midi.h"
#include "audio_output.h"
#include "audio_service.h"
#include "desktop_sound.h"
#include "media_player_panel.h"
#include "ospi.h"
#include "finder_apps.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int run_ffmpeg(const char *args) {
    char cmd[2048];
    snprintf(cmd, sizeof cmd,
             "ffmpeg -y -v error -f lavfi -i \"sine=frequency=440:sample_rate=44100:duration=0.35\" %s",
             args);
    return system(cmd) == 0;
}

static int write_aifc_mace(const char *path, const char *fourcc) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint8_t payload[600];
    for (size_t i = 0; i < sizeof payload; i++) {
        payload[i] = (uint8_t)((i * 73u + 0x5Au) & 0xFFu);
    }
    uint32_t ssnd_size = 8 + (uint32_t)sizeof payload;
    uint32_t form_size = 4 + (8 + 26) + (8 + ssnd_size);
    uint8_t header[50] = {
        'F','O','R','M',
        (uint8_t)(form_size >> 24), (uint8_t)(form_size >> 16),
        (uint8_t)(form_size >> 8), (uint8_t)form_size,
        'A','I','F','C',
        'C','O','M','M', 0,0,0,26,
        0,1,                               /* 1 channel */
        0,0,0x07,0x08,                     /* 1800 sample frames */
        0,16,                              /* 16-bit */
        0x40,0x0D,0xAC,0x44,0,0,0,0,0,0,   /* 22050 Hz 80-bit float */
        (uint8_t)fourcc[0], (uint8_t)fourcc[1], (uint8_t)fourcc[2], (uint8_t)fourcc[3],
        3,'M','A','C',
        'S','S','N','D'
    };
    uint8_t ssnd_hdr[12] = {
        (uint8_t)(ssnd_size >> 24), (uint8_t)(ssnd_size >> 16),
        (uint8_t)(ssnd_size >> 8), (uint8_t)ssnd_size,
        0,0,0,0, 0,0,0,0
    };
    fwrite(header, 1, sizeof header, f);
    fwrite(ssnd_hdr, 1, sizeof ssnd_hdr, f);
    fwrite(payload, 1, sizeof payload, f);
    fclose(f);
    return 1;
}

static int write_test_midi(const char *path) {
    /* Valid SMF Format 0 playing C4-E4-G4 major triad for 240 ticks */
    static const uint8_t smf[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,120,
        'M','T','r','k', 0,0,0,29,
        0x00, 0x90, 60, 100,
        0x00, 0x90, 64, 95,
        0x00, 0x90, 67, 90,
        0x81, 0x70, 0x80, 60, 0,
        0x00, 0x80, 64, 0,
        0x00, 0x80, 67, 0,
        0x00, 0xFF, 0x2F, 0x00
    };
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(smf, 1, sizeof smf, f);
    fclose(f);
    return n == sizeof smf;
}

static int verify_decode_file(const char *path, const char *expected_codec_substr,
                              int expected_src_channels) {
    AudioMetadata meta;
    if (audio_decoder_probe(path, &meta) != 0) {
        fprintf(stderr, "probe failed for %s\n", path);
        return 0;
    }
    if (expected_codec_substr && !strstr(meta.codec_name, expected_codec_substr)) {
        fprintf(stderr, "codec mismatch for %s: got '%s', expected '%s'\n",
                path, meta.codec_name, expected_codec_substr);
        return 0;
    }
    if (expected_src_channels > 0 && meta.channels != expected_src_channels) {
        fprintf(stderr, "channels mismatch for %s: got %d, expected %d\n",
                path, meta.channels, expected_src_channels);
        return 0;
    }
    AudioDecoder *dec = NULL;
    if (audio_decoder_open(&dec, path) != 0 || !dec) return 0;
    float pcm[4096 * AUDIO_INTERNAL_CHANNELS];
    int total_frames = 0;
    float peak = 0.0f;
    for (;;) {
        int eof = 0;
        int n = audio_decoder_read_frames(dec, pcm, 1024, &eof);
        if (n < 0) { audio_decoder_close(dec); return 0; }
        for (int i = 0; i < n * AUDIO_INTERNAL_CHANNELS; i++) {
            float a = fabsf(pcm[i]);
            if (a > peak) peak = a;
        }
        total_frames += n;
        if (eof || n == 0) break;
    }
    audio_decoder_close(dec);
    return total_frames > 100 && peak > 0.001f;
}

static void test_format_matrix(const char *dir) {
    char p[512], args[1024];

    /* 1. Everyday audio: MP3, AAC in M4A, FLAC, ALAC, Vorbis, Opus */
    snprintf(p, sizeof p, "%s/test.mp3", dir);
    snprintf(args, sizeof args, "-c:a libmp3lame -b:a 128k \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "mp3", 1));

    snprintf(p, sizeof p, "%s/test_aac.m4a", dir);
    snprintf(args, sizeof args, "-c:a aac -b:a 128k \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "aac", 1));

    snprintf(p, sizeof p, "%s/test.flac", dir);
    snprintf(args, sizeof args, "-c:a flac \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "flac", 1));

    snprintf(p, sizeof p, "%s/test_alac.m4a", dir);
    snprintf(args, sizeof args, "-c:a alac \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "alac", 1));

    snprintf(p, sizeof p, "%s/test.ogg", dir);
    snprintf(args, sizeof args, "-strict -2 -c:a vorbis -ac 2 \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "vorbis", 2));

    snprintf(p, sizeof p, "%s/test.opus", dir);
    snprintf(args, sizeof args, "-c:a libopus \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "opus", 1));

    /* Content probing without extension or with misleading extension */
    char noext[512], fake_txt[512];
    snprintf(noext, sizeof noext, "%s/mystery_blob_no_ext", dir);
    snprintf(fake_txt, sizeof fake_txt, "%s/misleading_name.txt", dir);
    snprintf(args, sizeof args, "cp \"%s/test_aac.m4a\" \"%s\" && cp \"%s/test_aac.m4a\" \"%s\"",
             dir, noext, dir, fake_txt);
    CHECK(system(args) == 0);
    CHECK(verify_decode_file(noext, "aac", 1));
    CHECK(verify_decode_file(fake_txt, "aac", 1));

    /* 2. Uncompressed / sample audio: WAV, AIFF, CAF, u8 PCM, f32 PCM */
    snprintf(p, sizeof p, "%s/test_s16.wav", dir);
    snprintf(args, sizeof args, "-c:a pcm_s16le \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_s16le", 1));

    snprintf(p, sizeof p, "%s/test.aiff", dir);
    snprintf(args, sizeof args, "-c:a pcm_s16be \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_s16be", 1));

    snprintf(p, sizeof p, "%s/test.caf", dir);
    snprintf(args, sizeof args, "-c:a pcm_s16le \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_s16le", 1));

    snprintf(p, sizeof p, "%s/test_u8.wav", dir);
    snprintf(args, sizeof args, "-c:a pcm_u8 \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_u8", 1));

    snprintf(p, sizeof p, "%s/test_f32.wav", dir);
    snprintf(args, sizeof args, "-c:a pcm_f32le \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_f32le", 1));

    /* 3. Older desktop media: WMA, RealAudio, AU, ADPCM */
    snprintf(p, sizeof p, "%s/test.wma", dir);
    snprintf(args, sizeof args, "-c:a wmav2 -b:a 128k \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "wmav2", 1));

    snprintf(p, sizeof p, "%s/test.rm", dir);
    snprintf(args, sizeof args, "-ar 8000 -ac 1 -c:a ra_144 \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "real_144", 1));

    snprintf(p, sizeof p, "%s/test.au", dir);
    snprintf(args, sizeof args, "-c:a pcm_s16be \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_s16be", 1));

    snprintf(p, sizeof p, "%s/test_adpcm.wav", dir);
    snprintf(args, sizeof args, "-c:a adpcm_ms \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "adpcm_ms", 1));

    /* 4. Surround / movie audio: AC-3 (5.1 6ch), E-AC-3, DTS, TrueHD */
    snprintf(p, sizeof p, "%s/test_51.ac3", dir);
    snprintf(args, sizeof args, "-ac 6 -c:a ac3 -b:a 384k \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "ac3", 6));

    snprintf(p, sizeof p, "%s/test.eac3", dir);
    snprintf(args, sizeof args, "-c:a eac3 -b:a 192k \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "eac3", 1));

    snprintf(p, sizeof p, "%s/test.dts", dir);
    snprintf(args, sizeof args, "-strict -2 -ac 2 -c:a dca -b:a 512k \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "dca", 2));

    snprintf(p, sizeof p, "%s/test.thd", dir);
    snprintf(args, sizeof args, "-strict -2 -ar 48000 -c:a truehd \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "truehd", 1));

    /* 5. Less-common lossless: WavPack */
    snprintf(p, sizeof p, "%s/test.wv", dir);
    snprintf(args, sizeof args, "-c:a wavpack \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "wavpack", 1));

    /* 6. Classic Mac-era compression: QuickTime IMA ADPCM, MACE 3:1, MACE 6:1 */
    snprintf(p, sizeof p, "%s/test_qt_ima.mov", dir);
    snprintf(args, sizeof args, "-c:a adpcm_ima_qt \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "adpcm_ima_qt", 1));

    snprintf(p, sizeof p, "%s/test_mace3.aifc", dir);
    CHECK(write_aifc_mace(p, "MAC3") && verify_decode_file(p, "mace3", 1));

    snprintf(p, sizeof p, "%s/test_mace6.aifc", dir);
    CHECK(write_aifc_mace(p, "MAC6") && verify_decode_file(p, "mace6", 1));

    /* 7. Retro / Synth: Standard MIDI File (.mid) */
    snprintf(p, sizeof p, "%s/test_theme.mid", dir);
    CHECK(write_test_midi(p) && verify_decode_file(p, "midi_synth", 2));
}

static void test_seeking_rates_and_corrupt(const char *dir) {
    char p[512], args[1024];

    /* Unusual sample rates: Classic Mac 22254 Hz and 96000 Hz */
    snprintf(p, sizeof p, "%s/rate_22254.wav", dir);
    snprintf(args, sizeof args, "-ar 22254 -c:a pcm_s16le \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "pcm_s16le", 1));

    snprintf(p, sizeof p, "%s/rate_96000.flac", dir);
    snprintf(args, sizeof args, "-ar 96000 -c:a flac \"%s\"", p);
    CHECK(run_ffmpeg(args) && verify_decode_file(p, "flac", 1));

    /* Seeking and EOF draining */
    AudioDecoder *dec = NULL;
    CHECK(audio_decoder_open(&dec, p) == 0 && dec != NULL);
    if (dec) {
        float pcm[1024 * AUDIO_INTERNAL_CHANNELS];
        int eof = 0;
        CHECK(audio_decoder_read_frames(dec, pcm, 512, &eof) > 0);
        CHECK(audio_decoder_seek(dec, 0.10) == 0);
        CHECK(audio_decoder_read_frames(dec, pcm, 512, &eof) > 0);
        CHECK(audio_decoder_seek(dec, 0.0) == 0);
        int drained = 0;
        while (!eof) {
            int n = audio_decoder_read_frames(dec, pcm, 1024, &eof);
            if (n <= 0) break;
            drained += n;
        }
        CHECK(drained > 0);
        CHECK(audio_decoder_read_frames(dec, pcm, 1024, &eof) == 0 && eof == 1);
        audio_decoder_close(dec);
    }

    /* Corrupt input rejection */
    snprintf(p, sizeof p, "%s/corrupt.mp3", dir);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) {
        const char bad[] = "NOT_AN_AUDIO_FILE_0123456789";
        fwrite(bad, 1, sizeof bad, f);
        fclose(f);
    }
    AudioMetadata meta;
    CHECK(audio_decoder_probe(p, &meta) != 0);
    AudioDecoder *bad_dec = NULL;
    CHECK(audio_decoder_open(&bad_dec, p) != 0 && bad_dec == NULL);

    /* Supported Formats runtime enumeration */
    AudioFormatSupport formats[64];
    size_t count = audio_decoder_enumerate_formats(formats, 64);
    CHECK(count >= 25);
    size_t available_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (formats[i].available) available_count++;
    }
    CHECK(available_count >= 25);
}

static void test_service_mixer_and_buses(const char *dir, const char *sounds_dir) {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    CHECK(audio_service_init(sounds_dir) == 0);
    audio_service_pause_output(1);
    CHECK(audio_cached_sound_count() >= 10);

    /* Short-sound cache trigger on System bus */
    CHECK(audio_play_system_sound("trash.empty"));
    float mix[1024 * AUDIO_INTERNAL_CHANNELS];
    audio_service_mix_frames(mix, 1024);
    float sys_energy = 0.0f;
    for (int i = 0; i < 1024 * AUDIO_INTERNAL_CHANNELS; i++) sys_energy += fabsf(mix[i]);
    CHECK(sys_energy > 0.1f);
    audio_clear_system_voices();

    /* App bus PCM submission */
    float app_tone[512 * AUDIO_INTERNAL_CHANNELS];
    for (int i = 0; i < 512 * AUDIO_INTERNAL_CHANNELS; i++) app_tone[i] = 0.25f;
    audio_set_bus_volume(AUDIO_BUS_SYSTEM, 0.0f);
    audio_set_bus_volume(AUDIO_BUS_APP, 0.5f);
    CHECK(audio_submit_app_pcm_f32(app_tone, 512) == 512);
    audio_service_mix_frames(mix, 256);
    CHECK(fabsf(mix[0] - 0.125f) < 0.01f);

    /* Master mute silences output */
    audio_set_master_muted(1);
    CHECK(audio_get_master_muted() == 1);
    audio_service_mix_frames(mix, 256);
    CHECK(fabsf(mix[0]) < 0.0001f);
    audio_set_master_muted(0);
    audio_set_bus_volume(AUDIO_BUS_SYSTEM, 0.8f);
    audio_set_bus_volume(AUDIO_BUS_APP, 0.0f);

    /* Streaming worker thread media player with bounded buffer */
    char mp3_path[512];
    snprintf(mp3_path, sizeof mp3_path, "%s/test.mp3", dir);
    AudioMediaPlayer *player = audio_open_media(mp3_path);
    CHECK(player != NULL);
    if (player) {
        audio_player_set_volume(player, 0.8f);
        audio_player_set_looping(player, 1);
        CHECK(audio_player_get_looping(player) == 1);
        audio_player_play(player);
        for (int tries = 0; tries < 50 && audio_player_buffered_frames(player) < 2048; tries++) {
            usleep(5000);
        }
        size_t buffered = audio_player_buffered_frames(player);
        CHECK(buffered > 0 && buffered <= AUDIO_MEDIA_RING_FRAMES);
        audio_eq_apply_preset(1);
        CHECK(strcmp(audio_eq_preset_name(audio_eq_get_preset()), "Rock") == 0);
        CHECK(audio_eq_get_band(0) > 4.0f);
        audio_set_media_balance(-1.0f);
        CHECK(fabsf(audio_get_media_balance() + 1.0f) < 0.01f);
        audio_service_mix_frames(mix, 512);
        float left_energy = 0.0f, right_energy = 0.0f;
        for (int i = 0; i < 512; i++) {
            left_energy += fabsf(mix[i * 2]);
            right_energy += fabsf(mix[i * 2 + 1]);
        }
        CHECK(left_energy > 0.01f && right_energy < 0.0001f);
        audio_set_media_balance(0.0f);
        audio_eq_apply_preset(0);
        uint8_t spec[AUDIO_VIS_BANDS], peaks[AUDIO_VIS_BANDS];
        int8_t wave[AUDIO_VIS_WAVE];
        audio_get_visualizer(spec, peaks, wave);
        int vis_sum = 0;
        for (int i = 0; i < AUDIO_VIS_BANDS; i++) vis_sum += spec[i] + peaks[i];
        CHECK(vis_sum > 0);
        audio_player_pause(player);
        CHECK(!audio_player_is_playing(player));
        CHECK(audio_player_seek(player, 0.05) == 0);
        audio_player_close(player);
    }
    audio_service_shutdown();
}

int test_media_panel_and_session(const char *dir, const char *sounds_dir);

int main(int argc, char **argv) {
    const char *sounds_dir = argc > 1 ? argv[1] : "assets/sounds/classic9";
    char tmp_template[] = "/tmp/os999-audio-test.XXXXXX";
    char *dir = mkdtemp(tmp_template);
    CHECK(dir != NULL);
    if (!dir) return 1;

    test_format_matrix(dir);
    test_seeking_rates_and_corrupt(dir);
    test_service_mixer_and_buses(dir, sounds_dir);
    failures += test_media_panel_and_session(dir, sounds_dir);

    char cleanup[640];
    snprintf(cleanup, sizeof cleanup, "rm -rf \"%s\"", dir);
    (void)system(cleanup);

    if (failures) {
        fprintf(stderr, "%d audio engine check(s) failed\n", failures);
        return 1;
    }
    printf("ok audio engine (FFmpeg + MIDI + SDL mixer + format matrix)\n");
    return 0;
}
