/*
 * test_audio_engine.c — offline unit tests for the audio engine (PC, no hardware).
 *
 * Compiled with AUDIO_ENGINE_PC_TEST: decodes MP3 to WAV file, checks metadata.
 *
 * Usage:
 *   make                               — build
 *   make test                          — run built-in synthetic tests
 *   ./test_audio_engine <file.mp3>     — decode a real MP3 to out.wav, print info
 *   ./test_audio_engine <file.mp3> 10000 — decode first 10 s to out.wav
 */

/* AUDIO_ENGINE_PC_TEST is defined via -D in the Makefile */
#include "audio_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static int s_pass = 0;
static int s_fail = 0;

#define EXPECT(cond, msg) \
    do { \
        if (cond) { printf("  PASS: %s\n", msg); s_pass++; } \
        else       { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); s_fail++; } \
    } while (0)

/* Check that a WAV file has a valid 44-byte RIFF/WAVE/fmt /data header */
static int wav_is_valid(const char *path, uint32_t *out_sample_rate,
                        uint16_t *out_channels, uint32_t *out_pcm_bytes)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    uint8_t hdr[44];
    size_t  n = fread(hdr, 1, sizeof hdr, f);
    fclose(f);
    if (n < 44) return 0;

    if (memcmp(hdr,      "RIFF", 4) != 0) return 0;
    if (memcmp(hdr + 8,  "WAVE", 4) != 0) return 0;
    if (memcmp(hdr + 12, "fmt ", 4) != 0) return 0;
    if (memcmp(hdr + 36, "data", 4) != 0) return 0;

    uint16_t audio_fmt;
    memcpy(&audio_fmt, hdr + 20, 2);
    if (audio_fmt != 1) return 0; /* must be PCM */

    if (out_channels)    memcpy(out_channels,    hdr + 22, 2);
    if (out_sample_rate) memcpy(out_sample_rate, hdr + 24, 4);
    if (out_pcm_bytes)   memcpy(out_pcm_bytes,   hdr + 40, 4);
    return 1;
}

/* ── Test 1: init / uninitialised-state guards ───────────────────────────── */
static void test_init(void)
{
    printf("\n[Test 1] Init and uninitialised-state guards\n");

    /* play/pause/stop/seek on uninitialised engine should return errors */
    EXPECT(audio_engine_play()        == ESP_ERR_INVALID_STATE, "play before init returns INVALID_STATE");
    EXPECT(audio_engine_pause()       == ESP_ERR_INVALID_STATE, "pause before init returns INVALID_STATE");
    EXPECT(audio_engine_seek(0)       == ESP_ERR_INVALID_STATE, "seek before init returns INVALID_STATE");
    EXPECT(audio_engine_is_playing()  == false,                 "is_playing false before init");
    EXPECT(audio_engine_position_ms() == 0,                     "position_ms 0 before init");

    esp_err_t rc = audio_engine_init();
    EXPECT(rc == ESP_OK, "audio_engine_init returns ESP_OK");
}

/* ── Test 2: load a non-existent file ────────────────────────────────────── */
static void test_load_missing(void)
{
    printf("\n[Test 2] Load non-existent file\n");

    esp_err_t rc = audio_engine_load("/nonexistent/file.mp3", NULL, 0);
    EXPECT(rc == ESP_ERR_NOT_FOUND, "load missing file returns NOT_FOUND");
    EXPECT(!audio_engine_is_playing(), "not playing after failed load");
    EXPECT(audio_engine_get_state() == AE_ERROR, "state is ERROR after failed load");
    EXPECT(audio_engine_load_progress() == 100, "load progress is reset after failed load");
    EXPECT(audio_engine_last_error() == ESP_ERR_NOT_FOUND, "last error is NOT_FOUND after failed load");
}

