#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "library.h"

#define FIXTURE_TRACK_COUNT 5

static library_track_t s_tracks[FIXTURE_TRACK_COUNT];
static int s_loaded_index;
static uint32_t s_generation;
static anlz_metadata_t s_current_meta;
static bool s_initialized;

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    snprintf(dst, cap, "%s", src ? src : "");
}

static void generate_realistic_waveform(uint32_t duration_ms, uint16_t bpm, uint8_t seed,
                                         uint8_t **out_high, uint32_t *out_high_len,
                                         uint8_t out_low[ANLZ_WAVEFORM_LOW_LEN])
{
    uint32_t sample_rate = 150; // 150 samples per second (Rekordbox ANLZ density)
    uint32_t sample_count = (duration_ms * sample_rate) / 1000;
    if (sample_count == 0) sample_count = 150;
    if (sample_count > 60000) sample_count = 60000;

    uint8_t *high = (uint8_t *)malloc(sample_count);
    if (!high) return;

    uint32_t beat_ms = bpm ? (60000u / bpm) : 500u;
    uint32_t samples_per_beat = (beat_ms * sample_rate) / 1000;
    if (samples_per_beat == 0) samples_per_beat = 73;

    for (uint32_t i = 0; i < sample_count; i++) {
        uint32_t t_ms = (i * 1000u) / sample_rate;
        uint32_t beat_idx = t_ms / beat_ms;
        uint32_t pos = i % samples_per_beat; // sample position within current beat (0..72)
        float phase = (float)pos / (float)samples_per_beat; // 0.0 .. 1.0

        // Section energy profile across the track duration
        float energy = 1.0f;
        if (t_ms < 16000) {
            energy = 0.6f; // Intro groove
        } else if (t_ms >= 48000 && t_ms < 68000) {
            energy = 0.35f; // Melodic breakdown
        } else if (t_ms >= 68000 && t_ms < 76000) {
            // Snare roll build-up to drop
            float p = (float)(t_ms - 68000) / 8000.0f;
            energy = 0.4f + 0.6f * p;
        } else if (t_ms >= 76000 && t_ms < 185000) {
            energy = 1.0f; // Main peak drop
        } else {
            energy = 0.65f; // Outro
        }

        uint8_t amp = 6;
        uint8_t hint = 2; // Mid-bass blue default

        // 1. Kick Drum Transient & Sub-Bass (Beats 1, 2, 3, 4)
        if (energy > 0.4f && pos < 5) {
            // High transient punch
            amp = (uint8_t)(28 + (i % 4));
            hint = 3; // Cyan/white peak transient
        } else if (energy > 0.4f && pos >= 5 && pos < 22) {
            // Sub-bass kick body decay
            float decay = 1.0f - (float)(pos - 5) / 17.0f;
            amp = (uint8_t)(16.0f + 12.0f * decay) + (uint8_t)(i % 3);
            hint = 2; // Deep blue sub-bass
        }
        // 2. Snare / Clap (Beats 2 and 4, i.e. beat_idx % 2 == 1)
        else if ((beat_idx % 2 == 1) && pos >= 2 && pos < 18) {
            float snare_decay = 1.0f - (float)(pos - 2) / 16.0f;
            amp = (uint8_t)(18.0f + 13.0f * snare_decay) + (uint8_t)((i * 5 + seed) % 4);
            hint = (pos < 6) ? 3 : 1; // Pink/red snare body with cyan transient top
        }
        // 3. Off-beat Open Hi-Hat (at phase 0.45 .. 0.58)
        else if (phase >= 0.45f && phase < 0.58f) {
            amp = (uint8_t)(14 + ((i * 7 + seed) % 8));
            hint = 7; // White/bright cyan high frequency
        }
        // 4. 16th Note Closed Hi-Hats & Percussion Ticks
        else if (pos % 18 < 4) {
            amp = (uint8_t)(10 + ((i + seed * 3) % 6));
            hint = 4; // Teal/green mid-high groove
        }
        // 5. Synth/Vocal Harmonics & Bassline tail
        else {
            // Organic micro-modulation for detailed visual texture
            int var = (int)(4.0f * sinf(i * 0.15f) + 3.0f * cosf(i * 0.07f));
            amp = (uint8_t)(8 + (seed % 5) + (var > 0 ? var : -var));
            hint = (uint8_t)(1 + ((i / 8 + seed) % 3));
        }

        // Breakdown special handling (lush synths, no kick)
        if (t_ms >= 48000 && t_ms < 68000) {
            amp = (uint8_t)(6 + (uint8_t)(6.0f * (1.0f + sinf(i * 0.08f))));
            hint = (uint8_t)(2 + (i % 3)); // Blue/green/purple synth pads
        }

        // Apply section energy scaling
        amp = (uint8_t)(amp * energy);
        if (amp > 31) amp = 31;
        if (amp < 2) amp = 2;

        high[i] = (uint8_t)((hint << 5) | amp);
    }

    *out_high = high;
    *out_high_len = sample_count;

    // Downsample to 200-sample waveform_low for mini overview
    for (uint32_t b = 0; b < ANLZ_WAVEFORM_LOW_LEN; b++) {
        uint32_t start_idx = (b * sample_count) / ANLZ_WAVEFORM_LOW_LEN;
        uint32_t end_idx = ((b + 1) * sample_count) / ANLZ_WAVEFORM_LOW_LEN;
        if (end_idx > sample_count) end_idx = sample_count;

        uint8_t max_amp = 0;
        uint8_t dom_hint = 2;
        for (uint32_t k = start_idx; k < end_idx; k++) {
            uint8_t a = high[k] & 0x1F;
            if (a > max_amp) {
                max_amp = a;
                dom_hint = high[k] >> 5;
            }
        }
        out_low[b] = (uint8_t)((dom_hint << 5) | max_amp);
    }
}

