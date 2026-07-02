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
#include "audio_pcm_ring.h"
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

    audio_engine_set_pitch_percent(16.0f);
    EXPECT(true, "set_pitch_percent(+16%) no crash");

    audio_engine_deck_set_pitch_percent(1, -6.0f);
    EXPECT(true, "deck 1 set_pitch_percent(-6%) no crash");

    /* Reset to normal */
    audio_engine_set_pitch_percent(0.0f);
}

static int nearf(float actual, float expected)
{
    float diff = actual - expected;
    if (diff < 0.0f) diff = -diff;
    return diff < 0.01f;
}

/* ── Test 4: mixer state API ─────────────────────────────────────────────── */
static void test_mixer_state_api(void)
{
    printf("\n[Test 4] Mixer state API\n");

    float deck1 = 0.0f;
    float deck2 = 0.0f;

    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets mixer state");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck1, 1.0f), "default deck 0 gain is unity");
    EXPECT(nearf(deck2, 1.0f), "default deck 1 gain is unity");
    EXPECT(nearf(audio_engine_get_master_trim(), 1.0f), "default master trim is unity");
    EXPECT(audio_engine_get_master_volume() == AUDIO_MIXER_CONTROL_MAX,
           "default controller master volume is max");
    EXPECT(audio_engine_get_pregain(0) == AUDIO_MIXER_CONTROL_CENTER,
           "deck 0 pregain defaults to center");
    EXPECT(audio_engine_get_pregain(1) == AUDIO_MIXER_CONTROL_CENTER,
           "deck 1 pregain defaults to center");
    EXPECT(audio_engine_set_pregain(2, AUDIO_MIXER_CONTROL_CENTER) == ESP_ERR_INVALID_ARG,
           "invalid pregain deck returns INVALID_ARG");
    EXPECT(audio_engine_set_pregain(0, AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
           "deck 0 pregain accepts center raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck1, 1.0f), "center pregain leaves deck 0 gain at unity");
    EXPECT(audio_engine_set_pregain(0, 0) == ESP_OK,
           "deck 0 pregain accepts minimum raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(deck1 > 0.24f && deck1 < 0.26f, "minimum pregain attenuates deck 0 to quarter gain");
    EXPECT(audio_engine_set_pregain(0, AUDIO_MIXER_CONTROL_MAX) == ESP_OK,
           "deck 0 pregain accepts maximum raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(deck1 > 1.99f && deck1 < 2.01f, "maximum pregain boosts deck 0 to +6 dB scalar");
    EXPECT(audio_engine_set_pregain(0, AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
           "deck 0 pregain restores center before other mixer tests");

    EXPECT(audio_engine_set_channel_volume(0, 8192) == ESP_OK,
           "deck 0 channel volume accepts center raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck1, 0.5f), "deck 0 channel volume affects output gain");
    EXPECT(nearf(deck2, 1.0f), "deck 1 remains unity");

    EXPECT(audio_engine_set_channel_volume(1, 0) == ESP_OK,
           "deck 1 channel volume accepts zero raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck2, 0.0f), "deck 1 channel volume mutes output gain");

    EXPECT(audio_engine_set_channel_volume(2, 0) == ESP_ERR_INVALID_ARG,
           "invalid mixer deck returns INVALID_ARG");

    EXPECT(audio_engine_set_channel_volume(1, 16383) == ESP_OK,
           "deck 1 channel volume accepts max raw value");
    EXPECT(audio_engine_set_crossfader(16383) == ESP_OK,
           "crossfader accepts max raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck1, 0.0f), "crossfader right mutes deck 0");
    EXPECT(nearf(deck2, 1.0f), "crossfader right keeps deck 1");

    EXPECT(audio_engine_set_master_trim(0.5f) == ESP_OK, "master trim accepts attenuation");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck1, 0.0f), "master trim keeps muted deck muted");
    EXPECT(nearf(deck2, 0.5f), "master trim attenuates audible deck gain");
    EXPECT(audio_engine_set_master_trim(2.0f) == ESP_OK, "master trim clamps boost to unity");
    EXPECT(nearf(audio_engine_get_master_trim(), 1.0f), "master trim never boosts above unity");
    EXPECT(audio_engine_set_master_trim(-1.0f) == ESP_OK, "master trim clamps negative gain to mute");
    EXPECT(nearf(audio_engine_get_master_trim(), 0.0f), "master trim clamps negative gain to zero");
    EXPECT(audio_engine_set_master_trim(1.0f) == ESP_OK, "master trim can restore unity");
    EXPECT(audio_engine_set_master_volume(AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
           "controller master volume accepts center raw value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(deck2 > 0.49f && deck2 < 0.51f,
           "controller master volume attenuates audible deck gain");
    EXPECT(audio_engine_set_master_volume(AUDIO_MIXER_CONTROL_MAX) == ESP_OK,
           "controller master volume restores full raw value");

    audio_engine_mixer_snapshot_t snapshot;
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.channel_volume[0] == 8192, "snapshot captures deck 0 raw channel fader");
    EXPECT(snapshot.channel_volume[1] == 16383, "snapshot captures deck 1 raw channel fader");
    EXPECT(snapshot.crossfader == 16383, "snapshot captures raw crossfader");
    EXPECT(snapshot.pregain[0] == AUDIO_MIXER_CONTROL_CENTER,
           "snapshot captures deck 0 pregain raw value");
    EXPECT(snapshot.pregain[1] == AUDIO_MIXER_CONTROL_CENTER,
           "snapshot captures deck 1 pregain raw value");
    EXPECT(nearf(snapshot.pregain_gain[0], 1.0f),
           "snapshot captures deck 0 pregain scalar");
    EXPECT(nearf(snapshot.pregain_gain[1], 1.0f),
           "snapshot captures deck 1 pregain scalar");
    EXPECT(nearf(snapshot.master_trim, 1.0f), "snapshot captures master trim");
    EXPECT(snapshot.master_volume == AUDIO_MIXER_CONTROL_MAX,
           "snapshot captures controller master volume raw value");
    EXPECT(nearf(snapshot.output_gain[0], 0.0f), "snapshot captures deck 0 output gain");
    EXPECT(nearf(snapshot.output_gain[1], 1.0f), "snapshot captures deck 1 output gain");

    EXPECT(audio_engine_get_eq(0, AUDIO_EQ_BAND_LOW) == AUDIO_EQ_RAW_CENTER,
           "deck 0 low EQ defaults to center");
    EXPECT(audio_engine_set_eq(0, AUDIO_EQ_BAND_LOW, 4096) == ESP_OK,
           "deck 0 low EQ accepts raw value");
    EXPECT(audio_engine_set_eq(1, AUDIO_EQ_BAND_HIGH, AUDIO_EQ_RAW_MAX) == ESP_OK,
           "deck 1 high EQ accepts max raw value");
    EXPECT(audio_engine_get_eq(0, AUDIO_EQ_BAND_LOW) == 4096,
           "deck 0 low EQ stores raw value");
    EXPECT(audio_engine_get_eq(1, AUDIO_EQ_BAND_HIGH) == AUDIO_EQ_RAW_MAX,
           "deck 1 high EQ stores raw value");
    EXPECT(audio_engine_set_eq(2, AUDIO_EQ_BAND_LOW, 0) == ESP_ERR_INVALID_ARG,
           "invalid EQ deck returns INVALID_ARG");
    EXPECT(audio_engine_set_eq(0, AUDIO_EQ_BAND_COUNT, 0) == ESP_ERR_INVALID_ARG,
           "invalid EQ band returns INVALID_ARG");
    EXPECT(audio_engine_get_filter(0) == AUDIO_FILTER_RAW_CENTER,
           "deck 0 filter defaults to center");
    EXPECT(audio_engine_set_filter(0, 2048) == ESP_OK,
           "deck 0 filter accepts raw value");
    EXPECT(audio_engine_set_filter(1, 12000) == ESP_OK,
           "deck 1 filter accepts raw value");
    EXPECT(audio_engine_get_filter(0) == 2048,
           "deck 0 filter stores raw value");
    EXPECT(audio_engine_set_filter(2, 8192) == ESP_ERR_INVALID_ARG,
           "invalid filter deck returns INVALID_ARG");
    EXPECT(!audio_engine_get_smart_cfx_enabled(), "Smart CFX defaults off");
    EXPECT(audio_engine_toggle_smart_cfx() == ESP_OK, "Smart CFX toggles on");
    EXPECT(audio_engine_get_smart_cfx_enabled(), "Smart CFX state reads on");
    EXPECT(!audio_engine_get_smart_fader_enabled(), "Smart Fader defaults off");
    EXPECT(audio_engine_toggle_smart_fader() == ESP_OK, "Smart Fader toggles on");
    EXPECT(audio_engine_get_smart_fader_enabled(), "Smart Fader state reads on");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.eq[0][AUDIO_EQ_BAND_LOW] == 4096,
           "snapshot captures deck 0 low EQ raw value");
    EXPECT(snapshot.eq[1][AUDIO_EQ_BAND_HIGH] == AUDIO_EQ_RAW_MAX,
           "snapshot captures deck 1 high EQ raw value");
    EXPECT(snapshot.filter[0] == 2048, "snapshot captures deck 0 filter raw value");
    EXPECT(snapshot.filter[1] == 12000, "snapshot captures deck 1 filter raw value");
    EXPECT(snapshot.smart_cfx_enabled, "snapshot captures Smart CFX state");
    EXPECT(snapshot.smart_fader_enabled, "snapshot captures Smart Fader state");
    EXPECT(audio_engine_set_filter(0, AUDIO_FILTER_RAW_CENTER - 512u) == ESP_OK,
           "Smart CFX accepts near-center filter raw");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.filter[0] == AUDIO_FILTER_RAW_CENTER - 512u,
           "snapshot keeps raw Smart CFX filter value");
    EXPECT(snapshot.smart_cfx_filter_effective[0] > snapshot.filter[0],
           "Smart CFX softens near-center low-pass travel");
    EXPECT(snapshot.smart_cfx_filter_effective[0] < AUDIO_FILTER_RAW_CENTER,
           "Smart CFX remains on low-pass side");
    EXPECT(!snapshot.beat_fx_filter_enabled[0], "Beat FX filter defaults off for deck 0");
    EXPECT(!snapshot.beat_fx_filter_enabled[1], "Beat FX filter defaults off for deck 1");
    EXPECT(audio_engine_set_beat_fx_filter(AUDIO_ENGINE_BEAT_FX_TARGET_CH2, 127, true) == ESP_OK,
           "Beat FX filter accepts CH2 target");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(!snapshot.beat_fx_filter_enabled[0], "Beat FX filter leaves untargeted deck 0 off");
    EXPECT(snapshot.beat_fx_filter_enabled[1], "Beat FX filter enables targeted deck 1");
    EXPECT(snapshot.beat_fx_filter_raw[1] == AUDIO_FILTER_RAW_MIN,
           "Beat FX filter depth 127 maps to max low-pass");
    EXPECT(audio_engine_set_beat_fx_filter(AUDIO_ENGINE_BEAT_FX_TARGET_BOTH, 0, true) == ESP_OK,
           "Beat FX filter accepts zero-depth both target");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(!snapshot.beat_fx_filter_enabled[0], "Beat FX filter zero depth bypasses deck 0");
    EXPECT(!snapshot.beat_fx_filter_enabled[1], "Beat FX filter zero depth bypasses deck 1");
    EXPECT(audio_engine_set_beat_fx_filter(AUDIO_ENGINE_BEAT_FX_TARGET_BOTH, 64, false) == ESP_OK,
           "Beat FX filter accepts disabled state");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(!snapshot.beat_fx_filter_enabled[0], "Beat FX filter disabled clears deck 0");
    EXPECT(!snapshot.beat_fx_filter_enabled[1], "Beat FX filter disabled clears deck 1");
    EXPECT(!snapshot.beat_fx_echo_enabled[0], "Beat FX echo defaults off for deck 0");
    EXPECT(!snapshot.beat_fx_echo_enabled[1], "Beat FX echo defaults off for deck 1");
    EXPECT(audio_engine_set_beat_fx_echo(AUDIO_ENGINE_BEAT_FX_TARGET_CH1,
                                         64,
                                         500,
                                         true) == ESP_OK,
           "Beat FX echo accepts CH1 target");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.beat_fx_echo_enabled[0], "Beat FX echo enables targeted deck 0");
    EXPECT(!snapshot.beat_fx_echo_enabled[1], "Beat FX echo leaves untargeted deck 1 off");
    EXPECT(snapshot.beat_fx_echo_delay_ms[0] == 500, "Beat FX echo stores delay ms");
    EXPECT(audio_engine_set_beat_fx_echo(AUDIO_ENGINE_BEAT_FX_TARGET_BOTH,
                                         0,
                                         250,
                                         true) == ESP_OK,
           "Beat FX echo depth zero bypasses both decks");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(!snapshot.beat_fx_echo_enabled[0], "Beat FX echo zero depth disables deck 0");
    EXPECT(!snapshot.beat_fx_echo_enabled[1], "Beat FX echo zero depth disables deck 1");
    EXPECT(!snapshot.pad_fx_active[0], "Pad FX defaults off for deck 0");
    EXPECT(!snapshot.pad_fx_active[1], "Pad FX defaults off for deck 1");
    EXPECT(audio_engine_set_pad_fx(0, AUDIO_PAD_FX_MODE_PAD_FX1, 0, true) == ESP_OK,
           "Pad FX accepts deck 0 filter pad press");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.pad_fx_active[0], "Pad FX activates targeted deck 0");
    EXPECT(!snapshot.pad_fx_active[1], "Pad FX leaves deck 1 untouched");
    EXPECT(snapshot.pad_fx_kind[0] == AUDIO_PAD_FX_KIND_FILTER,
           "Pad FX snapshot reports filter kind");
    EXPECT(audio_engine_set_pad_fx(0, AUDIO_PAD_FX_MODE_PAD_FX1, 0, false) == ESP_OK,
           "Pad FX accepts matching pad release");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(!snapshot.pad_fx_active[0], "Pad FX release clears active deck 0");
    EXPECT(audio_engine_set_pad_fx(2, AUDIO_PAD_FX_MODE_PAD_FX1, 0, true) == ESP_ERR_INVALID_ARG,
           "Pad FX invalid deck returns INVALID_ARG");
    EXPECT(audio_engine_set_pad_fx(0, (audio_pad_fx_mode_t)99, 0, true) == ESP_ERR_INVALID_ARG,
           "Pad FX invalid mode returns INVALID_ARG");
    EXPECT(audio_engine_set_channel_volume(0, AUDIO_MIXER_CONTROL_MAX) == ESP_OK,
           "deck 0 channel volume restores max for Smart Fader assist test");
    EXPECT(audio_engine_set_channel_volume(1, AUDIO_MIXER_CONTROL_MAX) == ESP_OK,
           "deck 1 channel volume restores max for Smart Fader assist test");
    EXPECT(audio_engine_set_crossfader(AUDIO_MIXER_CONTROL_CENTER / 2u) == ESP_OK,
           "crossfader accepts left-side Smart Fader assist test value");
    audio_engine_get_output_gains(&deck1, &deck2);
    EXPECT(nearf(deck1, 1.0f), "Smart Fader keeps open side at unity");
    EXPECT(deck2 > 0.24f && deck2 < 0.26f,
           "Smart Fader squares fade-out side for cleaner transition");
    EXPECT(audio_engine_set_crossfader(AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
           "crossfader restores center after Smart Fader assist test");

    audio_mixer_limiter_stats_t limiter_stats = {
        .limited_samples = 7,
        .positive_overloads = 4,
        .negative_overloads = 3,
        .peak_input_abs = 50000,
    };
    audio_engine_test_record_limiter_stats(&limiter_stats);
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.limiter.limited_samples == 7, "snapshot captures limiter sample count");
    EXPECT(snapshot.limiter.positive_overloads == 4, "snapshot captures positive overload count");
    EXPECT(snapshot.limiter.negative_overloads == 3, "snapshot captures negative overload count");
    EXPECT(snapshot.limiter.peak_input_abs == 50000, "snapshot captures limiter peak input");

    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets limiter telemetry");
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.limiter.limited_samples == 0, "limiter sample count resets on init");
    EXPECT(snapshot.limiter.peak_input_abs == 0, "limiter peak resets on init");
}

static void test_deck_peak_meter_api_returns_and_resets_peak(void)
{
    puts("\n[Test 4b] Deck peak meter API");
    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets peak meter state");
    EXPECT(audio_engine_get_deck_peak(0) == 0, "deck 0 peak defaults to zero");
    EXPECT(audio_engine_get_deck_peak(2) == 0, "invalid deck peak reads as zero");

    audio_engine_test_record_deck_peak(0, -1234, 8000);
    audio_engine_test_record_deck_peak(0, 3000, -2000);
    audio_engine_test_record_deck_peak(1, 1000, -9000);

    EXPECT(audio_engine_get_deck_peak(0) == 8000, "deck 0 peak returns max absolute sample");
    EXPECT(audio_engine_get_deck_peak(0) == 0, "deck 0 peak resets after read");
    EXPECT(audio_engine_get_deck_peak(1) == 9000, "deck 1 peak is independent");
}

static void test_diagnostics_snapshot_reports_audio_health_state(void)
{
    puts("\n[Test 4c] Diagnostics snapshot API");
    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets diagnostics snapshot");

    audio_engine_diagnostics_snapshot_t diag;
    audio_engine_get_diagnostics_snapshot(&diag);
    EXPECT(diag.ring_capacity == AUDIO_PCM_RING_FRAMES, "diagnostics reports ring capacity");
    EXPECT(diag.ring_used[0] == 0, "diagnostics ring 0 starts empty");
    EXPECT(diag.ring_used[1] == 0, "diagnostics ring 1 starts empty");
    EXPECT(!diag.deck_active[0], "diagnostics deck 0 starts inactive");
    EXPECT(!diag.deck_active[1], "diagnostics deck 1 starts inactive");
    EXPECT(diag.deck_sample_rate[0] == 0, "diagnostics deck 0 sample rate starts unknown");
    EXPECT(diag.deck_sample_rate[1] == 0, "diagnostics deck 1 sample rate starts unknown");
    EXPECT(diag.deck_channels[0] == 0, "diagnostics deck 0 channel count starts unknown");
    EXPECT(diag.deck_channels[1] == 0, "diagnostics deck 1 channel count starts unknown");
    EXPECT(diag.deck_file_bytes[0] == 0, "diagnostics deck 0 file size starts empty");
    EXPECT(diag.deck_file_bytes[1] == 0, "diagnostics deck 1 file size starts empty");
    EXPECT(diag.deck_load_progress[0] == 0, "diagnostics deck 0 load progress starts empty");
    EXPECT(diag.deck_load_progress[1] == 0, "diagnostics deck 1 load progress starts empty");
    EXPECT(diag.limiter.limited_samples == 0, "diagnostics limiter starts clear");

    audio_mixer_limiter_stats_t limiter_stats = {
        .limited_samples = 9,
        .positive_overloads = 6,
        .negative_overloads = 3,
        .peak_input_abs = 48000,
    };
    audio_engine_test_record_limiter_stats(&limiter_stats);
    audio_engine_get_diagnostics_snapshot(&diag);
    EXPECT(diag.limiter.limited_samples == 9, "diagnostics includes limiter sample count");
    EXPECT(diag.limiter.positive_overloads == 6, "diagnostics includes positive overload count");
    EXPECT(diag.limiter.negative_overloads == 3, "diagnostics includes negative overload count");
    EXPECT(diag.limiter.peak_input_abs == 48000, "diagnostics includes limiter peak");

    const char *path = "dummy_diag_audio.mp3";
    FILE *fp = fopen(path, "wb");
    EXPECT(fp != NULL, "dummy diagnostics audio file created");
    if (fp) {
        fputc(0, fp);
        fclose(fp);
    }
    EXPECT(audio_engine_deck_load(0, path, NULL, 10000) == ESP_OK,
           "deck 0 dummy diagnostics load returns ESP_OK");
    EXPECT(audio_engine_deck_load(1, path, NULL, 10000) == ESP_OK,
           "deck 1 dummy diagnostics load returns ESP_OK");
    EXPECT(audio_engine_deck_play(0) == ESP_OK, "deck 0 diagnostics play returns ESP_OK");
    EXPECT(audio_engine_deck_play(1) == ESP_OK, "deck 1 diagnostics play returns ESP_OK");
    audio_engine_get_diagnostics_snapshot(&diag);
    EXPECT(diag.deck_active[0], "diagnostics captures deck 0 active");
    EXPECT(diag.deck_active[1], "diagnostics captures deck 1 active");
    EXPECT(diag.deck_file_bytes[0] > 0, "diagnostics captures deck 0 file size");
    EXPECT(diag.deck_file_bytes[1] > 0, "diagnostics captures deck 1 file size");
    EXPECT(diag.deck_load_progress[0] == 100, "diagnostics captures deck 0 load progress");
    EXPECT(diag.deck_load_progress[1] == 100, "diagnostics captures deck 1 load progress");
    remove(path);
}

/* ── Test 5: per-deck transition API guards ─────────────────────────────── */
static void test_pfl_state_api(void)
{
    printf("\n[Test 5] PFL state API\n");

    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets PFL state");
    EXPECT(!audio_engine_get_pfl_enabled(0), "deck 0 PFL defaults off");
    EXPECT(!audio_engine_get_pfl_enabled(1), "deck 1 PFL defaults off");

    EXPECT(audio_engine_toggle_pfl(0) == ESP_OK, "deck 0 PFL toggle returns ESP_OK");
    EXPECT(audio_engine_get_pfl_enabled(0), "deck 0 PFL toggles on");
    EXPECT(!audio_engine_get_pfl_enabled(1), "deck 1 PFL remains off");

    EXPECT(audio_engine_toggle_pfl(0) == ESP_OK, "deck 0 second PFL toggle returns ESP_OK");
    EXPECT(!audio_engine_get_pfl_enabled(0), "deck 0 PFL toggles off");

    EXPECT(audio_engine_toggle_pfl(1) == ESP_OK, "deck 1 PFL toggle returns ESP_OK");
    EXPECT(audio_engine_get_pfl_enabled(1), "deck 1 PFL toggles independently");

    audio_engine_mixer_snapshot_t snapshot;
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(!snapshot.pfl_enabled[0], "snapshot captures deck 0 PFL off");
    EXPECT(snapshot.pfl_enabled[1], "snapshot captures deck 1 PFL on");

    EXPECT(audio_engine_toggle_pfl(2) == ESP_ERR_INVALID_ARG,
           "invalid PFL deck returns INVALID_ARG");
    EXPECT(!audio_engine_get_pfl_enabled(2), "invalid PFL deck reads as off");
}

/* ── Test 5b: Cue mode setting API ───────────────────────────────────────── */
static void test_cue_mode_api(void)
{
    printf("\n[Test 5b] Cue Mode API\n");

    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets cue mode");
    EXPECT(audio_engine_get_cue_mode() == 0, "default cue mode is 0 (stereo)");
    EXPECT(audio_engine_get_headphone_mode() == AUDIO_HEADPHONE_MODE_MASTER_MONO,
           "default headphone mode is master mono");

    EXPECT(audio_engine_set_cue_mode(1) == ESP_OK, "set_cue_mode(1) returns ESP_OK");
    EXPECT(audio_engine_get_cue_mode() == 1, "cue mode is 1 (split mono)");
    EXPECT(audio_engine_get_headphone_mode() == AUDIO_HEADPHONE_MODE_SPLIT_MONO,
           "cue mode split maps to headphone split mono");

    EXPECT(audio_engine_set_cue_mode(2) == ESP_ERR_INVALID_ARG, "invalid cue mode returns INVALID_ARG");
    EXPECT(audio_engine_get_cue_mode() == 1, "cue mode remains unchanged on invalid argument");

    EXPECT(audio_engine_set_cue_mode(0) == ESP_OK, "set_cue_mode(0) returns ESP_OK");
    EXPECT(audio_engine_get_cue_mode() == 0, "cue mode is 0 (stereo)");
    EXPECT(audio_engine_set_headphone_mode(AUDIO_HEADPHONE_MODE_CUE_MONO) == ESP_OK,
           "set_headphone_mode(CUE_MONO) returns ESP_OK");
    EXPECT(audio_engine_get_headphone_mode() == AUDIO_HEADPHONE_MODE_CUE_MONO,
           "headphone mode stores cue mono");
    EXPECT(audio_engine_get_cue_mode() == 1, "cue compatibility mode is split/non-master");
    EXPECT(audio_engine_set_headphone_mode((audio_headphone_mode_t)99) == ESP_ERR_INVALID_ARG,
           "invalid headphone mode returns INVALID_ARG");
    EXPECT(audio_engine_get_headphone_mode() == AUDIO_HEADPHONE_MODE_CUE_MONO,
           "invalid headphone mode leaves previous mode");
}

/* ── Test 5c: Headphone mix state API ───────────────────────────────────── */
static void test_headphone_mix_api(void)
{
    printf("\n[Test 5c] Headphone Mix API\n");

    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets headphone mix");
    EXPECT(audio_engine_get_headphone_mix() == AUDIO_MIXER_CONTROL_MAX,
           "headphone mix defaults to full master for legacy monitor behavior");
    EXPECT(audio_engine_set_headphone_mix(AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
           "headphone mix accepts center raw value");
    EXPECT(audio_engine_get_headphone_mix() == AUDIO_MIXER_CONTROL_CENTER,
           "headphone mix stores center raw value");
    EXPECT(audio_engine_set_headphone_mix(AUDIO_MIXER_CONTROL_MAX + 1000u) == ESP_OK,
           "headphone mix clamps raw values above max");
    EXPECT(audio_engine_get_headphone_mix() == AUDIO_MIXER_CONTROL_MAX,
           "headphone mix clamps to max");

    audio_engine_mixer_snapshot_t snapshot;
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.headphone_mix == AUDIO_MIXER_CONTROL_MAX,
           "snapshot captures headphone mix raw value");
}

static void test_master_cue_api_defaults_on_and_toggles(void)
{
    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets master cue");
    EXPECT(audio_engine_get_master_cue_enabled(), "master cue defaults enabled");
    EXPECT(audio_engine_toggle_master_cue() == ESP_OK, "toggle master cue returns ESP_OK");
    EXPECT(!audio_engine_get_master_cue_enabled(), "master cue toggles off");
    EXPECT(audio_engine_toggle_master_cue() == ESP_OK, "second toggle master cue returns ESP_OK");
    EXPECT(audio_engine_get_master_cue_enabled(), "master cue toggles back on");

    audio_engine_mixer_snapshot_t snapshot;
    audio_engine_get_mixer_snapshot(&snapshot);
    EXPECT(snapshot.master_cue_enabled, "snapshot captures master cue enabled state");
}

/* ── Test 6: per-deck transition API guards ─────────────────────────────── */
static void test_deck_api(void)
{
    printf("\n[Test 6] Per-deck transition API\n");

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

static void test_deck_status_is_independent(void)
{
    printf("\n[Test 7] Per-deck status API\n");

    audio_engine_deck_status_t status0;
    audio_engine_deck_status_t status1;

    EXPECT(audio_engine_init() == ESP_OK, "audio_engine_init resets deck status");
    EXPECT(audio_engine_deck_get_status(0, &status0) == ESP_OK,
           "deck 0 status read returns ESP_OK");
    EXPECT(audio_engine_deck_get_status(1, &status1) == ESP_OK,
           "deck 1 status read returns ESP_OK");
    EXPECT(status0.state == AE_IDLE, "deck 0 starts idle");
    EXPECT(status1.state == AE_IDLE, "deck 1 starts idle");

    EXPECT(audio_engine_deck_load(1, "/nonexistent/file.mp3", NULL, 0) == ESP_ERR_NOT_FOUND,
           "deck 1 failed load returns NOT_FOUND");
    EXPECT(audio_engine_deck_get_status(0, &status0) == ESP_OK,
           "deck 0 status still readable after deck 1 error");
    EXPECT(audio_engine_deck_get_status(1, &status1) == ESP_OK,
           "deck 1 status readable after deck 1 error");
    EXPECT(status0.state == AE_IDLE, "deck 1 error does not affect deck 0 state");
    EXPECT(status0.last_error == ESP_OK, "deck 1 error does not affect deck 0 error");
    EXPECT(status1.state == AE_ERROR, "deck 1 state reports ERROR");
    EXPECT(status1.last_error == ESP_ERR_NOT_FOUND, "deck 1 error reports NOT_FOUND");
    EXPECT(status1.load_progress == 100, "deck 1 failed load resets progress");

    EXPECT(audio_engine_deck_get_status(2, &status0) == ESP_ERR_INVALID_ARG,
           "invalid deck status returns INVALID_ARG");
    EXPECT(audio_engine_deck_get_status(0, NULL) == ESP_ERR_INVALID_ARG,
           "NULL status output returns INVALID_ARG");
}

static void test_deck_states_are_independent(void)
{
    printf("\n[Test 8] Per-deck state split\n");

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

static void test_deck_loops_are_independent(void)
{
    printf("\n[Test 9] Per-deck loop state\n");

    EXPECT(audio_engine_deck_set_loop(0, 1000, 2000) == ESP_OK,
           "deck 0 loop set returns ESP_OK");
    EXPECT(audio_engine_deck_set_loop(1, 3000, 5000) == ESP_OK,
           "deck 1 loop set returns ESP_OK");

    bool active = false;
    uint32_t start = 0;
    uint32_t end = 0;
    EXPECT(audio_engine_deck_get_loop_state(0, &active, &start, &end) == ESP_OK,
           "deck 0 loop read returns ESP_OK");
    EXPECT(active && start == 1000 && end == 2000,
           "deck 0 loop state is independent");

    active = false;
    start = 0;
    end = 0;
    EXPECT(audio_engine_deck_get_loop_state(1, &active, &start, &end) == ESP_OK,
           "deck 1 loop read returns ESP_OK");
    EXPECT(active && start == 3000 && end == 5000,
           "deck 1 loop state is independent");

    EXPECT(audio_engine_deck_clear_loop(0) == ESP_OK,
           "deck 0 loop clear returns ESP_OK");
    EXPECT(audio_engine_deck_get_loop_state(0, &active, &start, &end) == ESP_OK,
           "deck 0 cleared loop read returns ESP_OK");
    EXPECT(!active, "deck 0 loop clears");

    active = false;
    start = 0;
    end = 0;
    EXPECT(audio_engine_deck_get_loop_state(1, &active, &start, &end) == ESP_OK,
           "deck 1 loop still readable after deck 0 clear");
    EXPECT(active && start == 3000 && end == 5000,
           "deck 1 loop survives deck 0 clear");

    EXPECT(audio_engine_deck_set_loop(2, 0, 1) == ESP_ERR_INVALID_ARG,
           "invalid deck loop set returns INVALID_ARG");
    EXPECT(audio_engine_deck_clear_loop(2) == ESP_ERR_INVALID_ARG,
           "invalid deck loop clear returns INVALID_ARG");
    EXPECT(audio_engine_deck_get_loop_state(2, NULL, NULL, NULL) == ESP_ERR_INVALID_ARG,
           "invalid deck loop read returns INVALID_ARG");
}

/* ── Test 10: real MP3 decode to WAV (optional, skipped if no file given) ── */
static void test_decode_to_wav(const char *mp3_path, uint32_t max_ms)
{
    printf("\n[Test 10] Decode MP3 → WAV\n");
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
    test_mixer_state_api();
    test_deck_peak_meter_api_returns_and_resets_peak();
    test_diagnostics_snapshot_reports_audio_health_state();
    test_pfl_state_api();
    test_cue_mode_api();
    test_headphone_mix_api();
    test_master_cue_api_defaults_on_and_toggles();
    test_deck_api();
    test_deck_status_is_independent();
    test_deck_states_are_independent();
    test_deck_loops_are_independent();

    if (argc >= 2) {
        uint32_t max_ms = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 0u;
        test_decode_to_wav(argv[1], max_ms);
    } else {
        printf("\n[Test 10] Decode MP3 → WAV\n");
        printf("  SKIP: no MP3 path provided  (usage: %s <file.mp3> [max_ms])\n", argv[0]);
    }

    printf("\n============================\n");
    printf("Results: %d PASS / %d FAIL\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