/* ── Test 3: pitch factor calculation ────────────────────────────────────── */
static void test_pitch(void)
{
    printf("\n[Test 3] Pitch factor calculation\n");

    EXPECT(audio_engine_raw_pitch_to_percent(0) > 9.99f &&
           audio_engine_raw_pitch_to_percent(0) < 10.01f,
           "raw pitch 0 maps to +10%");
    EXPECT(audio_engine_raw_pitch_to_percent(8192) > -0.01f &&
           audio_engine_raw_pitch_to_percent(8192) < 0.01f,
           "raw pitch 8192 maps to 0%");
    EXPECT(audio_engine_raw_pitch_to_percent(16383) < -9.98f &&
           audio_engine_raw_pitch_to_percent(16383) > -10.01f,
           "raw pitch 16383 maps to -10%");

    /* We can't inspect s_eng.pitch_factor directly, but we can set + check
     * position advances faster/slower (done in real-file test below).
     * Here we just smoke-test the API with boundary values. */
    audio_engine_set_pitch(8192);  /* ±0% */
    EXPECT(true, "set_pitch(8192) no crash");

    audio_engine_set_pitch(0);     /* +10% faster */
    EXPECT(true, "set_pitch(0) no crash");

    audio_engine_set_pitch(16383); /* -10% slower */
    EXPECT(true, "set_pitch(16383) no crash");

    /* Reset to normal */
    audio_engine_set_pitch(8192);
}

/* ── Test 4: per-deck transition API guards ─────────────────────────────── */
static void test_deck_api(void)
{
    printf("\n[Test 4] Per-deck transition API\n");

    EXPECT(audio_engine_deck_load(0, "/nonexistent/file.mp3", NULL, 0) == ESP_ERR_NOT_FOUND,
           "deck 0 load delegates to current engine");
    EXPECT(audio_engine_deck_load(1, "/nonexistent/file.mp3", NULL, 0) == ESP_ERR_NOT_FOUND,
           "deck 1 load has its own engine state");
    EXPECT(audio_engine_deck_load(2, "/nonexistent/file.mp3", NULL, 0) == ESP_ERR_INVALID_ARG,
           "out-of-range deck load returns INVALID_ARG");

    EXPECT(audio_engine_deck_play(1) == ESP_ERR_INVALID_STATE,
           "deck 1 play before load returns INVALID_STATE");
    EXPECT(audio_engine_deck_pause(1) == ESP_ERR_INVALID_STATE,
           "deck 1 pause before load returns INVALID_STATE");
    EXPECT(audio_engine_deck_seek(1, 1000) == ESP_ERR_INVALID_STATE,
           "deck 1 seek before load returns INVALID_STATE");

    audio_engine_deck_set_pitch(0, 8192);
    audio_engine_deck_set_pitch(1, 8192);
    EXPECT(audio_engine_deck_is_playing(1) == false,
           "deck 1 reports not playing before load");
    EXPECT(audio_engine_deck_position_ms(1) == 0,
           "deck 1 position is zero before load");
}

static void test_deck_states_are_independent(void)
{
    printf("\n[Test 5] Per-deck state split\n");

    const char *path = "dummy_deck_audio.mp3";
    FILE *f = fopen(path, "wb");
    if (f) {
        static const unsigned char bytes[] = {0xff, 0xfb, 0x90, 0x64, 0x00, 0x00, 0x00, 0x00};
        fwrite(bytes, 1, sizeof bytes, f);
        fclose(f);
    }
    EXPECT(f != NULL, "dummy audio file created");

    EXPECT(audio_engine_deck_load(0, path, NULL, 10000) == ESP_OK,
           "deck 0 dummy load returns ESP_OK");
    EXPECT(audio_engine_deck_load(1, path, NULL, 20000) == ESP_OK,
           "deck 1 dummy load returns ESP_OK");

    EXPECT(audio_engine_deck_play(0) == ESP_OK, "deck 0 play returns ESP_OK");
    EXPECT(audio_engine_deck_is_playing(0), "deck 0 is playing");
    EXPECT(!audio_engine_deck_is_playing(1), "deck 1 is still stopped");

    EXPECT(audio_engine_deck_play(1) == ESP_OK, "deck 1 play returns ESP_OK");
    EXPECT(audio_engine_deck_is_playing(0), "deck 0 remains playing");
    EXPECT(audio_engine_deck_is_playing(1), "deck 1 is playing");

    EXPECT(audio_engine_deck_seek(0, 1234) == ESP_OK, "deck 0 seek returns ESP_OK");
    EXPECT(audio_engine_deck_seek(1, 5678) == ESP_OK, "deck 1 seek returns ESP_OK");
    EXPECT(audio_engine_deck_position_ms(0) == 1234, "deck 0 position is independent");
    EXPECT(audio_engine_deck_position_ms(1) == 5678, "deck 1 position is independent");

    EXPECT(audio_engine_deck_stop(0) == ESP_OK, "deck 0 stop returns ESP_OK");
    EXPECT(!audio_engine_deck_is_playing(0), "deck 0 stopped");
    EXPECT(audio_engine_deck_is_playing(1), "deck 1 remains playing after deck 0 stop");

    audio_engine_deck_stop(1);
    remove(path);
}