static void fill_waveform(library_track_t *track, uint8_t seed)
{
    uint8_t *high = NULL;
    uint32_t high_len = 0;
    generate_realistic_waveform(track->duration_ms, track->bpm, seed,
                                &high, &high_len, track->waveform_low);
    if (high) {
        free(high);
    }
    track->has_waveform = 1;
}

static void set_track(int index, uint32_t id, const char *title,
                      const char *artist, const char *key, uint16_t bpm,
                      uint32_t duration_ms)
{
    library_track_t *track = &s_tracks[index];
    memset(track, 0, sizeof(*track));
    track->track_id = id;
    track->bpm = bpm;
    track->duration_ms = duration_ms;
    copy_text(track->title, sizeof(track->title), title);
    copy_text(track->artist, sizeof(track->artist), artist);
    copy_text(track->album, sizeof(track->album), "Pajoniiir Simulator");
    copy_text(track->key, sizeof(track->key), key);
    snprintf(track->path, sizeof(track->path), "fixture://track/%lu.wav",
             (unsigned long)id);
    snprintf(track->anlz_path, sizeof(track->anlz_path), "fixture://anlz/%lu",
             (unsigned long)id);
    fill_waveform(track, (uint8_t)(index + 1));
}

esp_err_t library_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    set_track(0, 1001, "Midnight Circuit", "Pajoniiir", "8A", 124, 243000);
    set_track(1, 1002, "Neon Harbor", "Signal Drift", "9A", 128, 198000);
    set_track(2, 1003, "Static Bloom", "Blue Current", "5B", 122, 271000);
    set_track(3, 1004, "Afterimage", "Phase Garden", "11A", 130, 225000);
    set_track(4, 1005, "Low Orbit", "Vector Youth", "3A", 118, 304000);
    s_initialized = true;
    s_generation++;
    return ESP_OK;
}

void library_clear(void)
{
    memset(s_tracks, 0, sizeof(s_tracks));
    s_initialized = false;
    s_loaded_index = 0;
    library_free_current_anlz();
    s_generation++;
}

uint32_t library_generation(void) { return s_generation; }
int library_count(void) { return s_initialized ? FIXTURE_TRACK_COUNT : 0; }