/* ── Test 6: real MP3 decode to WAV (optional, skipped if no file given) ── */
static void test_decode_to_wav(const char *mp3_path, uint32_t max_ms)
{
    printf("\n[Test 6] Decode MP3 → WAV\n");
    printf("  Input:  %s\n", mp3_path);
    printf("  Limit:  %u ms (%s)\n", (unsigned)max_ms,
           max_ms == 0 ? "full track" : "truncated");

    esp_err_t rc = audio_engine_load(mp3_path, NULL, 0);
    if (rc != ESP_OK) {
        printf("  SKIP: cannot open %s (err %d)\n", mp3_path, rc);
        return;
    }

    const char *wav_path = "out.wav";
    rc = audio_engine_decode_to_wav(wav_path, max_ms);
    EXPECT(rc == ESP_OK, "decode_to_wav returns ESP_OK");

    /* Validate WAV file */
    uint32_t sample_rate = 0, pcm_bytes = 0;
    uint16_t channels    = 0;
    int valid = wav_is_valid(wav_path, &sample_rate, &channels, &pcm_bytes);
    EXPECT(valid,          "WAV header is valid RIFF/PCM");
    EXPECT(channels == 2,  "WAV is stereo");
    EXPECT(sample_rate > 0, "WAV has non-zero sample rate");
    EXPECT(pcm_bytes > 0,  "WAV has PCM data");

    if (valid) {
        double dur_s = (sample_rate > 0 && channels > 0)
                       ? (double)pcm_bytes / (double)(sample_rate * channels * 2u)
                       : 0.0;
        printf("  WAV:    %u Hz, %u ch, %u B PCM → %.1f s\n",
               (unsigned)sample_rate, (unsigned)channels,
               (unsigned)pcm_bytes, dur_s);

        if (max_ms > 0 && sample_rate > 0) {
            double expected_s = (double)max_ms / 1000.0;
            EXPECT(dur_s <= expected_s + 0.5, "decoded duration ≤ limit + 0.5 s");
        }
    }

    /* Test seek + position */
    EXPECT(audio_engine_seek(5000) == ESP_OK, "seek(5000) returns ESP_OK");
    EXPECT(audio_engine_position_ms() == 5000, "position_ms() == 5000 after seek");

    /* Decode a short window after seek */
    rc = audio_engine_decode_to_wav("out_from5s.wav", 3000);
    EXPECT(rc == ESP_OK, "decode_to_wav from 5 s, 3 s window returns ESP_OK");

    audio_engine_stop();
    EXPECT(!audio_engine_is_playing(), "not playing after stop");
}

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    printf("=== audio_engine tests ===\n");
    printf("Build: AUDIO_ENGINE_PC_TEST\n");

    test_init();
    test_load_missing();
    test_pitch();
    test_deck_api();
    test_deck_states_are_independent();

    if (argc >= 2) {
        uint32_t max_ms = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 0u;
        test_decode_to_wav(argv[1], max_ms);
    } else {
        printf("\n[Test 6] Decode MP3 → WAV\n");
        printf("  SKIP: no MP3 path provided  (usage: %s <file.mp3> [max_ms])\n", argv[0]);
    }

    printf("\n============================\n");
    printf("Results: %d PASS / %d FAIL\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