esp_err_t library_get(int index, library_track_t *out)
{
    if (!out || index < 0 || index >= library_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_tracks[index];
    return ESP_OK;
}

esp_err_t library_get_summary(int index, uint16_t *out_bpm,
                              uint32_t *out_duration_ms)
{
    if (index < 0 || index >= library_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_bpm) {
        *out_bpm = s_tracks[index].bpm;
    }
    if (out_duration_ms) {
        *out_duration_ms = s_tracks[index].duration_ms;
    }
    return ESP_OK;
}

library_track_t *library_get_ptr(int index)
{
    return index >= 0 && index < library_count() ? &s_tracks[index] : NULL;
}

/* Identity-only accessors, mirroring the production contract: the shared UI code
 * uses these instead of copying/holding whole track records for highlight and
 * selection lookups. */
esp_err_t library_get_row_key(int index, uint32_t *out_key)
{
    if (!out_key) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_key = 0u;
    if (index < 0 || index >= library_count()) {
        return ESP_ERR_NOT_FOUND;
    }
    *out_key = library_track_key(&s_tracks[index]);
    return ESP_OK;
}

int library_find_row_by_key(uint32_t track_key)
{
    if (track_key == 0u) {
        return -1;
    }
    const int count = library_count();
    for (int index = 0; index < count; ++index) {
        if (library_track_key(&s_tracks[index]) == track_key) {
            return index;
        }
    }
    return -1;
}

uint32_t library_track_key(const library_track_t *track)
{
    return track ? track->track_id : 0;
}

esp_err_t library_load_anlz(library_track_t *track)
{
    if (!track) {
        return ESP_ERR_INVALID_ARG;
    }
    library_free_current_anlz();
    memset(&s_current_meta, 0, sizeof(s_current_meta));
    s_current_meta.bpm = track->bpm;
    s_current_meta.beat_count = (uint16_t)((track->duration_ms * track->bpm) / 60000u);
    if (s_current_meta.beat_count == 0) s_current_meta.beat_count = 64;
    s_current_meta.beats = calloc(s_current_meta.beat_count, sizeof(anlz_beat_t));
    if (!s_current_meta.beats) {
        return ESP_ERR_NO_MEM;
    }
    uint32_t beat_ms = track->bpm ? (60000u / track->bpm) : 500u;
    for (uint16_t i = 0; i < s_current_meta.beat_count; i++) {
        s_current_meta.beats[i].beat_phase = (uint16_t)(i & 3u);
        s_current_meta.beats[i].bpm_x100 = (uint16_t)(track->bpm * 100u);
        s_current_meta.beats[i].time_ms = i * beat_ms;
    }

    generate_realistic_waveform(track->duration_ms, track->bpm, (uint8_t)track->track_id,
                                &s_current_meta.waveform_high,
                                &s_current_meta.waveform_high_len,
                                s_current_meta.waveform_low);
    s_current_meta.has_waveform_low = true;
    memcpy(track->waveform_low, s_current_meta.waveform_low, ANLZ_WAVEFORM_LOW_LEN);

    s_current_meta.cue_count = 2;
    s_current_meta.cues[0] = (anlz_cue_t){
        .type = ANLZ_CUE_SINGLE, .index = 0, .start_ms = beat_ms * 8u,
    };
    s_current_meta.cues[1] = (anlz_cue_t){
        .type = ANLZ_CUE_LOOP, .index = 1,
        .start_ms = beat_ms * 24u, .end_ms = beat_ms * 32u,
    };
    track->has_anlz = 1;
    return ESP_OK;
}

void library_last_anlz_load_stats(uint32_t *elapsed_ms, uint8_t *source,
                                  bool *cache_written)
{
    if (elapsed_ms) *elapsed_ms = 1;
    if (source) *source = 0;
    if (cache_written) *cache_written = false;
}

esp_err_t library_clone_current_anlz(anlz_metadata_t *out)
{
    if (!out || !s_current_meta.beats) {
        return ESP_ERR_NOT_FOUND;
    }
    return anlz_clone(&s_current_meta, out);
}

void library_free_current_anlz(void)
{
    anlz_free(&s_current_meta);
}

static int compare_title(const void *lhs, const void *rhs)
{
    const library_track_t *a = lhs;
    const library_track_t *b = rhs;
    return strcmp(a->title, b->title);
}

static int compare_artist(const void *lhs, const void *rhs)
{
    const library_track_t *a = lhs;
    const library_track_t *b = rhs;
    return strcmp(a->artist, b->artist);
}

static int compare_bpm(const void *lhs, const void *rhs)
{
    const library_track_t *a = lhs;
    const library_track_t *b = rhs;
    return (a->bpm > b->bpm) - (a->bpm < b->bpm);
}

void library_sort(int field_type, bool descending)
{
    int (*compare)(const void *, const void *) = compare_title;
    if (field_type == 0) compare = compare_artist;
    if (field_type == 2) compare = compare_bpm;
    qsort(s_tracks, FIXTURE_TRACK_COUNT, sizeof(s_tracks[0]), compare);
    if (descending) {
        for (int i = 0; i < FIXTURE_TRACK_COUNT / 2; i++) {
            library_track_t tmp = s_tracks[i];
            s_tracks[i] = s_tracks[FIXTURE_TRACK_COUNT - 1 - i];
            s_tracks[FIXTURE_TRACK_COUNT - 1 - i] = tmp;
        }
    }
    s_generation++;
}

void library_set_selected_track_index(int track_index)
{
    if (track_index >= 0 && track_index < library_count()) {
        s_loaded_index = track_index;
    }
}

int library_selected_track_index(void)
{
    return s_loaded_index;
}
